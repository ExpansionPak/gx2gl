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
    if (!fb) {
        return;
    }

    for (uint32_t i = 0; i < 8; ++i) {
        fb->draw_buffers[i] = GL_NONE;
    }
    fb->draw_buffers[0] = is_default ? GL_BACK : GL_COLOR_ATTACHMENT0;
}

static void init_read_buffer_default(GLFramebuffer *fb, bool is_default) {
    if (!fb) {
        return;
    }

    fb->read_buffer = is_default ? GL_BACK : GL_COLOR_ATTACHMENT0;
}

static bool is_framebuffer_target(GLenum target) {
    return target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER ||
           target == GL_READ_FRAMEBUFFER;
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
    if (!attachment) {
        return;
    }
    attachment->kind = GL_ATTACHMENT_KIND_NONE;
    attachment->object = 0;
}

static bool attachment_ref_present(const GLAttachmentRef *attachment) {
    return attachment && attachment->kind != GL_ATTACHMENT_KIND_NONE &&
           attachment->object != 0;
}

static GLAttachmentRef *get_attachment_ref(GLFramebuffer *fb, GLenum attachment) {
    if (!fb) {
        return NULL;
    }
    if (attachment >= GL_COLOR_ATTACHMENT0 && attachment <= GL_COLOR_ATTACHMENT7) {
        return &fb->color_attachments[attachment - GL_COLOR_ATTACHMENT0];
    }
    if (attachment == GL_DEPTH_ATTACHMENT) {
        return &fb->depth_attachment;
    }
    if (attachment == GL_STENCIL_ATTACHMENT) {
        return &fb->stencil_attachment;
    }
    return NULL;
}

static GLRenderbuffer *get_renderbuffer(GLuint id) {
    if (id == 0 || id >= MAX_RENDERBUFFERS || !g_renderbuffers[id].in_use) {
        return NULL;
    }
    return &g_renderbuffers[id];
}

static bool is_depth_internal_format(GLint internal_format) {
    switch (internal_format) {
    case GL_DEPTH_COMPONENT:
    case GL_DEPTH_COMPONENT32F:
    case GL_DEPTH_STENCIL:
    case GL_DEPTH24_STENCIL8:
        return true;
    default:
        return false;
    }
}

static bool is_stencil_internal_format(GLint internal_format) {
    return internal_format == GL_DEPTH_STENCIL ||
           internal_format == GL_DEPTH24_STENCIL8;
}

