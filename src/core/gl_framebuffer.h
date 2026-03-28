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
void _gl_DrawBuffers(GLsizei n, const GLenum *bufs);

void gl_bind_framebuffers(void);

#ifdef __cplusplus
}
#endif

#endif /* GL33_FRAMEBUFFER_H */
