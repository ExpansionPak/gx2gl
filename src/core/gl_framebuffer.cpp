#include "gl_framebuffer.h"
#include "gl_texture.h"
#include "endian/endian.h"
#include "mem/gl_mem.h"
#include "state/gl_state.h"
#ifdef __cplusplus
extern "C" {
#endif
#include <gx2/surface.h>
#include <gx2/state.h>
#include <gx2/event.h>
#include <gx2/display.h>
#include <gx2/registers.h>
#include <gx2/mem.h>
#include <whb/gfx.h>
#ifdef __cplusplus
}
#endif
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define MAX_FRAMEBUFFERS 128
#define MAX_RENDERBUFFERS 256

typedef enum {
    GL_ATTACHMENT_KIND_NONE = 0,
    GL_ATTACHMENT_KIND_TEXTURE,
    GL_ATTACHMENT_KIND_RENDERBUFFER
} GLAttachmentKind;

typedef struct {
    GLAttachmentKind kind;
    GLuint object;
} GLAttachmentRef;

typedef struct {
    bool in_use;
    bool is_depth;
    GLint internal_format;
    GLsizei width;
    GLsizei height;
    GX2Surface surface;
    GX2ColorBuffer color_buffer;
    GX2DepthBuffer depth_buffer;
} GLRenderbuffer;

typedef struct {
    bool in_use;
    GLAttachmentRef color_attachments[8];
    GLenum draw_buffers[8];
    GLenum read_buffer;
    GLAttachmentRef depth_attachment;
    GLAttachmentRef stencil_attachment;
    
    GX2ColorBuffer cb[8];
    GX2DepthBuffer db;
    bool dirty;
} GLFramebuffer;

static GLFramebuffer g_framebuffers[MAX_FRAMEBUFFERS];
static GLRenderbuffer g_renderbuffers[MAX_RENDERBUFFERS];

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
}

static bool attachment_ref_present(const GLAttachmentRef *attachment) {
    return attachment && attachment->kind != GL_ATTACHMENT_KIND_NONE && attachment->object != 0;
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
        if (is_default) enabled = (i == 0 && fb->draw_buffers[0] == GL_BACK);
        else if (fb->draw_buffers[i] == (GL_COLOR_ATTACHMENT0 + i) && attachment_ref_present(&fb->color_attachments[i])) enabled = true;
        if (enabled && color_mask != 0) {
            masks[i] = color_mask;
            active_target_mask |= (uint8_t)(1u << i);
            ++active_target_count;
        }
    }
    GX2SetTargetChannelMasks(masks[0], masks[1], masks[2], masks[3], masks[4], masks[5], masks[6], masks[7]);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, g_gl_context->blend_enabled ? active_target_mask : 0, active_target_count > 1 ? GX2_ENABLE : GX2_DISABLE, active_target_mask != 0 ? GX2_ENABLE : GX2_DISABLE);
}

static void init_color_buffer_from_surface(GX2ColorBuffer *cb, const GX2Surface *s) {
    memset(cb, 0, sizeof(*cb));
    cb->surface = *s;
    cb->viewNumSlices = 1;
    GX2InitColorBufferRegs(cb);
}

static void init_depth_buffer_from_surface(GX2DepthBuffer *db, const GX2Surface *s) {
    memset(db, 0, sizeof(*db));
    db->surface = *s;
    db->viewNumSlices = 1;
    db->depthClear = 1.0f;
    GX2InitDepthBufferRegs(db);
}

void gl_framebuffer_init(void) {
    memset(g_framebuffers, 0, sizeof(g_framebuffers));
    memset(g_renderbuffers, 0, sizeof(g_renderbuffers));
    g_framebuffers[0].in_use = true;
    init_draw_buffer_defaults(&g_framebuffers[0], true);
    init_read_buffer_default(&g_framebuffers[0], true);
}