static bool get_renderbuffer_format_info(GLenum internalformat,
                                         GX2SurfaceFormat *gx2_format,
                                         GX2SurfaceUse *surface_use,
                                         bool *is_depth) {
    if (!gx2_format || !surface_use || !is_depth) {
        return false;
    }

    switch (internalformat) {
    case 1:
    case GL_RED:
    case GL_R8:
        *gx2_format = GX2_SURFACE_FORMAT_UNORM_R8;
        *surface_use =
            (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
        *is_depth = false;
        return true;
    case 2:
    case GL_RG:
    case GL_RG8:
        *gx2_format = GX2_SURFACE_FORMAT_UNORM_R8_G8;
        *surface_use =
            (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
        *is_depth = false;
        return true;
    case 3:
    case GL_RGB:
    case GL_RGB8:
    case 4:
    case GL_RGBA:
    case GL_RGBA8:
        *gx2_format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
        *surface_use =
            (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
        *is_depth = false;
        return true;
    case GL_RGBA16F:
        *gx2_format = GX2_SURFACE_FORMAT_FLOAT_R16_G16_B16_A16;
        *surface_use =
            (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
        *is_depth = false;
        return true;
    case GL_RGBA32F:
        *gx2_format = GX2_SURFACE_FORMAT_FLOAT_R32_G32_B32_A32;
        *surface_use =
            (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
        *is_depth = false;
        return true;
    case GL_DEPTH_COMPONENT:
    case GL_DEPTH_COMPONENT32F:
        *gx2_format = GX2_SURFACE_FORMAT_FLOAT_R32;
        *surface_use =
            (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_DEPTH_BUFFER);
        *is_depth = true;
        return true;
    case GL_DEPTH_STENCIL:
    case GL_DEPTH24_STENCIL8:
        *gx2_format = GX2_SURFACE_FORMAT_UNORM_R24_X8;
        *surface_use =
            (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_DEPTH_BUFFER);
        *is_depth = true;
        return true;
    default:
        return false;
    }
}

static GX2ChannelMask build_color_channel_mask(void) {
    uint8_t mask = 0;

    if (!g_gl_context) {
        return (GX2ChannelMask)0;
    }
    if (g_gl_context->color_mask[0])
        mask |= GX2_CHANNEL_MASK_R;
    if (g_gl_context->color_mask[1])
        mask |= GX2_CHANNEL_MASK_G;
    if (g_gl_context->color_mask[2])
        mask |= GX2_CHANNEL_MASK_B;
    if (g_gl_context->color_mask[3])
        mask |= GX2_CHANNEL_MASK_A;

    return (GX2ChannelMask)mask;
}

static void apply_framebuffer_output_state(GLFramebuffer *fb, bool is_default) {
    GX2ChannelMask masks[8] = {
        (GX2ChannelMask)0, (GX2ChannelMask)0, (GX2ChannelMask)0,
        (GX2ChannelMask)0, (GX2ChannelMask)0, (GX2ChannelMask)0,
        (GX2ChannelMask)0, (GX2ChannelMask)0};
    GX2ChannelMask color_mask;
    uint8_t active_target_mask = 0;
    uint32_t active_target_count = 0;

    if (!g_gl_context || !fb) {
        return;
    }

    color_mask = build_color_channel_mask();
    for (uint32_t i = 0; i < 8; ++i) {
        bool enabled = false;

        if (is_default) {
            enabled = (i == 0 && fb->draw_buffers[0] == GL_BACK);
        } else if (fb->draw_buffers[i] == (GL_COLOR_ATTACHMENT0 + i) &&
                   attachment_ref_present(&fb->color_attachments[i])) {
            enabled = true;
        }

        if (enabled && color_mask != 0) {
            masks[i] = color_mask;
            active_target_mask |= (uint8_t)(1u << i);
            ++active_target_count;
        }
    }

    GX2SetTargetChannelMasks(masks[0], masks[1], masks[2], masks[3],
                             masks[4], masks[5], masks[6], masks[7]);
    GX2SetColorControl(GX2_LOGIC_OP_COPY,
                       g_gl_context->blend_enabled ? active_target_mask : 0,
                       active_target_count > 1 ? GX2_ENABLE : GX2_DISABLE,
                       active_target_mask != 0 ? GX2_ENABLE : GX2_DISABLE);
}

static void init_color_buffer_from_surface(GX2ColorBuffer *color_buffer,
                                           const GX2Surface *surface) {
    memset(color_buffer, 0, sizeof(*color_buffer));
    color_buffer->surface = *surface;
    color_buffer->viewMip = 0;
    color_buffer->viewFirstSlice = 0;
    color_buffer->viewNumSlices = 1;
    color_buffer->aaBuffer = NULL;
    color_buffer->aaSize = 0;
    GX2InitColorBufferRegs(color_buffer);
}

static void init_color_buffer_from_texture(GX2ColorBuffer *color_buffer,
                                           const GX2Texture *texture) {
    init_color_buffer_from_surface(color_buffer, &texture->surface);
}

static void init_depth_buffer_from_surface(GX2DepthBuffer *depth_buffer,
                                           const GX2Surface *surface) {
    memset(depth_buffer, 0, sizeof(*depth_buffer));
    depth_buffer->surface = *surface;
    depth_buffer->viewMip = 0;
    depth_buffer->viewFirstSlice = 0;
    depth_buffer->viewNumSlices = 1;
    depth_buffer->hiZPtr = NULL;
    depth_buffer->hiZSize = 0;
    depth_buffer->depthClear = 1.0f;
    depth_buffer->stencilClear = 0;
    GX2InitDepthBufferRegs(depth_buffer);
}

static void init_depth_buffer_from_texture(GX2DepthBuffer *depth_buffer,
                                           const GX2Texture *texture) {
    init_depth_buffer_from_surface(depth_buffer, &texture->surface);
}

static void free_renderbuffer_storage(GLRenderbuffer *renderbuffer) {
    if (!renderbuffer) {
        return;
    }
    if (renderbuffer->surface.image) {
        gl_mem_free(GL_MEM_TYPE_MEM2, renderbuffer->surface.image);
        renderbuffer->surface.image = NULL;
    }
    memset(&renderbuffer->surface, 0, sizeof(renderbuffer->surface));
    memset(&renderbuffer->color_buffer, 0, sizeof(renderbuffer->color_buffer));
    memset(&renderbuffer->depth_buffer, 0, sizeof(renderbuffer->depth_buffer));
    renderbuffer->internal_format = 0;
    renderbuffer->width = 0;
    renderbuffer->height = 0;
    renderbuffer->is_depth = false;
}

typedef struct {
    GX2Surface surface;
    bool owns_image;
    uint32_t base_x;
    uint32_t base_y;
} GLReadbackSurface;

static GLint infer_internal_format_from_surface_format(GX2SurfaceFormat format,
                                                       bool is_depth) {
    switch (format) {
    case GX2_SURFACE_FORMAT_UNORM_R8:
        return GL_R8;
    case GX2_SURFACE_FORMAT_UNORM_R8_G8:
        return GL_RG8;
    case GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8:
        return GL_RGBA8;
    case GX2_SURFACE_FORMAT_FLOAT_R16_G16_B16_A16:
        return GL_RGBA16F;
    case GX2_SURFACE_FORMAT_FLOAT_R32_G32_B32_A32:
        return GL_RGBA32F;
    case GX2_SURFACE_FORMAT_FLOAT_R32:
        return is_depth ? GL_DEPTH_COMPONENT32F : GL_RED;
    case GX2_SURFACE_FORMAT_UNORM_R24_X8:
        return GL_DEPTH24_STENCIL8;
    default:
        return 0;
    }
}

static uint32_t bytes_per_source_texel(GLint internal_format) {
    switch (internal_format) {
    case GL_ALPHA:
    case GL_LUMINANCE:
    case 1:
    case GL_RED:
    case GL_R8:
        return 1;
    case GL_LUMINANCE_ALPHA:
    case 2:
    case GL_RG:
    case GL_RG8:
        return 2;
    case 3:
    case GL_RGB:
    case GL_RGB8:
    case 4:
    case GL_RGBA:
    case GL_RGBA8:
        return 4;
    case GL_RGBA16F:
        return 8;
    case GL_RGBA32F:
        return 16;
    case GL_DEPTH_COMPONENT:
    case GL_DEPTH_COMPONENT32F:
    case GL_DEPTH_STENCIL:
    case GL_DEPTH24_STENCIL8:
        return 4;
    default:
        return 0;
    }
}

static uint32_t bytes_per_destination_pixel(GLenum format, GLenum type) {
    switch (type) {
    case GL_UNSIGNED_BYTE:
        switch (format) {
        case GL_RED:
            return 1;
        case GL_RG:
            return 2;
        case GL_RGB:
            return 3;
        case GL_RGBA:
            return 4;
        default:
            return 0;
        }
    case GL_FLOAT:
        switch (format) {
        case GL_RED:
        case GL_DEPTH_COMPONENT:
            return 4;
        case GL_RG:
            return 8;
        case GL_RGB:
            return 12;
        case GL_RGBA:
            return 16;
        default:
            return 0;
        }
    case GL_UNSIGNED_INT_24_8:
        return format == GL_DEPTH_STENCIL ? 4 : 0;
    default:
        return 0;
    }
}

static float clamp_unit_float(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

static float half_to_float(uint16_t half_value) {
    uint32_t sign = (uint32_t)(half_value & 0x8000u) << 16;
    uint32_t exponent = (half_value >> 10) & 0x1Fu;
    uint32_t mantissa = half_value & 0x03FFu;
    uint32_t bits;
    float result;

    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            int32_t shift = -1;
            do {
                ++shift;
                mantissa <<= 1;
            } while ((mantissa & 0x0400u) == 0);
            mantissa &= 0x03FFu;
            bits = sign | (uint32_t)(127 - 15 - shift) << 23 |
                   (mantissa << 13);
        }
    } else if (exponent == 0x1Fu) {
        bits = sign | 0x7F800000u | (mantissa << 13);
    } else {
        bits = sign | ((exponent + (127 - 15)) << 23) | (mantissa << 13);
    }

    memcpy(&result, &bits, sizeof(result));
    return result;
}

static bool decode_color_texel(const uint8_t *src, GLint internal_format,
                               float rgba[4]) {
    uint32_t word;

    rgba[0] = 0.0f;
    rgba[1] = 0.0f;
    rgba[2] = 0.0f;
    rgba[3] = 1.0f;

    switch (internal_format) {
    case GL_ALPHA:
        rgba[3] = src[0] / 255.0f;
        return true;
    case GL_LUMINANCE:
        rgba[0] = src[0] / 255.0f;
        rgba[1] = rgba[0];
        rgba[2] = rgba[0];
        return true;
    case GL_LUMINANCE_ALPHA:
        rgba[0] = src[0] / 255.0f;
        rgba[1] = rgba[0];
        rgba[2] = rgba[0];
        rgba[3] = src[1] / 255.0f;
        return true;
    case 1:
    case GL_RED:
    case GL_R8:
        rgba[0] = src[0] / 255.0f;
        return true;
    case 2:
    case GL_RG:
    case GL_RG8:
        rgba[0] = src[0] / 255.0f;
        rgba[1] = src[1] / 255.0f;
        return true;
    case 3:
    case GL_RGB:
    case GL_RGB8:
    case 4:
    case GL_RGBA:
    case GL_RGBA8:
        rgba[0] = src[0] / 255.0f;
        rgba[1] = src[1] / 255.0f;
        rgba[2] = src[2] / 255.0f;
        rgba[3] = src[3] / 255.0f;
        return true;
    case GL_RGBA16F: {
        uint16_t half_word;
        for (uint32_t i = 0; i < 4; ++i) {
            memcpy(&half_word, src + i * sizeof(uint16_t), sizeof(uint16_t));
            rgba[i] = half_to_float(GPU_TO_CPU_16(half_word));
        }
        return true;
    }
    case GL_RGBA32F:
        for (uint32_t i = 0; i < 4; ++i) {
            float component;
            memcpy(&word, src + i * sizeof(uint32_t), sizeof(uint32_t));
            word = GPU_TO_CPU_32(word);
            memcpy(&component, &word, sizeof(component));
            rgba[i] = component;
        }
        return true;
    default:
        return false;
    }
}

static bool decode_depth_texel(const uint8_t *src, GLint internal_format,
                               float *depth_out, uint8_t *stencil_out) {
    uint32_t word;
    float depth_value;

    if (!depth_out || !stencil_out) {
        return false;
    }

    *depth_out = 1.0f;
    *stencil_out = 0;

    switch (internal_format) {
    case GL_DEPTH_COMPONENT:
    case GL_DEPTH_COMPONENT32F:
        memcpy(&word, src, sizeof(word));
        word = GPU_TO_CPU_32(word);
        memcpy(&depth_value, &word, sizeof(depth_value));
        *depth_out = depth_value;
        return true;
    case GL_DEPTH_STENCIL:
    case GL_DEPTH24_STENCIL8:
        memcpy(&word, src, sizeof(word));
        word = GPU_TO_CPU_32(word);
        *depth_out = (float)((word >> 8) & 0x00FFFFFFu) / 16777215.0f;
        *stencil_out = (uint8_t)(word & 0xFFu);
        return true;
    default:
        return false;
    }
}

static bool encode_color_texel(const float rgba[4], GLenum format, GLenum type,
                               uint8_t *dst) {
    uint32_t component_count;
    uint32_t src_indices[4] = {0, 1, 2, 3};

    switch (format) {
    case GL_RED:
        component_count = 1;
        break;
    case GL_RG:
        component_count = 2;
        src_indices[1] = 1;
        break;
    case GL_RGB:
        component_count = 3;
        src_indices[1] = 1;
        src_indices[2] = 2;
        break;
    case GL_RGBA:
        component_count = 4;
        src_indices[1] = 1;
        src_indices[2] = 2;
        src_indices[3] = 3;
        break;
    default:
        return false;
    }

    if (type == GL_UNSIGNED_BYTE) {
        for (uint32_t i = 0; i < component_count; ++i) {
            float clamped = clamp_unit_float(rgba[src_indices[i]]);
            dst[i] = (uint8_t)(clamped * 255.0f + 0.5f);
        }
        return true;
    }

    if (type == GL_FLOAT) {
        float *dst_float = (float *)dst;
        for (uint32_t i = 0; i < component_count; ++i) {
            dst_float[i] = rgba[src_indices[i]];
        }
        return true;
    }

    return false;
}

static bool encode_depth_texel(float depth, uint8_t stencil, GLenum format,
                               GLenum type, uint8_t *dst) {
    if (format == GL_DEPTH_COMPONENT && type == GL_FLOAT) {
        ((float *)dst)[0] = depth;
        return true;
    }

    if (format == GL_DEPTH_STENCIL && type == GL_UNSIGNED_INT_24_8) {
        uint32_t depth_bits =
            (uint32_t)(clamp_unit_float(depth) * 16777215.0f + 0.5f);
        ((uint32_t *)dst)[0] = (depth_bits << 8) | stencil;
        return true;
    }

    return false;
}

static uint32_t align_transfer_row_bytes(uint32_t row_bytes, GLint alignment) {
    uint32_t align = (uint32_t)alignment;
    if (align <= 1u) {
        return row_bytes;
    }
    return (row_bytes + align - 1u) & ~(align - 1u);
}

static bool get_pack_layout(uint32_t texel_size, GLsizei width, GLsizei height,
                            uint32_t *row_bytes, uint32_t *image_bytes,
                            size_t *base_offset) {
    GLint row_length;
    GLint image_height;

    if (!g_gl_context || !row_bytes || !image_bytes || !base_offset) {
        return false;
    }

    row_length = g_gl_context->pack_row_length > 0 ? g_gl_context->pack_row_length
                                                   : width;
    image_height = g_gl_context->pack_image_height > 0
                       ? g_gl_context->pack_image_height
                       : height;

    *row_bytes = align_transfer_row_bytes(
        (uint32_t)row_length * texel_size, g_gl_context->pack_alignment);
    *image_bytes = *row_bytes * (uint32_t)image_height;
    *base_offset =
        (size_t)g_gl_context->pack_skip_images * (*image_bytes) +
        (size_t)g_gl_context->pack_skip_rows * (*row_bytes) +
        (size_t)g_gl_context->pack_skip_pixels * texel_size;
    return true;
}

static bool prepare_readback_surface(const GX2Surface *source_surface,
                                     bool is_depth_surface, GLint x, GLint y,
                                     GLsizei width, GLsizei height,
                                     GLReadbackSurface *out_surface) {
    GX2Rect rect;
    GX2Point point;
    GX2InvalidateMode invalidate_mode;

    if (!source_surface || !out_surface) {
        return false;
    }

    memset(out_surface, 0, sizeof(*out_surface));
    if (source_surface->tileMode == GX2_TILE_MODE_LINEAR_ALIGNED) {
        out_surface->surface = *source_surface;
        out_surface->base_x = (uint32_t)x;
        out_surface->base_y = (uint32_t)y;
        invalidate_mode = (GX2InvalidateMode)(GX2_INVALIDATE_MODE_CPU |
                                              GX2_INVALIDATE_MODE_TEXTURE |
                                              (is_depth_surface
                                                   ? GX2_INVALIDATE_MODE_DEPTH_BUFFER
                                                   : GX2_INVALIDATE_MODE_COLOR_BUFFER));
        GX2Invalidate(invalidate_mode, out_surface->surface.image,
                      out_surface->surface.imageSize);
        return true;
    }

    out_surface->surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    out_surface->surface.width = (uint32_t)width;
    out_surface->surface.height = (uint32_t)height;
    out_surface->surface.depth = 1;
    out_surface->surface.mipLevels = 1;
    out_surface->surface.format = source_surface->format;
    out_surface->surface.aa = GX2_AA_MODE1X;
    out_surface->surface.use =
        (GX2SurfaceUse)(source_surface->use | GX2_SURFACE_USE_TEXTURE);
    out_surface->surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
    GX2CalcSurfaceSizeAndAlignment(&out_surface->surface);
    out_surface->surface.image = gl_mem_alloc(
        GL_MEM_TYPE_MEM2, out_surface->surface.imageSize,
        out_surface->surface.alignment);
    if (!out_surface->surface.image) {
        memset(out_surface, 0, sizeof(*out_surface));
        return false;
    }

    memset(out_surface->surface.image, 0, out_surface->surface.imageSize);
    out_surface->owns_image = true;

    rect.left = x;
    rect.top = y;
    rect.right = x + width;
    rect.bottom = y + height;
    point.x = 0;
    point.y = 0;
    GX2CopySurfaceEx(source_surface, 0, 0, &out_surface->surface, 0, 0, 1,
                     &rect, &point);
    GX2DrawDone();
    invalidate_mode = (GX2InvalidateMode)(GX2_INVALIDATE_MODE_CPU |
                                          GX2_INVALIDATE_MODE_TEXTURE);
    GX2Invalidate(invalidate_mode, out_surface->surface.image,
                  out_surface->surface.imageSize);
    out_surface->base_x = 0;
    out_surface->base_y = 0;
    return true;
}

static void release_readback_surface(GLReadbackSurface *surface) {
    if (!surface) {
        return;
    }

    if (surface->owns_image && surface->surface.image) {
        gl_mem_free(GL_MEM_TYPE_MEM2, surface->surface.image);
    }
    memset(surface, 0, sizeof(*surface));
}

static bool get_attachment_dimensions(const GLAttachmentRef *attachment,
                                      GLsizei *width, GLsizei *height) {
    GX2Texture *texture;
    GLRenderbuffer *renderbuffer;

    if (!attachment_ref_present(attachment) || !width || !height) {
        return false;
    }

    if (attachment->kind == GL_ATTACHMENT_KIND_TEXTURE) {
        texture = gl_get_gx2_texture(attachment->object);
        if (!texture || !texture->surface.image) {
            return false;
        }
        *width = (GLsizei)texture->surface.width;
        *height = (GLsizei)texture->surface.height;
        return true;
    }

    if (attachment->kind == GL_ATTACHMENT_KIND_RENDERBUFFER) {
        renderbuffer = get_renderbuffer(attachment->object);
        if (!renderbuffer || !renderbuffer->surface.image) {
            return false;
        }
        *width = renderbuffer->width;
        *height = renderbuffer->height;
        return true;
    }

    return false;
}

static GLint get_attachment_internal_format(const GLAttachmentRef *attachment) {
    GLRenderbuffer *renderbuffer;

    if (!attachment_ref_present(attachment)) {
        return 0;
    }

    if (attachment->kind == GL_ATTACHMENT_KIND_TEXTURE) {
        return gl_get_texture_internal_format(attachment->object);
    }

    if (attachment->kind == GL_ATTACHMENT_KIND_RENDERBUFFER) {
        renderbuffer = get_renderbuffer(attachment->object);
        return renderbuffer ? renderbuffer->internal_format : 0;
    }

    return 0;
}

static bool ensure_framebuffer_cache(GLFramebuffer *fb) {
    if (!fb) {
        return false;
    }

    if (!fb->dirty) {
        return true;
    }

    for (int i = 0; i < 8; i++) {
        if (attachment_ref_present(&fb->color_attachments[i])) {
            if (fb->color_attachments[i].kind == GL_ATTACHMENT_KIND_TEXTURE) {
                GX2Texture *tex = gl_get_gx2_texture(fb->color_attachments[i].object);
                if (tex) {
                    init_color_buffer_from_texture(&fb->cb[i], tex);
                }
            } else if (fb->color_attachments[i].kind ==
                       GL_ATTACHMENT_KIND_RENDERBUFFER) {
                GLRenderbuffer *renderbuffer =
                    get_renderbuffer(fb->color_attachments[i].object);
                if (renderbuffer && renderbuffer->surface.image) {
                    fb->cb[i] = renderbuffer->color_buffer;
                } else {
                    memset(&fb->cb[i], 0, sizeof(fb->cb[i]));
                }
            } else {
                memset(&fb->cb[i], 0, sizeof(fb->cb[i]));
            }
        } else {
            memset(&fb->cb[i], 0, sizeof(fb->cb[i]));
        }
    }

    if (attachment_ref_present(&fb->depth_attachment)) {
        if (fb->depth_attachment.kind == GL_ATTACHMENT_KIND_TEXTURE) {
            GX2Texture *tex = gl_get_gx2_texture(fb->depth_attachment.object);
            if (tex) {
                init_depth_buffer_from_texture(&fb->db, tex);
            } else {
                memset(&fb->db, 0, sizeof(fb->db));
            }
        } else if (fb->depth_attachment.kind == GL_ATTACHMENT_KIND_RENDERBUFFER) {
            GLRenderbuffer *renderbuffer = get_renderbuffer(fb->depth_attachment.object);
            if (renderbuffer && renderbuffer->surface.image) {
                fb->db = renderbuffer->depth_buffer;
            } else {
                memset(&fb->db, 0, sizeof(fb->db));
            }
        } else {
            memset(&fb->db, 0, sizeof(fb->db));
        }
    } else if (attachment_ref_present(&fb->stencil_attachment)) {
        if (fb->stencil_attachment.kind == GL_ATTACHMENT_KIND_TEXTURE) {
            GX2Texture *tex = gl_get_gx2_texture(fb->stencil_attachment.object);
            if (tex) {
                init_depth_buffer_from_texture(&fb->db, tex);
            } else {
                memset(&fb->db, 0, sizeof(fb->db));
            }
        } else if (fb->stencil_attachment.kind == GL_ATTACHMENT_KIND_RENDERBUFFER) {
            GLRenderbuffer *renderbuffer =
                get_renderbuffer(fb->stencil_attachment.object);
            if (renderbuffer && renderbuffer->surface.image) {
                fb->db = renderbuffer->depth_buffer;
            } else {
                memset(&fb->db, 0, sizeof(fb->db));
            }
        } else {
            memset(&fb->db, 0, sizeof(fb->db));
        }
    } else {
        memset(&fb->db, 0, sizeof(fb->db));
    }

    fb->dirty = false;
    return true;
}

static GLuint get_read_color_attachment_index(GLuint fbo) {
    GLenum read_buffer = g_framebuffers[fbo].read_buffer;

    if (read_buffer == GL_NONE) {
        return UINT32_MAX;
    }
    if (fbo == 0) {
        return read_buffer == GL_BACK ? 0u : UINT32_MAX;
    }
    if (read_buffer < GL_COLOR_ATTACHMENT0 || read_buffer > GL_COLOR_ATTACHMENT7) {
        return UINT32_MAX;
    }
    return (GLuint)(read_buffer - GL_COLOR_ATTACHMENT0);
}

static GX2ColorBuffer *get_read_color_buffer(void) {
    GLuint fbo;
    GLuint attachment_index;

    if (!g_gl_context) {
        return NULL;
    }

    fbo = g_gl_context->bound_read_framebuffer;
    attachment_index = get_read_color_attachment_index(fbo);
    if (attachment_index == UINT32_MAX) {
        return NULL;
    }

    if (fbo == 0) {
        return WHBGfxGetTVColourBuffer();
    }

    if (!attachment_ref_present(&g_framebuffers[fbo].color_attachments[attachment_index])) {
        return NULL;
    }
    if (!ensure_framebuffer_cache(&g_framebuffers[fbo])) {
        return NULL;
    }

    return &g_framebuffers[fbo].cb[attachment_index];
}

static GLint get_read_color_internal_format(void) {
    GLuint fbo;
    GLuint attachment_index;

    if (!g_gl_context) {
        return 0;
    }

    fbo = g_gl_context->bound_read_framebuffer;
    attachment_index = get_read_color_attachment_index(fbo);
    if (attachment_index == UINT32_MAX) {
        return 0;
    }

    if (fbo == 0) {
        GX2ColorBuffer *tv_color = WHBGfxGetTVColourBuffer();
        return tv_color ? infer_internal_format_from_surface_format(
                              tv_color->surface.format, false)
                        : 0;
    }

    return get_attachment_internal_format(
        &g_framebuffers[fbo].color_attachments[attachment_index]);
}

static GX2DepthBuffer *get_read_depth_buffer(void) {
    GLuint fbo;

    if (!g_gl_context) {
        return NULL;
    }

    fbo = g_gl_context->bound_read_framebuffer;
    if (fbo == 0) {
        return WHBGfxGetTVDepthBuffer();
    }
    if (!attachment_ref_present(&g_framebuffers[fbo].depth_attachment) &&
        !attachment_ref_present(&g_framebuffers[fbo].stencil_attachment)) {
        return NULL;
    }
    if (!ensure_framebuffer_cache(&g_framebuffers[fbo])) {
        return NULL;
    }

    return &g_framebuffers[fbo].db;
}

static GLint get_read_depth_internal_format(void) {
    GLuint fbo;

    if (!g_gl_context) {
        return 0;
    }

    fbo = g_gl_context->bound_read_framebuffer;
    if (fbo == 0) {
        GX2DepthBuffer *tv_depth = WHBGfxGetTVDepthBuffer();
        return tv_depth ? infer_internal_format_from_surface_format(
                              tv_depth->surface.format, true)
                        : 0;
    }

    if (attachment_ref_present(&g_framebuffers[fbo].depth_attachment)) {
        return get_attachment_internal_format(&g_framebuffers[fbo].depth_attachment);
    }

    return get_attachment_internal_format(&g_framebuffers[fbo].stencil_attachment);
}

void gl_framebuffer_init(void) {
    memset(g_framebuffers, 0, sizeof(g_framebuffers));
    memset(g_renderbuffers, 0, sizeof(g_renderbuffers));
    // Reserve default FBO
    g_framebuffers[0].in_use = true;
    init_draw_buffer_defaults(&g_framebuffers[0], true);
    init_read_buffer_default(&g_framebuffers[0], true);
}

#ifdef __cplusplus
extern "C" {
#endif

void _gl_GenFramebuffers(GLsizei n, GLuint *framebuffers) {
    if (!g_gl_context || n < 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    int count = 0;
    for (int i = 1; i < MAX_FRAMEBUFFERS && count < n; i++) {
        if (!g_framebuffers[i].in_use) {
            g_framebuffers[i].in_use = true;
            g_framebuffers[i].dirty = true;
            memset(g_framebuffers[i].color_attachments, 0, sizeof(g_framebuffers[i].color_attachments));
            init_draw_buffer_defaults(&g_framebuffers[i], false);
            init_read_buffer_default(&g_framebuffers[i], false);
            clear_attachment_ref(&g_framebuffers[i].depth_attachment);
            clear_attachment_ref(&g_framebuffers[i].stencil_attachment);
            framebuffers[count++] = i;
        }
    }
}

void _gl_GenRenderbuffers(GLsizei n, GLuint *renderbuffers) {
    int count = 0;

    if (!g_gl_context || n < 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }

    for (int i = 1; i < MAX_RENDERBUFFERS && count < n; ++i) {
        if (!g_renderbuffers[i].in_use) {
            memset(&g_renderbuffers[i], 0, sizeof(g_renderbuffers[i]));
            g_renderbuffers[i].in_use = true;
            renderbuffers[count++] = i;
        }
    }

    if (count < n) {
        _gl_set_error(GL_OUT_OF_MEMORY);
    }
}

GLboolean _gl_IsRenderbuffer(GLuint renderbuffer) {
    return get_renderbuffer(renderbuffer) ? GL_TRUE : GL_FALSE;
}

void _gl_DeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers) {
    if (!g_gl_context || n < 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }

    for (int i = 0; i < n; ++i) {
        GLuint id = renderbuffers[i];
        if (id == 0 || id >= MAX_RENDERBUFFERS || !g_renderbuffers[id].in_use) {
            continue;
        }

        for (int fbo = 1; fbo < MAX_FRAMEBUFFERS; ++fbo) {
            if (!g_framebuffers[fbo].in_use) {
                continue;
            }

            for (int color = 0; color < 8; ++color) {
                if (g_framebuffers[fbo].color_attachments[color].kind ==
                        GL_ATTACHMENT_KIND_RENDERBUFFER &&
                    g_framebuffers[fbo].color_attachments[color].object == id) {
                    clear_attachment_ref(&g_framebuffers[fbo].color_attachments[color]);
                    g_framebuffers[fbo].dirty = true;
                }
            }

            if (g_framebuffers[fbo].depth_attachment.kind ==
                    GL_ATTACHMENT_KIND_RENDERBUFFER &&
                g_framebuffers[fbo].depth_attachment.object == id) {
                clear_attachment_ref(&g_framebuffers[fbo].depth_attachment);
                g_framebuffers[fbo].dirty = true;
            }

            if (g_framebuffers[fbo].stencil_attachment.kind ==
                    GL_ATTACHMENT_KIND_RENDERBUFFER &&
                g_framebuffers[fbo].stencil_attachment.object == id) {
                clear_attachment_ref(&g_framebuffers[fbo].stencil_attachment);
                g_framebuffers[fbo].dirty = true;
            }
        }

        if (g_gl_context->bound_renderbuffer == id) {
            g_gl_context->bound_renderbuffer = 0;
        }

        free_renderbuffer_storage(&g_renderbuffers[id]);
        memset(&g_renderbuffers[id], 0, sizeof(g_renderbuffers[id]));
    }

    g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
}

GLboolean _gl_IsFramebuffer(GLuint framebuffer) {
    return (framebuffer < MAX_FRAMEBUFFERS && g_framebuffers[framebuffer].in_use)
               ? GL_TRUE
               : GL_FALSE;
}

void _gl_DeleteFramebuffers(GLsizei n, const GLuint *framebuffers) {
    if (!g_gl_context || n < 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    for (int i = 0; i < n; i++) {
        GLuint id = framebuffers[i];
        if (id > 0 && id < MAX_FRAMEBUFFERS && g_framebuffers[id].in_use) {
            g_framebuffers[id].in_use = false;
            if (g_gl_context->bound_framebuffer == id) {
                g_gl_context->bound_framebuffer = 0;
                g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
            }
            if (g_gl_context->bound_read_framebuffer == id) {
                g_gl_context->bound_read_framebuffer = 0;
            }
        }
    }
}

void _gl_BindFramebuffer(GLenum target, GLuint framebuffer) {
    if (!g_gl_context) return;
    if (!is_framebuffer_target(target)) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (framebuffer >= MAX_FRAMEBUFFERS || (!g_framebuffers[framebuffer].in_use && framebuffer > 0)) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    switch (target) {
    case GL_FRAMEBUFFER:
        g_gl_context->bound_framebuffer = framebuffer;
        g_gl_context->bound_read_framebuffer = framebuffer;
        g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
        break;
    case GL_DRAW_FRAMEBUFFER:
        g_gl_context->bound_framebuffer = framebuffer;
        g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
        break;
    case GL_READ_FRAMEBUFFER:
        g_gl_context->bound_read_framebuffer = framebuffer;
        break;
    default:
        _gl_set_error(GL_INVALID_ENUM);
        break;
    }
}

void _gl_BindRenderbuffer(GLenum target, GLuint renderbuffer) {
    if (!g_gl_context) {
        return;
    }
    if (target != GL_RENDERBUFFER) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (renderbuffer >= MAX_RENDERBUFFERS ||
        (renderbuffer > 0 && !g_renderbuffers[renderbuffer].in_use)) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }

    g_gl_context->bound_renderbuffer = renderbuffer;
}

void _gl_GetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params) {
    GLRenderbuffer *renderbuffer;

    if (!g_gl_context || !params) {
        return;
    }
    if (target != GL_RENDERBUFFER) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }

    renderbuffer = get_renderbuffer(g_gl_context->bound_renderbuffer);
    if (!renderbuffer) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }

    switch (pname) {
    case GL_RENDERBUFFER_WIDTH:
        *params = renderbuffer->width;
        break;
    case GL_RENDERBUFFER_HEIGHT:
        *params = renderbuffer->height;
        break;
    case GL_RENDERBUFFER_INTERNAL_FORMAT:
        *params = renderbuffer->internal_format;
        break;
    default:
        _gl_set_error(GL_INVALID_ENUM);
        break;
    }
}

void _gl_GetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment,
                                             GLenum pname, GLint *params) {
    GLuint fbo;
    GLFramebuffer *fb;
    GLAttachmentRef *ref;

    if (!g_gl_context || !params) {
        return;
    }
    if (!is_framebuffer_target(target)) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }

    fbo = get_bound_framebuffer_for_target(target);
    if (fbo == 0) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }

    fb = &g_framebuffers[fbo];
    if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        ref = attachment_ref_present(&fb->depth_attachment)
                  ? &fb->depth_attachment
                  : &fb->stencil_attachment;
    } else {
        ref = get_attachment_ref(fb, attachment);
    }
    if (!ref) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }

    switch (pname) {
    case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE:
        if (!attachment_ref_present(ref)) {
            *params = GL_NONE;
        } else if (ref->kind == GL_ATTACHMENT_KIND_TEXTURE) {
            *params = GL_TEXTURE;
        } else if (ref->kind == GL_ATTACHMENT_KIND_RENDERBUFFER) {
            *params = GL_RENDERBUFFER;
        } else {
            *params = GL_NONE;
        }
        break;
    case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME:
        *params = attachment_ref_present(ref) ? (GLint)ref->object : 0;
        break;
    case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL:
        if (!attachment_ref_present(ref)) {
            *params = 0;
            break;
        }
        if (ref->kind != GL_ATTACHMENT_KIND_TEXTURE) {
            _gl_set_error(GL_INVALID_OPERATION);
            return;
        }
        *params = 0;
        break;
    default:
        _gl_set_error(GL_INVALID_ENUM);
        break;
    }
}

