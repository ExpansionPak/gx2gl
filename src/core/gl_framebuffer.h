#ifndef GL33_FRAMEBUFFER_H
#define GL33_FRAMEBUFFER_H

#include "core/gl_context.h"
#include <gx2/surface.h>
#include <gx2/state.h>

#ifdef __cplusplus
extern "C" {
#endif

void gl_framebuffer_init(void);

void _gl_GenFramebuffers(GLsizei n, GLuint *framebuffers);
void _gl_DeleteFramebuffers(GLsizei n, const GLuint *framebuffers);
void _gl_BindFramebuffer(GLenum target, GLuint framebuffer);
void _gl_FramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
void _gl_DrawBuffer(GLenum buf);
void _gl_DrawBuffers(GLsizei n, const GLenum *bufs);
void _gl_ReadBuffer(GLenum src);
void _gl_ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                    GLenum format, GLenum type, GLvoid *pixels);

void gl_bind_framebuffers(void);
GX2ColorBuffer *gl_get_draw_color_buffer(GLuint index);
GX2DepthBuffer *gl_get_draw_depth_buffer(void);
GLboolean gl_is_draw_color_buffer_enabled(GLuint index);

#ifdef __cplusplus
}
#endif

#endif /* GL33_FRAMEBUFFER_H */