#ifdef __cplusplus
extern "C" {
#endif

void _gl_GenFramebuffers(GLsizei n, GLuint *fbs) {
    if (!g_gl_context || n < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    int count = 0;
    for (int i = 1; i < MAX_FRAMEBUFFERS && count < n; i++) {
        if (!g_framebuffers[i].in_use) {
            memset(&g_framebuffers[i], 0, sizeof(GLFramebuffer));
            g_framebuffers[i].in_use = true;
            g_framebuffers[i].dirty = true;
            init_draw_buffer_defaults(&g_framebuffers[i], false);
            init_read_buffer_default(&g_framebuffers[i], false);
            fbs[count++] = i;
        }
    }
}

void _gl_GenRenderbuffers(GLsizei n, GLuint *rbs) {
    if (!g_gl_context || n < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    int count = 0;
    for (int i = 1; i < MAX_RENDERBUFFERS && count < n; i++) {
        if (!g_renderbuffers[i].in_use) {
            memset(&g_renderbuffers[i], 0, sizeof(GLRenderbuffer));
            g_renderbuffers[i].in_use = true;
            rbs[count++] = i;
        }
    }
}

GLboolean _gl_IsRenderbuffer(GLuint rb) { return get_renderbuffer(rb) ? GL_TRUE : GL_FALSE; }
void _gl_DeleteRenderbuffers(GLsizei n, const GLuint *rbs) {
    if (!g_gl_context || n < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    for (int i = 0; i < n; i++) {
        GLuint id = rbs[i];
        if (id > 0 && id < MAX_RENDERBUFFERS && g_renderbuffers[id].in_use) {
            if (g_renderbuffers[id].surface.image) gl_mem_free(GL_MEM_TYPE_MEM2, g_renderbuffers[id].surface.image);
            memset(&g_renderbuffers[id], 0, sizeof(GLRenderbuffer));
        }
    }
}

GLboolean _gl_IsFramebuffer(GLuint fb) { return (fb < MAX_FRAMEBUFFERS && g_framebuffers[fb].in_use) ? GL_TRUE : GL_FALSE; }
void _gl_DeleteFramebuffers(GLsizei n, const GLuint *fbs) {
    if (!g_gl_context || n < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    for (int i = 0; i < n; i++) {
        GLuint id = fbs[i];
        if (id > 0 && id < MAX_FRAMEBUFFERS && g_framebuffers[id].in_use) {
            g_framebuffers[id].in_use = false;
        }
    }
}

void _gl_BindFramebuffer(GLenum target, GLuint fb) {
    if (!g_gl_context) return;
    if (!is_framebuffer_target(target)) { _gl_set_error(GL_INVALID_ENUM); return; }
    if (fb >= MAX_FRAMEBUFFERS || (fb > 0 && !g_framebuffers[fb].in_use)) { _gl_set_error(GL_INVALID_OPERATION); return; }
    if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) { g_gl_context->bound_framebuffer = fb; g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER; }
    if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER) { g_gl_context->bound_read_framebuffer = fb; }
}

void _gl_BindRenderbuffer(GLenum target, GLuint rb) {
    if (!g_gl_context) return;
    if (target != GL_RENDERBUFFER) { _gl_set_error(GL_INVALID_ENUM); return; }
    if (rb >= MAX_RENDERBUFFERS || (rb > 0 && !g_renderbuffers[rb].in_use)) { _gl_set_error(GL_INVALID_OPERATION); return; }
    g_gl_context->bound_renderbuffer = rb;
}

void _gl_FramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    if (!g_gl_context) return;
    if (!is_framebuffer_target(target) || textarget != GL_TEXTURE_2D) { _gl_set_error(GL_INVALID_ENUM); return; }
    GLuint fbo = get_bound_framebuffer_for_target(target);
    if (fbo == 0) { _gl_set_error(GL_INVALID_OPERATION); return; }
    GLAttachmentRef *ref = get_attachment_ref(&g_framebuffers[fbo], attachment);
    if (!ref) { _gl_set_error(GL_INVALID_ENUM); return; }
    if (texture == 0) clear_attachment_ref(ref);
    else { ref->kind = GL_ATTACHMENT_KIND_TEXTURE; ref->object = texture; }
    g_framebuffers[fbo].dirty = true;
    g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
}

void _gl_FramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum rbtarget, GLuint rb) {
    if (!g_gl_context) return;
    if (!is_framebuffer_target(target) || rbtarget != GL_RENDERBUFFER) { _gl_set_error(GL_INVALID_ENUM); return; }
    GLuint fbo = get_bound_framebuffer_for_target(target);
    if (fbo == 0) { _gl_set_error(GL_INVALID_OPERATION); return; }
    GLAttachmentRef *ref = get_attachment_ref(&g_framebuffers[fbo], attachment);
    if (!ref) { _gl_set_error(GL_INVALID_ENUM); return; }
    if (rb == 0) clear_attachment_ref(ref);
    else { ref->kind = GL_ATTACHMENT_KIND_RENDERBUFFER; ref->object = rb; }
    g_framebuffers[fbo].dirty = true;
    g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
}

void _gl_RenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) {
    if (!g_gl_context || target != GL_RENDERBUFFER) { _gl_set_error(GL_INVALID_ENUM); return; }
    GLuint id = g_gl_context->bound_renderbuffer;
    if (id == 0) { _gl_set_error(GL_INVALID_OPERATION); return; }
    GLRenderbuffer *rb = &g_renderbuffers[id];
    GX2SurfaceFormat fmt; GX2SurfaceUse use; bool is_depth;
    if (!get_renderbuffer_format_info(internalformat, &fmt, &use, &is_depth)) { _gl_set_error(GL_INVALID_ENUM); return; }
    if (rb->surface.image) gl_mem_free(GL_MEM_TYPE_MEM2, rb->surface.image);
    rb->surface.dim = GX2_SURFACE_DIM_TEXTURE_2D; rb->surface.width = width; rb->surface.height = height;
    rb->surface.depth = 1; rb->surface.mipLevels = 1; rb->surface.format = fmt; rb->surface.aa = GX2_AA_MODE1X; rb->surface.use = use;
    rb->surface.tileMode = is_depth ? GX2_TILE_MODE_DEFAULT : GX2_TILE_MODE_LINEAR_ALIGNED;
    GX2CalcSurfaceSizeAndAlignment(&rb->surface);
    rb->surface.image = gl_mem_alloc(GL_MEM_TYPE_MEM2, rb->surface.imageSize, rb->surface.alignment);
    rb->width = width; rb->height = height; rb->internal_format = internalformat; rb->is_depth = is_depth;
    if (is_depth) init_depth_buffer_from_surface(&rb->depth_buffer, &rb->surface);
    else init_color_buffer_from_surface(&rb->color_buffer, &rb->surface);
    g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
}