static GLenum validate_attachment_for_status(const GLAttachmentRef *attachment,
                                             bool allow_color,
                                             bool allow_depth,
                                             bool allow_stencil,
                                             GLsizei *width,
                                             GLsizei *height,
                                             bool *found_attachment) {
    GLint internal_format;
    GLsizei attachment_width;
    GLsizei attachment_height;

    if (!attachment_ref_present(attachment)) {
        return GL_FRAMEBUFFER_COMPLETE;
    }

    internal_format = get_attachment_internal_format(attachment);
    if (internal_format == 0 ||
        !get_attachment_dimensions(attachment, &attachment_width,
                                   &attachment_height)) {
        return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    }
    if (attachment_width <= 0 || attachment_height <= 0) {
        return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    }

    if (allow_color && is_depth_internal_format(internal_format)) {
        return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    }
    if (allow_depth && !allow_stencil && !is_depth_internal_format(internal_format)) {
        return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    }
    if (!allow_depth && allow_stencil && !is_stencil_internal_format(internal_format)) {
        return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    }

    if (*width < 0) {
        *width = attachment_width;
        *height = attachment_height;
    } else if (*width != attachment_width || *height != attachment_height) {
        return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    }

    *found_attachment = true;
    return GL_FRAMEBUFFER_COMPLETE;
}

