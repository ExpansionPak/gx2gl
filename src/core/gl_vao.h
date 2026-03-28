#ifndef GL33_VAO_H
#define GL33_VAO_H

#include "core/gl_context.h"
#include <gx2/shaders.h>

#ifdef __cplusplus
extern "C" {
#endif

void gl_vao_init(void);

void _gl_GenVertexArrays(GLsizei n, GLuint *arrays);
void _gl_DeleteVertexArrays(GLsizei n, const GLuint *arrays);
void _gl_BindVertexArray(GLuint array);
void _gl_EnableVertexAttribArray(GLuint index);
void _gl_DisableVertexAttribArray(GLuint index);
void _gl_VertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid *pointer);
void _gl_VertexAttribDivisor(GLuint index, GLuint divisor);

void gl_vao_set_element_array_buffer(GLuint buffer);
GLuint gl_vao_get_element_array_buffer(void);

void gl_bind_vao(void);

#ifdef __cplusplus
}
#endif

#endif /* GL33_VAO_H */
