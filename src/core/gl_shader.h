#ifndef GL33_SHADER_H
#define GL33_SHADER_H

#include "core/gl_context.h"
#include <gx2/shaders.h>
#include <gx2/state.h>

#ifdef __cplusplus
extern "C" {
#endif

void gl_shader_init(void);

GLuint _gl_CreateShader(GLenum type);
void _gl_ShaderSource(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length);
void _gl_CompileShader(GLuint shader);
GLuint _gl_CreateProgram(void);
void _gl_AttachShader(GLuint program, GLuint shader);
void _gl_LinkProgram(GLuint program);
void _gl_UseProgram(GLuint program);
void _gl_Uniform1f(GLint location, GLfloat v0);
void _gl_Uniform1fv(GLint location, GLsizei count, const GLfloat *value);
void _gl_Uniform1i(GLint location, GLint v0);
void _gl_Uniform2f(GLint location, GLfloat v0, GLfloat v1);
void _gl_Uniform2fv(GLint location, GLsizei count, const GLfloat *value);
void _gl_Uniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
void _gl_Uniform3fv(GLint location, GLsizei count, const GLfloat *value);
void _gl_Uniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2,
                   GLfloat v3);
void _gl_Uniform4fv(GLint location, GLsizei count, const GLfloat *value);
void _gl_UniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
GLint _gl_GetUniformLocation(GLuint program, const GLchar *name);
GLuint _gl_GetUniformBlockIndex(GLuint program, const GLchar *uniformBlockName);
void _gl_UniformBlockBinding(GLuint program, GLuint uniformBlockIndex,
                             GLuint uniformBlockBinding);
void _gl_WiiULoadShaderGroup(GLuint program, const void* shaderGroup);
void _gl_WiiULoadShaderGroupGFD(GLuint program, GLuint index,
                                const void *gfdData);

void gl_bind_shaders(void);

#ifdef __cplusplus
}
#endif

#endif /* GL33_SHADER_H */