GLenum _gl_CheckFramebufferStatus(GLenum target) {
    GLuint fbo;
    GLFramebuffer *fb;
    GLsizei width = -1;
    GLsizei height = -1;
    bool found_attachment = false;

    if (!g_gl_context) {
        return GL_FRAMEBUFFER_UNSUPPORTED;
    }
    if (!is_framebuffer_target(target)) {
        _gl_set_error(GL_INVALID_ENUM);
        return 0;
    }

    fbo = get_bound_framebuffer_for_target(target);
    if (fbo == 0) {
        return GL_FRAMEBUFFER_COMPLETE;
    }

    fb = &g_framebuffers[fbo];

    for (uint32_t i = 0; i < 8; ++i) {
        GLenum status = validate_attachment_for_status(
            &fb->color_attachments[i], true, false, false, &width, &height,
            &found_attachment);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            return status;
        }
    }

    {
        GLenum status = validate_attachment_for_status(
            &fb->depth_attachment, false, true, false, &width, &height,
            &found_attachment);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            return status;
        }
    }

    {
        GLenum status = validate_attachment_for_status(
            &fb->stencil_attachment, false, false, true, &width, &height,
            &found_attachment);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            return status;
        }
    }

    if (!found_attachment) {
        return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
    }

    for (uint32_t i = 0; i < 8; ++i) {
        if (fb->draw_buffers[i] == GL_NONE) {
            continue;
        }
        if (fb->draw_buffers[i] != (GL_COLOR_ATTACHMENT0 + i) ||
            !attachment_ref_present(&fb->color_attachments[i])) {
            return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        }
    }

    if (fb->read_buffer != GL_NONE) {
        if (fb->read_buffer < GL_COLOR_ATTACHMENT0 ||
            fb->read_buffer > GL_COLOR_ATTACHMENT7) {
            return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        }
        if (!attachment_ref_present(
                &fb->color_attachments[fb->read_buffer - GL_COLOR_ATTACHMENT0])) {
            return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        }
    }

    return GL_FRAMEBUFFER_COMPLETE;
}