void _gl_ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels) {
    (void)x; (void)y; (void)width; (void)height; (void)format; (void)type; (void)pixels;
    _gl_set_error(GL_INVALID_OPERATION);
}

void _gl_FramebufferTexture(GLenum target, GLenum attachment, GLuint texture, GLint level) {
    _gl_FramebufferTexture2D(target, attachment, GL_TEXTURE_2D, texture, level);
}

void _gl_BlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter) {
    (void)srcX0; (void)srcY0; (void)srcX1; (void)srcY1; (void)dstX0; (void)dstY0; (void)dstX1; (void)dstY1; (void)mask; (void)filter;
    _gl_set_error(GL_INVALID_OPERATION);
}

void _gl_RenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height) {
    (void)samples; _gl_RenderbufferStorage(target, internalformat, width, height);
}

void _gl_GenQueries(GLsizei n, GLuint *ids) { if (n < 0) { _gl_set_error(GL_INVALID_VALUE); return; } for (int i = 0; i < n; i++) ids[i] = 0; }
void _gl_DeleteQueries(GLsizei n, const GLuint *ids) { (void)n; (void)ids; }
GLboolean _gl_IsQuery(GLuint id) { (void)id; return GL_FALSE; }
void _gl_BeginQuery(GLenum target, GLuint id) { (void)target; (void)id; _gl_set_error(GL_INVALID_OPERATION); }
void _gl_EndQuery(GLenum target) { (void)target; _gl_set_error(GL_INVALID_OPERATION); }
void _gl_GetQueryiv(GLenum target, GLenum pname, GLint *params) { (void)target; (void)pname; (void)params; _gl_set_error(GL_INVALID_OPERATION); }
void _gl_GetQueryObjectiv(GLuint id, GLenum pname, GLint *params) { (void)id; (void)pname; (void)params; _gl_set_error(GL_INVALID_OPERATION); }
void _gl_GetQueryObjectuiv(GLuint id, GLenum pname, GLuint *params) { (void)id; (void)pname; (void)params; _gl_set_error(GL_INVALID_OPERATION); }
void _gl_BeginConditionalRender(GLuint id, GLenum mode) { (void)id; (void)mode; _gl_set_error(GL_INVALID_OPERATION); }
void _gl_EndConditionalRender(void) { _gl_set_error(GL_INVALID_OPERATION); }
void _gl_BeginQueryIndexed(GLenum target, GLuint index, GLuint id) { (void)target; (void)index; (void)id; _gl_set_error(GL_INVALID_OPERATION); }
void _gl_EndQueryIndexed(GLenum target, GLuint index) { (void)target; (void)index; _gl_set_error(GL_INVALID_OPERATION); }
void _gl_GetQueryIndexediv(GLenum target, GLuint index, GLenum pname, GLint *params) { (void)target; (void)index; (void)pname; (void)params; _gl_set_error(GL_INVALID_OPERATION); }
void _gl_GenTransformFeedbacks(GLsizei n, GLuint *ids) { if (n < 0) { _gl_set_error(GL_INVALID_VALUE); return; } for (int i = 0; i < n; i++) ids[i] = 0; }
void _gl_DeleteTransformFeedbacks(GLsizei n, const GLuint *ids) { (void)n; (void)ids; }

