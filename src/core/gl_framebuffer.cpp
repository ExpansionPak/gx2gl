#include "gl_framebuffer.h"
#include "gl_texture.h"
#include "mem/gl_mem.h"
#include "state/gl_state.h"
#ifdef __cplusplus
extern "C" {
#endif
#include <gx2/surface.h>
#include <gx2/state.h>
#include <gx2/display.h>
#include <gx2/registers.h>
#include <gx2/mem.h>
#include <whb/gfx.h>
#ifdef __cplusplus
}
#endif
#include <string.h>

#define MAX_FRAMEBUFFERS 128

typedef struct {
    bool in_use;
    GLuint color_attachments[8];
    GLenum draw_buffers[8];
    GLuint depth_attachment;
    GLuint stencil_attachment;
    
    // Cached GX2 objects
    GX2ColorBuffer cb[8];
    GX2DepthBuffer db;
    bool dirty;
} GLFramebuffer;

static GLFramebuffer g_framebuffers[MAX_FRAMEBUFFERS];

static void init_draw_buffer_defaults(GLFramebuffer *fb, bool is_default) {
    if (!fb) {
        return;
    }

    for (uint32_t i = 0; i < 8; ++i) {
        fb->draw_buffers[i] = GL_NONE;
    }
    fb->draw_buffers[0] = is_default ? GL_BACK : GL_COLOR_ATTACHMENT0;
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
                   fb->color_attachments[i] != 0) {
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

static void init_color_buffer_from_texture(GX2ColorBuffer *color_buffer,
                                           const GX2Texture *texture) {
    memset(color_buffer, 0, sizeof(*color_buffer));
    color_buffer->surface = texture->surface;
    color_buffer->viewMip = 0;
    color_buffer->viewFirstSlice = 0;
    color_buffer->viewNumSlices = 1;
    color_buffer->aaBuffer = NULL;
    color_buffer->aaSize = 0;
    GX2InitColorBufferRegs(color_buffer);
}

static void init_depth_buffer_from_texture(GX2DepthBuffer *depth_buffer,
                                           const GX2Texture *texture) {
    memset(depth_buffer, 0, sizeof(*depth_buffer));
    depth_buffer->surface = texture->surface;
    depth_buffer->viewMip = 0;
    depth_buffer->viewFirstSlice = 0;
    depth_buffer->viewNumSlices = 1;
    depth_buffer->hiZPtr = NULL;
    depth_buffer->hiZSize = 0;
    depth_buffer->depthClear = 1.0f;
    depth_buffer->stencilClear = 0;
    GX2InitDepthBufferRegs(depth_buffer);
}

void gl_framebuffer_init(void) {
    memset(g_framebuffers, 0, sizeof(g_framebuffers));
    /* Framebuffer 0 is Default FB */
    g_framebuffers[0].in_use = true;
    init_draw_buffer_defaults(&g_framebuffers[0], true);
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
            g_framebuffers[i].depth_attachment = 0;
            g_framebuffers[i].stencil_attachment = 0;
            framebuffers[count++] = i;
        }
    }
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
        g_framebuffers[fbo].color_attachments[attachment - GL_COLOR_ATTACHMENT0] = texture;
    } else if (attachment == GL_DEPTH_ATTACHMENT) {
        g_framebuffers[fbo].depth_attachment = texture;
    } else if (attachment == GL_STENCIL_ATTACHMENT) {
        g_framebuffers[fbo].stencil_attachment = texture;
    } else if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        g_framebuffers[fbo].depth_attachment = texture;
        g_framebuffers[fbo].stencil_attachment = texture;
    } else {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    
    g_framebuffers[fbo].dirty = true;
    g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
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
            /* GX2 exposes fixed render target slots, so arbitrary remapping of
             * fragment output location i onto attachment j is not expressed
             * through WUT's public state API. */
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
    
    if (fb->dirty) {
        for (int i = 0; i < 8; i++) {
            if (fb->color_attachments[i]) {
                GX2Texture *tex = gl_get_gx2_texture(fb->color_attachments[i]);
                if (tex) {
                    init_color_buffer_from_texture(&fb->cb[i], tex);
                }
            } else {
                memset(&fb->cb[i], 0, sizeof(fb->cb[i]));
            }
        }
        
        if (fb->depth_attachment) {
            GX2Texture *tex = gl_get_gx2_texture(fb->depth_attachment);
            if (tex) {
                init_depth_buffer_from_texture(&fb->db, tex);
            }
        } else {
            memset(&fb->db, 0, sizeof(fb->db));
        }
        fb->dirty = false;
    }
    
    for (int i = 0; i < 8; ++i) {
        if (fb->color_attachments[i]) {
            GX2SetColorBuffer(&fb->cb[i], (GX2RenderTarget)i);
        }
    }
    if (fb->depth_attachment) {
        GX2SetDepthBuffer(&fb->db);
    }
    apply_framebuffer_output_state(fb, false);
}

#ifdef __cplusplus
}
#endif