void _gl_FramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    if (!g_gl_context) return;
    if (!is_framebuffer_target(target) || textarget != GL_TEXTURE_2D) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (level != 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    GLuint fbo = get_bound_framebuffer_for_target(target);
    if (fbo == 0) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    if (texture != 0 && !gl_get_gx2_texture(texture)) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    
    if (attachment >= GL_COLOR_ATTACHMENT0 && attachment <= GL_COLOR_ATTACHMENT7) {
        GLAttachmentRef *ref =
            &g_framebuffers[fbo].color_attachments[attachment - GL_COLOR_ATTACHMENT0];
        if (texture == 0) {
            clear_attachment_ref(ref);
        } else {
            ref->kind = GL_ATTACHMENT_KIND_TEXTURE;
            ref->object = texture;
        }
    } else if (attachment == GL_DEPTH_ATTACHMENT) {
        if (texture == 0) {
            clear_attachment_ref(&g_framebuffers[fbo].depth_attachment);
        } else {
            g_framebuffers[fbo].depth_attachment.kind = GL_ATTACHMENT_KIND_TEXTURE;
            g_framebuffers[fbo].depth_attachment.object = texture;
        }
    } else if (attachment == GL_STENCIL_ATTACHMENT) {
        if (texture == 0) {
            clear_attachment_ref(&g_framebuffers[fbo].stencil_attachment);
        } else {
            g_framebuffers[fbo].stencil_attachment.kind = GL_ATTACHMENT_KIND_TEXTURE;
            g_framebuffers[fbo].stencil_attachment.object = texture;
        }
    } else if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        if (texture == 0) {
            clear_attachment_ref(&g_framebuffers[fbo].depth_attachment);
            clear_attachment_ref(&g_framebuffers[fbo].stencil_attachment);
        } else {
            g_framebuffers[fbo].depth_attachment.kind = GL_ATTACHMENT_KIND_TEXTURE;
            g_framebuffers[fbo].depth_attachment.object = texture;
            g_framebuffers[fbo].stencil_attachment.kind = GL_ATTACHMENT_KIND_TEXTURE;
            g_framebuffers[fbo].stencil_attachment.object = texture;
        }
    } else {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    
    g_framebuffers[fbo].dirty = true;
    g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
}

