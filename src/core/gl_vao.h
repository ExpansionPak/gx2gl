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
GLboolean _gl_IsVertexArray(GLuint array);
void _gl_BindVertexArray(GLuint array);
void _gl_EnableVertexAttribArray(GLuint index);
void _gl_DisableVertexAttribArray(GLuint index);
void _gl_GetVertexAttribfv(GLuint index, GLenum pname, GLfloat *params);
void _gl_GetVertexAttribiv(GLuint index, GLenum pname, GLint *params);
void _gl_GetVertexAttribPointerv(GLuint index, GLenum pname, GLvoid **pointer);
void _gl_VertexAttrib1f(GLuint index, GLfloat x);
void _gl_VertexAttrib1fv(GLuint index, const GLfloat *v);
void _gl_VertexAttrib2f(GLuint index, GLfloat x, GLfloat y);
void _gl_VertexAttrib2fv(GLuint index, const GLfloat *v);
void _gl_VertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z);
void _gl_VertexAttrib3fv(GLuint index, const GLfloat *v);
void _gl_VertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z,
                        GLfloat w);
void _gl_VertexAttrib4fv(GLuint index, const GLfloat *v);
void _gl_VertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid *pointer);
void _gl_VertexAttribDivisor(GLuint index, GLuint divisor);

void gl_vao_set_element_array_buffer(GLuint buffer);
GLuint gl_vao_get_element_array_buffer(void);

void gl_bind_vao(void);

#ifdef __cplusplus
}
#endif

#endif // VAO header guard
