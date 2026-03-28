#include "gl_context.h"
#include "mem/gl_mem.h"
#include "state/gl_state.h"
#include "gl_buffer.h"
#include "gl_texture.h"
#include "gl_shader.h"
#include "gl_vao.h"
#include "gl_framebuffer.h"
#include "gl_draw.h"
#include <coreinit/memexpheap.h>
#include <coreinit/mutex.h>
#include <gx2/event.h>
#include <gx2/draw.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

gl_context_t *g_gl_context = NULL;

// Stub functions for dispatch table
static void _gl_stub_void() {}
static void _gl_stub_gen(GLsizei n, GLuint *ids) {
  (void)n;
  (void)ids;
}
static void _gl_stub_delete(GLsizei n, const GLuint *ids) {
  (void)n;
  (void)ids;
}
static void _gl_stub_bind(GLenum target, GLuint id) {
  (void)target;
  (void)id;
}
static void _gl_stub_data(GLenum t, GLsizeiptr s, const GLvoid *d, GLenum u) {
  (void)t;
  (void)s;
  (void)d;
  (void)u;
}
static void _gl_stub_subdata(GLenum t, GLintptr o, GLsizeiptr s,
                             const GLvoid *d) {
  (void)t;
  (void)o;
  (void)s;
  (void)d;
}
static void _gl_stub_draw_arrays(GLenum m, GLint f, GLsizei c) {
  (void)m;
  (void)f;
  (void)c;
}
static void _gl_stub_draw_elements(GLenum m, GLsizei c, GLenum t,
                                   const GLvoid *i) {
  (void)m;
  (void)c;
  (void)t;
  (void)i;
}

void _gl_set_error(GLenum error) {
  if (!g_gl_context)
    return;

  OSLockMutex(&g_gl_context->error_mutex);
  uint32_t next = (g_gl_context->error_head + 1) % GL_ERROR_QUEUE_SIZE;
  if (next != g_gl_context->error_tail) {
    g_gl_context->error_queue[g_gl_context->error_head] = error;
    g_gl_context->error_head = next;
  }
  OSUnlockMutex(&g_gl_context->error_mutex);
}

GLenum glGetError(void) {
  if (!g_gl_context)
    return GL_NO_ERROR;

  OSLockMutex(&g_gl_context->error_mutex);
  if (g_gl_context->error_head == g_gl_context->error_tail) {
    OSUnlockMutex(&g_gl_context->error_mutex);
    return GL_NO_ERROR;
  }

  GLenum error = g_gl_context->error_queue[g_gl_context->error_tail];
  g_gl_context->error_tail =
      (g_gl_context->error_tail + 1) % GL_ERROR_QUEUE_SIZE;
  OSUnlockMutex(&g_gl_context->error_mutex);

  return error;
}

