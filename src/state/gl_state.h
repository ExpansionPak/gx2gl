#ifndef GL33_STATE_H
#define GL33_STATE_H

#include "core/gl_context.h"

#ifdef __cplusplus
extern "C" {
#endif



// Internal function to apply all dirty state to GX2
void gl_flush_state(void);

// These will be assigned to dispatch table
void _gl_BlendFunc(GLenum sfactor, GLenum dfactor);
void _gl_BlendEquation(GLenum mode);
void _gl_BlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB,
                           GLenum sfactorAlpha, GLenum dfactorAlpha);
void _gl_DepthFunc(GLenum func);
void _gl_DepthMask(GLboolean flag);
void _gl_StencilFunc(GLenum func, GLint ref, GLuint mask);
void _gl_StencilOp(GLenum fail, GLenum zfail, GLenum zpass);
void _gl_CullFace(GLenum mode);
void _gl_FrontFace(GLenum mode);
void _gl_PolygonMode(GLenum face, GLenum mode);
void _gl_Viewport(GLint x, GLint y, GLsizei width, GLsizei height);
void _gl_Scissor(GLint x, GLint y, GLsizei width, GLsizei height);
void _gl_ColorMask(GLboolean red, GLboolean green, GLboolean blue,
                   GLboolean alpha);
void _gl_LineWidth(GLfloat width);
void _gl_Enable(GLenum cap);
void _gl_Disable(GLenum cap);

#ifdef __cplusplus
}
#endif

#endif // GL33_STATE_H
