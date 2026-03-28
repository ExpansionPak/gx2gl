#ifndef GL33_DRAW_H
#define GL33_DRAW_H

#include "core/gl_context.h"

#ifdef __cplusplus
extern "C" {
#endif

void _gl_DrawArrays(GLenum mode, GLint first, GLsizei count);
void _gl_DrawArraysInstanced(GLenum mode, GLint first, GLsizei count,
                             GLsizei instancecount);
void _gl_DrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
void _gl_DrawElementsInstanced(GLenum mode, GLsizei count, GLenum type,
                               const GLvoid *indices, GLsizei instancecount);

#ifdef __cplusplus
}
#endif

#endif /* GL33_DRAW_H */