gl_context_t *gl_context_create(void) {
  // Allocate context from MEM2
  gl_context_t *ctx =
      (gl_context_t *)gl_mem_alloc(GL_MEM_TYPE_MEM2, sizeof(gl_context_t), 4);
  if (!ctx)
    return NULL;

  memset(ctx, 0, sizeof(gl_context_t));
  OSInitMutex(&ctx->error_mutex);

  gl_buffer_init();
  gl_texture_init();
  gl_shader_init();
  gl_vao_init();
  gl_framebuffer_init();

  // Populate dispatch table with stubs
  ctx->dispatch.glGenBuffers = _gl_GenBuffers;
  ctx->dispatch.glDeleteBuffers = _gl_DeleteBuffers;
  ctx->dispatch.glBindBuffer = _gl_BindBuffer;
  ctx->dispatch.glBindBufferBase = _gl_BindBufferBase;
  ctx->dispatch.glBindBufferRange = _gl_BindBufferRange;
  ctx->dispatch.glBufferData = _gl_BufferData;
  ctx->dispatch.glBufferSubData = _gl_BufferSubData;
  ctx->dispatch.glMapBuffer = _gl_MapBuffer;
  ctx->dispatch.glMapBufferRange = _gl_MapBufferRange;
  ctx->dispatch.glUnmapBuffer = _gl_UnmapBuffer;

  ctx->dispatch.glGenTextures = _gl_GenTextures;
  ctx->dispatch.glDeleteTextures = _gl_DeleteTextures;
  ctx->dispatch.glBindTexture = _gl_BindTexture;
  ctx->dispatch.glActiveTexture = _gl_ActiveTexture;
  ctx->dispatch.glTexImage2D = _gl_TexImage2D;
  ctx->dispatch.glTexImage3D = _gl_TexImage3D;
  ctx->dispatch.glTexSubImage2D = _gl_TexSubImage2D;
  ctx->dispatch.glTexSubImage3D = _gl_TexSubImage3D;
  ctx->dispatch.glTexParameteri = _gl_TexParameteri;
  ctx->dispatch.glGenerateMipmap = _gl_GenerateMipmap;
  
  ctx->dispatch.glCreateShader = _gl_CreateShader;
  ctx->dispatch.glShaderSource = _gl_ShaderSource;
  ctx->dispatch.glCompileShader = _gl_CompileShader;
  ctx->dispatch.glCreateProgram = _gl_CreateProgram;
  ctx->dispatch.glAttachShader = _gl_AttachShader;
  ctx->dispatch.glLinkProgram = _gl_LinkProgram;
  ctx->dispatch.glUseProgram = _gl_UseProgram;
  ctx->dispatch.glUniform1f = _gl_Uniform1f;
  ctx->dispatch.glUniform1fv = _gl_Uniform1fv;
  ctx->dispatch.glUniform1i = _gl_Uniform1i;
  ctx->dispatch.glUniform2f = _gl_Uniform2f;
  ctx->dispatch.glUniform2fv = _gl_Uniform2fv;
  ctx->dispatch.glUniform3f = _gl_Uniform3f;
  ctx->dispatch.glUniform3fv = _gl_Uniform3fv;
  ctx->dispatch.glUniform4f = _gl_Uniform4f;
  ctx->dispatch.glUniform4fv = _gl_Uniform4fv;
  ctx->dispatch.glUniformMatrix4fv = _gl_UniformMatrix4fv;
  ctx->dispatch.glGetUniformLocation = _gl_GetUniformLocation;
  ctx->dispatch.glGetUniformBlockIndex = _gl_GetUniformBlockIndex;
  ctx->dispatch.glUniformBlockBinding = _gl_UniformBlockBinding;
  ctx->dispatch.glWiiULoadShaderGroup = _gl_WiiULoadShaderGroup;
  ctx->dispatch.glWiiULoadShaderGroupGFD = _gl_WiiULoadShaderGroupGFD;
  
  ctx->dispatch.glGenVertexArrays = _gl_GenVertexArrays;
  ctx->dispatch.glDeleteVertexArrays = _gl_DeleteVertexArrays;
  ctx->dispatch.glBindVertexArray = _gl_BindVertexArray;
  ctx->dispatch.glEnableVertexAttribArray = _gl_EnableVertexAttribArray;
  ctx->dispatch.glDisableVertexAttribArray = _gl_DisableVertexAttribArray;
  ctx->dispatch.glVertexAttribPointer = _gl_VertexAttribPointer;
  ctx->dispatch.glVertexAttribDivisor = _gl_VertexAttribDivisor;

  ctx->dispatch.glGenFramebuffers = _gl_GenFramebuffers;
  ctx->dispatch.glDeleteFramebuffers = _gl_DeleteFramebuffers;
  ctx->dispatch.glBindFramebuffer = _gl_BindFramebuffer;
  ctx->dispatch.glFramebufferTexture2D = _gl_FramebufferTexture2D;
  ctx->dispatch.glDrawBuffers = _gl_DrawBuffers;

  ctx->dispatch.glDrawArrays = _gl_DrawArrays;
  ctx->dispatch.glDrawArraysInstanced = _gl_DrawArraysInstanced;
  ctx->dispatch.glDrawElements = _gl_DrawElements;
  ctx->dispatch.glDrawElementsInstanced = _gl_DrawElementsInstanced;

  // State Pointers
  ctx->dispatch.glEnable = _gl_Enable;
  ctx->dispatch.glDisable = _gl_Disable;
  ctx->dispatch.glBlendFunc = _gl_BlendFunc;
  ctx->dispatch.glBlendEquation = _gl_BlendEquation;
  ctx->dispatch.glBlendFuncSeparate = _gl_BlendFuncSeparate;
  ctx->dispatch.glDepthFunc = _gl_DepthFunc;
  ctx->dispatch.glDepthMask = _gl_DepthMask;
  ctx->dispatch.glStencilFunc = _gl_StencilFunc;
  ctx->dispatch.glStencilOp = _gl_StencilOp;
  ctx->dispatch.glCullFace = _gl_CullFace;
  ctx->dispatch.glFrontFace = _gl_FrontFace;
  ctx->dispatch.glPolygonMode = _gl_PolygonMode;
  ctx->dispatch.glViewport = _gl_Viewport;
  ctx->dispatch.glScissor = _gl_Scissor;
  ctx->dispatch.glColorMask = _gl_ColorMask;
  ctx->dispatch.glLineWidth = _gl_LineWidth;
  
  ctx->dispatch.glGetString = _gl_GetString;
  ctx->dispatch.glGetIntegerv = _gl_GetIntegerv;
  ctx->dispatch.glGetFloatv = _gl_GetFloatv;

  return ctx;
}

