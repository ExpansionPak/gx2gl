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
GLboolean _gl_IsFramebuffer(GLuint framebuffer);
void _gl_GenRenderbuffers(GLsizei n, GLuint *renderbuffers);
void _gl_DeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers);
GLboolean _gl_IsRenderbuffer(GLuint renderbuffer);
void _gl_BindFramebuffer(GLenum target, GLuint framebuffer);
void _gl_BindRenderbuffer(GLenum target, GLuint renderbuffer);
GLenum _gl_CheckFramebufferStatus(GLenum target);
void _gl_FramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
void _gl_FramebufferRenderbuffer(GLenum target, GLenum attachment,
                                 GLenum renderbuffertarget,
                                 GLuint renderbuffer);
void _gl_RenderbufferStorage(GLenum target, GLenum internalformat,
                             GLsizei width, GLsizei height);
void _gl_GetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params);
void _gl_GetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment,
                                             GLenum pname, GLint *params);
void _gl_DrawBuffer(GLenum buf);
void _gl_DrawBuffers(GLsizei n, const GLenum *bufs);
void _gl_ReadBuffer(GLenum src);
void _gl_ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                    GLenum format, GLenum type, GLvoid *pixels);

void gl_bind_framebuffers(void);
GLboolean gl_read_color_pixels_rgba8(GLint x, GLint y, GLsizei width,
                                     GLsizei height, GLvoid *pixels);
GX2ColorBuffer *gl_get_draw_color_buffer(GLuint index);
GX2DepthBuffer *gl_get_draw_depth_buffer(void);
GLboolean gl_is_draw_color_buffer_enabled(GLuint index);
void gl_framebuffer_mark_texture_dirty(GLuint texture);

#ifdef __cplusplus
}
#endif

#endif // Framebuffer header guard
