#include "gl_framebuffer.h"
#include "gl_texture.h"
#include "endian/endian.h"
#include "mem/gl_mem.h"
#include "state/gl_state.h"
#ifdef __cplusplus
extern "C" {
#endif
#include <coreinit/cache.h>
#include <gx2/surface.h>
#include <gx2/state.h>
#include <gx2/event.h>
#include <gx2/display.h>
#include <gx2/registers.h>
#include <gx2/mem.h>
#include <gx2r/surface.h>
#include <whb/gfx.h>
#ifdef __cplusplus
}
#endif
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifndef GL_TEXTURE_CUBE_MAP_POSITIVE_X
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#endif

#ifndef GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Z 0x851A
#endif

#define MAX_FRAMEBUFFERS 128
#define MAX_RENDERBUFFERS 256
#define GX2GL_MAX_RENDERBUFFER_SIZE 8192
#define GX2GL_MAX_RENDERBUFFER_SAMPLES 1
#define GX2GL_MAX_FRAMEBUFFER_3D_TEXTURE_SIZE 2048

typedef enum {
    GL_ATTACHMENT_KIND_NONE = 0,
    GL_ATTACHMENT_KIND_TEXTURE,
    GL_ATTACHMENT_KIND_RENDERBUFFER
} GLAttachmentKind;

typedef struct {
    GLAttachmentKind kind;
    GLuint object;
    GLenum textarget;
    GLint level;
    GLint layer;
    GLboolean layered;
} GLAttachmentRef;

typedef struct {
    bool allocated;
    GX2Surface surface;
    GX2ColorBuffer color_buffer;
} GLTextureColorTarget;

typedef struct {
    bool reserved;
    bool in_use;
    bool is_depth;
    GLint internal_format;
    GLsizei width;
    GLsizei height;
    GLsizei samples;
    GX2Surface surface;
    GX2ColorBuffer color_buffer;
    GX2DepthBuffer depth_buffer;
} GLRenderbuffer;

typedef struct {
    bool reserved;
    bool in_use;
    GLAttachmentRef color_attachments[8];
    GLenum draw_buffers[8];
    GLenum read_buffer;
    GLAttachmentRef depth_attachment;
    GLAttachmentRef stencil_attachment;

    GX2ColorBuffer cb[8];
    GLTextureColorTarget texture_targets[8];
    bool color_needs_resolve[8];
    bool color_buffer_owns_aux[8];
    GX2DepthBuffer db;
    bool dirty;
} GLFramebuffer;

static GLFramebuffer g_framebuffers[MAX_FRAMEBUFFERS];
static GLRenderbuffer g_renderbuffers[MAX_RENDERBUFFERS];
static bool g_default_framebuffer_uses_drc = false;

static bool attachment_ref_present(const GLAttachmentRef *attachment);
static GLRenderbuffer *get_renderbuffer(GLuint id);
static GX2ColorBuffer *get_default_color_buffer(void);
static void clear_attachment_ref(GLAttachmentRef *attachment);
static void free_framebuffer_texture_target(GLFramebuffer *fb, uint32_t index);
static uint32_t texture_level_extent(uint32_t extent, GLint level);
static uint32_t attachment_view_slice(const GLAttachmentRef *attachment);
static uint32_t texture_level_depth_for_attachment(const GLAttachmentRef *attachment,
                                                   const GX2Surface *surface);

static void detach_renderbuffer_from_framebuffer(GLuint framebuffer,
                                                GLuint renderbuffer) {
    GLFramebuffer *fb;
    bool detached = false;

    if (renderbuffer == 0 || framebuffer == 0 ||
        framebuffer >= MAX_FRAMEBUFFERS || !g_framebuffers[framebuffer].in_use) {
        return;
    }

    fb = &g_framebuffers[framebuffer];

    for (uint32_t j = 0; j < 8; ++j) {
        if (fb->color_attachments[j].kind == GL_ATTACHMENT_KIND_RENDERBUFFER &&
            fb->color_attachments[j].object == renderbuffer) {
            clear_attachment_ref(&fb->color_attachments[j]);
            fb->color_needs_resolve[j] = false;
            free_framebuffer_texture_target(fb, j);
            detached = true;
        }
    }

    if (fb->depth_attachment.kind == GL_ATTACHMENT_KIND_RENDERBUFFER &&
        fb->depth_attachment.object == renderbuffer) {
        clear_attachment_ref(&fb->depth_attachment);
        memset(&fb->db, 0, sizeof(fb->db));
        detached = true;
    }

    if (fb->stencil_attachment.kind == GL_ATTACHMENT_KIND_RENDERBUFFER &&
        fb->stencil_attachment.object == renderbuffer) {
        clear_attachment_ref(&fb->stencil_attachment);
        memset(&fb->db, 0, sizeof(fb->db));
        detached = true;
    }

    if (detached) {
        fb->dirty = true;
        if (g_gl_context &&
            (g_gl_context->bound_framebuffer == framebuffer ||
             g_gl_context->bound_read_framebuffer == framebuffer)) {
            g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
        }
    }
}

static void detach_renderbuffer_from_current_framebuffers(GLuint renderbuffer) {
    if (!g_gl_context || renderbuffer == 0) return;

    detach_renderbuffer_from_framebuffer(g_gl_context->bound_framebuffer,
                                         renderbuffer);
    if (g_gl_context->bound_read_framebuffer != g_gl_context->bound_framebuffer) {
        detach_renderbuffer_from_framebuffer(g_gl_context->bound_read_framebuffer,
                                             renderbuffer);
    }
}

static void mark_framebuffers_for_renderbuffer(GLuint renderbuffer) {
    if (renderbuffer == 0) return;

    for (uint32_t i = 0; i < MAX_FRAMEBUFFERS; ++i) {
        GLFramebuffer *fb = &g_framebuffers[i];
        bool referenced = false;

        if (!fb->in_use) continue;

        for (uint32_t j = 0; j < 8; ++j) {
            referenced = referenced ||
                         (fb->color_attachments[j].kind == GL_ATTACHMENT_KIND_RENDERBUFFER &&
                          fb->color_attachments[j].object == renderbuffer);
        }

        referenced = referenced ||
                     (fb->depth_attachment.kind == GL_ATTACHMENT_KIND_RENDERBUFFER &&
                      fb->depth_attachment.object == renderbuffer) ||
                     (fb->stencil_attachment.kind == GL_ATTACHMENT_KIND_RENDERBUFFER &&
                      fb->stencil_attachment.object == renderbuffer);

        if (referenced) {
            fb->dirty = true;
            if (g_gl_context &&
                (g_gl_context->bound_framebuffer == i ||
                 g_gl_context->bound_read_framebuffer == i)) {
                g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
            }
        }
    }
}

static bool get_color_attachment_index(GLenum attachment, uint32_t *index) {
    if (attachment < GL_COLOR_ATTACHMENT0 || attachment > GL_COLOR_ATTACHMENT7) {
        return false;
    }
    if (index) {
        *index = (uint32_t)(attachment - GL_COLOR_ATTACHMENT0);
    }
    return true;
}

static bool is_color_attachment_name(GLenum attachment, uint32_t *index) {
    if (attachment < GL_COLOR_ATTACHMENT0) {
        return false;
    }
    if (index) {
        *index = (uint32_t)(attachment - GL_COLOR_ATTACHMENT0);
    }
    return true;
}

static bool is_default_framebuffer_color_name(GLenum buffer) {
    return buffer == GL_FRONT || buffer == GL_BACK || buffer == GL_FRONT_AND_BACK;
}

static GX2RResourceFlags build_framebuffer_surface_flags(GLint internal_format,
                                                         bool is_depth,
                                                         bool sampled_texture) {
    GX2RResourceFlags flags;

    flags = (GX2RResourceFlags)(GX2R_RESOURCE_USAGE_CPU_WRITE |
                                GX2R_RESOURCE_USAGE_CPU_READ |
                                GX2R_RESOURCE_USAGE_GPU_READ |
                                GX2R_RESOURCE_USAGE_GPU_WRITE |
                                GX2R_RESOURCE_USAGE_FORCE_MEM2);

    if (sampled_texture)
        flags = (GX2RResourceFlags)(flags | GX2R_RESOURCE_BIND_TEXTURE);

    if (is_depth) {
        flags = (GX2RResourceFlags)(flags | GX2R_RESOURCE_BIND_DEPTH_BUFFER);
    }
    else {
        flags = (GX2RResourceFlags)(flags | GX2R_RESOURCE_BIND_COLOR_BUFFER);
    }

    (void)internal_format;
    return flags;
}

static void free_surface_storage(GX2Surface *surface) {
    if (!surface) return;
    if (surface->resourceFlags) {
        GX2RDestroySurfaceEx(surface, GX2R_RESOURCE_BIND_NONE);
        memset(surface, 0, sizeof(*surface));
        return;
    }
    if (surface->image) gl_mem_free(GL_MEM_TYPE_MEM2, surface->image);
    if (surface->mipmaps) gl_mem_free(GL_MEM_TYPE_MEM2, surface->mipmaps);
    memset(surface, 0, sizeof(*surface));
}

static void free_color_buffer_aux(GX2ColorBuffer *color_buffer) {
    if (!color_buffer) return;
    if (color_buffer->aaBuffer) gl_mem_free(GL_MEM_TYPE_MEM2, color_buffer->aaBuffer);
    color_buffer->aaBuffer = NULL;
    color_buffer->aaSize = 0;
}

static void free_texture_color_target(GLTextureColorTarget *target) {
    if (!target) return;
    free_color_buffer_aux(&target->color_buffer);
    if (target->allocated) free_surface_storage(&target->surface);
    memset(target, 0, sizeof(*target));
}

static void free_framebuffer_texture_target(GLFramebuffer *fb, uint32_t index) {
    if (!fb || index >= 8) return;
    free_texture_color_target(&fb->texture_targets[index]);
    fb->color_needs_resolve[index] = false;
    if (fb->color_buffer_owns_aux[index]) {
        free_color_buffer_aux(&fb->cb[index]);
    }
    fb->color_buffer_owns_aux[index] = false;
    memset(&fb->cb[index], 0, sizeof(fb->cb[index]));
}

static void invalidate_surface_after_color_write(const GX2Surface *surface) {
    if (!surface) return;
    if (surface->resourceFlags) {
        GX2RInvalidateSurface((GX2Surface *)surface, 0,
                              (GX2RResourceFlags)(GX2R_RESOURCE_USAGE_GPU_WRITE |
                                                  GX2R_RESOURCE_USAGE_GPU_READ));
        return;
    }
    if (surface->image && surface->imageSize) {
        GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER |
                                          GX2_INVALIDATE_MODE_TEXTURE),
                      surface->image, surface->imageSize);
    }
    if (surface->mipmaps && surface->mipmapSize) {
        GX2Invalidate(GX2_INVALIDATE_MODE_TEXTURE,
                      surface->mipmaps, surface->mipmapSize);
    }
}

