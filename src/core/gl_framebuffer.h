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
void _gl_FramebufferTexture(GLenum target, GLenum attachment, GLuint texture, GLint level);
void _gl_FramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
void _gl_FramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
void _gl_RenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
void _gl_RenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height);
void _gl_GetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params);
void _gl_GetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname, GLint *params);
void _gl_BlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);
void _gl_DrawBuffer(GLenum buf);
void _gl_DrawBuffers(GLsizei n, const GLenum *bufs);
void _gl_ReadBuffer(GLenum src);
void _gl_ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels);

void _gl_GenQueries(GLsizei n, GLuint *ids);
void _gl_DeleteQueries(GLsizei n, const GLuint *ids);
GLboolean _gl_IsQuery(GLuint id);
void _gl_BeginQuery(GLenum target, GLuint id);
void _gl_EndQuery(GLenum target);
void _gl_GetQueryiv(GLenum target, GLenum pname, GLint *params);
void _gl_GetQueryObjectiv(GLuint id, GLenum pname, GLint *params);
void _gl_GetQueryObjectuiv(GLuint id, GLenum pname, GLuint *params);
void _gl_BeginConditionalRender(GLuint id, GLenum mode);
void _gl_EndConditionalRender(void);
void _gl_BeginQueryIndexed(GLenum target, GLuint index, GLuint id);
void _gl_EndQueryIndexed(GLenum target, GLuint index);
void _gl_GetQueryIndexediv(GLenum target, GLuint index, GLenum pname, GLint *params);
void _gl_GenTransformFeedbacks(GLsizei n, GLuint *ids);
void _gl_DeleteTransformFeedbacks(GLsizei n, const GLuint *ids);

void gl_bind_framebuffers(void);
GLboolean gl_is_draw_color_buffer_enabled(GLuint index);
GX2ColorBuffer *gl_get_draw_color_buffer(GLuint index);
GX2DepthBuffer *gl_get_draw_depth_buffer(void);
void gl_framebuffer_mark_texture_dirty(GLuint texture);
GLboolean gl_read_color_pixels_rgba8(GLint x, GLint y, GLsizei width, GLsizei height, void *pixels);

#ifdef __cplusplus
}
#endif

#endif