void _gl_DrawBuffer(GLenum buf) {
    if (!g_gl_context) return;
    GLuint fbo = g_gl_context->bound_framebuffer;
    GLFramebuffer *fb = &g_framebuffers[fbo];
    fb->draw_buffers[0] = buf;
    for (uint32_t i = 1; i < 8; ++i) fb->draw_buffers[i] = GL_NONE;
    fb->dirty = true;
    g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
}
void _gl_DrawBuffers(GLsizei n, const GLenum *bufs) {
    if (!g_gl_context || n < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    GLuint fbo = g_gl_context->bound_framebuffer;
    GLFramebuffer *fb = &g_framebuffers[fbo];
    for (uint32_t i = 0; i < 8; ++i)
        fb->draw_buffers[i] = (i < (uint32_t)n) ? bufs[i] : GL_NONE;
    fb->dirty = true;
    g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
}
void _gl_ReadBuffer(GLenum src) {
    if (!g_gl_context) return;
    GLuint fbo = g_gl_context->bound_read_framebuffer;
    g_framebuffers[fbo].read_buffer = src;
}

GLenum _gl_CheckFramebufferStatus(GLenum target) {
    if (!is_framebuffer_target(target)) { _gl_set_error(GL_INVALID_ENUM); return 0; }
    GLuint fbo = get_bound_framebuffer_for_target(target);
    if (fbo == 0) return GL_FRAMEBUFFER_COMPLETE;
    GLFramebuffer *fb = &g_framebuffers[fbo];
    bool has_att = false;
    for (uint32_t i = 0; i < 8; ++i) {
        if (!attachment_ref_present(&fb->color_attachments[i])) continue;
        has_att = true;
        if (fb->color_attachments[i].kind == GL_ATTACHMENT_KIND_TEXTURE) {
            GX2Texture *t = gl_get_gx2_texture(fb->color_attachments[i].object);
            if (!t || !t->surface.image) return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        } else {
            GLRenderbuffer *rb = get_renderbuffer(fb->color_attachments[i].object);
            if (!rb || !rb->surface.image) return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        }
    }
    if (attachment_ref_present(&fb->depth_attachment)) {
        has_att = true;
        if (fb->depth_attachment.kind == GL_ATTACHMENT_KIND_TEXTURE) {
            GX2Texture *t = gl_get_gx2_texture(fb->depth_attachment.object);
            if (!t || !t->surface.image) return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        } else {
            GLRenderbuffer *rb = get_renderbuffer(fb->depth_attachment.object);
            if (!rb || !rb->surface.image) return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        }
    }
    if (!has_att) return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
    return GL_FRAMEBUFFER_COMPLETE;
}

void _gl_GetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params) {
    if (!g_gl_context || target != GL_RENDERBUFFER) { _gl_set_error(GL_INVALID_ENUM); return; }
    GLuint id = g_gl_context->bound_renderbuffer;
    GLRenderbuffer *rb = get_renderbuffer(id);
    if (!rb) { _gl_set_error(GL_INVALID_OPERATION); return; }
    switch (pname) {
    case GL_RENDERBUFFER_WIDTH:           *params = rb->width; break;
    case GL_RENDERBUFFER_HEIGHT:          *params = rb->height; break;
    case GL_RENDERBUFFER_INTERNAL_FORMAT: *params = rb->internal_format; break;
    default: _gl_set_error(GL_INVALID_ENUM); break;
    }
}