void gl_context_destroy(gl_context_t *ctx) {
  if (!ctx)
    return;
  gl_mem_free(GL_MEM_TYPE_MEM2, ctx);
}

// Public entry points for dispatched functions
void glGenBuffers(GLsizei n, GLuint *buffers) {
  if (g_gl_context)
    g_gl_context->dispatch.glGenBuffers(n, buffers);
}
void glDeleteBuffers(GLsizei n, const GLuint *buffers) {
  if (g_gl_context)
    g_gl_context->dispatch.glDeleteBuffers(n, buffers);
}
void glBindBuffer(GLenum target, GLuint buffer) {
  if (g_gl_context)
    g_gl_context->dispatch.glBindBuffer(target, buffer);
}
void glBindBufferBase(GLenum target, GLuint index, GLuint buffer) {
  if (g_gl_context)
    g_gl_context->dispatch.glBindBufferBase(target, index, buffer);
}
void glBindBufferRange(GLenum target, GLuint index, GLuint buffer,
                       GLintptr offset, GLsizeiptr size) {
  if (g_gl_context)
    g_gl_context->dispatch.glBindBufferRange(target, index, buffer, offset, size);
}
void glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage) {
  if (g_gl_context)
    g_gl_context->dispatch.glBufferData(target, size, data, usage);
}
void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data) {
  if (g_gl_context)
    g_gl_context->dispatch.glBufferSubData(target, offset, size, data);
}
void* glMapBuffer(GLenum target, GLenum access) {
  return g_gl_context ? g_gl_context->dispatch.glMapBuffer(target, access) : NULL;
}
void* glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) {
  return g_gl_context ? g_gl_context->dispatch.glMapBufferRange(target, offset, length, access) : NULL;
}
GLboolean glUnmapBuffer(GLenum target) {
  return g_gl_context ? g_gl_context->dispatch.glUnmapBuffer(target) : GL_FALSE;
}
void glGenTextures(GLsizei n, GLuint *textures) { if(g_gl_context) g_gl_context->dispatch.glGenTextures(n, textures); }
void glDeleteTextures(GLsizei n, const GLuint *textures) { if(g_gl_context) g_gl_context->dispatch.glDeleteTextures(n, textures); }
void glBindTexture(GLenum target, GLuint texture) { if(g_gl_context) g_gl_context->dispatch.glBindTexture(target, texture); }
void glActiveTexture(GLenum texture) { if(g_gl_context) g_gl_context->dispatch.glActiveTexture(texture); }
void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels) { if(g_gl_context) g_gl_context->dispatch.glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels); }
void glTexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid *pixels) { if(g_gl_context) g_gl_context->dispatch.glTexImage3D(target, level, internalformat, width, height, depth, border, format, type, pixels); }
void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels) { if(g_gl_context) g_gl_context->dispatch.glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels); }
void glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const GLvoid *pixels) { if(g_gl_context) g_gl_context->dispatch.glTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels); }
void glTexParameteri(GLenum target, GLenum pname, GLint param) { if(g_gl_context) g_gl_context->dispatch.glTexParameteri(target, pname, param); }
void glGenerateMipmap(GLenum target) { if(g_gl_context) g_gl_context->dispatch.glGenerateMipmap(target); }