void _gl_FramebufferRenderbuffer(GLenum target, GLenum attachment,
                                 GLenum renderbuffertarget,
                                 GLuint renderbuffer) {
    GLuint fbo;

    if (!g_gl_context) {
        return;
    }
    if (!is_framebuffer_target(target) || renderbuffertarget != GL_RENDERBUFFER) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }

    fbo = get_bound_framebuffer_for_target(target);
    if (fbo == 0) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    if (renderbuffer != 0 && !get_renderbuffer(renderbuffer)) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }

    if (attachment >= GL_COLOR_ATTACHMENT0 && attachment <= GL_COLOR_ATTACHMENT7) {
        GLAttachmentRef *ref =
            &g_framebuffers[fbo].color_attachments[attachment - GL_COLOR_ATTACHMENT0];
        if (renderbuffer == 0) {
            clear_attachment_ref(ref);
        } else {
            ref->kind = GL_ATTACHMENT_KIND_RENDERBUFFER;
            ref->object = renderbuffer;
        }
    } else if (attachment == GL_DEPTH_ATTACHMENT) {
        if (renderbuffer == 0) {
            clear_attachment_ref(&g_framebuffers[fbo].depth_attachment);
        } else {
            g_framebuffers[fbo].depth_attachment.kind =
                GL_ATTACHMENT_KIND_RENDERBUFFER;
            g_framebuffers[fbo].depth_attachment.object = renderbuffer;
        }
    } else if (attachment == GL_STENCIL_ATTACHMENT) {
        if (renderbuffer == 0) {
            clear_attachment_ref(&g_framebuffers[fbo].stencil_attachment);
        } else {
            g_framebuffers[fbo].stencil_attachment.kind =
                GL_ATTACHMENT_KIND_RENDERBUFFER;
            g_framebuffers[fbo].stencil_attachment.object = renderbuffer;
        }
    } else if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        if (renderbuffer == 0) {
            clear_attachment_ref(&g_framebuffers[fbo].depth_attachment);
            clear_attachment_ref(&g_framebuffers[fbo].stencil_attachment);
        } else {
            g_framebuffers[fbo].depth_attachment.kind =
                GL_ATTACHMENT_KIND_RENDERBUFFER;
            g_framebuffers[fbo].depth_attachment.object = renderbuffer;
            g_framebuffers[fbo].stencil_attachment.kind =
                GL_ATTACHMENT_KIND_RENDERBUFFER;
            g_framebuffers[fbo].stencil_attachment.object = renderbuffer;
        }
    } else {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }

    g_framebuffers[fbo].dirty = true;
    g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
}

void _gl_RenderbufferStorage(GLenum target, GLenum internalformat,
                             GLsizei width, GLsizei height) {
    GLRenderbuffer *renderbuffer;
    GX2SurfaceFormat surface_format;
    GX2SurfaceUse surface_use;
    bool is_depth;
    GLuint renderbuffer_id;

    if (!g_gl_context) {
        return;
    }
    if (target != GL_RENDERBUFFER) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (width < 0 || height < 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (g_gl_context->bound_renderbuffer == 0) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    if (!get_renderbuffer_format_info(internalformat, &surface_format,
                                      &surface_use, &is_depth)) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }

    renderbuffer_id = g_gl_context->bound_renderbuffer;
    renderbuffer = &g_renderbuffers[renderbuffer_id];
    free_renderbuffer_storage(renderbuffer);

    renderbuffer->surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    renderbuffer->surface.width = (uint32_t)width;
    renderbuffer->surface.height = (uint32_t)height;
    renderbuffer->surface.depth = 1;
    renderbuffer->surface.mipLevels = 1;
    renderbuffer->surface.format = surface_format;
    renderbuffer->surface.aa = GX2_AA_MODE1X;
    renderbuffer->surface.use = surface_use;
    renderbuffer->surface.tileMode =
        is_depth ? GX2_TILE_MODE_DEFAULT : GX2_TILE_MODE_LINEAR_ALIGNED;
    GX2CalcSurfaceSizeAndAlignment(&renderbuffer->surface);
    renderbuffer->internal_format = internalformat;
    renderbuffer->width = width;
    renderbuffer->height = height;
    renderbuffer->is_depth = is_depth;
    if (width == 0 || height == 0 || renderbuffer->surface.imageSize == 0) {
        return;
    }
    renderbuffer->surface.image = gl_mem_alloc(
        GL_MEM_TYPE_MEM2, renderbuffer->surface.imageSize,
        renderbuffer->surface.alignment);
    if (!renderbuffer->surface.image) {
        memset(&renderbuffer->surface, 0, sizeof(renderbuffer->surface));
        _gl_set_error(GL_OUT_OF_MEMORY);
        return;
    }

    memset(renderbuffer->surface.image, 0, renderbuffer->surface.imageSize);

    if (is_depth) {
        init_depth_buffer_from_surface(&renderbuffer->depth_buffer,
                                       &renderbuffer->surface);
    } else {
        init_color_buffer_from_surface(&renderbuffer->color_buffer,
                                       &renderbuffer->surface);
    }

    for (int fbo = 1; fbo < MAX_FRAMEBUFFERS; ++fbo) {
        if (!g_framebuffers[fbo].in_use) {
            continue;
        }

        for (int color = 0; color < 8; ++color) {
            if (g_framebuffers[fbo].color_attachments[color].kind ==
                    GL_ATTACHMENT_KIND_RENDERBUFFER &&
                g_framebuffers[fbo].color_attachments[color].object ==
                    renderbuffer_id) {
                g_framebuffers[fbo].dirty = true;
            }
        }

        if (g_framebuffers[fbo].depth_attachment.kind ==
                GL_ATTACHMENT_KIND_RENDERBUFFER &&
            g_framebuffers[fbo].depth_attachment.object == renderbuffer_id) {
            g_framebuffers[fbo].dirty = true;
        }

        if (g_framebuffers[fbo].stencil_attachment.kind ==
                GL_ATTACHMENT_KIND_RENDERBUFFER &&
            g_framebuffers[fbo].stencil_attachment.object == renderbuffer_id) {
            g_framebuffers[fbo].dirty = true;
        }
    }

    g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
}