void _gl_GetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname, GLint *params) {
    if (!is_framebuffer_target(target)) { _gl_set_error(GL_INVALID_ENUM); return; }
    GLuint fbo = get_bound_framebuffer_for_target(target);
    if (fbo == 0) { *params = GL_NONE; return; }
    GLAttachmentRef *ref = get_attachment_ref(&g_framebuffers[fbo], attachment);
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
    default: _gl_set_error(GL_INVALID_ENUM); break;
    }
}

void gl_bind_framebuffers(void) {
    if (!g_gl_context) return;
    GLuint fbo = g_gl_context->bound_framebuffer;
    if (fbo == 0) {
        GX2ColorBuffer *tv_color = WHBGfxGetTVColourBuffer();
        GX2DepthBuffer *tv_depth = WHBGfxGetTVDepthBuffer();
        if (tv_color) GX2SetColorBuffer(tv_color, GX2_RENDER_TARGET_0);
        if (tv_depth) GX2SetDepthBuffer(tv_depth);
        apply_framebuffer_output_state(&g_framebuffers[0], true);
        return;
    }
    GLFramebuffer *fb = &g_framebuffers[fbo];
    if (fb->dirty) {
        for (uint32_t i = 0; i < 8; ++i) {
            if (!attachment_ref_present(&fb->color_attachments[i])) continue;
            if (fb->color_attachments[i].kind == GL_ATTACHMENT_KIND_TEXTURE) {
                GX2Texture *t = gl_get_gx2_texture(fb->color_attachments[i].object);
                if (t) init_color_buffer_from_surface(&fb->cb[i], &t->surface);
            } else {
                GLRenderbuffer *rb = get_renderbuffer(fb->color_attachments[i].object);
                if (rb && !rb->is_depth) fb->cb[i] = rb->color_buffer;
            }
        }
        if (attachment_ref_present(&fb->depth_attachment)) {
            if (fb->depth_attachment.kind == GL_ATTACHMENT_KIND_TEXTURE) {
                GX2Texture *t = gl_get_gx2_texture(fb->depth_attachment.object);
                if (t) init_depth_buffer_from_surface(&fb->db, &t->surface);
            } else {
                GLRenderbuffer *rb = get_renderbuffer(fb->depth_attachment.object);
                if (rb && rb->is_depth) fb->db = rb->depth_buffer;
            }
        }
        fb->dirty = false;
    }
    for (uint32_t i = 0; i < 8; ++i) {
        if (attachment_ref_present(&fb->color_attachments[i]) &&
            fb->draw_buffers[i] == (GL_COLOR_ATTACHMENT0 + i))
            GX2SetColorBuffer(&fb->cb[i], (GX2RenderTarget)i);
    }
    if (attachment_ref_present(&fb->depth_attachment))
        GX2SetDepthBuffer(&fb->db);
    apply_framebuffer_output_state(fb, false);
}

GLboolean gl_is_draw_color_buffer_enabled(GLuint index) { (void)index; return GL_TRUE; }
GX2ColorBuffer *gl_get_draw_color_buffer(GLuint index) {
    if (!g_gl_context) return NULL;
    GLuint fbo = g_gl_context->bound_framebuffer;
    if (fbo == 0) return (index == 0) ? WHBGfxGetTVColourBuffer() : NULL;
    GLFramebuffer *fb = &g_framebuffers[fbo];
    if (index < 8 && attachment_ref_present(&fb->color_attachments[index])) return &fb->cb[index];
    return NULL;
}
GX2DepthBuffer *gl_get_draw_depth_buffer(void) {
    if (!g_gl_context) return NULL;
    GLuint fbo = g_gl_context->bound_framebuffer;
    if (fbo == 0) return WHBGfxGetTVDepthBuffer();
    GLFramebuffer *fb = &g_framebuffers[fbo];
    if (attachment_ref_present(&fb->depth_attachment)) return &fb->db;
    return NULL;
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
    (void)x; (void)y; (void)width; (void)height; (void)pixels;
    return GL_FALSE;
}

#ifdef __cplusplus
}
#endif