GLuint glCreateShader(GLenum type) { return g_gl_context ? g_gl_context->dispatch.glCreateShader(type) : 0; }
void glShaderSource(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length) { if(g_gl_context) g_gl_context->dispatch.glShaderSource(shader, count, string, length); }
void glCompileShader(GLuint shader) { if(g_gl_context) g_gl_context->dispatch.glCompileShader(shader); }
GLuint glCreateProgram(void) { return g_gl_context ? g_gl_context->dispatch.glCreateProgram() : 0; }
void glAttachShader(GLuint program, GLuint shader) { if(g_gl_context) g_gl_context->dispatch.glAttachShader(program, shader); }
void glLinkProgram(GLuint program) { if(g_gl_context) g_gl_context->dispatch.glLinkProgram(program); }
void glUseProgram(GLuint program) { if(g_gl_context) g_gl_context->dispatch.glUseProgram(program); }
void glUniform1f(GLint location, GLfloat v0) { if(g_gl_context) g_gl_context->dispatch.glUniform1f(location, v0); }
void glUniform1fv(GLint location, GLsizei count, const GLfloat *value) { if(g_gl_context) g_gl_context->dispatch.glUniform1fv(location, count, value); }
void glUniform1i(GLint location, GLint v0) { if(g_gl_context) g_gl_context->dispatch.glUniform1i(location, v0); }
void glUniform2f(GLint location, GLfloat v0, GLfloat v1) { if(g_gl_context) g_gl_context->dispatch.glUniform2f(location, v0, v1); }
void glUniform2fv(GLint location, GLsizei count, const GLfloat *value) { if(g_gl_context) g_gl_context->dispatch.glUniform2fv(location, count, value); }
void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) { if(g_gl_context) g_gl_context->dispatch.glUniform3f(location, v0, v1, v2); }
void glUniform3fv(GLint location, GLsizei count, const GLfloat *value) { if(g_gl_context) g_gl_context->dispatch.glUniform3fv(location, count, value); }
void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) { if(g_gl_context) g_gl_context->dispatch.glUniform4f(location, v0, v1, v2, v3); }
void glUniform4fv(GLint location, GLsizei count, const GLfloat *value) { if(g_gl_context) g_gl_context->dispatch.glUniform4fv(location, count, value); }
void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { if(g_gl_context) g_gl_context->dispatch.glUniformMatrix4fv(location, count, transpose, value); }
GLint glGetUniformLocation(GLuint program, const GLchar *name) { return g_gl_context ? g_gl_context->dispatch.glGetUniformLocation(program, name) : -1; }
GLuint glGetUniformBlockIndex(GLuint program, const GLchar *uniformBlockName) { return g_gl_context ? g_gl_context->dispatch.glGetUniformBlockIndex(program, uniformBlockName) : GL_INVALID_INDEX; }
void glUniformBlockBinding(GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding) { if(g_gl_context) g_gl_context->dispatch.glUniformBlockBinding(program, uniformBlockIndex, uniformBlockBinding); }
void glWiiULoadShaderGroup(GLuint program, const void* shaderGroup) { if(g_gl_context) g_gl_context->dispatch.glWiiULoadShaderGroup(program, shaderGroup); }
void glWiiULoadShaderGroupGFD(GLuint program, GLuint index, const void* gfdData) { if(g_gl_context) g_gl_context->dispatch.glWiiULoadShaderGroupGFD(program, index, gfdData); }

void glGenVertexArrays(GLsizei n, GLuint *arrays) { if(g_gl_context) g_gl_context->dispatch.glGenVertexArrays(n, arrays); }
void glDeleteVertexArrays(GLsizei n, const GLuint *arrays) { if(g_gl_context) g_gl_context->dispatch.glDeleteVertexArrays(n, arrays); }
void glBindVertexArray(GLuint array) { if(g_gl_context) g_gl_context->dispatch.glBindVertexArray(array); }
void glEnableVertexAttribArray(GLuint index) { if(g_gl_context) g_gl_context->dispatch.glEnableVertexAttribArray(index); }
void glDisableVertexAttribArray(GLuint index) { if(g_gl_context) g_gl_context->dispatch.glDisableVertexAttribArray(index); }
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid *pointer) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribPointer(index, size, type, normalized, stride, pointer); }
void glVertexAttribDivisor(GLuint index, GLuint divisor) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribDivisor(index, divisor); }