static bool stage_color_surface_for_cpu_read(const GX2Surface *source,
                                             GLint internal_format,
                                             GX2Surface *staging_surface) {
    GX2RResourceFlags surface_flags;

    if (!source || !source->image || !staging_surface) return false;

    memset(staging_surface, 0, sizeof(*staging_surface));
    staging_surface->dim = source->dim;
    staging_surface->width = source->width;
    staging_surface->height = source->height;
    staging_surface->depth = source->depth ? source->depth : 1;
    staging_surface->mipLevels = 1;
    staging_surface->format = source->format;
    staging_surface->aa = GX2_AA_MODE1X;
    staging_surface->use = (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
    staging_surface->tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
    GX2CalcSurfaceSizeAndAlignment(staging_surface);
    surface_flags = build_framebuffer_surface_flags(internal_format, false, true);

    if (!GX2RCreateSurface(staging_surface, surface_flags)) {
        memset(staging_surface, 0, sizeof(*staging_surface));
        return false;
    }

    GX2CopySurface(source, 0, 0, staging_surface, 0, 0);
    GX2DrawDone();
    invalidate_surface_after_color_write(staging_surface);
    DCInvalidateRange(staging_surface->image, staging_surface->imageSize);
    return true;
}

static bool init_color_buffer_from_surface_view(GX2ColorBuffer *cb,
                                                const GX2Surface *s,
                                                GLint level,
                                                uint32_t slice) {
    uint32_t aux_size = 0;
    uint32_t aux_alignment = 0;

    if (!cb || !s) return false;

    free_color_buffer_aux(cb);

    memset(cb, 0, sizeof(*cb));
    cb->surface = *s;
    cb->viewMip = (uint32_t)(level < 0 ? 0 : level);
    cb->viewFirstSlice = slice;
    cb->viewNumSlices = 1;

    GX2CalcColorBufferAuxInfo(cb, &aux_size, &aux_alignment);
    if (aux_size) {
        cb->aaBuffer = gl_mem_alloc(GL_MEM_TYPE_MEM2, aux_size, aux_alignment ? aux_alignment : 0x100);
        if (!cb->aaBuffer) {
            memset(cb, 0, sizeof(*cb));
            return false;
        }

        memset(cb->aaBuffer, 0, aux_size);
        cb->aaSize = aux_size;
    }

    GX2InitColorBufferRegs(cb);
    return true;
}

static bool init_color_buffer_from_surface(GX2ColorBuffer *cb, const GX2Surface *s) {
    return init_color_buffer_from_surface_view(cb, s, 0, 0);
}

static bool ensure_texture_color_target(GLFramebuffer *fb, uint32_t index, const GX2Texture *texture) {
    GLTextureColorTarget *target;
    GX2Surface *surface;
    GX2RResourceFlags surface_flags;
    const GLAttachmentRef *attachment;
    uint32_t depth;
    uint32_t slice;

    if (!fb || index >= 8 || !texture || !texture->surface.image) return false;

    attachment = &fb->color_attachments[index];
    depth = texture_level_depth_for_attachment(attachment, &texture->surface);
    slice = attachment_view_slice(attachment);
    if (slice >= depth) return false;

    target = &fb->texture_targets[index];
    surface = &target->surface;
    if (target->allocated &&
        surface->dim == texture->surface.dim &&
        surface->width == texture_level_extent(texture->surface.width, attachment->level) &&
        surface->height == texture_level_extent(texture->surface.height, attachment->level) &&
        surface->depth == depth &&
        surface->format == texture->surface.format) {
        return true;
    }

    free_texture_color_target(target);

    memset(surface, 0, sizeof(*surface));
    surface->dim = texture->surface.dim;
    surface->width = texture_level_extent(texture->surface.width, attachment->level);
    surface->height = texture_level_extent(texture->surface.height, attachment->level);
    surface->depth = depth;
    surface->mipLevels = 1;
    surface->format = texture->surface.format;
    surface->aa = GX2_AA_MODE1X;
    surface->use = (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
    surface->tileMode = GX2_TILE_MODE_DEFAULT;
    GX2CalcSurfaceSizeAndAlignment(surface);
    surface_flags = build_framebuffer_surface_flags(
        gl_get_texture_internal_format(fb->color_attachments[index].object), false, true);

    if (!GX2RCreateSurface(surface, surface_flags)) {
        memset(surface, 0, sizeof(*surface));
        return false;
    }

    if (surface->image && surface->imageSize)
        memset(surface->image, 0, surface->imageSize);
    if (surface->mipmaps && surface->mipmapSize)
        memset(surface->mipmaps, 0, surface->mipmapSize);

    if (!init_color_buffer_from_surface_view(&target->color_buffer, surface,
                                             0, slice)) {
        free_surface_storage(surface);
        return false;
    }

    target->allocated = true;
    return true;
}

static void resolve_framebuffer_texture_targets(GLFramebuffer *fb) {
    if (!fb) return;
    for (uint32_t i = 0; i < 8; ++i) {
        GX2Texture *texture;
        GLTextureColorTarget *target;
        if (!fb->color_needs_resolve[i]) continue;
        if (fb->color_attachments[i].kind != GL_ATTACHMENT_KIND_TEXTURE) {
            fb->color_needs_resolve[i] = false;
            continue;
        }

        texture = gl_get_gx2_texture(fb->color_attachments[i].object);
        if (!texture || !texture->surface.image) {
            fb->color_needs_resolve[i] = false;
            continue;
        }

        target = &fb->texture_targets[i];
        GX2DrawDone();
        if (target->allocated && target->surface.image) {
            uint32_t slice = attachment_view_slice(&fb->color_attachments[i]);
            GX2CopySurface(&target->surface, 0, slice,
                           &texture->surface,
                           (uint32_t)fb->color_attachments[i].level,
                           slice);
            GX2DrawDone();
        }
        invalidate_surface_after_color_write(&texture->surface);
        fb->color_needs_resolve[i] = false;
    }
}

static void init_draw_buffer_defaults(GLFramebuffer *fb, bool is_default) {
    if (!fb) return;
    for (uint32_t i = 0; i < 8; ++i) fb->draw_buffers[i] = GL_NONE;
    fb->draw_buffers[0] = is_default ? GL_BACK : GL_COLOR_ATTACHMENT0;
}

static void init_read_buffer_default(GLFramebuffer *fb, bool is_default) {
    if (!fb) return;
    fb->read_buffer = is_default ? GL_BACK : GL_COLOR_ATTACHMENT0;
}

static bool is_framebuffer_target(GLenum target) {
    return target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER;
}

static bool is_cube_map_face_target(GLenum target) {
    return target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
           target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z;
}

static uint32_t cube_map_face_slice(GLenum target) {
    return is_cube_map_face_target(target)
               ? (uint32_t)(target - GL_TEXTURE_CUBE_MAP_POSITIVE_X)
               : 0u;
}

static uint32_t texture_level_extent(uint32_t extent, GLint level) {
    uint32_t shifted;

    if (level <= 0) return extent ? extent : 1u;
    shifted = extent >> (uint32_t)level;
    return shifted ? shifted : 1u;
}

static GLint max_texture_level_for_size(uint32_t size) {
    GLint level = 0;

    while (size > 1u) {
        size >>= 1u;
        ++level;
    }
    return level;
}

static uint32_t attachment_view_slice(const GLAttachmentRef *attachment) {
    if (!attachment || attachment->kind != GL_ATTACHMENT_KIND_TEXTURE) return 0u;
    if (attachment->textarget == GL_TEXTURE_3D) return (uint32_t)attachment->layer;
    return cube_map_face_slice(attachment->textarget);
}

static uint32_t texture_level_depth_for_attachment(const GLAttachmentRef *attachment,
                                                   const GX2Surface *surface) {
    if (!attachment || !surface) return 1u;
    if (attachment->textarget == GL_TEXTURE_3D) {
        return texture_level_extent(surface->depth, attachment->level);
    }
    if (is_cube_map_face_target(attachment->textarget)) {
        return surface->depth ? surface->depth : 6u;
    }
    return 1u;
}

static bool attachment_view_exists(const GLAttachmentRef *attachment,
                                   const GX2Texture *texture) {
    uint32_t depth;

    if (!attachment_ref_present(attachment) || !texture ||
        !texture->surface.image || attachment->level < 0 ||
        (uint32_t)attachment->level >= texture->surface.mipLevels) {
        return false;
    }

    depth = texture_level_depth_for_attachment(attachment, &texture->surface);
    return attachment_view_slice(attachment) < depth;
}

static bool build_texture_attachment_read_surface(const GLAttachmentRef *attachment,
                                                  const GX2Surface *source,
                                                  GX2Surface *view) {
    GX2Surface level_surface;
    uint8_t *level_base;
    uint32_t depth;
    uint32_t slice;
    uint32_t slice_size;

    if (!attachment || !source || !view || !source->image ||
        attachment->level < 0 ||
        (uint32_t)attachment->level >= source->mipLevels) {
        return false;
    }

    depth = texture_level_depth_for_attachment(attachment, source);
    slice = attachment_view_slice(attachment);
    if (depth == 0 || slice >= depth) return false;

    level_base = attachment->level == 0
                     ? (uint8_t *)source->image
                     : (source->mipmaps
                            ? (uint8_t *)source->mipmaps +
                                  source->mipLevelOffset[attachment->level - 1]
                            : NULL);
    if (!level_base) return false;

    memset(&level_surface, 0, sizeof(level_surface));
    level_surface.dim = source->dim;
    level_surface.width = texture_level_extent(source->width, attachment->level);
    level_surface.height = texture_level_extent(source->height, attachment->level);
    level_surface.depth = depth;
    level_surface.mipLevels = 1;
    level_surface.format = source->format;
    level_surface.aa = source->aa;
    level_surface.use = source->use;
    level_surface.tileMode = source->tileMode;
    GX2CalcSurfaceSizeAndAlignment(&level_surface);

    slice_size = depth > 0 ? level_surface.imageSize / depth
                           : level_surface.imageSize;
    *view = level_surface;
    view->depth = 1;
    view->image = level_base + (size_t)slice * (size_t)slice_size;
    view->imageSize = slice_size;
    view->mipmaps = NULL;
    view->mipmapSize = 0;
    view->resourceFlags = (GX2RResourceFlags)0;
    return true;
}

static GLuint get_bound_framebuffer_for_target(GLenum target) {
    switch (target) {
    case GL_FRAMEBUFFER:
    case GL_DRAW_FRAMEBUFFER:
        return g_gl_context ? g_gl_context->bound_framebuffer : 0;
    case GL_READ_FRAMEBUFFER:
        return g_gl_context ? g_gl_context->bound_read_framebuffer : 0;
    default:
        return 0;
    }
}

static void clear_attachment_ref(GLAttachmentRef *attachment) {
    if (!attachment) return;
    attachment->kind = GL_ATTACHMENT_KIND_NONE;
    attachment->object = 0;
    attachment->textarget = 0;
    attachment->level = 0;
    attachment->layer = 0;
    attachment->layered = GL_FALSE;
}

static bool attachment_ref_present(const GLAttachmentRef *attachment) {
    return attachment && attachment->kind != GL_ATTACHMENT_KIND_NONE && attachment->object != 0;
}

static void set_texture_attachment_ref(GLAttachmentRef *attachment,
                                       GLuint texture,
                                       GLenum textarget,
                                       GLint level,
                                       GLint layer,
                                       GLboolean layered) {
    if (!attachment) return;
    attachment->kind = GL_ATTACHMENT_KIND_TEXTURE;
    attachment->object = texture;
    attachment->textarget = textarget;
    attachment->level = level;
    attachment->layer = layer;
    attachment->layered = layered;
}

static void set_renderbuffer_attachment_ref(GLAttachmentRef *attachment,
                                           GLuint renderbuffer) {
    if (!attachment) return;
    attachment->kind = GL_ATTACHMENT_KIND_RENDERBUFFER;
    attachment->object = renderbuffer;
    attachment->textarget = 0;
    attachment->level = 0;
    attachment->layer = 0;
    attachment->layered = GL_FALSE;
}

static void init_framebuffer_object(GLFramebuffer *fb, bool is_default) {
    if (!fb) return;
    memset(fb, 0, sizeof(*fb));
    fb->in_use = true;
    fb->reserved = false;
    fb->dirty = true;
    init_draw_buffer_defaults(fb, is_default);
    init_read_buffer_default(fb, is_default);
}

static void reserve_framebuffer_name(GLFramebuffer *fb) {
    if (!fb) return;
    memset(fb, 0, sizeof(*fb));
    fb->reserved = true;
}

static bool framebuffer_name_is_valid(GLuint id) {
    return id == 0 ||
           (id < MAX_FRAMEBUFFERS &&
            (g_framebuffers[id].reserved || g_framebuffers[id].in_use));
}

static void init_renderbuffer_object(GLRenderbuffer *rb) {
    if (!rb) return;
    memset(rb, 0, sizeof(*rb));
    rb->in_use = true;
    rb->reserved = false;
}

static void reserve_renderbuffer_name(GLRenderbuffer *rb) {
    if (!rb) return;
    memset(rb, 0, sizeof(*rb));
    rb->reserved = true;
}

static bool renderbuffer_name_is_valid(GLuint id) {
    return id == 0 ||
           (id < MAX_RENDERBUFFERS &&
            (g_renderbuffers[id].reserved || g_renderbuffers[id].in_use));
}

static GLAttachmentRef *get_attachment_ref(GLFramebuffer *fb, GLenum attachment) {
    if (!fb) return NULL;
    if (attachment >= GL_COLOR_ATTACHMENT0 && attachment <= GL_COLOR_ATTACHMENT7) return &fb->color_attachments[attachment - GL_COLOR_ATTACHMENT0];
    if (attachment == GL_DEPTH_ATTACHMENT) return &fb->depth_attachment;
    if (attachment == GL_STENCIL_ATTACHMENT) return &fb->stencil_attachment;
    return NULL;
}

static GLRenderbuffer *get_renderbuffer(GLuint id) {
    if (id == 0 || id >= MAX_RENDERBUFFERS || !g_renderbuffers[id].in_use) return NULL;
    return &g_renderbuffers[id];
}

static bool is_depth_internal_format(GLint internal_format) {
    switch (internal_format) {
    case GL_DEPTH_COMPONENT: case GL_DEPTH_COMPONENT32F: case GL_DEPTH_STENCIL: case GL_DEPTH24_STENCIL8: return true;
    default: return false;
    }
}

static bool is_stencil_internal_format(GLint internal_format) {
    return internal_format == GL_DEPTH_STENCIL || internal_format == GL_DEPTH24_STENCIL8;
}

static GLint renderbuffer_component_bits(GLint internal_format, GLenum component) {
    switch (internal_format) {
    case 1:
    case GL_RED:
    case GL_R8:
        return component == GL_RENDERBUFFER_RED_SIZE ? 8 : 0;
    case 2:
    case GL_RG:
    case GL_RG8:
        return (component == GL_RENDERBUFFER_RED_SIZE ||
                component == GL_RENDERBUFFER_GREEN_SIZE) ? 8 : 0;
    case 3:
    case GL_RGB:
    case GL_RGB8:
        return (component == GL_RENDERBUFFER_RED_SIZE ||
                component == GL_RENDERBUFFER_GREEN_SIZE ||
                component == GL_RENDERBUFFER_BLUE_SIZE) ? 8 : 0;
    case 4:
    case GL_RGBA:
    case GL_RGBA8:
        return (component == GL_RENDERBUFFER_RED_SIZE ||
                component == GL_RENDERBUFFER_GREEN_SIZE ||
                component == GL_RENDERBUFFER_BLUE_SIZE ||
                component == GL_RENDERBUFFER_ALPHA_SIZE) ? 8 : 0;
    case GL_RGBA16F:
        return (component == GL_RENDERBUFFER_RED_SIZE ||
                component == GL_RENDERBUFFER_GREEN_SIZE ||
                component == GL_RENDERBUFFER_BLUE_SIZE ||
                component == GL_RENDERBUFFER_ALPHA_SIZE) ? 16 : 0;
    case GL_RGBA32F:
        return (component == GL_RENDERBUFFER_RED_SIZE ||
                component == GL_RENDERBUFFER_GREEN_SIZE ||
                component == GL_RENDERBUFFER_BLUE_SIZE ||
                component == GL_RENDERBUFFER_ALPHA_SIZE) ? 32 : 0;
    case GL_DEPTH_COMPONENT:
    case GL_DEPTH_COMPONENT32F:
        return component == GL_RENDERBUFFER_DEPTH_SIZE ? 32 : 0;
    case GL_DEPTH_STENCIL:
    case GL_DEPTH24_STENCIL8:
        if (component == GL_RENDERBUFFER_DEPTH_SIZE) return 24;
        if (component == GL_RENDERBUFFER_STENCIL_SIZE) return 8;
        return 0;
    default:
        return 0;
    }
}

static GLint attachment_component_bits(GLint internal_format, GLenum pname) {
    switch (pname) {
    case GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE:
        return renderbuffer_component_bits(internal_format, GL_RENDERBUFFER_RED_SIZE);
    case GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE:
        return renderbuffer_component_bits(internal_format, GL_RENDERBUFFER_GREEN_SIZE);
    case GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE:
        return renderbuffer_component_bits(internal_format, GL_RENDERBUFFER_BLUE_SIZE);
    case GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE:
        return renderbuffer_component_bits(internal_format, GL_RENDERBUFFER_ALPHA_SIZE);
    case GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE:
        return renderbuffer_component_bits(internal_format, GL_RENDERBUFFER_DEPTH_SIZE);
    case GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE:
        return renderbuffer_component_bits(internal_format, GL_RENDERBUFFER_STENCIL_SIZE);
    default:
        return 0;
    }
}

static GLenum attachment_component_type(GLint internal_format) {
    switch (internal_format) {
    case GL_RGBA16F:
    case GL_RGBA32F:
    case GL_DEPTH_COMPONENT:
    case GL_DEPTH_COMPONENT32F:
        return GL_FLOAT;
    case 1:
    case 2:
    case 3:
    case 4:
    case GL_RED:
    case GL_RG:
    case GL_RGB:
    case GL_RGBA:
    case GL_R8:
    case GL_RG8:
    case GL_RGB8:
    case GL_RGBA8:
    case GL_DEPTH_STENCIL:
    case GL_DEPTH24_STENCIL8:
        return GL_UNSIGNED_NORMALIZED;
    default:
        return GL_NONE;
    }
}

static GX2LogicOp map_framebuffer_logic_op(GLenum op) {
    switch (op) {
    case GL_CLEAR:         return GX2_LOGIC_OP_CLEAR;
    case GL_SET:           return GX2_LOGIC_OP_SET;
    case GL_COPY:          return GX2_LOGIC_OP_COPY;
    case GL_COPY_INVERTED: return GX2_LOGIC_OP_INV_COPY;
    case GL_NOOP:          return GX2_LOGIC_OP_NOP;
    case GL_INVERT:        return GX2_LOGIC_OP_INV;
    case GL_AND:           return GX2_LOGIC_OP_AND;
    case GL_NAND:          return GX2_LOGIC_OP_NOT_AND;
    case GL_OR:            return GX2_LOGIC_OP_OR;
    case GL_NOR:           return GX2_LOGIC_OP_NOR;
    case GL_XOR:           return GX2_LOGIC_OP_XOR;
    case GL_EQUIV:         return GX2_LOGIC_OP_EQUIV;
    case GL_AND_REVERSE:   return GX2_LOGIC_OP_REV_AND;
    case GL_AND_INVERTED:  return GX2_LOGIC_OP_INV_AND;
    case GL_OR_REVERSE:    return GX2_LOGIC_OP_REV_OR;
    case GL_OR_INVERTED:   return GX2_LOGIC_OP_INV_OR;
    default:               return GX2_LOGIC_OP_COPY;
    }
}

static bool get_renderbuffer_format_info(GLenum internalformat, GX2SurfaceFormat *gx2_format, GX2SurfaceUse *surface_use, bool *is_depth) {
    if (!gx2_format || !surface_use || !is_depth) return false;
    switch (internalformat) {
    case 1: case GL_RED: case GL_R8:
        *gx2_format = GX2_SURFACE_FORMAT_UNORM_R8;
        *surface_use = (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
        *is_depth = false; return true;
    case 2: case GL_RG: case GL_RG8:
        *gx2_format = GX2_SURFACE_FORMAT_UNORM_R8_G8;
        *surface_use = (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
        *is_depth = false; return true;
    case 3: case GL_RGB: case GL_RGB8:
    case 4: case GL_RGBA: case GL_RGBA8:
        *gx2_format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
        *surface_use = (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
        *is_depth = false; return true;
    case GL_RGBA16F:
        *gx2_format = GX2_SURFACE_FORMAT_FLOAT_R16_G16_B16_A16;
        *surface_use = (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
        *is_depth = false; return true;
    case GL_RGBA32F:
        *gx2_format = GX2_SURFACE_FORMAT_FLOAT_R32_G32_B32_A32;
        *surface_use = (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
        *is_depth = false; return true;
    case GL_DEPTH_COMPONENT: case GL_DEPTH_COMPONENT32F:
        *gx2_format = GX2_SURFACE_FORMAT_FLOAT_R32;
        *surface_use = (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_DEPTH_BUFFER);
        *is_depth = true; return true;
    case GL_DEPTH_STENCIL: case GL_DEPTH24_STENCIL8:
        *gx2_format = GX2_SURFACE_FORMAT_UNORM_R24_X8;
        *surface_use = (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_DEPTH_BUFFER);
        *is_depth = true; return true;
    default: return false;
    }
}

static GX2ChannelMask build_color_channel_mask(void) {
    uint8_t mask = 0;
    if (!g_gl_context) return (GX2ChannelMask)0;
    if (g_gl_context->color_mask[0]) mask |= GX2_CHANNEL_MASK_R;
    if (g_gl_context->color_mask[1]) mask |= GX2_CHANNEL_MASK_G;
    if (g_gl_context->color_mask[2]) mask |= GX2_CHANNEL_MASK_B;
    if (g_gl_context->color_mask[3]) mask |= GX2_CHANNEL_MASK_A;
    return (GX2ChannelMask)mask;
}

static void apply_framebuffer_output_state(GLFramebuffer *fb, bool is_default) {
    GX2ChannelMask masks[8] = {(GX2ChannelMask)0};
    GX2ChannelMask color_mask;
    uint8_t active_target_mask = 0;
    uint32_t active_target_count = 0;
    if (!g_gl_context || !fb) return;
    color_mask = build_color_channel_mask();
    for (uint32_t i = 0; i < 8; ++i) {
        bool enabled = false;
        uint32_t attachment_index = 0;
        if (is_default) enabled = (i == 0 && fb->draw_buffers[0] == GL_BACK);
        else if (get_color_attachment_index(fb->draw_buffers[i], &attachment_index) &&
                 attachment_ref_present(&fb->color_attachments[attachment_index])) enabled = true;
        if (enabled && color_mask != 0) {
            masks[i] = color_mask;
            active_target_mask |= (uint8_t)(1u << i);
            ++active_target_count;
        }
    }
    GX2SetTargetChannelMasks(masks[0], masks[1], masks[2], masks[3], masks[4], masks[5], masks[6], masks[7]);
    GX2SetColorControl(map_framebuffer_logic_op(g_gl_context->logic_op),
                       g_gl_context->blend_enabled ? active_target_mask : 0,
                       active_target_count > 1 ? GX2_ENABLE : GX2_DISABLE,
                       active_target_mask != 0 ? GX2_ENABLE : GX2_DISABLE);
}

static void init_depth_buffer_from_surface_view(GX2DepthBuffer *db,
                                                const GX2Surface *s,
                                                GLint level,
                                                uint32_t slice) {
    memset(db, 0, sizeof(*db));
    db->surface = *s;
    db->viewMip = (uint32_t)(level < 0 ? 0 : level);
    db->viewFirstSlice = slice;
    db->viewNumSlices = 1;
    db->depthClear = 1.0f;
    GX2InitDepthBufferRegs(db);
}

static void init_depth_buffer_from_surface(GX2DepthBuffer *db, const GX2Surface *s) {
    init_depth_buffer_from_surface_view(db, s, 0, 0);
}

typedef struct {
    GLsizei width;
    GLsizei height;
    GLsizei samples;
    GLint internal_format;
    bool depth_renderable;
    bool stencil_renderable;
} GLAttachmentImageInfo;

static bool get_attachment_image_info(const GLAttachmentRef *attachment,
                                      GLAttachmentImageInfo *info) {
    if (!attachment_ref_present(attachment) || !info) return false;

    if (attachment->kind == GL_ATTACHMENT_KIND_TEXTURE) {
        GX2Texture *texture = gl_get_gx2_texture(attachment->object);
        if (!attachment_view_exists(attachment, texture)) return false;
        info->width = (GLsizei)texture_level_extent(texture->surface.width,
                                                    attachment->level);
        info->height = (GLsizei)texture_level_extent(texture->surface.height,
                                                     attachment->level);
        info->samples = 0;
        info->internal_format = gl_get_texture_internal_format(attachment->object);
        info->depth_renderable = is_depth_internal_format(info->internal_format);
        info->stencil_renderable = is_stencil_internal_format(info->internal_format);
        return true;
    }

    if (attachment->kind == GL_ATTACHMENT_KIND_RENDERBUFFER) {
        GLRenderbuffer *renderbuffer = get_renderbuffer(attachment->object);
        if (!renderbuffer) return false;
        info->width = renderbuffer->width;
        info->height = renderbuffer->height;
        info->samples = renderbuffer->samples;
        info->internal_format = renderbuffer->internal_format;
        info->depth_renderable = is_depth_internal_format(info->internal_format);
        info->stencil_renderable = is_stencil_internal_format(info->internal_format);
        return true;
    }

    return false;
}

static float decode_gpu_float32(const uint8_t *ptr) {
    uint32_t word;
    float value;

    memcpy(&word, ptr, sizeof(word));
    word = GPU_TO_CPU_32(word);
    memcpy(&value, &word, sizeof(value));
    return value;
}

static void encode_gpu_float32(uint8_t *ptr, float value) {
    uint32_t word;

    memcpy(&word, &value, sizeof(word));
    word = CPU_TO_GPU_32(word);
    memcpy(ptr, &word, sizeof(word));
}

static float decode_gpu_half_float(const uint8_t *ptr) {
    uint16_t word;
    uint32_t sign;
    uint32_t exponent;
    uint32_t mantissa;
    uint32_t out_word;
    float value;

    memcpy(&word, ptr, sizeof(word));
    word = GPU_TO_CPU_16(word);

    sign = (uint32_t)(word & 0x8000u) << 16;
    exponent = (word >> 10) & 0x1Fu;
    mantissa = word & 0x03FFu;

    if (exponent == 0) {
        if (mantissa == 0) {
            out_word = sign;
        } else {
            exponent = 1;
            while ((mantissa & 0x0400u) == 0) {
                mantissa <<= 1;
                --exponent;
            }
            mantissa &= 0x03FFu;
            out_word = sign | ((exponent + (127u - 15u)) << 23) | (mantissa << 13);
        }
    } else if (exponent == 0x1Fu) {
        out_word = sign | 0x7F800000u | (mantissa << 13);
    } else {
        out_word = sign | ((exponent + (127u - 15u)) << 23) | (mantissa << 13);
    }

    memcpy(&value, &out_word, sizeof(value));
    return value;
}

static uint8_t float_to_unorm8(float value) {
    if (value <= 0.0f) return 0;
    if (value >= 1.0f) return 255;
    return (uint8_t)(value * 255.0f + 0.5f);
}

static GX2ColorBuffer *get_read_color_buffer(void) {
    uint32_t attachment_index = 0;
    GLuint fbo;

    if (!g_gl_context) return NULL;

    fbo = g_gl_context->bound_read_framebuffer;
    if (fbo == 0) {
        if (g_framebuffers[0].read_buffer != GL_BACK) return NULL;
        return get_default_color_buffer();
    }

    if (fbo >= MAX_FRAMEBUFFERS || !g_framebuffers[fbo].in_use) return NULL;

    if (g_gl_context->bound_framebuffer == fbo) {
        gl_bind_framebuffers();
    } else {
        GLFramebuffer *fb = &g_framebuffers[fbo];
        if (fb->dirty) {
            for (uint32_t i = 0; i < 8; ++i) {
                if (!attachment_ref_present(&fb->color_attachments[i])) {
                    free_framebuffer_texture_target(fb, i);
                    continue;
                }
                if (fb->color_attachments[i].kind == GL_ATTACHMENT_KIND_TEXTURE) {
                    GX2Texture *t = gl_get_gx2_texture(fb->color_attachments[i].object);
                    free_framebuffer_texture_target(fb, i);
                    if (t && init_color_buffer_from_surface_view(
                                 &fb->cb[i], &t->surface,
                                 fb->color_attachments[i].level,
                                 attachment_view_slice(&fb->color_attachments[i]))) {
                        fb->color_buffer_owns_aux[i] = true;
                    }
                } else {
                    free_framebuffer_texture_target(fb, i);
                    GLRenderbuffer *rb = get_renderbuffer(fb->color_attachments[i].object);
                    if (rb && !rb->is_depth) {
                        fb->cb[i] = rb->color_buffer;
                        fb->color_buffer_owns_aux[i] = false;
                    }
                }
            }
            memset(&fb->db, 0, sizeof(fb->db));
            if (attachment_ref_present(&fb->depth_attachment)) {
                if (fb->depth_attachment.kind == GL_ATTACHMENT_KIND_TEXTURE) {
                    GX2Texture *t = gl_get_gx2_texture(fb->depth_attachment.object);
                    if (t) init_depth_buffer_from_surface_view(
                               &fb->db, &t->surface, fb->depth_attachment.level,
                               attachment_view_slice(&fb->depth_attachment));
                } else {
                    GLRenderbuffer *rb = get_renderbuffer(fb->depth_attachment.object);
                    if (rb && rb->is_depth) fb->db = rb->depth_buffer;
                }
            } else if (attachment_ref_present(&fb->stencil_attachment)) {
                if (fb->stencil_attachment.kind == GL_ATTACHMENT_KIND_TEXTURE) {
                    GX2Texture *t = gl_get_gx2_texture(fb->stencil_attachment.object);
                    if (t) init_depth_buffer_from_surface_view(
                               &fb->db, &t->surface, fb->stencil_attachment.level,
                               attachment_view_slice(&fb->stencil_attachment));
                } else {
                    GLRenderbuffer *rb = get_renderbuffer(fb->stencil_attachment.object);
                    if (rb && rb->is_depth) fb->db = rb->depth_buffer;
                }
            }
            fb->dirty = false;
        }
    }

    resolve_framebuffer_texture_targets(&g_framebuffers[fbo]);

    if (g_framebuffers[fbo].read_buffer == GL_NONE) return NULL;
    if (!get_color_attachment_index(g_framebuffers[fbo].read_buffer, &attachment_index)) {
        return NULL;
    }

    if (!attachment_ref_present(&g_framebuffers[g_gl_context->bound_read_framebuffer].color_attachments[attachment_index])) {
        return NULL;
    }
    return &g_framebuffers[g_gl_context->bound_read_framebuffer].cb[attachment_index];
}

static GLint get_read_color_internal_format(void) {
    GLuint fbo;
    uint32_t attachment_index = 0;

    if (!g_gl_context) return GL_RGBA8;

    fbo = g_gl_context->bound_read_framebuffer;
    if (fbo == 0) {
        return GL_RGBA8;
    }
    if (fbo >= MAX_FRAMEBUFFERS || !g_framebuffers[fbo].in_use) {
        return GL_RGBA8;
    }
    if (!get_color_attachment_index(g_framebuffers[fbo].read_buffer, &attachment_index)) {
        return GL_RGBA8;
    }

    if (g_framebuffers[fbo].color_attachments[attachment_index].kind == GL_ATTACHMENT_KIND_TEXTURE) {
        return gl_get_texture_internal_format(g_framebuffers[fbo].color_attachments[attachment_index].object);
    }
    if (g_framebuffers[fbo].color_attachments[attachment_index].kind == GL_ATTACHMENT_KIND_RENDERBUFFER) {
        GLRenderbuffer *rb = get_renderbuffer(g_framebuffers[fbo].color_attachments[attachment_index].object);
        return rb ? rb->internal_format : GL_RGBA8;
    }

    return GL_RGBA8;
}

static GX2ColorBuffer *get_default_color_buffer(void) {
    GX2ColorBuffer *preferred = g_default_framebuffer_uses_drc ? WHBGfxGetDRCColourBuffer()
                                                               : WHBGfxGetTVColourBuffer();
    if (preferred) {
        return preferred;
    }
    return g_default_framebuffer_uses_drc ? WHBGfxGetTVColourBuffer()
                                          : WHBGfxGetDRCColourBuffer();
}

static GX2DepthBuffer *get_default_depth_buffer(void) {
    GX2DepthBuffer *preferred = g_default_framebuffer_uses_drc ? WHBGfxGetDRCDepthBuffer()
                                                               : WHBGfxGetTVDepthBuffer();
    if (preferred) {
        return preferred;
    }
    return g_default_framebuffer_uses_drc ? WHBGfxGetTVDepthBuffer()
                                          : WHBGfxGetDRCDepthBuffer();
}

void gl_framebuffer_init(void) {
    memset(g_framebuffers, 0, sizeof(g_framebuffers));
    memset(g_renderbuffers, 0, sizeof(g_renderbuffers));
    g_default_framebuffer_uses_drc = false;
    init_framebuffer_object(&g_framebuffers[0], true);
}

#ifdef __cplusplus
extern "C" {
#endif

void gl_framebuffer_set_default_target_drc(GLboolean use_drc) {
    g_default_framebuffer_uses_drc = use_drc ? true : false;
    if (g_gl_context && g_gl_context->bound_framebuffer == 0)
        g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
}

void _gl_GenFramebuffers(GLsizei n, GLuint *fbs) {
    if (!g_gl_context) return;
    if (n < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    if (n > 0 && !fbs) { _gl_set_error(GL_INVALID_VALUE); return; }
    int count = 0;
    for (int i = 1; i < MAX_FRAMEBUFFERS && count < n; i++) {
        if (!g_framebuffers[i].reserved && !g_framebuffers[i].in_use) {
            reserve_framebuffer_name(&g_framebuffers[i]);
            fbs[count++] = i;
        }
    }
    if (count < n) {
        for (int i = count; i < n; ++i) fbs[i] = 0;
        _gl_set_error(GL_OUT_OF_MEMORY);
    }
}

void _gl_GenRenderbuffers(GLsizei n, GLuint *rbs) {
    if (!g_gl_context) return;
    if (n < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    if (n > 0 && !rbs) { _gl_set_error(GL_INVALID_VALUE); return; }
    int count = 0;
    for (int i = 1; i < MAX_RENDERBUFFERS && count < n; i++) {
        if (!g_renderbuffers[i].reserved && !g_renderbuffers[i].in_use) {
            reserve_renderbuffer_name(&g_renderbuffers[i]);
            rbs[count++] = i;
        }
    }
    if (count < n) {
        for (int i = count; i < n; ++i) rbs[i] = 0;
        _gl_set_error(GL_OUT_OF_MEMORY);
    }
}

GLboolean _gl_IsRenderbuffer(GLuint rb) { return get_renderbuffer(rb) ? GL_TRUE : GL_FALSE; }
void _gl_DeleteRenderbuffers(GLsizei n, const GLuint *rbs) {
    if (!g_gl_context) return;
    if (n < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    if (n > 0 && !rbs) { _gl_set_error(GL_INVALID_VALUE); return; }
    for (int i = 0; i < n; i++) {
        GLuint id = rbs[i];
        if (id > 0 && id < MAX_RENDERBUFFERS &&
            (g_renderbuffers[id].reserved || g_renderbuffers[id].in_use)) {
            if (g_gl_context->bound_renderbuffer == id) {
                g_gl_context->bound_renderbuffer = 0;
            }
            if (g_renderbuffers[id].in_use) {
                detach_renderbuffer_from_current_framebuffers(id);
                free_color_buffer_aux(&g_renderbuffers[id].color_buffer);
                free_surface_storage(&g_renderbuffers[id].surface);
            }
            memset(&g_renderbuffers[id], 0, sizeof(GLRenderbuffer));
        }
    }
}

GLboolean _gl_IsFramebuffer(GLuint fb) {
    return (fb > 0 && fb < MAX_FRAMEBUFFERS && g_framebuffers[fb].in_use)
               ? GL_TRUE
               : GL_FALSE;
}
void _gl_DeleteFramebuffers(GLsizei n, const GLuint *fbs) {
    if (!g_gl_context) return;
    if (n < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    if (n > 0 && !fbs) { _gl_set_error(GL_INVALID_VALUE); return; }
    for (int i = 0; i < n; i++) {
        GLuint id = fbs[i];
        if (id > 0 && id < MAX_FRAMEBUFFERS &&
            (g_framebuffers[id].reserved || g_framebuffers[id].in_use)) {
            if (g_gl_context->bound_framebuffer == id) {
                g_gl_context->bound_framebuffer = 0;
                g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
            }
            if (g_gl_context->bound_read_framebuffer == id) {
                g_gl_context->bound_read_framebuffer = 0;
            }
            if (g_framebuffers[id].in_use) {
                resolve_framebuffer_texture_targets(&g_framebuffers[id]);
                for (uint32_t j = 0; j < 8; ++j) {
                    free_framebuffer_texture_target(&g_framebuffers[id], j);
                }
            }
            memset(&g_framebuffers[id], 0, sizeof(GLFramebuffer));
        }
    }
}

void _gl_BindFramebuffer(GLenum target, GLuint fb) {
    GLuint previous_draw_fb;
    if (!g_gl_context) return;
    if (!is_framebuffer_target(target)) { _gl_set_error(GL_INVALID_ENUM); return; }
    if (!framebuffer_name_is_valid(fb)) { _gl_set_error(GL_INVALID_OPERATION); return; }
    if (fb > 0 && !g_framebuffers[fb].in_use) {
        init_framebuffer_object(&g_framebuffers[fb], false);
    }
    previous_draw_fb = g_gl_context->bound_framebuffer;
    if ((target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) && previous_draw_fb != fb && previous_draw_fb < MAX_FRAMEBUFFERS)
        resolve_framebuffer_texture_targets(&g_framebuffers[previous_draw_fb]);
    if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) { g_gl_context->bound_framebuffer = fb; g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER; }
    if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER) { g_gl_context->bound_read_framebuffer = fb; }
}

void _gl_BindRenderbuffer(GLenum target, GLuint rb) {
    if (!g_gl_context) return;
    if (target != GL_RENDERBUFFER) { _gl_set_error(GL_INVALID_ENUM); return; }
    if (!renderbuffer_name_is_valid(rb)) { _gl_set_error(GL_INVALID_OPERATION); return; }
    if (rb > 0 && !g_renderbuffers[rb].in_use) {
        init_renderbuffer_object(&g_renderbuffers[rb]);
    }
    g_gl_context->bound_renderbuffer = rb;
}

void _gl_FramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    GLenum texture_target;

    if (!g_gl_context) return;
    if (!is_framebuffer_target(target) ||
        (textarget != GL_TEXTURE_2D && !is_cube_map_face_target(textarget))) { _gl_set_error(GL_INVALID_ENUM); return; }
    if (level < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    if (texture != 0) {
        if (_gl_IsTexture(texture) != GL_TRUE) { _gl_set_error(GL_INVALID_OPERATION); return; }
        texture_target = gl_get_texture_target(texture);
        if ((textarget == GL_TEXTURE_2D && texture_target != GL_TEXTURE_2D) ||
            (is_cube_map_face_target(textarget) && texture_target != GL_TEXTURE_CUBE_MAP)) {
            _gl_set_error(GL_INVALID_OPERATION);
            return;
        }
        if (level != 0) {
            _gl_set_error(GL_INVALID_VALUE);
            return;
        }
    }
    GLuint fbo = get_bound_framebuffer_for_target(target);
    if (fbo == 0) { _gl_set_error(GL_INVALID_OPERATION); return; }
    if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        if (texture == 0) {
            clear_attachment_ref(&g_framebuffers[fbo].depth_attachment);
            clear_attachment_ref(&g_framebuffers[fbo].stencil_attachment);
        } else {
            set_texture_attachment_ref(&g_framebuffers[fbo].depth_attachment,
                                       texture, textarget, level, 0, GL_FALSE);
            set_texture_attachment_ref(&g_framebuffers[fbo].stencil_attachment,
                                       texture, textarget, level, 0, GL_FALSE);
        }
        g_framebuffers[fbo].dirty = true;
        g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
        return;
    }
    GLAttachmentRef *ref = get_attachment_ref(&g_framebuffers[fbo], attachment);
    if (!ref) { _gl_set_error(GL_INVALID_ENUM); return; }
    if (texture == 0) clear_attachment_ref(ref);
    else set_texture_attachment_ref(ref, texture, textarget, level, 0, GL_FALSE);
    if (attachment >= GL_COLOR_ATTACHMENT0 && attachment <= GL_COLOR_ATTACHMENT7)
        free_framebuffer_texture_target(&g_framebuffers[fbo], attachment - GL_COLOR_ATTACHMENT0);
    g_framebuffers[fbo].dirty = true;
    g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
}

void _gl_FramebufferTextureLayer(GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer) {
    GLenum texture_target;
    GLuint fbo;
    GLFramebuffer *fb;

    if (!g_gl_context) return;
    if (!is_framebuffer_target(target)) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (attachment != GL_DEPTH_STENCIL_ATTACHMENT &&
        !get_attachment_ref(&g_framebuffers[0], attachment)) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (level < 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (texture != 0) {
        if (_gl_IsTexture(texture) != GL_TRUE) {
            _gl_set_error(GL_INVALID_VALUE);
            return;
        }
        if (layer < 0 ||
            layer >= GX2GL_MAX_FRAMEBUFFER_3D_TEXTURE_SIZE ||
            level > max_texture_level_for_size(GX2GL_MAX_FRAMEBUFFER_3D_TEXTURE_SIZE)) {
            _gl_set_error(GL_INVALID_VALUE);
            return;
        }
        texture_target = gl_get_texture_target(texture);
        if (texture_target != GL_TEXTURE_3D) {
            _gl_set_error(GL_INVALID_OPERATION);
            return;
        }
    }

    fbo = get_bound_framebuffer_for_target(target);
    if (fbo == 0) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }

    fb = &g_framebuffers[fbo];
    if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        if (texture == 0) {
            clear_attachment_ref(&fb->depth_attachment);
            clear_attachment_ref(&fb->stencil_attachment);
        } else {
            set_texture_attachment_ref(&fb->depth_attachment, texture,
                                       GL_TEXTURE_3D, level, layer, GL_FALSE);
            set_texture_attachment_ref(&fb->stencil_attachment, texture,
                                       GL_TEXTURE_3D, level, layer, GL_FALSE);
        }
        fb->dirty = true;
        g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
        return;
    }

    {
        GLAttachmentRef *ref = get_attachment_ref(fb, attachment);
        if (!ref) {
            _gl_set_error(GL_INVALID_ENUM);
            return;
        }
        if (texture == 0) clear_attachment_ref(ref);
        else set_texture_attachment_ref(ref, texture, GL_TEXTURE_3D, level,
                                        layer, GL_FALSE);
    }

    if (attachment >= GL_COLOR_ATTACHMENT0 && attachment <= GL_COLOR_ATTACHMENT7) {
        free_framebuffer_texture_target(fb, attachment - GL_COLOR_ATTACHMENT0);
    }
    fb->dirty = true;
    g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
}

void _gl_FramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum rbtarget, GLuint rb) {
    if (!g_gl_context) return;
    if (!is_framebuffer_target(target) || rbtarget != GL_RENDERBUFFER) { _gl_set_error(GL_INVALID_ENUM); return; }
    if (rb != 0 && _gl_IsRenderbuffer(rb) != GL_TRUE) { _gl_set_error(GL_INVALID_OPERATION); return; }
    GLuint fbo = get_bound_framebuffer_for_target(target);
    if (fbo == 0) { _gl_set_error(GL_INVALID_OPERATION); return; }
    if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        if (rb == 0) {
            clear_attachment_ref(&g_framebuffers[fbo].depth_attachment);
            clear_attachment_ref(&g_framebuffers[fbo].stencil_attachment);
        } else {
            set_renderbuffer_attachment_ref(&g_framebuffers[fbo].depth_attachment, rb);
            set_renderbuffer_attachment_ref(&g_framebuffers[fbo].stencil_attachment, rb);
        }
        g_framebuffers[fbo].dirty = true;
        g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
        return;
    }
    GLAttachmentRef *ref = get_attachment_ref(&g_framebuffers[fbo], attachment);
    if (!ref) { _gl_set_error(GL_INVALID_ENUM); return; }
    if (rb == 0) clear_attachment_ref(ref);
    else set_renderbuffer_attachment_ref(ref, rb);
    if (attachment >= GL_COLOR_ATTACHMENT0 && attachment <= GL_COLOR_ATTACHMENT7)
        free_framebuffer_texture_target(&g_framebuffers[fbo], attachment - GL_COLOR_ATTACHMENT0);
    g_framebuffers[fbo].dirty = true;
    g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
}

static void renderbuffer_storage(GLenum target,
                                 GLsizei samples,
                                 GLenum internalformat,
                                 GLsizei width,
                                 GLsizei height) {
    GX2Surface new_surface;
    GX2ColorBuffer new_color_buffer;
    GX2DepthBuffer new_depth_buffer;

    if (!g_gl_context || target != GL_RENDERBUFFER) { _gl_set_error(GL_INVALID_ENUM); return; }
    if (samples < 0 || samples > GX2GL_MAX_RENDERBUFFER_SAMPLES ||
        width < 0 || height < 0 ||
        width > GX2GL_MAX_RENDERBUFFER_SIZE ||
        height > GX2GL_MAX_RENDERBUFFER_SIZE) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    GLuint id = g_gl_context->bound_renderbuffer;
    if (id == 0) { _gl_set_error(GL_INVALID_OPERATION); return; }
    GLRenderbuffer *rb = &g_renderbuffers[id];
    GX2SurfaceFormat fmt; GX2SurfaceUse use; bool is_depth;
    GX2RResourceFlags surface_flags;
    if (!get_renderbuffer_format_info(internalformat, &fmt, &use, &is_depth)) { _gl_set_error(GL_INVALID_ENUM); return; }

    memset(&new_surface, 0, sizeof(new_surface));
    memset(&new_color_buffer, 0, sizeof(new_color_buffer));
    memset(&new_depth_buffer, 0, sizeof(new_depth_buffer));

    if (width == 0 || height == 0) {
        free_color_buffer_aux(&rb->color_buffer);
        free_surface_storage(&rb->surface);
        memset(&rb->color_buffer, 0, sizeof(rb->color_buffer));
        memset(&rb->depth_buffer, 0, sizeof(rb->depth_buffer));
        rb->width = width;
        rb->height = height;
        rb->internal_format = internalformat;
        rb->is_depth = is_depth;
        rb->samples = samples == 0 ? 0 : 1;
        mark_framebuffers_for_renderbuffer(id);
        g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
        return;
    }

    new_surface.dim = GX2_SURFACE_DIM_TEXTURE_2D; new_surface.width = width; new_surface.height = height;
    new_surface.depth = 1; new_surface.mipLevels = 1; new_surface.format = fmt; new_surface.aa = GX2_AA_MODE1X; new_surface.use = use;
    new_surface.tileMode = is_depth ? GX2_TILE_MODE_DEFAULT : GX2_TILE_MODE_LINEAR_ALIGNED;
    GX2CalcSurfaceSizeAndAlignment(&new_surface);
    surface_flags = build_framebuffer_surface_flags(internalformat, is_depth, false);
    if (!GX2RCreateSurface(&new_surface, surface_flags)) {
        memset(&new_surface, 0, sizeof(new_surface));
        _gl_set_error(GL_OUT_OF_MEMORY);
        return;
    }
    if (new_surface.image && new_surface.imageSize)
        memset(new_surface.image, 0, new_surface.imageSize);
    if (new_surface.mipmaps && new_surface.mipmapSize)
        memset(new_surface.mipmaps, 0, new_surface.mipmapSize);
    if (is_depth) init_depth_buffer_from_surface(&new_depth_buffer, &new_surface);
    else if (!init_color_buffer_from_surface(&new_color_buffer, &new_surface)) {
        free_surface_storage(&new_surface);
        _gl_set_error(GL_OUT_OF_MEMORY);
        return;
    }

    free_color_buffer_aux(&rb->color_buffer);
    free_surface_storage(&rb->surface);
    rb->surface = new_surface;
    rb->color_buffer = new_color_buffer;
    rb->depth_buffer = new_depth_buffer;
    rb->width = width;
    rb->height = height;
    rb->internal_format = internalformat;
    rb->is_depth = is_depth;
    rb->samples = samples == 0 ? 0 : 1;
    mark_framebuffers_for_renderbuffer(id);
    g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
}

void _gl_RenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) {
    renderbuffer_storage(target, 0, internalformat, width, height);
}

void _gl_ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels) {
    size_t tight_row_bytes;
    size_t tight_size;
    uint8_t *tight_pixels;
    uint8_t *dst_base;
    GLint pack_row_length;
    GLint pack_skip_rows;
    GLint pack_skip_pixels;
    GLint pack_alignment;
    size_t dst_row_pixels;
    size_t dst_row_bytes;
    size_t dst_stride;

    if (!g_gl_context) return;
    if (width < 0 || height < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    if (!pixels) { _gl_set_error(GL_INVALID_OPERATION); return; }
    if (format != GL_RGBA || type != GL_UNSIGNED_BYTE) { _gl_set_error(GL_INVALID_ENUM); return; }
    if (_gl_CheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        _gl_set_error(GL_INVALID_FRAMEBUFFER_OPERATION);
        return;
    }
    if (width == 0 || height == 0) return;
    if (!get_read_color_buffer()) { _gl_set_error(GL_INVALID_OPERATION); return; }

    tight_row_bytes = (size_t)width * 4u;
    tight_size = tight_row_bytes * (size_t)height;
    tight_pixels = (uint8_t *)gl_mem_alloc(GL_MEM_TYPE_MEM2, tight_size, 64);
    if (!tight_pixels) {
        _gl_set_error(GL_OUT_OF_MEMORY);
        return;
    }

    if (!gl_read_color_pixels_rgba8(x, y, width, height, tight_pixels)) {
        gl_mem_free(GL_MEM_TYPE_MEM2, tight_pixels);
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }

    pack_row_length = g_gl_context->pack_row_length > 0 ? g_gl_context->pack_row_length : width;
    pack_skip_rows = g_gl_context->pack_skip_rows > 0 ? g_gl_context->pack_skip_rows : 0;
    pack_skip_pixels = g_gl_context->pack_skip_pixels > 0 ? g_gl_context->pack_skip_pixels : 0;
    pack_alignment = g_gl_context->pack_alignment > 0 ? g_gl_context->pack_alignment : 1;
    if (pack_row_length < width) {
        gl_mem_free(GL_MEM_TYPE_MEM2, tight_pixels);
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    dst_row_pixels = (size_t)pack_row_length;
    dst_row_bytes = dst_row_pixels * 4u;
    dst_stride = ((dst_row_bytes + (size_t)pack_alignment - 1u) / (size_t)pack_alignment) * (size_t)pack_alignment;
    dst_base = (uint8_t *)pixels + (size_t)pack_skip_rows * dst_stride + (size_t)pack_skip_pixels * 4u;

    for (GLsizei row = 0; row < height; ++row) {
        memcpy(dst_base + (size_t)row * dst_stride,
               tight_pixels + (size_t)row * tight_row_bytes,
               tight_row_bytes);
    }

    gl_mem_free(GL_MEM_TYPE_MEM2, tight_pixels);
}

void _gl_FramebufferTexture(GLenum target, GLenum attachment, GLuint texture, GLint level) {
    _gl_FramebufferTexture2D(target, attachment, GL_TEXTURE_2D, texture, level);
}

static GX2ColorBuffer *get_draw_blit_color_buffer(void) {
    GLuint fbo;

    if (!g_gl_context) return NULL;
    fbo = g_gl_context->bound_framebuffer;
    if (fbo == 0) {
        return g_framebuffers[0].draw_buffers[0] == GL_BACK ? get_default_color_buffer() : NULL;
    }

    gl_bind_framebuffers();

    if (fbo >= MAX_FRAMEBUFFERS || !g_framebuffers[fbo].in_use) return NULL;
    for (uint32_t i = 0; i < 8; ++i) {
        uint32_t attachment_index = 0;
        if (get_color_attachment_index(g_framebuffers[fbo].draw_buffers[i], &attachment_index) &&
            attachment_ref_present(&g_framebuffers[fbo].color_attachments[attachment_index])) {
            return &g_framebuffers[fbo].cb[attachment_index];
        }
    }
    return NULL;
}

static uint16_t encode_half_float_bits(float value) {
    uint32_t word;
    uint32_t sign;
    int32_t exponent;
    uint32_t mantissa;

    memcpy(&word, &value, sizeof(word));
    sign = (word >> 16) & 0x8000u;
    exponent = (int32_t)((word >> 23) & 0xFFu) - 127 + 15;
    mantissa = word & 0x7FFFFFu;

    if (((word >> 23) & 0xFFu) == 0xFFu) {
        if (mantissa != 0) return (uint16_t)(sign | 0x7E00u);
        return (uint16_t)(sign | 0x7C00u);
    }
    if (exponent <= 0) {
        if (exponent < -10) return (uint16_t)sign;
        mantissa |= 0x800000u;
        mantissa = mantissa >> (uint32_t)(1 - exponent);
        if (mantissa & 0x00001000u) mantissa += 0x00002000u;
        return (uint16_t)(sign | (mantissa >> 13));
    }
    if (exponent >= 31) {
        return (uint16_t)(sign | 0x7C00u);
    }
    if (mantissa & 0x00001000u) {
        mantissa += 0x00002000u;
        if (mantissa & 0x00800000u) {
            mantissa = 0;
            ++exponent;
            if (exponent >= 31) return (uint16_t)(sign | 0x7C00u);
        }
    }
    return (uint16_t)(sign | ((uint32_t)exponent << 10) | (mantissa >> 13));
}

static void encode_gpu_half_float(uint8_t *ptr, float value) {
    uint16_t word = CPU_TO_GPU_16(encode_half_float_bits(value));
    memcpy(ptr, &word, sizeof(word));
}

static uint8_t clamp_u8_from_float(float value) {
    if (value <= 0.0f) return 0;
    if (value >= 255.0f) return 255;
    return (uint8_t)(value + 0.5f);
}

static const uint8_t *get_rgba8_source_texel(const uint8_t *pixels,
                                             GLsizei width,
                                             GLsizei height,
                                             GLint x,
                                             GLint y,
                                             bool flip_x,
                                             bool flip_y) {
    GLint sample_x = flip_x ? (width - 1 - x) : x;
    GLint sample_y = flip_y ? (height - 1 - y) : y;

    if (sample_x < 0) sample_x = 0;
    if (sample_x >= width) sample_x = width - 1;
    if (sample_y < 0) sample_y = 0;
    if (sample_y >= height) sample_y = height - 1;

    return pixels + (((size_t)sample_y * (size_t)width) + (size_t)sample_x) * 4u;
}

static void scale_rgba8_pixels(const uint8_t *src_pixels,
                               GLsizei src_width,
                               GLsizei src_height,
                               GLsizei dst_width,
                               GLsizei dst_height,
                               GLenum filter,
                               bool src_flip_x,
                               bool src_flip_y,
                               uint8_t *dst_pixels) {
    for (GLsizei dy = 0; dy < dst_height; ++dy) {
        for (GLsizei dx = 0; dx < dst_width; ++dx) {
            uint8_t *dst_texel = dst_pixels + (((size_t)dy * (size_t)dst_width) + (size_t)dx) * 4u;

            if (filter == GL_NEAREST) {
                GLint sx = (GLint)(((float)dx + 0.5f) * (float)src_width / (float)dst_width);
                GLint sy = (GLint)(((float)dy + 0.5f) * (float)src_height / (float)dst_height);
                const uint8_t *src_texel;

                if (sx >= src_width) sx = src_width - 1;
                if (sy >= src_height) sy = src_height - 1;
                src_texel = get_rgba8_source_texel(src_pixels, src_width, src_height,
                                                   sx, sy, src_flip_x, src_flip_y);
                memcpy(dst_texel, src_texel, 4u);
                continue;
            }

            {
                float src_x = (((float)dx + 0.5f) * (float)src_width / (float)dst_width) - 0.5f;
                float src_y = (((float)dy + 0.5f) * (float)src_height / (float)dst_height) - 0.5f;
                GLint x0 = (GLint)floorf(src_x);
                GLint y0 = (GLint)floorf(src_y);
                GLint x1 = x0 + 1;
                GLint y1 = y0 + 1;
                float tx = src_x - (float)x0;
                float ty = src_y - (float)y0;
                const uint8_t *p00;
                const uint8_t *p10;
                const uint8_t *p01;
                const uint8_t *p11;

                p00 = get_rgba8_source_texel(src_pixels, src_width, src_height, x0, y0, src_flip_x, src_flip_y);
                p10 = get_rgba8_source_texel(src_pixels, src_width, src_height, x1, y0, src_flip_x, src_flip_y);
                p01 = get_rgba8_source_texel(src_pixels, src_width, src_height, x0, y1, src_flip_x, src_flip_y);
                p11 = get_rgba8_source_texel(src_pixels, src_width, src_height, x1, y1, src_flip_x, src_flip_y);

                for (uint32_t c = 0; c < 4; ++c) {
                    float top = (float)p00[c] + ((float)p10[c] - (float)p00[c]) * tx;
                    float bottom = (float)p01[c] + ((float)p11[c] - (float)p01[c]) * tx;
                    dst_texel[c] = clamp_u8_from_float(top + (bottom - top) * ty);
                }
            }
        }
    }
}

static bool write_rgba8_to_surface(GX2Surface *surface,
                                   GLint dstX0,
                                   GLint dstY0,
                                   GLint dstX1,
                                   GLint dstY1,
                                   const uint8_t *pixels) {
    GLsizei width;
    GLsizei height;
    bool flip_x;
    bool flip_y;
    GLint min_x;
    GLint max_x;
    GLint min_y;
    GLint max_y;
    uint8_t *surface_bytes;

    if (!surface || !surface->image || !pixels) return false;

    width = dstX1 >= dstX0 ? (dstX1 - dstX0) : (dstX0 - dstX1);
    height = dstY1 >= dstY0 ? (dstY1 - dstY0) : (dstY0 - dstY1);
    if (width <= 0 || height <= 0) return true;

    flip_x = dstX1 < dstX0;
    flip_y = dstY1 < dstY0;
    min_x = flip_x ? dstX1 : dstX0;
    max_x = flip_x ? dstX0 : dstX1;
    min_y = flip_y ? dstY1 : dstY0;
    max_y = flip_y ? dstY0 : dstY1;
    surface_bytes = (uint8_t *)surface->image;

    for (GLsizei dy = 0; dy < height; ++dy) {
        GLint gl_y = flip_y ? (max_y - 1 - dy) : (min_y + dy);
        uint32_t surface_y;
        const uint8_t *src_row;

        if (gl_y < 0 || gl_y >= (GLint)surface->height) continue;
        surface_y = (uint32_t)((GLint)surface->height - 1 - gl_y);
        src_row = pixels + (size_t)dy * (size_t)width * 4u;

        for (GLsizei dx = 0; dx < width; ++dx) {
            GLint gl_x = flip_x ? (max_x - 1 - dx) : (min_x + dx);
            const uint8_t *src_texel;

            if (gl_x < 0 || gl_x >= (GLint)surface->width) continue;

            src_texel = src_row + (size_t)dx * 4u;
            switch (surface->format) {
            case GX2_SURFACE_FORMAT_UNORM_R8: {
                uint8_t *dst_texel = surface_bytes + (size_t)surface_y * (size_t)surface->pitch + (size_t)gl_x;
                dst_texel[0] = src_texel[0];
                break;
            }
            case GX2_SURFACE_FORMAT_UNORM_R8_G8: {
                uint8_t *dst_texel =
                    surface_bytes + (((size_t)surface_y * (size_t)surface->pitch) + (size_t)gl_x) * 2u;
                dst_texel[0] = src_texel[0];
                dst_texel[1] = src_texel[1];
                break;
            }
            case GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8: {
                uint8_t *dst_texel =
                    surface_bytes + (((size_t)surface_y * (size_t)surface->pitch) + (size_t)gl_x) * 4u;
                dst_texel[0] = src_texel[0];
                dst_texel[1] = src_texel[1];
                dst_texel[2] = src_texel[2];
                dst_texel[3] = src_texel[3];
                break;
            }
            case GX2_SURFACE_FORMAT_FLOAT_R16_G16_B16_A16: {
                uint8_t *dst_texel =
                    surface_bytes + (((size_t)surface_y * (size_t)surface->pitch) + (size_t)gl_x) * 8u;
                encode_gpu_half_float(dst_texel + 0u, (float)src_texel[0] / 255.0f);
                encode_gpu_half_float(dst_texel + 2u, (float)src_texel[1] / 255.0f);
                encode_gpu_half_float(dst_texel + 4u, (float)src_texel[2] / 255.0f);
                encode_gpu_half_float(dst_texel + 6u, (float)src_texel[3] / 255.0f);
                break;
            }
            case GX2_SURFACE_FORMAT_FLOAT_R32_G32_B32_A32: {
                uint8_t *dst_texel =
                    surface_bytes + (((size_t)surface_y * (size_t)surface->pitch) + (size_t)gl_x) * 16u;
                float r = (float)src_texel[0] / 255.0f;
                float g = (float)src_texel[1] / 255.0f;
                float b = (float)src_texel[2] / 255.0f;
                float a = (float)src_texel[3] / 255.0f;
                encode_gpu_float32(dst_texel + 0u, r);
                encode_gpu_float32(dst_texel + 4u, g);
                encode_gpu_float32(dst_texel + 8u, b);
                encode_gpu_float32(dst_texel + 12u, a);
                break;
            }
            default:
                return false;
            }
        }
    }

    DCFlushRange(surface->image, surface->imageSize);
    invalidate_surface_after_color_write(surface);
    return true;
}

void _gl_BlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter) {
    GX2ColorBuffer *src_cb;
    GX2ColorBuffer *dst_cb;
    GX2Surface *src_surface;
    GX2Surface *dst_surface;
    GLsizei src_width;
    GLsizei src_height;
    GLsizei dst_width;
    GLsizei dst_height;
    GLint src_min_x;
    GLint src_min_y;
    bool src_flip_x;
    bool src_flip_y;
    bool whole_surface_copy;
    uint8_t *src_pixels;
    uint8_t *dst_pixels;
    size_t src_size;
    size_t dst_size;

    if (!g_gl_context) return;
    if (mask == 0) return;
    if (filter != GL_NEAREST && filter != GL_LINEAR) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    /* TODO: support nearest depth/stencil blits; for now the depth/stencil path remains intentionally unimplemented. */
    if (mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    if ((mask & ~GL_COLOR_BUFFER_BIT) != 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }

    if (_gl_CheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
        _gl_CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        _gl_set_error(GL_INVALID_FRAMEBUFFER_OPERATION);
        return;
    }

    src_width = srcX1 >= srcX0 ? (srcX1 - srcX0) : (srcX0 - srcX1);
    src_height = srcY1 >= srcY0 ? (srcY1 - srcY0) : (srcY0 - srcY1);
    dst_width = dstX1 >= dstX0 ? (dstX1 - dstX0) : (dstX0 - dstX1);
    dst_height = dstY1 >= dstY0 ? (dstY1 - dstY0) : (dstY0 - dstY1);
    if (src_width == 0 || src_height == 0 || dst_width == 0 || dst_height == 0) return;

    src_cb = get_read_color_buffer();
    dst_cb = get_draw_blit_color_buffer();
    if (!src_cb || !dst_cb || !src_cb->surface.image || !dst_cb->surface.image) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }

    src_surface = &src_cb->surface;
    dst_surface = &dst_cb->surface;
    whole_surface_copy =
        src_width == dst_width &&
        src_height == dst_height &&
        srcX0 <= srcX1 && srcY0 <= srcY1 &&
        dstX0 <= dstX1 && dstY0 <= dstY1 &&
        srcX0 == 0 && srcY0 == 0 &&
        dstX0 == 0 && dstY0 == 0 &&
        src_width == (GLsizei)src_surface->width &&
        src_height == (GLsizei)src_surface->height &&
        dst_width == (GLsizei)dst_surface->width &&
        dst_height == (GLsizei)dst_surface->height &&
        src_surface->format == dst_surface->format;

    if (whole_surface_copy) {
        GX2DrawDone();
        GX2CopySurface(src_surface, 0, 0, dst_surface, 0, 0);
        GX2DrawDone();
        invalidate_surface_after_color_write(dst_surface);
        return;
    }

    src_size = (size_t)src_width * (size_t)src_height * 4u;
    dst_size = (size_t)dst_width * (size_t)dst_height * 4u;
    src_pixels = (uint8_t *)gl_mem_alloc(GL_MEM_TYPE_MEM2, src_size, 64);
    dst_pixels = (uint8_t *)gl_mem_alloc(GL_MEM_TYPE_MEM2, dst_size, 64);
    if (!src_pixels || !dst_pixels) {
        if (src_pixels) gl_mem_free(GL_MEM_TYPE_MEM2, src_pixels);
        if (dst_pixels) gl_mem_free(GL_MEM_TYPE_MEM2, dst_pixels);
        _gl_set_error(GL_OUT_OF_MEMORY);
        return;
    }

    src_min_x = srcX0 < srcX1 ? srcX0 : srcX1;
    src_min_y = srcY0 < srcY1 ? srcY0 : srcY1;
    src_flip_x = srcX1 < srcX0;
    src_flip_y = srcY1 < srcY0;

    if (!gl_read_color_pixels_rgba8(src_min_x, src_min_y, src_width, src_height, src_pixels)) {
        gl_mem_free(GL_MEM_TYPE_MEM2, src_pixels);
        gl_mem_free(GL_MEM_TYPE_MEM2, dst_pixels);
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }

    scale_rgba8_pixels(src_pixels, src_width, src_height,
                       dst_width, dst_height, filter,
                       src_flip_x, src_flip_y, dst_pixels);
    gl_mem_free(GL_MEM_TYPE_MEM2, src_pixels);

    if (!write_rgba8_to_surface(dst_surface, dstX0, dstY0, dstX1, dstY1, dst_pixels)) {
        gl_mem_free(GL_MEM_TYPE_MEM2, dst_pixels);
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }

    gl_mem_free(GL_MEM_TYPE_MEM2, dst_pixels);
}

void _gl_RenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height) {
    renderbuffer_storage(target, samples, internalformat, width, height);
}

void _gl_DrawBuffer(GLenum buf) {
    if (!g_gl_context) return;
    GLuint fbo = g_gl_context->bound_framebuffer;
    GLFramebuffer *fb = &g_framebuffers[fbo];
    if (fbo == 0) {
        if (buf != GL_NONE && buf != GL_BACK) {
            _gl_set_error(is_default_framebuffer_color_name(buf) ||
                          is_color_attachment_name(buf, NULL)
                              ? GL_INVALID_OPERATION
                              : GL_INVALID_ENUM);
            return;
        }
    } else if (buf != GL_NONE && !get_color_attachment_index(buf, NULL)) {
        if (is_default_framebuffer_color_name(buf)) {
            _gl_set_error(GL_INVALID_OPERATION);
        } else if (is_color_attachment_name(buf, NULL)) {
            _gl_set_error(GL_INVALID_VALUE);
        } else {
            _gl_set_error(GL_INVALID_ENUM);
        }
        return;
    }
    fb->draw_buffers[0] = buf;
    for (uint32_t i = 1; i < 8; ++i) fb->draw_buffers[i] = GL_NONE;
    fb->dirty = true;
    g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
}
void _gl_DrawBuffers(GLsizei n, const GLenum *bufs) {
    if (!g_gl_context || n < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    if (n > 0 && !bufs) { _gl_set_error(GL_INVALID_VALUE); return; }
    if (n > 8) { _gl_set_error(GL_INVALID_VALUE); return; }
    GLuint fbo = g_gl_context->bound_framebuffer;
    GLFramebuffer *fb = &g_framebuffers[fbo];
    if (fbo == 0 && n != 1) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    for (GLsizei i = 0; i < n; ++i) {
        if (is_default_framebuffer_color_name(bufs[i])) {
            _gl_set_error(GL_INVALID_ENUM);
            return;
        }
        if (fbo == 0) {
            if (bufs[i] != GL_NONE) {
                _gl_set_error(is_color_attachment_name(bufs[i], NULL)
                                  ? GL_INVALID_OPERATION
                                  : GL_INVALID_ENUM);
                return;
            }
        } else if (bufs[i] != GL_NONE && !get_color_attachment_index(bufs[i], NULL)) {
            _gl_set_error(is_color_attachment_name(bufs[i], NULL)
                              ? GL_INVALID_OPERATION
                              : GL_INVALID_ENUM);
            return;
        }
        if (bufs[i] != GL_NONE) {
            for (GLsizei j = 0; j < i; ++j) {
                if (bufs[j] == bufs[i]) {
                    _gl_set_error(GL_INVALID_OPERATION);
                    return;
                }
            }
        }
    }
    for (uint32_t i = 0; i < 8; ++i)
        fb->draw_buffers[i] = (i < (uint32_t)n) ? bufs[i] : GL_NONE;
    fb->dirty = true;
    g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
}
void _gl_ReadBuffer(GLenum src) {
    if (!g_gl_context) return;
    GLuint fbo = g_gl_context->bound_read_framebuffer;
    if (fbo == 0) {
        if (src != GL_NONE && src != GL_BACK) {
            _gl_set_error(is_default_framebuffer_color_name(src) ||
                          is_color_attachment_name(src, NULL)
                              ? GL_INVALID_OPERATION
                              : GL_INVALID_ENUM);
            return;
        }
    } else if (src != GL_NONE && !get_color_attachment_index(src, NULL)) {
        _gl_set_error(is_default_framebuffer_color_name(src) ||
                      is_color_attachment_name(src, NULL)
                          ? GL_INVALID_OPERATION
                          : GL_INVALID_ENUM);
        return;
    }
    g_framebuffers[fbo].read_buffer = src;
}

GLenum _gl_CheckFramebufferStatus(GLenum target) {
    if (!is_framebuffer_target(target)) { _gl_set_error(GL_INVALID_ENUM); return 0; }
    GLuint fbo = get_bound_framebuffer_for_target(target);
    if (fbo == 0) return GL_FRAMEBUFFER_COMPLETE;
    GLFramebuffer *fb = &g_framebuffers[fbo];
    bool has_att = false;
    GLsizei expected_samples = 0;
    bool expected_samples_set = false;
    for (uint32_t i = 0; i < 8; ++i) {
        GLAttachmentImageInfo info;
        if (!attachment_ref_present(&fb->color_attachments[i])) continue;
        has_att = true;
        if (!get_attachment_image_info(&fb->color_attachments[i], &info) ||
            info.width <= 0 || info.height <= 0 ||
            info.depth_renderable || info.stencil_renderable) {
            return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        }
        if (!expected_samples_set) {
            expected_samples = info.samples;
            expected_samples_set = true;
        } else if (info.samples != expected_samples) {
            return GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE;
        }
    }
    if (attachment_ref_present(&fb->depth_attachment)) {
        GLAttachmentImageInfo info;
        has_att = true;
        if (!get_attachment_image_info(&fb->depth_attachment, &info) ||
            info.width <= 0 || info.height <= 0 ||
            !info.depth_renderable) {
            return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        }
        if (!expected_samples_set) {
            expected_samples = info.samples;
            expected_samples_set = true;
        } else if (info.samples != expected_samples) {
            return GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE;
        }
    }
    if (attachment_ref_present(&fb->stencil_attachment)) {
        GLAttachmentImageInfo info;
        has_att = true;
        if (!get_attachment_image_info(&fb->stencil_attachment, &info) ||
            info.width <= 0 || info.height <= 0 ||
            !info.stencil_renderable) {
            return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        }
        if (!expected_samples_set) {
            expected_samples = info.samples;
            expected_samples_set = true;
        } else if (info.samples != expected_samples) {
            return GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE;
        }
    }
    if (!has_att) return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
    for (uint32_t i = 0; i < 8; ++i) {
        uint32_t attachment_index = 0;
        if (fb->draw_buffers[i] == GL_NONE) continue;
        if (!get_color_attachment_index(fb->draw_buffers[i], &attachment_index) ||
            !attachment_ref_present(&fb->color_attachments[attachment_index])) {
            return GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER;
        }
    }
    if (fb->read_buffer != GL_NONE) {
        uint32_t attachment_index = 0;
        if (!get_color_attachment_index(fb->read_buffer, &attachment_index)) {
            return GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER;
        }
        if (!attachment_ref_present(&fb->color_attachments[attachment_index])) {
            return GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER;
        }
    }
    return GL_FRAMEBUFFER_COMPLETE;
}

void _gl_GetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params) {
    if (!params) return;
    if (!g_gl_context || target != GL_RENDERBUFFER) { _gl_set_error(GL_INVALID_ENUM); return; }
    GLuint id = g_gl_context->bound_renderbuffer;
    GLRenderbuffer *rb = get_renderbuffer(id);
    if (!rb) { _gl_set_error(GL_INVALID_OPERATION); return; }
    switch (pname) {
    case GL_RENDERBUFFER_WIDTH:           *params = rb->width; break;
    case GL_RENDERBUFFER_HEIGHT:          *params = rb->height; break;
    case GL_RENDERBUFFER_INTERNAL_FORMAT: *params = rb->internal_format; break;
    case GL_RENDERBUFFER_SAMPLES:         *params = rb->samples; break;
    case GL_RENDERBUFFER_RED_SIZE:
    case GL_RENDERBUFFER_GREEN_SIZE:
    case GL_RENDERBUFFER_BLUE_SIZE:
    case GL_RENDERBUFFER_ALPHA_SIZE:
    case GL_RENDERBUFFER_DEPTH_SIZE:
    case GL_RENDERBUFFER_STENCIL_SIZE:
        *params = renderbuffer_component_bits(rb->internal_format, pname);
        break;
    default: _gl_set_error(GL_INVALID_ENUM); break;
    }
}

void _gl_GetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname, GLint *params) {
    GLAttachmentRef *ref;
    GLAttachmentImageInfo info;

    if (!params) return;
    if (!is_framebuffer_target(target)) { _gl_set_error(GL_INVALID_ENUM); return; }
    GLuint fbo = get_bound_framebuffer_for_target(target);
    if (fbo == 0) { *params = GL_NONE; return; }
    if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        GLFramebuffer *fb = &g_framebuffers[fbo];
        bool depth_present = attachment_ref_present(&fb->depth_attachment);
        bool stencil_present = attachment_ref_present(&fb->stencil_attachment);
        if (depth_present && stencil_present &&
            (fb->depth_attachment.kind != fb->stencil_attachment.kind ||
             fb->depth_attachment.object != fb->stencil_attachment.object)) {
            _gl_set_error(GL_INVALID_OPERATION);
            return;
        }
        ref = depth_present ? &fb->depth_attachment : &fb->stencil_attachment;
    } else {
        ref = get_attachment_ref(&g_framebuffers[fbo], attachment);
    }
    if (!ref) { _gl_set_error(GL_INVALID_ENUM); return; }
    switch (pname) {
    case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE:
        if (!attachment_ref_present(ref)) *params = GL_NONE;
        else if (ref->kind == GL_ATTACHMENT_KIND_TEXTURE) *params = GL_TEXTURE;
        else *params = GL_RENDERBUFFER;
        break;
    case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME:
        *params = attachment_ref_present(ref) ? (GLint)ref->object : 0;
        break;
    case GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE:
    case GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE:
    case GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE:
    case GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE:
    case GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE:
    case GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE:
        if (!attachment_ref_present(ref)) { _gl_set_error(GL_INVALID_OPERATION); return; }
        if (!get_attachment_image_info(ref, &info)) { _gl_set_error(GL_INVALID_OPERATION); return; }
        *params = attachment_component_bits(info.internal_format, pname);
        break;
    case GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE:
        if (!attachment_ref_present(ref)) { _gl_set_error(GL_INVALID_OPERATION); return; }
        if (!get_attachment_image_info(ref, &info)) { _gl_set_error(GL_INVALID_OPERATION); return; }
        *params = attachment_component_type(info.internal_format);
        break;
    case GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING:
        if (!attachment_ref_present(ref)) { _gl_set_error(GL_INVALID_OPERATION); return; }
        *params = GL_LINEAR;
        break;
    case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL:
        if (!attachment_ref_present(ref)) { _gl_set_error(GL_INVALID_OPERATION); return; }
        if (ref->kind != GL_ATTACHMENT_KIND_TEXTURE) { _gl_set_error(GL_INVALID_ENUM); return; }
        *params = ref->level;
        break;
    case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE:
        if (!attachment_ref_present(ref)) { _gl_set_error(GL_INVALID_OPERATION); return; }
        if (ref->kind != GL_ATTACHMENT_KIND_TEXTURE) { _gl_set_error(GL_INVALID_ENUM); return; }
        *params = is_cube_map_face_target(ref->textarget) ? (GLint)ref->textarget : 0;
        break;
    case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER:
        if (!attachment_ref_present(ref)) { _gl_set_error(GL_INVALID_OPERATION); return; }
        if (ref->kind != GL_ATTACHMENT_KIND_TEXTURE) { _gl_set_error(GL_INVALID_ENUM); return; }
        *params = ref->textarget == GL_TEXTURE_3D ? ref->layer : 0;
        break;
    case GL_FRAMEBUFFER_ATTACHMENT_LAYERED:
        if (!attachment_ref_present(ref)) { _gl_set_error(GL_INVALID_OPERATION); return; }
        if (ref->kind != GL_ATTACHMENT_KIND_TEXTURE) { _gl_set_error(GL_INVALID_ENUM); return; }
        *params = ref->layered;
        break;
    default: _gl_set_error(GL_INVALID_ENUM); break;
    }
}

void gl_bind_framebuffers(void) {
    if (!g_gl_context) return;
    GLuint fbo = g_gl_context->bound_framebuffer;
    if (fbo == 0) {
        GX2ColorBuffer *default_color = get_default_color_buffer();
        GX2DepthBuffer *default_depth = get_default_depth_buffer();
        if (default_color) GX2SetColorBuffer(default_color, GX2_RENDER_TARGET_0);
        if (default_depth) GX2SetDepthBuffer(default_depth);
        apply_framebuffer_output_state(&g_framebuffers[0], true);
        return;
    }
    GLFramebuffer *fb = &g_framebuffers[fbo];
    if (fb->dirty) {
        for (uint32_t i = 0; i < 8; ++i) {
            if (!attachment_ref_present(&fb->color_attachments[i])) {
                free_framebuffer_texture_target(fb, i);
                continue;
            }
            if (fb->color_attachments[i].kind == GL_ATTACHMENT_KIND_TEXTURE) {
                GX2Texture *t = gl_get_gx2_texture(fb->color_attachments[i].object);
                free_framebuffer_texture_target(fb, i);
                if (t && init_color_buffer_from_surface_view(
                             &fb->cb[i], &t->surface,
                             fb->color_attachments[i].level,
                             attachment_view_slice(&fb->color_attachments[i]))) {
                    fb->color_buffer_owns_aux[i] = true;
                }
            } else {
                free_framebuffer_texture_target(fb, i);
                GLRenderbuffer *rb = get_renderbuffer(fb->color_attachments[i].object);
                if (rb && !rb->is_depth) {
                    fb->cb[i] = rb->color_buffer;
                    fb->color_buffer_owns_aux[i] = false;
                }
            }
        }
        memset(&fb->db, 0, sizeof(fb->db));
        if (attachment_ref_present(&fb->depth_attachment)) {
            if (fb->depth_attachment.kind == GL_ATTACHMENT_KIND_TEXTURE) {
                GX2Texture *t = gl_get_gx2_texture(fb->depth_attachment.object);
                if (t) init_depth_buffer_from_surface_view(
                           &fb->db, &t->surface, fb->depth_attachment.level,
                           attachment_view_slice(&fb->depth_attachment));
            } else {
                GLRenderbuffer *rb = get_renderbuffer(fb->depth_attachment.object);
                if (rb && rb->is_depth) fb->db = rb->depth_buffer;
            }
        } else if (attachment_ref_present(&fb->stencil_attachment)) {
            if (fb->stencil_attachment.kind == GL_ATTACHMENT_KIND_TEXTURE) {
                GX2Texture *t = gl_get_gx2_texture(fb->stencil_attachment.object);
                if (t) init_depth_buffer_from_surface_view(
                           &fb->db, &t->surface, fb->stencil_attachment.level,
                           attachment_view_slice(&fb->stencil_attachment));
            } else {
                GLRenderbuffer *rb = get_renderbuffer(fb->stencil_attachment.object);
                if (rb && rb->is_depth) fb->db = rb->depth_buffer;
            }
        }
        fb->dirty = false;
    }
    for (uint32_t i = 0; i < 8; ++i) {
        uint32_t attachment_index = 0;
        if (get_color_attachment_index(fb->draw_buffers[i], &attachment_index) &&
            attachment_ref_present(&fb->color_attachments[attachment_index]))
            GX2SetColorBuffer(&fb->cb[attachment_index], (GX2RenderTarget)i);
    }
    GX2SetDepthBuffer(&fb->db);
    apply_framebuffer_output_state(fb, false);
}

GLboolean gl_is_draw_color_buffer_enabled(GLuint index) {
    if (!g_gl_context || index >= 8) return GL_FALSE;
    if (g_gl_context->bound_framebuffer == 0) {
        return (index == 0 && g_framebuffers[0].draw_buffers[0] == GL_BACK) ? GL_TRUE : GL_FALSE;
    }

    {
        GLFramebuffer *fb = &g_framebuffers[g_gl_context->bound_framebuffer];
        uint32_t attachment_index = 0;
        return (get_color_attachment_index(fb->draw_buffers[index], &attachment_index) &&
                attachment_ref_present(&fb->color_attachments[attachment_index])) ? GL_TRUE : GL_FALSE;
    }
}
GX2ColorBuffer *gl_get_draw_color_buffer(GLuint index) {
    if (!g_gl_context) return NULL;
    GLuint fbo = g_gl_context->bound_framebuffer;
    if (fbo == 0) return (index == 0 && g_framebuffers[0].draw_buffers[0] == GL_BACK) ? get_default_color_buffer() : NULL;
    if (index >= 8) return NULL;
    {
        GLFramebuffer *fb = &g_framebuffers[fbo];
        uint32_t attachment_index = 0;
        if (get_color_attachment_index(fb->draw_buffers[index], &attachment_index) &&
            attachment_ref_present(&fb->color_attachments[attachment_index])) return &fb->cb[attachment_index];
    }
    return NULL;
}
GX2DepthBuffer *gl_get_draw_depth_buffer(void) {
    if (!g_gl_context) return NULL;
    GLuint fbo = g_gl_context->bound_framebuffer;
    if (fbo == 0) return get_default_depth_buffer();
    GLFramebuffer *fb = &g_framebuffers[fbo];
    if (attachment_ref_present(&fb->depth_attachment)) return &fb->db;
    return NULL;
}

void gl_framebuffer_mark_bound_color_dirty(void) {
    GLuint fbo;
    GLFramebuffer *fb;

    if (!g_gl_context) return;
    fbo = g_gl_context->bound_framebuffer;
    if (fbo == 0 || fbo >= MAX_FRAMEBUFFERS) return;

    fb = &g_framebuffers[fbo];
    for (uint32_t i = 0; i < 8; ++i) {
        uint32_t attachment_index = 0;
        if (get_color_attachment_index(fb->draw_buffers[i], &attachment_index) &&
            fb->color_attachments[attachment_index].kind == GL_ATTACHMENT_KIND_TEXTURE) {
            fb->color_needs_resolve[attachment_index] = true;
        }
    }
}

void gl_framebuffer_mark_bound_color_buffer_dirty(GLuint index) {
    GLuint fbo;
    GLFramebuffer *fb;
    uint32_t attachment_index = 0;

    if (!g_gl_context || index >= 8) return;
    fbo = g_gl_context->bound_framebuffer;
    if (fbo == 0 || fbo >= MAX_FRAMEBUFFERS) return;

    fb = &g_framebuffers[fbo];
    if (get_color_attachment_index(fb->draw_buffers[index], &attachment_index) &&
        fb->color_attachments[attachment_index].kind == GL_ATTACHMENT_KIND_TEXTURE) {
        fb->color_needs_resolve[attachment_index] = true;
    }
}

void gl_framebuffer_sync_bound_color_targets(void) {
    GLuint fbo;
    if (!g_gl_context) return;
    fbo = g_gl_context->bound_framebuffer;
    if (fbo == 0 || fbo >= MAX_FRAMEBUFFERS) return;
    resolve_framebuffer_texture_targets(&g_framebuffers[fbo]);
}

void gl_framebuffer_sync_texture_for_sampling(GLuint texture) {
    if (texture == 0) return;
    for (uint32_t i = 0; i < MAX_FRAMEBUFFERS; ++i) {
        GLFramebuffer *fb = &g_framebuffers[i];
        if (!fb->in_use) continue;
        for (uint32_t j = 0; j < 8; ++j) {
            if (fb->color_attachments[j].kind == GL_ATTACHMENT_KIND_TEXTURE &&
                fb->color_attachments[j].object == texture &&
                fb->color_needs_resolve[j]) {
                resolve_framebuffer_texture_targets(fb);
                break;
            }
        }
    }
}

void gl_framebuffer_mark_texture_dirty(GLuint texture) {
    if (texture == 0) return;
    for (uint32_t i = 0; i < MAX_FRAMEBUFFERS; ++i) {
        if (!g_framebuffers[i].in_use) continue;
        bool fb_dirty = false;
        for (uint32_t j = 0; j < 8; ++j) {
            if (g_framebuffers[i].color_attachments[j].kind == GL_ATTACHMENT_KIND_TEXTURE &&
                g_framebuffers[i].color_attachments[j].object == texture) fb_dirty = true;
        }
        if (g_framebuffers[i].depth_attachment.kind == GL_ATTACHMENT_KIND_TEXTURE &&
            g_framebuffers[i].depth_attachment.object == texture) fb_dirty = true;
        if (g_framebuffers[i].stencil_attachment.kind == GL_ATTACHMENT_KIND_TEXTURE &&
            g_framebuffers[i].stencil_attachment.object == texture) fb_dirty = true;
        if (fb_dirty) g_framebuffers[i].dirty = true;
    }
}

GLboolean gl_read_color_pixels_rgba8(GLint x, GLint y, GLsizei width, GLsizei height, void *pixels) {
    GX2ColorBuffer *cb;
    GX2Surface *surface;
    GX2Surface staging_surface;
    GX2Surface texture_read_surface;
    GX2Surface *read_surface;
    uint8_t *dst;
    GLint internal_format;
    bool staged_surface = false;
    GLuint read_fbo;
    uint32_t texture_attachment_index = 0;

    if (!g_gl_context || !pixels || width < 0 || height < 0) return GL_FALSE;
    if (width == 0 || height == 0) return GL_TRUE;

    cb = get_read_color_buffer();
    if (!cb || !cb->surface.image) return GL_FALSE;

    surface = &cb->surface;
    memset(&texture_read_surface, 0, sizeof(texture_read_surface));
    read_fbo = g_gl_context->bound_read_framebuffer;
    if (read_fbo > 0 && read_fbo < MAX_FRAMEBUFFERS &&
        get_color_attachment_index(g_framebuffers[read_fbo].read_buffer, &texture_attachment_index) &&
        g_framebuffers[read_fbo].color_attachments[texture_attachment_index].kind == GL_ATTACHMENT_KIND_TEXTURE) {
        GLAttachmentRef *attachment =
            &g_framebuffers[read_fbo].color_attachments[texture_attachment_index];
        GX2Texture *texture =
            gl_get_gx2_texture(attachment->object);
        if (texture && build_texture_attachment_read_surface(attachment,
                                                             &texture->surface,
                                                             &texture_read_surface)) {
            surface = &texture_read_surface;
        }
    }
    internal_format = get_read_color_internal_format();
    dst = (uint8_t *)pixels;
    memset(dst, 0, (size_t)width * (size_t)height * 4u);

    GX2DrawDone();
    memset(&staging_surface, 0, sizeof(staging_surface));
    if (g_gl_context->bound_read_framebuffer == 0 &&
        stage_color_surface_for_cpu_read(surface, internal_format, &staging_surface)) {
        read_surface = &staging_surface;
        staged_surface = true;
    } else {
        read_surface = surface;
        invalidate_surface_after_color_write(read_surface);
        DCInvalidateRange(read_surface->image, read_surface->imageSize);
    }

    for (GLsizei row = 0; row < height; ++row) {
        GLint src_y = y + row;
        if (src_y < 0 || src_y >= (GLint)read_surface->height) continue;

        for (GLsizei col = 0; col < width; ++col) {
            GLint src_x = x + col;
            uint8_t *dst_texel;

            if (src_x < 0 || src_x >= (GLint)read_surface->width) continue;

            dst_texel = dst + ((size_t)row * (size_t)width + (size_t)col) * 4u;
            switch (read_surface->format) {
            case GX2_SURFACE_FORMAT_UNORM_R8: {
                const uint8_t *src_texel =
                    (const uint8_t *)read_surface->image + (size_t)src_y * (size_t)read_surface->pitch + (size_t)src_x;
                if (internal_format == GL_ALPHA) {
                    dst_texel[0] = 0;
                    dst_texel[1] = 0;
                    dst_texel[2] = 0;
                    dst_texel[3] = src_texel[0];
                } else if (internal_format == GL_LUMINANCE) {
                    dst_texel[0] = src_texel[0];
                    dst_texel[1] = src_texel[0];
                    dst_texel[2] = src_texel[0];
                    dst_texel[3] = 0xFF;
                } else {
                    dst_texel[0] = src_texel[0];
                    dst_texel[1] = 0;
                    dst_texel[2] = 0;
                    dst_texel[3] = 0xFF;
                }
                break;
            }
            case GX2_SURFACE_FORMAT_UNORM_R8_G8: {
                const uint8_t *src_texel =
                    (const uint8_t *)read_surface->image +
                    ((size_t)src_y * (size_t)read_surface->pitch + (size_t)src_x) * 2u;
                if (internal_format == GL_LUMINANCE_ALPHA) {
                    dst_texel[0] = src_texel[0];
                    dst_texel[1] = src_texel[0];
                    dst_texel[2] = src_texel[0];
                    dst_texel[3] = src_texel[1];
                } else {
                    dst_texel[0] = src_texel[0];
                    dst_texel[1] = src_texel[1];
                    dst_texel[2] = 0;
                    dst_texel[3] = 0xFF;
                }
                break;
            }
            case GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8: {
                const uint8_t *src_texel =
                    (const uint8_t *)read_surface->image +
                    (((size_t)src_y * (size_t)read_surface->pitch) + (size_t)src_x) * 4u;
                uint32_t pixel;
                memcpy(&pixel, src_texel, sizeof(pixel));
                pixel = GPU_TO_CPU_32(pixel);
                dst_texel[0] = (uint8_t)((pixel >> 24) & 0xFFu);
                dst_texel[1] = (uint8_t)((pixel >> 16) & 0xFFu);
                dst_texel[2] = (uint8_t)((pixel >> 8) & 0xFFu);
                dst_texel[3] = (uint8_t)(pixel & 0xFFu);
                break;
            }
            case GX2_SURFACE_FORMAT_FLOAT_R16_G16_B16_A16: {
                const uint8_t *src_texel =
                    (const uint8_t *)read_surface->image +
                    ((size_t)src_y * (size_t)read_surface->pitch + (size_t)src_x) * 8u;
                dst_texel[0] = float_to_unorm8(decode_gpu_half_float(src_texel + 0));
                dst_texel[1] = float_to_unorm8(decode_gpu_half_float(src_texel + 2));
                dst_texel[2] = float_to_unorm8(decode_gpu_half_float(src_texel + 4));
                dst_texel[3] = float_to_unorm8(decode_gpu_half_float(src_texel + 6));
                break;
            }
            case GX2_SURFACE_FORMAT_FLOAT_R32_G32_B32_A32: {
                const uint8_t *src_texel =
                    (const uint8_t *)read_surface->image +
                    ((size_t)src_y * (size_t)read_surface->pitch + (size_t)src_x) * 16u;
                dst_texel[0] = float_to_unorm8(decode_gpu_float32(src_texel + 0));
                dst_texel[1] = float_to_unorm8(decode_gpu_float32(src_texel + 4));
                dst_texel[2] = float_to_unorm8(decode_gpu_float32(src_texel + 8));
                dst_texel[3] = float_to_unorm8(decode_gpu_float32(src_texel + 12));
                break;
            }
            default:
                return GL_FALSE;
            }
        }
    }

    if (staged_surface) {
        free_surface_storage(&staging_surface);
    }
    return GL_TRUE;
}

#ifdef __cplusplus
}
#endif