void _gl_DrawBuffer(GLenum buf) {
    _gl_DrawBuffers(1, &buf);
}

void _gl_DrawBuffers(GLsizei n, const GLenum *bufs) {
    GLFramebuffer *fb;
    GLuint fbo;
    GLenum new_draw_buffers[8];

    if (!g_gl_context) {
        return;
    }
    if (n < 0 || n > 8) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (n > 0 && !bufs) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }

    fbo = g_gl_context->bound_framebuffer;
    fb = &g_framebuffers[fbo];

    if (fbo == 0) {
        if (n != 1 || (bufs[0] != GL_BACK && bufs[0] != GL_NONE)) {
            _gl_set_error(GL_INVALID_OPERATION);
            return;
        }

        init_draw_buffer_defaults(fb, true);
        fb->draw_buffers[0] = bufs[0];
        g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
        return;
    }

    for (GLsizei i = 0; i < 8; ++i) {
        new_draw_buffers[i] = GL_NONE;
    }

    for (GLsizei i = 0; i < n; ++i) {
        GLenum draw_buffer = bufs[i];

        if (draw_buffer == GL_NONE) {
            continue;
        }
        if (draw_buffer < GL_COLOR_ATTACHMENT0 ||
            draw_buffer > GL_COLOR_ATTACHMENT7) {
            _gl_set_error(GL_INVALID_ENUM);
            return;
        }
        if (draw_buffer != (GL_COLOR_ATTACHMENT0 + i)) {
            // No MRT remap
            _gl_set_error(GL_INVALID_OPERATION);
            return;
        }

        new_draw_buffers[i] = draw_buffer;
    }

    for (GLsizei i = 0; i < 8; ++i) {
        fb->draw_buffers[i] = new_draw_buffers[i];
    }

    g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
}

void _gl_ReadBuffer(GLenum src) {
    GLuint fbo;
    GLFramebuffer *fb;

    if (!g_gl_context) {
        return;
    }

    fbo = g_gl_context->bound_read_framebuffer;
    fb = &g_framebuffers[fbo];

    if (fbo == 0) {
        if (src == GL_BACK || src == GL_NONE) {
            fb->read_buffer = src;
            return;
        }
        if (src >= GL_COLOR_ATTACHMENT0 && src <= GL_COLOR_ATTACHMENT7) {
            _gl_set_error(GL_INVALID_OPERATION);
            return;
        }
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }

    if (src == GL_NONE) {
        fb->read_buffer = src;
        return;
    }
    if (src >= GL_COLOR_ATTACHMENT0 && src <= GL_COLOR_ATTACHMENT7) {
        fb->read_buffer = src;
        return;
    }
    if (src == GL_BACK) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    _gl_set_error(GL_INVALID_ENUM);
}

GLboolean gl_read_color_pixels_rgba8(GLint x, GLint y, GLsizei width,
                                     GLsizei height, GLvoid *pixels) {
    GX2ColorBuffer *color_buffer;
    GX2Surface *source_surface;
    GLint internal_format;
    uint32_t src_texel_size;
    GLReadbackSurface readback_surface;

    if (!g_gl_context) {
        return GL_FALSE;
    }
    if (width < 0 || height < 0 || x < 0 || y < 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return GL_FALSE;
    }
    if (width == 0 || height == 0) {
        return GL_TRUE;
    }
    if (!pixels) {
        _gl_set_error(GL_INVALID_VALUE);
        return GL_FALSE;
    }

    gl_flush_state();
    GX2DrawDone();

    color_buffer = get_read_color_buffer();
    internal_format = get_read_color_internal_format();
    if (!color_buffer || internal_format == 0 || !color_buffer->surface.image) {
        _gl_set_error(GL_INVALID_OPERATION);
        return GL_FALSE;
    }

    source_surface = &color_buffer->surface;
    if ((uint32_t)x + (uint32_t)width > source_surface->width ||
        (uint32_t)y + (uint32_t)height > source_surface->height) {
        _gl_set_error(GL_INVALID_VALUE);
        return GL_FALSE;
    }

    src_texel_size = bytes_per_source_texel(internal_format);
    if (src_texel_size == 0) {
        _gl_set_error(GL_INVALID_OPERATION);
        return GL_FALSE;
    }

    if (!prepare_readback_surface(source_surface, false, x, y, width, height,
                                  &readback_surface)) {
        _gl_set_error(GL_OUT_OF_MEMORY);
        return GL_FALSE;
    }

    for (GLsizei row = 0; row < height; ++row) {
        uint8_t *dst_row = (uint8_t *)pixels + (uint32_t)row * (uint32_t)width * 4u;
        const uint8_t *src_row = (const uint8_t *)readback_surface.surface.image +
                                 ((readback_surface.base_y + (uint32_t)row) *
                                      readback_surface.surface.pitch +
                                  readback_surface.base_x) *
                                     src_texel_size;

        for (GLsizei col = 0; col < width; ++col) {
            const uint8_t *src_texel = src_row + (uint32_t)col * src_texel_size;
            uint8_t *dst_texel = dst_row + (uint32_t)col * 4u;
            float rgba[4];

            if (!decode_color_texel(src_texel, internal_format, rgba) ||
                !encode_color_texel(rgba, GL_RGBA, GL_UNSIGNED_BYTE,
                                    dst_texel)) {
                release_readback_surface(&readback_surface);
                _gl_set_error(GL_INVALID_OPERATION);
                return GL_FALSE;
            }
        }
    }

    release_readback_surface(&readback_surface);
    return GL_TRUE;
}