void glGenFramebuffers(GLsizei n, GLuint *framebuffers) { if(g_gl_context) g_gl_context->dispatch.glGenFramebuffers(n, framebuffers); }
void glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers) { if(g_gl_context) g_gl_context->dispatch.glDeleteFramebuffers(n, framebuffers); }
void glBindFramebuffer(GLenum target, GLuint framebuffer) { if(g_gl_context) g_gl_context->dispatch.glBindFramebuffer(target, framebuffer); }
void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) { if(g_gl_context) g_gl_context->dispatch.glFramebufferTexture2D(target, attachment, textarget, texture, level); }
void glDrawBuffers(GLsizei n, const GLenum *bufs) { if(g_gl_context) g_gl_context->dispatch.glDrawBuffers(n, bufs); }

void glDrawArrays(GLenum mode, GLint first, GLsizei count) { if(g_gl_context) g_gl_context->dispatch.glDrawArrays(mode, first, count); }
void glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instancecount) { if(g_gl_context) g_gl_context->dispatch.glDrawArraysInstanced(mode, first, count, instancecount); }
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices) { if(g_gl_context) g_gl_context->dispatch.glDrawElements(mode, count, type, indices); }
void glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices, GLsizei instancecount) { if(g_gl_context) g_gl_context->dispatch.glDrawElementsInstanced(mode, count, type, indices, instancecount); }

void glFlush(void) {
  if (g_gl_context) {
    gl_flush_state();
    GX2Flush();
  }
}

void glFinish(void) {
  if (g_gl_context) {
    gl_flush_state();
    GX2DrawDone();
  }
}

void glEnable(GLenum cap) {
  if (g_gl_context)
    g_gl_context->dispatch.glEnable(cap);
}
void glDisable(GLenum cap) {
  if (g_gl_context)
    g_gl_context->dispatch.glDisable(cap);
}

void glBlendFunc(GLenum sfactor, GLenum dfactor) {
  if (g_gl_context)
    g_gl_context->dispatch.glBlendFunc(sfactor, dfactor);
}
void glBlendEquation(GLenum mode) {
  if (g_gl_context)
    g_gl_context->dispatch.glBlendEquation(mode);
}
void glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB,
                         GLenum sfactorAlpha, GLenum dfactorAlpha) {
  if (g_gl_context)
    g_gl_context->dispatch.glBlendFuncSeparate(sfactorRGB, dfactorRGB,
                                               sfactorAlpha, dfactorAlpha);
}
void glDepthFunc(GLenum func) {
  if (g_gl_context)
    g_gl_context->dispatch.glDepthFunc(func);
}
void glDepthMask(GLboolean flag) {
  if (g_gl_context)
    g_gl_context->dispatch.glDepthMask(flag);
}
void glStencilFunc(GLenum func, GLint ref, GLuint mask) {
  if (g_gl_context)
    g_gl_context->dispatch.glStencilFunc(func, ref, mask);
}
void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass) {
  if (g_gl_context)
    g_gl_context->dispatch.glStencilOp(fail, zfail, zpass);
}
void glCullFace(GLenum mode) {
  if (g_gl_context)
    g_gl_context->dispatch.glCullFace(mode);
}
void glFrontFace(GLenum mode) {
  if (g_gl_context)
    g_gl_context->dispatch.glFrontFace(mode);
}
void glPolygonMode(GLenum face, GLenum mode) {
  if (g_gl_context)
    g_gl_context->dispatch.glPolygonMode(face, mode);
}
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
  if (g_gl_context)
    g_gl_context->dispatch.glViewport(x, y, width, height);
}
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
  if (g_gl_context)
    g_gl_context->dispatch.glScissor(x, y, width, height);
}
void glColorMask(GLboolean red, GLboolean green, GLboolean blue,
                 GLboolean alpha) {
  if (g_gl_context)
    g_gl_context->dispatch.glColorMask(red, green, blue, alpha);
}
void glLineWidth(GLfloat width) {
  if (g_gl_context)
    g_gl_context->dispatch.glLineWidth(width);
}

const GLubyte *glGetString(GLenum name) {
    return g_gl_context ? g_gl_context->dispatch.glGetString(name) : NULL;
}

void glGetIntegerv(GLenum pname, GLint *data) {
    if (g_gl_context)
        g_gl_context->dispatch.glGetIntegerv(pname, data);
}

void glGetFloatv(GLenum pname, GLfloat *data) {
    if (g_gl_context)
        g_gl_context->dispatch.glGetFloatv(pname, data);
}

#ifdef __cplusplus
}
#endif
