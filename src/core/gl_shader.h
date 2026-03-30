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
void _gl_DeleteShader(GLuint shader);
GLboolean _gl_IsShader(GLuint shader);
void _gl_ShaderSource(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length);
void _gl_GetShaderSource(GLuint shader, GLsizei bufSize, GLsizei *length,
                         GLchar *source);
void _gl_CompileShader(GLuint shader);
GLuint _gl_CreateProgram(void);
void _gl_DeleteProgram(GLuint program);
GLboolean _gl_IsProgram(GLuint program);
void _gl_AttachShader(GLuint program, GLuint shader);
void _gl_DetachShader(GLuint program, GLuint shader);
void _gl_BindAttribLocation(GLuint program, GLuint index, const GLchar *name);
void _gl_GetAttachedShaders(GLuint program, GLsizei maxCount, GLsizei *count,
                            GLuint *shaders);
void _gl_GetActiveAttrib(GLuint program, GLuint index, GLsizei bufSize,
                         GLsizei *length, GLint *size, GLenum *type,
                         GLchar *name);
void _gl_GetActiveUniform(GLuint program, GLuint index, GLsizei bufSize,
                          GLsizei *length, GLint *size, GLenum *type,
                          GLchar *name);
void _gl_LinkProgram(GLuint program);
void _gl_ValidateProgram(GLuint program);
void _gl_UseProgram(GLuint program);
void _gl_GetShaderiv(GLuint shader, GLenum pname, GLint *params);
void _gl_GetProgramiv(GLuint program, GLenum pname, GLint *params);
void _gl_GetShaderInfoLog(GLuint shader, GLsizei maxLength, GLsizei *length,
                          GLchar *infoLog);
void _gl_GetProgramInfoLog(GLuint program, GLsizei maxLength, GLsizei *length,
                           GLchar *infoLog);
void _gl_GetUniformfv(GLuint program, GLint location, GLfloat *params);
void _gl_GetUniformiv(GLuint program, GLint location, GLint *params);
void _gl_ReleaseShaderCompiler(void);
void _gl_ShaderBinary(GLsizei count, const GLuint *shaders, GLenum binaryFormat,
                      const GLvoid *binary, GLsizei length);
void _gl_GetShaderPrecisionFormat(GLenum shadertype, GLenum precisiontype,
                                  GLint *range, GLint *precision);
void _gl_Uniform1f(GLint location, GLfloat v0);
void _gl_Uniform1fv(GLint location, GLsizei count, const GLfloat *value);
void _gl_Uniform1i(GLint location, GLint v0);
void _gl_Uniform1iv(GLint location, GLsizei count, const GLint *value);
void _gl_Uniform2f(GLint location, GLfloat v0, GLfloat v1);
void _gl_Uniform2fv(GLint location, GLsizei count, const GLfloat *value);
void _gl_Uniform2i(GLint location, GLint v0, GLint v1);
void _gl_Uniform2iv(GLint location, GLsizei count, const GLint *value);
void _gl_Uniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
void _gl_Uniform3fv(GLint location, GLsizei count, const GLfloat *value);
void _gl_Uniform3i(GLint location, GLint v0, GLint v1, GLint v2);
void _gl_Uniform3iv(GLint location, GLsizei count, const GLint *value);
void _gl_Uniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2,
                   GLfloat v3);
void _gl_Uniform4fv(GLint location, GLsizei count, const GLfloat *value);
void _gl_Uniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3);
void _gl_Uniform4iv(GLint location, GLsizei count, const GLint *value);
void _gl_UniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose,
                          const GLfloat *value);
void _gl_UniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose,
                          const GLfloat *value);
void _gl_UniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
GLint _gl_GetUniformLocation(GLuint program, const GLchar *name);
GLint _gl_GetAttribLocation(GLuint program, const GLchar *name);
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

#endif // Shader header guard