void _gl_ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                    GLenum format, GLenum type, GLvoid *pixels) {
    bool depth_read;
    GX2Surface *source_surface = NULL;
    GX2ColorBuffer *color_buffer;
    GX2DepthBuffer *depth_buffer;
    GLint internal_format;
    uint32_t src_texel_size;
    uint32_t dst_texel_size;
    uint32_t dst_row_bytes;
    uint32_t dst_image_bytes;
    GLReadbackSurface readback_surface;
    size_t dst_base_offset;

    if (!g_gl_context) {
        return;
    }
    if (width < 0 || height < 0 || x < 0 || y < 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (width == 0 || height == 0) {
        return;
    }
    if (!pixels) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }

    depth_read = (format == GL_DEPTH_COMPONENT || format == GL_DEPTH_STENCIL);
    if (depth_read) {
        if (!((format == GL_DEPTH_COMPONENT && type == GL_FLOAT) ||
              (format == GL_DEPTH_STENCIL && type == GL_UNSIGNED_INT_24_8))) {
            _gl_set_error(GL_INVALID_ENUM);
            return;
        }
    } else {
        bool valid_format = format == GL_RED || format == GL_RG ||
                            format == GL_RGB || format == GL_RGBA;
        bool valid_type = type == GL_UNSIGNED_BYTE || type == GL_FLOAT;
        if (!valid_format || !valid_type) {
            _gl_set_error(GL_INVALID_ENUM);
            return;
        }
    }

    gl_flush_state();
    GX2DrawDone();

    if (depth_read) {
        depth_buffer = get_read_depth_buffer();
        internal_format = get_read_depth_internal_format();
        if (!depth_buffer || internal_format == 0 || !depth_buffer->surface.image) {
            _gl_set_error(GL_INVALID_OPERATION);
            return;
        }
        source_surface = &depth_buffer->surface;
    } else {
        color_buffer = get_read_color_buffer();
        internal_format = get_read_color_internal_format();
        if (!color_buffer || internal_format == 0 || !color_buffer->surface.image) {
            _gl_set_error(GL_INVALID_OPERATION);
            return;
        }
        source_surface = &color_buffer->surface;
    }

    if ((uint32_t)x + (uint32_t)width > source_surface->width ||
        (uint32_t)y + (uint32_t)height > source_surface->height) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }

    src_texel_size = bytes_per_source_texel(internal_format);
    dst_texel_size = bytes_per_destination_pixel(format, type);
    if (src_texel_size == 0 || dst_texel_size == 0) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    if (!get_pack_layout(dst_texel_size, width, height, &dst_row_bytes,
                         &dst_image_bytes, &dst_base_offset)) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }

    if (!prepare_readback_surface(source_surface, depth_read, x, y, width, height,
                                  &readback_surface)) {
        _gl_set_error(GL_OUT_OF_MEMORY);
        return;
    }

    for (GLsizei row = 0; row < height; ++row) {
        uint8_t *dst_row = (uint8_t *)pixels + dst_base_offset +
                           (uint32_t)row * dst_row_bytes;
        const uint8_t *src_row = (const uint8_t *)readback_surface.surface.image +
                                 ((readback_surface.base_y + (uint32_t)row) *
                                      readback_surface.surface.pitch +
                                  readback_surface.base_x) *
                                     src_texel_size;

        for (GLsizei col = 0; col < width; ++col) {
            const uint8_t *src_texel = src_row + (uint32_t)col * src_texel_size;
            uint8_t *dst_texel = dst_row + (uint32_t)col * dst_texel_size;

            if (depth_read) {
                float depth_value;
                uint8_t stencil_value;
                if (!decode_depth_texel(src_texel, internal_format, &depth_value,
                                        &stencil_value) ||
                    !encode_depth_texel(depth_value, stencil_value, format, type,
                                        dst_texel)) {
                    release_readback_surface(&readback_surface);
                    _gl_set_error(GL_INVALID_OPERATION);
                    return;
                }
            } else {
                float rgba[4];
                if (!decode_color_texel(src_texel, internal_format, rgba) ||
                    !encode_color_texel(rgba, format, type, dst_texel)) {
                    release_readback_surface(&readback_surface);
                    _gl_set_error(GL_INVALID_OPERATION);
                    return;
                }
            }
        }
    }

    release_readback_surface(&readback_surface);
}

void gl_bind_framebuffers(void) {
    if (!g_gl_context ||
        !(g_gl_context->dirty_flags &
          (GL_DIRTY_FRAMEBUFFER | GL_DIRTY_COLOR_MASK | GL_DIRTY_BLEND))) {
        return;
    }
    
    GLuint fbo = g_gl_context->bound_framebuffer;
    
    if (fbo == 0) {
        GX2ColorBuffer *tv_color = WHBGfxGetTVColourBuffer();
        GX2DepthBuffer *tv_depth = WHBGfxGetTVDepthBuffer();
        if (tv_color) {
            GX2SetColorBuffer(tv_color, GX2_RENDER_TARGET_0);
        }
        if (tv_depth) {
            GX2SetDepthBuffer(tv_depth);
        }
        apply_framebuffer_output_state(&g_framebuffers[0], true);
        return;
    }
    
    GLFramebuffer *fb = &g_framebuffers[fbo];

    ensure_framebuffer_cache(fb);
    
    for (int i = 0; i < 8; ++i) {
        if (attachment_ref_present(&fb->color_attachments[i])) {
            GX2SetColorBuffer(&fb->cb[i], (GX2RenderTarget)i);
        }
    }
    if (attachment_ref_present(&fb->depth_attachment) ||
        attachment_ref_present(&fb->stencil_attachment)) {
        GX2SetDepthBuffer(&fb->db);
    }
    apply_framebuffer_output_state(fb, false);
}

GX2ColorBuffer *gl_get_draw_color_buffer(GLuint index) {
    GLuint fbo;
    GLFramebuffer *fb;

    if (!g_gl_context || index >= 8) {
        return NULL;
    }

    fbo = g_gl_context->bound_framebuffer;
    if (fbo == 0) {
        if (index != 0 || g_framebuffers[0].draw_buffers[0] != GL_BACK) {
            return NULL;
        }
        return WHBGfxGetTVColourBuffer();
    }

    fb = &g_framebuffers[fbo];
    if (!attachment_ref_present(&fb->color_attachments[index])) {
        return NULL;
    }
    ensure_framebuffer_cache(fb);

    return &fb->cb[index];
}

GX2DepthBuffer *gl_get_draw_depth_buffer(void) {
    GLuint fbo;
    GLFramebuffer *fb;

    if (!g_gl_context) {
        return NULL;
    }

    fbo = g_gl_context->bound_framebuffer;
    if (fbo == 0) {
        return WHBGfxGetTVDepthBuffer();
    }

    fb = &g_framebuffers[fbo];
    if (!attachment_ref_present(&fb->depth_attachment) &&
        !attachment_ref_present(&fb->stencil_attachment)) {
        return NULL;
    }
    ensure_framebuffer_cache(fb);

    return &fb->db;
}

GLboolean gl_is_draw_color_buffer_enabled(GLuint index) {
    GLuint fbo;
    GLFramebuffer *fb;

    if (!g_gl_context || index >= 8) {
        return GL_FALSE;
    }

    fbo = g_gl_context->bound_framebuffer;
    if (fbo == 0) {
        return (index == 0 && g_framebuffers[0].draw_buffers[0] == GL_BACK)
                   ? GL_TRUE
                   : GL_FALSE;
    }

    fb = &g_framebuffers[fbo];
    return (fb->draw_buffers[index] == (GL_COLOR_ATTACHMENT0 + index) &&
            attachment_ref_present(&fb->color_attachments[index]))
               ? GL_TRUE
               : GL_FALSE;
}

void gl_framebuffer_mark_texture_dirty(GLuint texture) {
    if (texture == 0) {
        return;
    }

    for (int fbo = 1; fbo < MAX_FRAMEBUFFERS; ++fbo) {
        if (!g_framebuffers[fbo].in_use) {
            continue;
        }

        for (int color = 0; color < 8; ++color) {
            if (g_framebuffers[fbo].color_attachments[color].kind ==
                    GL_ATTACHMENT_KIND_TEXTURE &&
                g_framebuffers[fbo].color_attachments[color].object ==
                    texture) {
                g_framebuffers[fbo].dirty = true;
            }
        }

        if (g_framebuffers[fbo].depth_attachment.kind ==
                GL_ATTACHMENT_KIND_TEXTURE &&
            g_framebuffers[fbo].depth_attachment.object == texture) {
            g_framebuffers[fbo].dirty = true;
        }

        if (g_framebuffers[fbo].stencil_attachment.kind ==
                GL_ATTACHMENT_KIND_TEXTURE &&
            g_framebuffers[fbo].stencil_attachment.object == texture) {
            g_framebuffers[fbo].dirty = true;
        }
    }
}

#ifdef __cplusplus
}
#endif
