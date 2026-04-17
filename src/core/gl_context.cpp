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
#include <whb/gfx.h>
#include <gx2/clear.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

gl_context_t *g_gl_context = NULL;

static void gl_context_init_defaults(gl_context_t *ctx) {
  GX2ColorBuffer *tv_color;
  if (!ctx) return;
  memset(ctx, 0, sizeof(gl_context_t));
  ctx->active_texture = 0;
  ctx->blend_src_rgb = GL_ONE;
  ctx->blend_dst_rgb = GL_ZERO;
  ctx->blend_src_alpha = GL_ONE;
  ctx->blend_dst_alpha = GL_ZERO;
  ctx->blend_eq_rgb = GL_FUNC_ADD;
  ctx->blend_eq_alpha = GL_FUNC_ADD;
  ctx->depth_func = GL_LESS;
  ctx->depth_mask = GL_TRUE;
  ctx->viewport.near_z = 0.0f;
  ctx->viewport.far_z = 1.0f;
  ctx->pack_alignment = 4;
  ctx->unpack_alignment = 4;
  ctx->stencil_compare_mask[0] = 0xFFu;
  ctx->stencil_compare_mask[1] = 0xFFu;
  ctx->stencil_write_mask[0] = 0xFFu;
  ctx->stencil_write_mask[1] = 0xFFu;
  ctx->stencil_func[0] = GL_ALWAYS;
  ctx->stencil_func[1] = GL_ALWAYS;
  ctx->stencil_fail[0] = GL_KEEP;
  ctx->stencil_fail[1] = GL_KEEP;
  ctx->stencil_zfail[0] = GL_KEEP;
  ctx->stencil_zfail[1] = GL_KEEP;
  ctx->stencil_zpass[0] = GL_KEEP;
  ctx->stencil_zpass[1] = GL_KEEP;
  ctx->cull_face_mode = GL_BACK;
  ctx->front_face = GL_CCW;
  ctx->polygon_mode = GL_FILL;
  ctx->line_width = 1.0f;
  ctx->logic_op = GL_COPY;
  ctx->point_size = 1.0f;
  for (uint32_t i = 0; i < GL33_MAX_VERTEX_ATTRIBS; ++i) ctx->current_vertex_attrib[i][3] = 1.0f;
  ctx->clear_depth = 1.0f;
  ctx->sample_coverage_value = 1.0f;
  ctx->generate_mipmap_hint = GL_DONT_CARE;
  tv_color = WHBGfxGetTVColourBuffer();
  if (tv_color) {
    ctx->viewport.width = (GLsizei)tv_color->surface.width;
    ctx->viewport.height = (GLsizei)tv_color->surface.height;
    ctx->scissor.width = (GLsizei)tv_color->surface.width;
    ctx->scissor.height = (GLsizei)tv_color->surface.height;
  }
  ctx->dirty_flags = 0xFFFFFFFF;
}

void _gl_set_error(GLenum error) {
  if (!g_gl_context) return;
  OSLockMutex(&g_gl_context->error_mutex);
  uint32_t next = (g_gl_context->error_head + 1) % GL_ERROR_QUEUE_SIZE;
  if (next != g_gl_context->error_tail) {
    g_gl_context->error_queue[g_gl_context->error_head] = error;
    g_gl_context->error_head = next;
  }
  OSUnlockMutex(&g_gl_context->error_mutex);
}

GLenum glGetError(void) {
  if (!g_gl_context) return GL_NO_ERROR;
  OSLockMutex(&g_gl_context->error_mutex);
  if (g_gl_context->error_head == g_gl_context->error_tail) { OSUnlockMutex(&g_gl_context->error_mutex); return GL_NO_ERROR; }
  GLenum error = g_gl_context->error_queue[g_gl_context->error_tail];
  g_gl_context->error_tail = (g_gl_context->error_tail + 1) % GL_ERROR_QUEUE_SIZE;
  OSUnlockMutex(&g_gl_context->error_mutex);
  return error;
}

void _gl_Flush(void) { gl_flush_state(); GX2Flush(); }
void _gl_Finish(void) { GX2DrawDone(); }

gl_context_t *gl_context_create(void) {
  gl_context_t *ctx = (gl_context_t *)gl_mem_alloc(GL_MEM_TYPE_MEM2, sizeof(gl_context_t), 4);
  if (!ctx) return NULL;
  gl_context_init_defaults(ctx);
  OSInitMutex(&ctx->error_mutex);
  gl_buffer_init(); gl_texture_init(); gl_shader_init(); gl_vao_init(); gl_framebuffer_init();

  ctx->dispatch.glGenBuffers = _gl_GenBuffers;
  ctx->dispatch.glDeleteBuffers = _gl_DeleteBuffers;
  ctx->dispatch.glIsBuffer = _gl_IsBuffer;
  ctx->dispatch.glBindBuffer = _gl_BindBuffer;
  ctx->dispatch.glBindBufferBase = _gl_BindBufferBase;
  ctx->dispatch.glBindBufferRange = _gl_BindBufferRange;
  ctx->dispatch.glBufferData = _gl_BufferData;
  ctx->dispatch.glBufferSubData = _gl_BufferSubData;
  ctx->dispatch.glGetBufferParameteriv = _gl_GetBufferParameteriv;
  ctx->dispatch.glGetBufferPointerv = _gl_GetBufferPointerv;
  ctx->dispatch.glMapBuffer = _gl_MapBuffer;
  ctx->dispatch.glMapBufferRange = _gl_MapBufferRange;
  ctx->dispatch.glUnmapBuffer = _gl_UnmapBuffer;
  ctx->dispatch.glEnable = _gl_Enable;
  ctx->dispatch.glDisable = _gl_Disable;
  ctx->dispatch.glIsEnabled = _gl_IsEnabled;
  ctx->dispatch.glClearColor = _gl_ClearColor;
  ctx->dispatch.glClearDepth = _gl_ClearDepth;
  ctx->dispatch.glClearStencil = _gl_ClearStencil;
  ctx->dispatch.glClear = _gl_Clear;
  ctx->dispatch.glGenTextures = _gl_GenTextures;
  ctx->dispatch.glDeleteTextures = _gl_DeleteTextures;
  ctx->dispatch.glIsTexture = _gl_IsTexture;
  ctx->dispatch.glGenSamplers = _gl_GenSamplers;
  ctx->dispatch.glDeleteSamplers = _gl_DeleteSamplers;
  ctx->dispatch.glIsSampler = _gl_IsSampler;
  ctx->dispatch.glBindTexture = _gl_BindTexture;
  ctx->dispatch.glBindSampler = _gl_BindSampler;
  ctx->dispatch.glActiveTexture = _gl_ActiveTexture;
  ctx->dispatch.glTexImage2D = _gl_TexImage2D;
  ctx->dispatch.glTexImage3D = _gl_TexImage3D;
  ctx->dispatch.glTexSubImage2D = _gl_TexSubImage2D;
  ctx->dispatch.glTexSubImage3D = _gl_TexSubImage3D;
  ctx->dispatch.glTexParameteri = _gl_TexParameteri;
  ctx->dispatch.glTexParameterf = _gl_TexParameterf;
  ctx->dispatch.glTexParameteriv = _gl_TexParameteriv;
  ctx->dispatch.glTexParameterfv = _gl_TexParameterfv;
  ctx->dispatch.glGetTexParameteriv = _gl_GetTexParameteriv;
  ctx->dispatch.glGetTexParameterfv = _gl_GetTexParameterfv;
  ctx->dispatch.glSamplerParameteriv = _gl_SamplerParameteriv;
  ctx->dispatch.glSamplerParameterfv = _gl_SamplerParameterfv;
  ctx->dispatch.glSamplerParameteri = _gl_SamplerParameteri;
  ctx->dispatch.glSamplerParameterf = _gl_SamplerParameterf;
  ctx->dispatch.glGetSamplerParameteriv = _gl_GetSamplerParameteriv;
  ctx->dispatch.glGetSamplerParameterfv = _gl_GetSamplerParameterfv;
  ctx->dispatch.glGenerateMipmap = _gl_GenerateMipmap;
  ctx->dispatch.glCreateShader = _gl_CreateShader;
  ctx->dispatch.glDeleteShader = _gl_DeleteShader;
  ctx->dispatch.glIsShader = _gl_IsShader;
  ctx->dispatch.glShaderSource = _gl_ShaderSource;
  ctx->dispatch.glGetShaderSource = _gl_GetShaderSource;
  ctx->dispatch.glCompileShader = _gl_CompileShader;
  ctx->dispatch.glCreateProgram = _gl_CreateProgram;
  ctx->dispatch.glDeleteProgram = _gl_DeleteProgram;
  ctx->dispatch.glIsProgram = _gl_IsProgram;
  ctx->dispatch.glAttachShader = _gl_AttachShader;
  ctx->dispatch.glDetachShader = _gl_DetachShader;
  ctx->dispatch.glLinkProgram = _gl_LinkProgram;
  ctx->dispatch.glValidateProgram = _gl_ValidateProgram;
  ctx->dispatch.glUseProgram = _gl_UseProgram;
  ctx->dispatch.glGetShaderiv = _gl_GetShaderiv;
  ctx->dispatch.glGetProgramiv = _gl_GetProgramiv;
  ctx->dispatch.glGetShaderInfoLog = _gl_GetShaderInfoLog;
  ctx->dispatch.glGetProgramInfoLog = _gl_GetProgramInfoLog;
  ctx->dispatch.glBindAttribLocation = _gl_BindAttribLocation;
  ctx->dispatch.glGetAttachedShaders = _gl_GetAttachedShaders;
  ctx->dispatch.glGetActiveAttrib = _gl_GetActiveAttrib;
  ctx->dispatch.glGetActiveUniform = _gl_GetActiveUniform;
  ctx->dispatch.glGetUniformLocation = _gl_GetUniformLocation;
  ctx->dispatch.glGetAttribLocation = _gl_GetAttribLocation;
  ctx->dispatch.glGetUniformBlockIndex = _gl_GetUniformBlockIndex;
  ctx->dispatch.glUniformBlockBinding = _gl_UniformBlockBinding;
  ctx->dispatch.glWiiULoadShaderGroup = _gl_WiiULoadShaderGroup;
  ctx->dispatch.glWiiULoadShaderGroupGFD = _gl_WiiULoadShaderGroupGFD;
  ctx->dispatch.glUniform1f = _gl_Uniform1f;
  ctx->dispatch.glUniform1fv = _gl_Uniform1fv;
  ctx->dispatch.glUniform1i = _gl_Uniform1i;
  ctx->dispatch.glUniform1iv = _gl_Uniform1iv;
  ctx->dispatch.glUniform2f = _gl_Uniform2f;
  ctx->dispatch.glUniform2fv = _gl_Uniform2fv;
  ctx->dispatch.glUniform2i = _gl_Uniform2i;
  ctx->dispatch.glUniform2iv = _gl_Uniform2iv;
  ctx->dispatch.glUniform3f = _gl_Uniform3f;
  ctx->dispatch.glUniform3fv = _gl_Uniform3fv;
  ctx->dispatch.glUniform3i = _gl_Uniform3i;
  ctx->dispatch.glUniform3iv = _gl_Uniform3iv;
  ctx->dispatch.glUniform4f = _gl_Uniform4f;
  ctx->dispatch.glUniform4fv = _gl_Uniform4fv;
  ctx->dispatch.glUniform4i = _gl_Uniform4i;
  ctx->dispatch.glUniform4iv = _gl_Uniform4iv;
  ctx->dispatch.glUniform1ui = _gl_Uniform1ui;
  ctx->dispatch.glUniform2ui = _gl_Uniform2ui;
  ctx->dispatch.glUniform3ui = _gl_Uniform3ui;
  ctx->dispatch.glUniform4ui = _gl_Uniform4ui;
  ctx->dispatch.glUniform1uiv = _gl_Uniform1uiv;
  ctx->dispatch.glUniform2uiv = _gl_Uniform2uiv;
  ctx->dispatch.glUniform3uiv = _gl_Uniform3uiv;
  ctx->dispatch.glUniform4uiv = _gl_Uniform4uiv;
  ctx->dispatch.glUniformMatrix2fv = _gl_UniformMatrix2fv;
  ctx->dispatch.glUniformMatrix3fv = _gl_UniformMatrix3fv;
  ctx->dispatch.glUniformMatrix4fv = _gl_UniformMatrix4fv;
  ctx->dispatch.glUniformMatrix2x3fv = _gl_UniformMatrix2x3fv;
  ctx->dispatch.glUniformMatrix3x2fv = _gl_UniformMatrix3x2fv;
  ctx->dispatch.glUniformMatrix2x4fv = _gl_UniformMatrix2x4fv;
  ctx->dispatch.glUniformMatrix4x2fv = _gl_UniformMatrix4x2fv;
  ctx->dispatch.glUniformMatrix3x4fv = _gl_UniformMatrix3x4fv;
  ctx->dispatch.glUniformMatrix4x3fv = _gl_UniformMatrix4x3fv;
  ctx->dispatch.glGetUniformfv = _gl_GetUniformfv;
  ctx->dispatch.glGetUniformiv = _gl_GetUniformiv;
  ctx->dispatch.glGetUniformuiv = _gl_GetUniformuiv;
  ctx->dispatch.glGenVertexArrays = _gl_GenVertexArrays;
  ctx->dispatch.glDeleteVertexArrays = _gl_DeleteVertexArrays;
  ctx->dispatch.glIsVertexArray = _gl_IsVertexArray;
  ctx->dispatch.glBindVertexArray = _gl_BindVertexArray;
  ctx->dispatch.glEnableVertexAttribArray = _gl_EnableVertexAttribArray;
  ctx->dispatch.glDisableVertexAttribArray = _gl_DisableVertexAttribArray;
  ctx->dispatch.glGetVertexAttribiv = _gl_GetVertexAttribiv;
  ctx->dispatch.glGetVertexAttribfv = _gl_GetVertexAttribfv;
  ctx->dispatch.glGetVertexAttribPointerv = _gl_GetVertexAttribPointerv;
  ctx->dispatch.glVertexAttribPointer = _gl_VertexAttribPointer;
  ctx->dispatch.glVertexAttribIPointer = _gl_VertexAttribIPointer;
  ctx->dispatch.glGetVertexAttribIiv = _gl_GetVertexAttribIiv;
  ctx->dispatch.glGetVertexAttribIuiv = _gl_GetVertexAttribIuiv;
  ctx->dispatch.glVertexAttribI1i = _gl_VertexAttribI1i;
  ctx->dispatch.glVertexAttribI2i = _gl_VertexAttribI2i;
  ctx->dispatch.glVertexAttribI3i = _gl_VertexAttribI3i;
  ctx->dispatch.glVertexAttribI4i = _gl_VertexAttribI4i;
  ctx->dispatch.glVertexAttribI1ui = _gl_VertexAttribI1ui;
  ctx->dispatch.glVertexAttribI2ui = _gl_VertexAttribI2ui;
  ctx->dispatch.glVertexAttribI3ui = _gl_VertexAttribI3ui;
  ctx->dispatch.glVertexAttribI4ui = _gl_VertexAttribI4ui;
  ctx->dispatch.glVertexAttribI1iv = _gl_VertexAttribI1iv;
  ctx->dispatch.glVertexAttribI2iv = _gl_VertexAttribI2iv;
  ctx->dispatch.glVertexAttribI3iv = _gl_VertexAttribI3iv;
  ctx->dispatch.glVertexAttribI4iv = _gl_VertexAttribI4iv;
  ctx->dispatch.glVertexAttribI1uiv = _gl_VertexAttribI1uiv;
  ctx->dispatch.glVertexAttribI2uiv = _gl_VertexAttribI2uiv;
  ctx->dispatch.glVertexAttribI3uiv = _gl_VertexAttribI3uiv;
  ctx->dispatch.glVertexAttribI4uiv = _gl_VertexAttribI4uiv;
  ctx->dispatch.glVertexAttribDivisor = _gl_VertexAttribDivisor;
  ctx->dispatch.glGenFramebuffers = _gl_GenFramebuffers;
  ctx->dispatch.glDeleteFramebuffers = _gl_DeleteFramebuffers;
  ctx->dispatch.glIsFramebuffer = _gl_IsFramebuffer;
  ctx->dispatch.glGenRenderbuffers = _gl_GenRenderbuffers;
  ctx->dispatch.glDeleteRenderbuffers = _gl_DeleteRenderbuffers;
  ctx->dispatch.glIsRenderbuffer = _gl_IsRenderbuffer;
  ctx->dispatch.glBindFramebuffer = _gl_BindFramebuffer;
  ctx->dispatch.glBindRenderbuffer = _gl_BindRenderbuffer;
  ctx->dispatch.glCheckFramebufferStatus = _gl_CheckFramebufferStatus;
  ctx->dispatch.glFramebufferTexture = _gl_FramebufferTexture;
  ctx->dispatch.glFramebufferTexture2D = _gl_FramebufferTexture2D;
  ctx->dispatch.glFramebufferRenderbuffer = _gl_FramebufferRenderbuffer;
  ctx->dispatch.glRenderbufferStorage = _gl_RenderbufferStorage;
  ctx->dispatch.glRenderbufferStorageMultisample = _gl_RenderbufferStorageMultisample;
  ctx->dispatch.glGetRenderbufferParameteriv = _gl_GetRenderbufferParameteriv;
  ctx->dispatch.glGetFramebufferAttachmentParameteriv = _gl_GetFramebufferAttachmentParameteriv;
  ctx->dispatch.glBlitFramebuffer = _gl_BlitFramebuffer;
  ctx->dispatch.glDrawBuffer = _gl_DrawBuffer;
  ctx->dispatch.glDrawBuffers = _gl_DrawBuffers;
  ctx->dispatch.glReadBuffer = _gl_ReadBuffer;
  ctx->dispatch.glReadPixels = _gl_ReadPixels;
  ctx->dispatch.glFlush = _gl_Flush;
  ctx->dispatch.glFinish = _gl_Finish;
  ctx->dispatch.glGenQueries = _gl_GenQueries;
  ctx->dispatch.glDeleteQueries = _gl_DeleteQueries;
  ctx->dispatch.glIsQuery = _gl_IsQuery;
  ctx->dispatch.glBeginQuery = _gl_BeginQuery;
  ctx->dispatch.glEndQuery = _gl_EndQuery;
  ctx->dispatch.glGetQueryiv = _gl_GetQueryiv;
  ctx->dispatch.glGetQueryObjectiv = _gl_GetQueryObjectiv;
  ctx->dispatch.glGetQueryObjectuiv = _gl_GetQueryObjectuiv;
  ctx->dispatch.glBeginConditionalRender = _gl_BeginConditionalRender;
  ctx->dispatch.glEndConditionalRender = _gl_EndConditionalRender;
  ctx->dispatch.glBeginQueryIndexed = _gl_BeginQueryIndexed;
  ctx->dispatch.glEndQueryIndexed = _gl_EndQueryIndexed;
  ctx->dispatch.glGetQueryIndexediv = _gl_GetQueryIndexediv;
  ctx->dispatch.glGenTransformFeedbacks = _gl_GenTransformFeedbacks;
  ctx->dispatch.glDeleteTransformFeedbacks = _gl_DeleteTransformFeedbacks;
  ctx->dispatch.glGetBooleanv = _gl_GetBooleanv;
  ctx->dispatch.glGetDoublev = _gl_GetDoublev;
  ctx->dispatch.glGetIntegerv = _gl_GetIntegerv;
  ctx->dispatch.glGetFloatv = _gl_GetFloatv;
  ctx->dispatch.glGetString = _gl_GetString;
  ctx->dispatch.glGetStringi = _gl_GetStringi;
  ctx->dispatch.glLogicOp = _gl_LogicOp;
  ctx->dispatch.glPointSize = _gl_PointSize;
  ctx->dispatch.glFlushMappedBufferRange = _gl_FlushMappedBufferRange;
  ctx->dispatch.glTexImage1D = _gl_TexImage1D;
  ctx->dispatch.glTexSubImage1D = _gl_TexSubImage1D;
  ctx->dispatch.glCopyTexImage1D = _gl_CopyTexImage1D;
  ctx->dispatch.glCopyTexSubImage1D = _gl_CopyTexSubImage1D;
  ctx->dispatch.glCopyTexSubImage3D = _gl_CopyTexSubImage3D;
  ctx->dispatch.glCompressedTexImage1D = _gl_CompressedTexImage1D;
  ctx->dispatch.glCompressedTexImage3D = _gl_CompressedTexImage3D;
  ctx->dispatch.glCompressedTexSubImage1D = _gl_CompressedTexSubImage1D;
  ctx->dispatch.glCompressedTexSubImage3D = _gl_CompressedTexSubImage3D;
  ctx->dispatch.glGetTexLevelParameteriv = _gl_GetTexLevelParameteriv;
  ctx->dispatch.glGetTexLevelParameterfv = _gl_GetTexLevelParameterfv;
  ctx->dispatch.glGetActiveUniformBlockiv = _gl_GetActiveUniformBlockiv;
  ctx->dispatch.glGetActiveUniformBlockName = _gl_GetActiveUniformBlockName;
  ctx->dispatch.glGetActiveUniformsiv = _gl_GetActiveUniformsiv;
  ctx->dispatch.glGetActiveUniformName = _gl_GetActiveUniformName;
  ctx->dispatch.glDrawArrays = _gl_DrawArrays;
  ctx->dispatch.glDrawArraysInstanced = _gl_DrawArraysInstanced;
  ctx->dispatch.glDrawElements = _gl_DrawElements;
  ctx->dispatch.glDrawElementsInstanced = _gl_DrawElementsInstanced;
  ctx->dispatch.glBlendFunc = _gl_BlendFunc;
  ctx->dispatch.glBlendEquation = _gl_BlendEquation;
  ctx->dispatch.glBlendEquationSeparate = _gl_BlendEquationSeparate;
  ctx->dispatch.glBlendFuncSeparate = _gl_BlendFuncSeparate;
  ctx->dispatch.glBlendColor = _gl_BlendColor;
  ctx->dispatch.glDepthFunc = _gl_DepthFunc;
  ctx->dispatch.glDepthMask = _gl_DepthMask;
  ctx->dispatch.glDepthRange = _gl_DepthRange;
  ctx->dispatch.glStencilFunc = _gl_StencilFunc;
  ctx->dispatch.glStencilFuncSeparate = _gl_StencilFuncSeparate;
  ctx->dispatch.glStencilOp = _gl_StencilOp;
  ctx->dispatch.glStencilOpSeparate = _gl_StencilOpSeparate;
  ctx->dispatch.glStencilMask = _gl_StencilMask;
  ctx->dispatch.glStencilMaskSeparate = _gl_StencilMaskSeparate;
  ctx->dispatch.glCullFace = _gl_CullFace;
  ctx->dispatch.glFrontFace = _gl_FrontFace;
  ctx->dispatch.glPolygonMode = _gl_PolygonMode;
  ctx->dispatch.glPolygonOffset = _gl_PolygonOffset;
  ctx->dispatch.glViewport = _gl_Viewport;
  ctx->dispatch.glScissor = _gl_Scissor;
  ctx->dispatch.glColorMask = _gl_ColorMask;
  ctx->dispatch.glLineWidth = _gl_LineWidth;
  ctx->dispatch.glPixelStorei = _gl_PixelStorei;
  ctx->dispatch.glPrimitiveRestartIndex = _gl_PrimitiveRestartIndex;

  return ctx;
}

void gl_context_destroy(gl_context_t *ctx) { gl_mem_free(GL_MEM_TYPE_MEM2, ctx); }

void glGenBuffers(GLsizei n, GLuint *b) { if(g_gl_context) g_gl_context->dispatch.glGenBuffers(n, b); }
void glDeleteBuffers(GLsizei n, const GLuint *b) { if(g_gl_context) g_gl_context->dispatch.glDeleteBuffers(n, b); }
GLboolean glIsBuffer(GLuint b) { return g_gl_context ? g_gl_context->dispatch.glIsBuffer(b) : GL_FALSE; }
void glBindBuffer(GLenum t, GLuint b) { if(g_gl_context) g_gl_context->dispatch.glBindBuffer(t, b); }
void glBindBufferBase(GLenum t, GLuint i, GLuint b) { if(g_gl_context) g_gl_context->dispatch.glBindBufferBase(t, i, b); }
void glBindBufferRange(GLenum t, GLuint i, GLuint b, GLintptr o, GLsizeiptr s) { if(g_gl_context) g_gl_context->dispatch.glBindBufferRange(t, i, b, o, s); }
void glBufferData(GLenum t, GLsizeiptr s, const GLvoid *d, GLenum u) { if(g_gl_context) g_gl_context->dispatch.glBufferData(t, s, d, u); }
void glBufferSubData(GLenum t, GLintptr o, GLsizeiptr s, const GLvoid *d) { if(g_gl_context) g_gl_context->dispatch.glBufferSubData(t, o, s, d); }
void glGetBufferParameteriv(GLenum t, GLenum p, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetBufferParameteriv(t, p, v); }
void glGetBufferPointerv(GLenum t, GLenum p, GLvoid **v) { if(g_gl_context) g_gl_context->dispatch.glGetBufferPointerv(t, p, v); }
void* glMapBuffer(GLenum t, GLenum a) { return g_gl_context ? g_gl_context->dispatch.glMapBuffer(t, a) : NULL; }
void* glMapBufferRange(GLenum t, GLintptr o, GLsizeiptr l, GLbitfield a) { return g_gl_context ? g_gl_context->dispatch.glMapBufferRange(t, o, l, a) : NULL; }
GLboolean glUnmapBuffer(GLenum t) { return g_gl_context ? g_gl_context->dispatch.glUnmapBuffer(t) : GL_FALSE; }
void glEnable(GLenum c) { if(g_gl_context) g_gl_context->dispatch.glEnable(c); }
void glDisable(GLenum c) { if(g_gl_context) g_gl_context->dispatch.glDisable(c); }
GLboolean glIsEnabled(GLenum c) { return g_gl_context ? g_gl_context->dispatch.glIsEnabled(c) : GL_FALSE; }
void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) { if(g_gl_context) g_gl_context->dispatch.glClearColor(r, g, b, a); }
void glClearDepth(GLclampd d) { if(g_gl_context) g_gl_context->dispatch.glClearDepth(d); }
void glClearDepthf(GLclampf d) { glClearDepth((GLclampd)d); }
void glClearStencil(GLint s) { if(g_gl_context) g_gl_context->dispatch.glClearStencil(s); }
void glClear(GLbitfield m) { if(g_gl_context) g_gl_context->dispatch.glClear(m); }
void glGenTextures(GLsizei n, GLuint *t) { if(g_gl_context) g_gl_context->dispatch.glGenTextures(n, t); }
void glDeleteTextures(GLsizei n, const GLuint *t) { if(g_gl_context) g_gl_context->dispatch.glDeleteTextures(n, t); }
GLboolean glIsTexture(GLuint t) { return g_gl_context ? g_gl_context->dispatch.glIsTexture(t) : GL_FALSE; }
void glGenSamplers(GLsizei n, GLuint *s) { if(g_gl_context) g_gl_context->dispatch.glGenSamplers(n, s); }
void glDeleteSamplers(GLsizei n, const GLuint *s) { if(g_gl_context) g_gl_context->dispatch.glDeleteSamplers(n, s); }
GLboolean glIsSampler(GLuint s) { return g_gl_context ? g_gl_context->dispatch.glIsSampler(s) : GL_FALSE; }
void glBindTexture(GLenum t, GLuint i) { if(g_gl_context) g_gl_context->dispatch.glBindTexture(t, i); }
void glBindSampler(GLuint u, GLuint s) { if(g_gl_context) g_gl_context->dispatch.glBindSampler(u, s); }
void glActiveTexture(GLenum t) { if(g_gl_context) g_gl_context->dispatch.glActiveTexture(t); }
void glTexImage2D(GLenum t, GLint l, GLint i, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum p, const GLvoid *px) { if(g_gl_context) g_gl_context->dispatch.glTexImage2D(t, l, i, w, h, b, f, p, px); }
void glTexImage3D(GLenum t, GLint l, GLint i, GLsizei w, GLsizei h, GLsizei d, GLint b, GLenum f, GLenum p, const GLvoid *px) { if(g_gl_context) g_gl_context->dispatch.glTexImage3D(t, l, i, w, h, d, b, f, p, px); }
void glTexSubImage2D(GLenum t, GLint l, GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum p, const GLvoid *px) { if(g_gl_context) g_gl_context->dispatch.glTexSubImage2D(t, l, x, y, w, h, f, p, px); }
void glTexSubImage3D(GLenum t, GLint l, GLint x, GLint y, GLint z, GLsizei w, GLsizei h, GLsizei d, GLenum f, GLenum p, const GLvoid *px) { if(g_gl_context) g_gl_context->dispatch.glTexSubImage3D(t, l, x, y, z, w, h, d, f, p, px); }
void glTexParameteri(GLenum t, GLenum p, GLint v) { if(g_gl_context) g_gl_context->dispatch.glTexParameteri(t, p, v); }
void glTexParameterf(GLenum t, GLenum p, GLfloat v) { if(g_gl_context) g_gl_context->dispatch.glTexParameterf(t, p, v); }
void glTexParameteriv(GLenum t, GLenum p, const GLint *v) { if(g_gl_context) g_gl_context->dispatch.glTexParameteriv(t, p, v); }
void glTexParameterfv(GLenum t, GLenum p, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glTexParameterfv(t, p, v); }
void glGetTexParameteriv(GLenum t, GLenum p, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetTexParameteriv(t, p, v); }
void glGetTexParameterfv(GLenum t, GLenum p, GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glGetTexParameterfv(t, p, v); }
void glSamplerParameteriv(GLuint s, GLenum p, const GLint *v) { if(g_gl_context) g_gl_context->dispatch.glSamplerParameteriv(s, p, v); }
void glSamplerParameterfv(GLuint s, GLenum p, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glSamplerParameterfv(s, p, v); }
void glSamplerParameteri(GLuint s, GLenum p, GLint v) { if(g_gl_context) g_gl_context->dispatch.glSamplerParameteri(s, p, v); }
void glSamplerParameterf(GLuint s, GLenum p, GLfloat v) { if(g_gl_context) g_gl_context->dispatch.glSamplerParameterf(s, p, v); }
void glGetSamplerParameteriv(GLuint s, GLenum p, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetSamplerParameteriv(s, p, v); }
void glGetSamplerParameterfv(GLuint s, GLenum p, GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glGetSamplerParameterfv(s, p, v); }
void glGenerateMipmap(GLenum t) { if(g_gl_context) g_gl_context->dispatch.glGenerateMipmap(t); }
GLuint glCreateShader(GLenum t) { return g_gl_context ? g_gl_context->dispatch.glCreateShader(t) : 0; }
void glDeleteShader(GLuint s) { if(g_gl_context) g_gl_context->dispatch.glDeleteShader(s); }
GLboolean glIsShader(GLuint s) { return g_gl_context ? g_gl_context->dispatch.glIsShader(s) : GL_FALSE; }
void glShaderSource(GLuint s, GLsizei c, const GLchar *const *str, const GLint *l) { if(g_gl_context) g_gl_context->dispatch.glShaderSource(s, c, str, l); }
void glGetShaderSource(GLuint s, GLsizei b, GLsizei *l, GLchar *src) { if(g_gl_context) g_gl_context->dispatch.glGetShaderSource(s, b, l, src); }
void glCompileShader(GLuint s) { if(g_gl_context) g_gl_context->dispatch.glCompileShader(s); }
GLuint glCreateProgram(void) { return g_gl_context ? g_gl_context->dispatch.glCreateProgram() : 0; }
void glDeleteProgram(GLuint p) { if(g_gl_context) g_gl_context->dispatch.glDeleteProgram(p); }
GLboolean glIsProgram(GLuint p) { return g_gl_context ? g_gl_context->dispatch.glIsProgram(p) : GL_FALSE; }
void glAttachShader(GLuint p, GLuint s) { if(g_gl_context) g_gl_context->dispatch.glAttachShader(p, s); }
void glDetachShader(GLuint p, GLuint s) { if(g_gl_context) g_gl_context->dispatch.glDetachShader(p, s); }
void glLinkProgram(GLuint p) { if(g_gl_context) g_gl_context->dispatch.glLinkProgram(p); }
void glValidateProgram(GLuint p) { if(g_gl_context) g_gl_context->dispatch.glValidateProgram(p); }
void glUseProgram(GLuint p) { if(g_gl_context) g_gl_context->dispatch.glUseProgram(p); }
void glGetShaderiv(GLuint s, GLenum p, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetShaderiv(s, p, v); }
void glGetProgramiv(GLuint p, GLenum n, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetProgramiv(p, n, v); }
void glGetShaderInfoLog(GLuint s, GLsizei m, GLsizei *l, GLchar *i) { if(g_gl_context) g_gl_context->dispatch.glGetShaderInfoLog(s, m, l, i); }
void glGetProgramInfoLog(GLuint p, GLsizei m, GLsizei *l, GLchar *i) { if(g_gl_context) g_gl_context->dispatch.glGetProgramInfoLog(p, m, l, i); }
void glBindAttribLocation(GLuint p, GLuint i, const GLchar *n) { if(g_gl_context) g_gl_context->dispatch.glBindAttribLocation(p, i, n); }
void glGetAttachedShaders(GLuint p, GLsizei m, GLsizei *c, GLuint *s) { if(g_gl_context) g_gl_context->dispatch.glGetAttachedShaders(p, m, c, s); }
void glGetActiveAttrib(GLuint p, GLuint i, GLsizei b, GLsizei *l, GLint *s, GLenum *t, GLchar *n) { if(g_gl_context) g_gl_context->dispatch.glGetActiveAttrib(p, i, b, l, s, t, n); }
void glGetActiveUniform(GLuint p, GLuint i, GLsizei b, GLsizei *l, GLint *s, GLenum *t, GLchar *n) { if(g_gl_context) g_gl_context->dispatch.glGetActiveUniform(p, i, b, l, s, t, n); }
GLint glGetUniformLocation(GLuint p, const GLchar *n) { return g_gl_context ? g_gl_context->dispatch.glGetUniformLocation(p, n) : -1; }
GLint glGetAttribLocation(GLuint p, const GLchar *n) { return g_gl_context ? g_gl_context->dispatch.glGetAttribLocation(p, n) : -1; }
GLuint glGetUniformBlockIndex(GLuint p, const GLchar *n) { return g_gl_context ? g_gl_context->dispatch.glGetUniformBlockIndex(p, n) : GL_INVALID_INDEX; }
void glUniformBlockBinding(GLuint p, GLuint i, GLuint b) { if(g_gl_context) g_gl_context->dispatch.glUniformBlockBinding(p, i, b); }
void glUniform1f(GLint l, GLfloat v) { if(g_gl_context) g_gl_context->dispatch.glUniform1f(l, v); }
void glUniform1fv(GLint l, GLsizei c, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glUniform1fv(l, c, v); }
void glUniform1i(GLint l, GLint v) { if(g_gl_context) g_gl_context->dispatch.glUniform1i(l, v); }
void glUniform1iv(GLint l, GLsizei c, const GLint *v) { if(g_gl_context) g_gl_context->dispatch.glUniform1iv(l, c, v); }
void glUniform2f(GLint l, GLfloat v0, GLfloat v1) { if(g_gl_context) g_gl_context->dispatch.glUniform2f(l, v0, v1); }
void glUniform2fv(GLint l, GLsizei c, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glUniform2fv(l, c, v); }
void glUniform2i(GLint l, GLint v0, GLint v1) { if(g_gl_context) g_gl_context->dispatch.glUniform2i(l, v0, v1); }
void glUniform2iv(GLint l, GLsizei c, const GLint *v) { if(g_gl_context) g_gl_context->dispatch.glUniform2iv(l, c, v); }
void glUniform3f(GLint l, GLfloat v0, GLfloat v1, GLfloat v2) { if(g_gl_context) g_gl_context->dispatch.glUniform3f(l, v0, v1, v2); }
void glUniform3fv(GLint l, GLsizei c, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glUniform3fv(l, c, v); }
void glUniform3i(GLint l, GLint v0, GLint v1, GLint v2) { if(g_gl_context) g_gl_context->dispatch.glUniform3i(l, v0, v1, v2); }
void glUniform3iv(GLint l, GLsizei c, const GLint *v) { if(g_gl_context) g_gl_context->dispatch.glUniform3iv(l, c, v); }
void glUniform4f(GLint l, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) { if(g_gl_context) g_gl_context->dispatch.glUniform4f(l, v0, v1, v2, v3); }
void glUniform4fv(GLint l, GLsizei c, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glUniform4fv(l, c, v); }
void glUniform4i(GLint l, GLint v0, GLint v1, GLint v2, GLint v3) { if(g_gl_context) g_gl_context->dispatch.glUniform4i(l, v0, v1, v2, v3); }
void glUniform4iv(GLint l, GLsizei c, const GLint *v) { if(g_gl_context) g_gl_context->dispatch.glUniform4iv(l, c, v); }
void glUniform1ui(GLint l, GLuint v) { if(g_gl_context) g_gl_context->dispatch.glUniform1ui(l, v); }
void glUniform2ui(GLint l, GLuint v0, GLuint v1) { if(g_gl_context) g_gl_context->dispatch.glUniform2ui(l, v0, v1); }
void glUniform3ui(GLint l, GLuint v0, GLuint v1, GLuint v2) { if(g_gl_context) g_gl_context->dispatch.glUniform3ui(l, v0, v1, v2); }
void glUniform4ui(GLint l, GLuint v0, GLuint v1, GLuint v2, GLuint v3) { if(g_gl_context) g_gl_context->dispatch.glUniform4ui(l, v0, v1, v2, v3); }
void glUniform1uiv(GLint l, GLsizei c, const GLuint *v) { if(g_gl_context) g_gl_context->dispatch.glUniform1uiv(l, c, v); }
void glUniform2uiv(GLint l, GLsizei c, const GLuint *v) { if(g_gl_context) g_gl_context->dispatch.glUniform2uiv(l, c, v); }
void glUniform3uiv(GLint l, GLsizei c, const GLuint *v) { if(g_gl_context) g_gl_context->dispatch.glUniform3uiv(l, c, v); }
void glUniform4uiv(GLint l, GLsizei c, const GLuint *v) { if(g_gl_context) g_gl_context->dispatch.glUniform4uiv(l, c, v); }
void glUniformMatrix2fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glUniformMatrix2fv(l, c, t, v); }
void glUniformMatrix3fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glUniformMatrix3fv(l, c, t, v); }
void glUniformMatrix4fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glUniformMatrix4fv(l, c, t, v); }
void glUniformMatrix2x3fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glUniformMatrix2x3fv(l, c, t, v); }
void glUniformMatrix3x2fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glUniformMatrix3x2fv(l, c, t, v); }
void glUniformMatrix2x4fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glUniformMatrix2x4fv(l, c, t, v); }
void glUniformMatrix4x2fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glUniformMatrix4x2fv(l, c, t, v); }
void glUniformMatrix3x4fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glUniformMatrix3x4fv(l, c, t, v); }
void glUniformMatrix4x3fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glUniformMatrix4x3fv(l, c, t, v); }
void glGetUniformfv(GLuint p, GLint l, GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glGetUniformfv(p, l, v); }
void glGetUniformiv(GLuint p, GLint l, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetUniformiv(p, l, v); }
void glGetUniformuiv(GLuint p, GLint l, GLuint *v) { if(g_gl_context) g_gl_context->dispatch.glGetUniformuiv(p, l, v); }
void glGenVertexArrays(GLsizei n, GLuint *a) { if(g_gl_context) g_gl_context->dispatch.glGenVertexArrays(n, a); }
void glDeleteVertexArrays(GLsizei n, const GLuint *a) { if(g_gl_context) g_gl_context->dispatch.glDeleteVertexArrays(n, a); }
GLboolean glIsVertexArray(GLuint a) { return g_gl_context ? g_gl_context->dispatch.glIsVertexArray(a) : GL_FALSE; }
void glBindVertexArray(GLuint a) { if(g_gl_context) g_gl_context->dispatch.glBindVertexArray(a); }
void glEnableVertexAttribArray(GLuint i) { if(g_gl_context) g_gl_context->dispatch.glEnableVertexAttribArray(i); }
void glDisableVertexAttribArray(GLuint i) { if(g_gl_context) g_gl_context->dispatch.glDisableVertexAttribArray(i); }
void glGetVertexAttribiv(GLuint i, GLenum p, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetVertexAttribiv(i, p, v); }
void glGetVertexAttribfv(GLuint i, GLenum p, GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glGetVertexAttribfv(i, p, v); }
void glGetVertexAttribPointerv(GLuint i, GLenum p, GLvoid **v) { if(g_gl_context) g_gl_context->dispatch.glGetVertexAttribPointerv(i, p, v); }
void glVertexAttribPointer(GLuint i, GLint s, GLenum t, GLboolean n, GLsizei r, const GLvoid *p) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribPointer(i, s, t, n, r, p); }
void glVertexAttribIPointer(GLuint i, GLint s, GLenum t, GLsizei r, const GLvoid *p) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribIPointer(i, s, t, r, p); }
void glGetVertexAttribIiv(GLuint i, GLenum p, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetVertexAttribIiv(i, p, v); }
void glGetVertexAttribIuiv(GLuint i, GLenum p, GLuint *v) { if(g_gl_context) g_gl_context->dispatch.glGetVertexAttribIuiv(i, p, v); }
void glVertexAttribI1i(GLuint i, GLint x) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI1i(i, x); }
void glVertexAttribI2i(GLuint i, GLint x, GLint y) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI2i(i, x, y); }
void glVertexAttribI3i(GLuint i, GLint x, GLint y, GLint z) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI3i(i, x, y, z); }
void glVertexAttribI4i(GLuint i, GLint x, GLint y, GLint z, GLint w) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI4i(i, x, y, z, w); }
void glVertexAttribI1ui(GLuint i, GLuint x) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI1ui(i, x); }
void glVertexAttribI2ui(GLuint i, GLuint x, GLuint y) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI2ui(i, x, y); }
void glVertexAttribI3ui(GLuint i, GLuint x, GLuint y, GLuint z) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI3ui(i, x, y, z); }
void glVertexAttribI4ui(GLuint i, GLuint x, GLuint y, GLuint z, GLuint w) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI4ui(i, x, y, z, w); }
void glVertexAttribI1iv(GLuint i, const GLint *v) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI1iv(i, v); }
void glVertexAttribI2iv(GLuint i, const GLint *v) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI2iv(i, v); }
void glVertexAttribI3iv(GLuint i, const GLint *v) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI3iv(i, v); }
void glVertexAttribI4iv(GLuint i, const GLint *v) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI4iv(i, v); }
void glVertexAttribI1uiv(GLuint i, const GLuint *v) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI1uiv(i, v); }
void glVertexAttribI2uiv(GLuint i, const GLuint *v) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI2uiv(i, v); }
void glVertexAttribI3uiv(GLuint i, const GLuint *v) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI3uiv(i, v); }
void glVertexAttribI4uiv(GLuint i, const GLuint *v) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI4uiv(i, v); }
void glVertexAttribDivisor(GLuint i, GLuint d) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribDivisor(i, d); }
void glGenFramebuffers(GLsizei n, GLuint *f) { if(g_gl_context) g_gl_context->dispatch.glGenFramebuffers(n, f); }
void glDeleteFramebuffers(GLsizei n, const GLuint *f) { if(g_gl_context) g_gl_context->dispatch.glDeleteFramebuffers(n, f); }
GLboolean glIsFramebuffer(GLuint f) { return g_gl_context ? g_gl_context->dispatch.glIsFramebuffer(f) : GL_FALSE; }
void glGenRenderbuffers(GLsizei n, GLuint *r) { if(g_gl_context) g_gl_context->dispatch.glGenRenderbuffers(n, r); }
void glDeleteRenderbuffers(GLsizei n, const GLuint *r) { if(g_gl_context) g_gl_context->dispatch.glDeleteRenderbuffers(n, r); }
GLboolean glIsRenderbuffer(GLuint r) { return g_gl_context ? g_gl_context->dispatch.glIsRenderbuffer(r) : GL_FALSE; }
void glBindFramebuffer(GLenum t, GLuint f) { if(g_gl_context) g_gl_context->dispatch.glBindFramebuffer(t, f); }
void glBindRenderbuffer(GLenum t, GLuint r) { if(g_gl_context) g_gl_context->dispatch.glBindRenderbuffer(t, r); }
GLenum glCheckFramebufferStatus(GLenum t) { return g_gl_context ? g_gl_context->dispatch.glCheckFramebufferStatus(t) : GL_FRAMEBUFFER_UNSUPPORTED; }
void glFramebufferTexture(GLenum t, GLenum a, GLuint x, GLint l) { if(g_gl_context) g_gl_context->dispatch.glFramebufferTexture(t, a, x, l); }
void glFramebufferTexture2D(GLenum t, GLenum a, GLenum s, GLuint x, GLint l) { if(g_gl_context) g_gl_context->dispatch.glFramebufferTexture2D(t, a, s, x, l); }
void glFramebufferRenderbuffer(GLenum t, GLenum a, GLenum r, GLuint b) { if(g_gl_context) g_gl_context->dispatch.glFramebufferRenderbuffer(t, a, r, b); }
void glRenderbufferStorage(GLenum t, GLenum i, GLsizei w, GLsizei h) { if(g_gl_context) g_gl_context->dispatch.glRenderbufferStorage(t, i, w, h); }
void glRenderbufferStorageMultisample(GLenum t, GLsizei s, GLenum i, GLsizei w, GLsizei h) { if(g_gl_context) g_gl_context->dispatch.glRenderbufferStorageMultisample(t, s, i, w, h); }
void glGetRenderbufferParameteriv(GLenum t, GLenum p, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetRenderbufferParameteriv(t, p, v); }
void glGetFramebufferAttachmentParameteriv(GLenum t, GLenum a, GLenum p, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetFramebufferAttachmentParameteriv(t, a, p, v); }
void glBlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield m, GLenum f) { if(g_gl_context) g_gl_context->dispatch.glBlitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, m, f); }
void glDrawBuffer(GLenum b) { if(g_gl_context) g_gl_context->dispatch.glDrawBuffer(b); }
void glDrawBuffers(GLsizei n, const GLenum *b) { if(g_gl_context) g_gl_context->dispatch.glDrawBuffers(n, b); }
void glReadBuffer(GLenum s) { if(g_gl_context) g_gl_context->dispatch.glReadBuffer(s); }
void glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum p, GLvoid *px) { if(g_gl_context) g_gl_context->dispatch.glReadPixels(x, y, w, h, f, p, px); }
void glFlush(void) { if(g_gl_context) g_gl_context->dispatch.glFlush(); }
void glFinish(void) { if(g_gl_context) g_gl_context->dispatch.glFinish(); }
void glGenQueries(GLsizei n, GLuint *i) { if(g_gl_context) g_gl_context->dispatch.glGenQueries(n, i); }
void glDeleteQueries(GLsizei n, const GLuint *i) { if(g_gl_context) g_gl_context->dispatch.glDeleteQueries(n, i); }
GLboolean glIsQuery(GLuint i) { return g_gl_context ? g_gl_context->dispatch.glIsQuery(i) : GL_FALSE; }
void glBeginQuery(GLenum t, GLuint i) { if(g_gl_context) g_gl_context->dispatch.glBeginQuery(t, i); }
void glEndQuery(GLenum t) { if(g_gl_context) g_gl_context->dispatch.glEndQuery(t); }
void glGetQueryiv(GLenum t, GLenum p, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetQueryiv(t, p, v); }
void glGetQueryObjectiv(GLuint i, GLenum p, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetQueryObjectiv(i, p, v); }
void glGetQueryObjectuiv(GLuint i, GLenum p, GLuint *v) { if(g_gl_context) g_gl_context->dispatch.glGetQueryObjectuiv(i, p, v); }
void glBeginConditionalRender(GLuint i, GLenum m) { if(g_gl_context) g_gl_context->dispatch.glBeginConditionalRender(i, m); }
void glEndConditionalRender(void) { if(g_gl_context) g_gl_context->dispatch.glEndConditionalRender(); }
void glBeginQueryIndexed(GLenum t, GLuint i, GLuint d) { if(g_gl_context) g_gl_context->dispatch.glBeginQueryIndexed(t, i, d); }
void glEndQueryIndexed(GLenum t, GLuint i) { if(g_gl_context) g_gl_context->dispatch.glEndQueryIndexed(t, i); }
void glGetQueryIndexediv(GLenum t, GLuint i, GLenum p, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetQueryIndexediv(t, i, p, v); }
void glGenTransformFeedbacks(GLsizei n, GLuint *i) { if(g_gl_context) g_gl_context->dispatch.glGenTransformFeedbacks(n, i); }
void glDeleteTransformFeedbacks(GLsizei n, const GLuint *i) { if(g_gl_context) g_gl_context->dispatch.glDeleteTransformFeedbacks(n, i); }
void glGetBooleanv(GLenum p, GLboolean *d) { if(g_gl_context) g_gl_context->dispatch.glGetBooleanv(p, d); }
void glGetDoublev(GLenum p, GLdouble *d) { if(g_gl_context) g_gl_context->dispatch.glGetDoublev(p, d); }
void glGetIntegerv(GLenum p, GLint *d) { if(g_gl_context) g_gl_context->dispatch.glGetIntegerv(p, d); }
void glGetFloatv(GLenum p, GLfloat *d) { if(g_gl_context) g_gl_context->dispatch.glGetFloatv(p, d); }
const GLubyte *glGetString(GLenum n) { return g_gl_context ? g_gl_context->dispatch.glGetString(n) : NULL; }
const GLubyte *glGetStringi(GLenum n, GLuint i) { return g_gl_context ? g_gl_context->dispatch.glGetStringi(n, i) : NULL; }
void glLogicOp(GLenum o) { if(g_gl_context) g_gl_context->dispatch.glLogicOp(o); }
void glPointSize(GLfloat s) { if(g_gl_context) g_gl_context->dispatch.glPointSize(s); }
void glFlushMappedBufferRange(GLenum t, GLintptr o, GLsizeiptr l) { if(g_gl_context) g_gl_context->dispatch.glFlushMappedBufferRange(t, o, l); }
void glTexImage1D(GLenum t, GLint l, GLint i, GLsizei w, GLint b, GLenum f, GLenum p, const GLvoid *px) { if(g_gl_context) g_gl_context->dispatch.glTexImage1D(t, l, i, w, b, f, p, px); }
void glTexSubImage1D(GLenum t, GLint l, GLint o, GLsizei w, GLenum f, GLenum p, const GLvoid *px) { if(g_gl_context) g_gl_context->dispatch.glTexSubImage1D(t, l, o, w, f, p, px); }
void glCopyTexImage1D(GLenum t, GLint l, GLenum i, GLint x, GLint y, GLsizei w, GLint b) { if(g_gl_context) g_gl_context->dispatch.glCopyTexImage1D(t, l, i, x, y, w, b); }
void glCopyTexSubImage1D(GLenum t, GLint l, GLint o, GLint x, GLint y, GLsizei w) { if(g_gl_context) g_gl_context->dispatch.glCopyTexSubImage1D(t, l, o, x, y, w); }
void glCopyTexSubImage3D(GLenum t, GLint l, GLint o, GLint y, GLint z, GLint x, GLint r, GLsizei w, GLsizei h) { if(g_gl_context) g_gl_context->dispatch.glCopyTexSubImage3D(t, l, o, y, z, x, r, w, h); }
void glCompressedTexImage1D(GLenum t, GLint l, GLenum i, GLsizei w, GLint b, GLsizei s, const GLvoid *d) { if(g_gl_context) g_gl_context->dispatch.glCompressedTexImage1D(t, l, i, w, b, s, d); }
void glCompressedTexImage3D(GLenum t, GLint l, GLenum i, GLsizei w, GLsizei h, GLsizei d, GLint b, GLsizei s, const GLvoid *x) { if(g_gl_context) g_gl_context->dispatch.glCompressedTexImage3D(t, l, i, w, h, d, b, s, x); }
void glCompressedTexSubImage1D(GLenum t, GLint l, GLint o, GLsizei w, GLenum f, GLsizei s, const GLvoid *d) { if(g_gl_context) g_gl_context->dispatch.glCompressedTexSubImage1D(t, l, o, w, f, s, d); }
void glCompressedTexSubImage3D(GLenum t, GLint l, GLint o, GLint y, GLint z, GLsizei w, GLsizei h, GLsizei d, GLenum f, GLsizei s, const GLvoid *x) { if(g_gl_context) g_gl_context->dispatch.glCompressedTexSubImage3D(t, l, o, y, z, w, h, d, f, s, x); }
void glGetTexLevelParameteriv(GLenum t, GLint l, GLenum n, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetTexLevelParameteriv(t, l, n, v); }
void glGetTexLevelParameterfv(GLenum t, GLint l, GLenum n, GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glGetTexLevelParameterfv(t, l, n, v); }
void glGetActiveUniformBlockiv(GLuint p, GLuint i, GLenum n, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetActiveUniformBlockiv(p, i, n, v); }
void glGetActiveUniformBlockName(GLuint p, GLuint i, GLsizei s, GLsizei *l, GLchar *n) { if(g_gl_context) g_gl_context->dispatch.glGetActiveUniformBlockName(p, i, s, l, n); }
void glGetActiveUniformsiv(GLuint p, GLsizei c, const GLuint *i, GLenum n, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetActiveUniformsiv(p, c, i, n, v); }
void glGetActiveUniformName(GLuint p, GLuint i, GLsizei s, GLsizei *l, GLchar *n) { if(g_gl_context) g_gl_context->dispatch.glGetActiveUniformName(p, i, s, l, n); }
void glDrawArrays(GLenum m, GLint f, GLsizei c) { if(g_gl_context) g_gl_context->dispatch.glDrawArrays(m, f, c); }
void glDrawArraysInstanced(GLenum m, GLint f, GLsizei c, GLsizei i) { if(g_gl_context) g_gl_context->dispatch.glDrawArraysInstanced(m, f, c, i); }
void glDrawElements(GLenum m, GLsizei c, GLenum t, const GLvoid *i) { if(g_gl_context) g_gl_context->dispatch.glDrawElements(m, c, t, i); }
void glDrawElementsInstanced(GLenum m, GLsizei c, GLenum t, const GLvoid *i, GLsizei s) { if(g_gl_context) g_gl_context->dispatch.glDrawElementsInstanced(m, c, t, i, s); }
void glBlendFunc(GLenum s, GLenum d) { if(g_gl_context) g_gl_context->dispatch.glBlendFunc(s, d); }
void glBlendEquation(GLenum m) { if(g_gl_context) g_gl_context->dispatch.glBlendEquation(m); }
void glBlendEquationSeparate(GLenum r, GLenum a) { if(g_gl_context) g_gl_context->dispatch.glBlendEquationSeparate(r, a); }
void glBlendFuncSeparate(GLenum r, GLenum d, GLenum a, GLenum e) { if(g_gl_context) g_gl_context->dispatch.glBlendFuncSeparate(r, d, a, e); }
void glBlendColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) { if(g_gl_context) g_gl_context->dispatch.glBlendColor(r, g, b, a); }
void glDepthFunc(GLenum f) { if(g_gl_context) g_gl_context->dispatch.glDepthFunc(f); }
void glDepthMask(GLboolean f) { if(g_gl_context) g_gl_context->dispatch.glDepthMask(f); }
void glDepthRange(GLclampd n, GLclampd f) { if(g_gl_context) g_gl_context->dispatch.glDepthRange(n, f); }
void glStencilFunc(GLenum f, GLint r, GLuint m) { if(g_gl_context) g_gl_context->dispatch.glStencilFunc(f, r, m); }
void glStencilFuncSeparate(GLenum f, GLenum n, GLint r, GLuint m) { if(g_gl_context) g_gl_context->dispatch.glStencilFuncSeparate(f, n, r, m); }
void glStencilOp(GLenum f, GLenum z, GLenum p) { if(g_gl_context) g_gl_context->dispatch.glStencilOp(f, z, p); }
void glStencilOpSeparate(GLenum f, GLenum a, GLenum z, GLenum p) { if(g_gl_context) g_gl_context->dispatch.glStencilOpSeparate(f, a, z, p); }
void glStencilMask(GLuint m) { if(g_gl_context) g_gl_context->dispatch.glStencilMask(m); }
void glStencilMaskSeparate(GLenum f, GLuint m) { if(g_gl_context) g_gl_context->dispatch.glStencilMaskSeparate(f, m); }
void glCullFace(GLenum m) { if(g_gl_context) g_gl_context->dispatch.glCullFace(m); }
void glFrontFace(GLenum m) { if(g_gl_context) g_gl_context->dispatch.glFrontFace(m); }
void glPolygonMode(GLenum f, GLenum m) { if(g_gl_context) g_gl_context->dispatch.glPolygonMode(f, m); }
void glPolygonOffset(GLfloat f, GLfloat u) { if(g_gl_context) g_gl_context->dispatch.glPolygonOffset(f, u); }
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) { if(g_gl_context) g_gl_context->dispatch.glViewport(x, y, w, h); }
void glScissor(GLint x, GLint y, GLsizei w, GLsizei h) { if(g_gl_context) g_gl_context->dispatch.glScissor(x, y, w, h); }
void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) { if(g_gl_context) g_gl_context->dispatch.glColorMask(r, g, b, a); }
void glLineWidth(GLfloat w) { if(g_gl_context) g_gl_context->dispatch.glLineWidth(w); }
void glPixelStorei(GLenum n, GLint p) { if(g_gl_context) g_gl_context->dispatch.glPixelStorei(n, p); }
void glPrimitiveRestartIndex(GLuint i) { if(g_gl_context) g_gl_context->dispatch.glPrimitiveRestartIndex(i); }
void glReleaseShaderCompiler(void) { _gl_ReleaseShaderCompiler(); }
void glShaderBinary(GLsizei count, const GLuint *shaders, GLenum binaryFormat, const GLvoid *binary, GLsizei length) { _gl_ShaderBinary(count, shaders, binaryFormat, binary, length); }
void glGetShaderPrecisionFormat(GLenum shadertype, GLenum precisiontype, GLint *range, GLint *precision) { _gl_GetShaderPrecisionFormat(shadertype, precisiontype, range, precision); }
void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid *data) { _gl_CompressedTexImage2D(target, level, internalformat, width, height, border, imageSize, data); }
void glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border) { _gl_CopyTexImage2D(target, level, internalformat, x, y, width, height, border); }
void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height) { _gl_CopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width, height); }
void glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const GLvoid *data) { _gl_CompressedTexSubImage2D(target, level, xoffset, yoffset, width, height, format, imageSize, data); }
void glHint(GLenum target, GLenum mode) { _gl_Hint(target, mode); }
void glSampleCoverage(GLclampf value, GLboolean invert) { _gl_SampleCoverage(value, invert); }
void glDepthRangef(GLclampf n, GLclampf f) { _gl_DepthRange((GLclampd)n, (GLclampd)f); }
void glWiiULoadShaderGroup(GLuint p, const void *g) { _gl_WiiULoadShaderGroup(p, g); }
void glWiiULoadShaderGroupGFD(GLuint p, GLuint i, const void *d) { _gl_WiiULoadShaderGroupGFD(p, i, d); }
void glVertexAttrib1f(GLuint i, GLfloat x) { _gl_VertexAttrib1f(i, x); }
void glVertexAttrib2f(GLuint i, GLfloat x, GLfloat y) { _gl_VertexAttrib2f(i, x, y); }
void glVertexAttrib3f(GLuint i, GLfloat x, GLfloat y, GLfloat z) { _gl_VertexAttrib3f(i, x, y, z); }
void glVertexAttrib4f(GLuint i, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { _gl_VertexAttrib4f(i, x, y, z, w); }
void glVertexAttrib1fv(GLuint i, const GLfloat *v) { _gl_VertexAttrib1fv(i, v); }
void glVertexAttrib2fv(GLuint i, const GLfloat *v) { _gl_VertexAttrib2fv(i, v); }
void glVertexAttrib3fv(GLuint i, const GLfloat *v) { _gl_VertexAttrib3fv(i, v); }
void glVertexAttrib4fv(GLuint i, const GLfloat *v) { _gl_VertexAttrib4fv(i, v); }



void glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *i) { _gl_DrawRangeElements(mode, start, end, count, type, i); }
void glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const GLvoid *i, GLint bv) { _gl_DrawElementsBaseVertex(mode, count, type, i, bv); }
void glDrawRangeElementsBaseVertex(GLenum mode, GLuint s, GLuint e, GLsizei count, GLenum type, const GLvoid *i, GLint bv) { _gl_DrawRangeElementsBaseVertex(mode, s, e, count, type, i, bv); }
void glDrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type, const GLvoid *i, GLsizei ic, GLint bv) { _gl_DrawElementsInstancedBaseVertex(mode, count, type, i, ic, bv); }
void glMultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei dc) { _gl_MultiDrawArrays(mode, first, count, dc); }
void glMultiDrawElements(GLenum mode, const GLsizei *count, GLenum type, const GLvoid *const *i, GLsizei dc) { _gl_MultiDrawElements(mode, count, type, i, dc); }
void glMultiDrawElementsBaseVertex(GLenum mode, const GLsizei *count, GLenum type, const GLvoid *const *i, GLsizei dc, const GLint *bv) { _gl_MultiDrawElementsBaseVertex(mode, count, type, i, dc, bv); }


void glClearBufferiv(GLenum buffer, GLint drawbuffer, const GLint *value) {
    if (!g_gl_context) return;
    gl_flush_state();
    if (buffer == GL_STENCIL) {
        GX2DepthBuffer *db = gl_get_draw_depth_buffer();
        if (db) GX2ClearDepthStencilEx(db, db->depthClear, (uint8_t)value[0], GX2_CLEAR_FLAGS_STENCIL);
    }
}
void glClearBufferuiv(GLenum buffer, GLint drawbuffer, const GLuint *value) { (void)buffer; (void)drawbuffer; (void)value; }
void glClearBufferfv(GLenum buffer, GLint drawbuffer, const GLfloat *value) {
    if (!g_gl_context) return;
    gl_flush_state();
    if (buffer == GL_COLOR) {
        GX2ColorBuffer *cb = gl_get_draw_color_buffer((GLuint)drawbuffer);
        if (cb) GX2ClearColor(cb, value[0], value[1], value[2], value[3]);
    } else if (buffer == GL_DEPTH) {
        GX2DepthBuffer *db = gl_get_draw_depth_buffer();
        if (db) GX2ClearDepthStencilEx(db, value[0], (uint8_t)db->stencilClear, GX2_CLEAR_FLAGS_DEPTH);
    }
}
void glClearBufferfi(GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil) {
    (void)buffer; (void)drawbuffer;
    if (!g_gl_context) return;
    gl_flush_state();
    GX2DepthBuffer *db = gl_get_draw_depth_buffer();
    if (db) GX2ClearDepthStencilEx(db, depth, (uint8_t)stencil, GX2_CLEAR_FLAGS_BOTH);
}


void glEnablei(GLenum cap, GLuint index) { (void)index; glEnable(cap); }
void glDisablei(GLenum cap, GLuint index) { (void)index; glDisable(cap); }
GLboolean glIsEnabledi(GLenum cap, GLuint index) { (void)index; return glIsEnabled(cap); }
void glColorMaski(GLuint index, GLboolean r, GLboolean g, GLboolean b, GLboolean a) { (void)index; glColorMask(r,g,b,a); }


void glGetIntegeri_v(GLenum target, GLuint index, GLint *data) {
    if (!g_gl_context || !data) return;
    if (target == GL_UNIFORM_BUFFER_BINDING && index < GL33_MAX_UNIFORM_BUFFER_BINDINGS)
        *data = (GLint)g_gl_context->uniform_buffer_bindings[index].buffer;
    else
        *data = 0;
}
void glGetInteger64v(GLenum pname, GLint64 *data) {
    if (!data) return;
    GLint v = 0; glGetIntegerv(pname, &v); *data = (GLint64)v;
}
void glGetInteger64i_v(GLenum target, GLuint index, GLint64 *data) {
    if (!data) return; GLint v = 0; glGetIntegeri_v(target, index, &v); *data = (GLint64)v;
}
void glGetBufferParameteri64v(GLenum target, GLenum pname, GLint64 *params) {
    if (!params) return; GLint v = 0; glGetBufferParameteriv(target, pname, &v); *params = (GLint64)v;
}


void glCopyBufferSubData(GLenum readTarget, GLenum writeTarget, GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size) {
    void *src = glMapBufferRange(readTarget, readOffset, size, GL_MAP_READ_BIT);
    if (!src) return;
    void *tmp = malloc((size_t)size);
    if (tmp) { memcpy(tmp, src, (size_t)size); glUnmapBuffer(readTarget); }
    else { glUnmapBuffer(readTarget); return; }
    void *dst = glMapBufferRange(writeTarget, writeOffset, size, GL_MAP_WRITE_BIT);
    if (dst) memcpy(dst, tmp, (size_t)size);
    glUnmapBuffer(writeTarget);
    free(tmp);
}


void glGetUniformIndices(GLuint program, GLsizei count, const GLchar *const *names, GLuint *indices) {
    for (GLsizei i = 0; i < count; i++) indices[i] = GL_INVALID_INDEX;
    (void)program; (void)names;
}


void glFramebufferTextureLayer(GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer) {
    (void)layer; glFramebufferTexture2D(target, attachment, GL_TEXTURE_2D, texture, level);
}


void glBindFragDataLocation(GLuint program, GLuint color, const GLchar *name) { (void)program; (void)color; (void)name; }
GLint glGetFragDataLocation(GLuint program, const GLchar *name) { (void)program; (void)name; return -1; }
void glBindFragDataLocationIndexed(GLuint program, GLuint colorNumber, GLuint index, const GLchar *name) { (void)program; (void)colorNumber; (void)index; (void)name; }
GLint glGetFragDataIndex(GLuint program, const GLchar *name) { (void)program; (void)name; return -1; }


void glBeginTransformFeedback(GLenum primitiveMode) { (void)primitiveMode; _gl_set_error(GL_INVALID_OPERATION); }
void glEndTransformFeedback(void) { _gl_set_error(GL_INVALID_OPERATION); }
void glPauseTransformFeedback(void) {}
void glResumeTransformFeedback(void) {}
void glBindTransformFeedback(GLenum target, GLuint id) { (void)target; (void)id; }
GLboolean glIsTransformFeedback(GLuint id) { (void)id; return GL_FALSE; }
void glTransformFeedbackVaryings(GLuint p, GLsizei c, const GLchar *const *v, GLenum m) { (void)p;(void)c;(void)v;(void)m; }
void glGetTransformFeedbackVarying(GLuint p, GLuint i, GLsizei bs, GLsizei *l, GLsizei *s, GLenum *t, GLchar *n) { (void)p;(void)i;(void)bs; if(l)*l=0; if(n)n[0]='\0'; (void)s;(void)t; }
void glDrawTransformFeedback(GLenum mode, GLuint id) { (void)id; glDrawArrays(mode, 0, 0); }
void glClampColor(GLenum target, GLenum clamp) { (void)target; (void)clamp; }
void glProvokingVertex(GLenum mode) { (void)mode; }


struct __GLsync { int dummy; };
static struct __GLsync g_sync_sentinel = {1};
GLsync glFenceSync(GLenum condition, GLbitfield flags) { (void)condition; (void)flags; GX2DrawDone(); return &g_sync_sentinel; }
GLboolean glIsSync(GLsync sync) { return (sync == &g_sync_sentinel) ? GL_TRUE : GL_FALSE; }
void glDeleteSync(GLsync sync) { (void)sync; }
GLenum glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) { (void)sync; (void)flags; (void)timeout; GX2DrawDone(); return GL_ALREADY_SIGNALED; }
void glWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) { (void)sync; (void)flags; (void)timeout; GX2DrawDone(); }
void glGetSynciv(GLsync sync, GLenum pname, GLsizei bufSize, GLsizei *length, GLint *values) {
    (void)sync; (void)pname; (void)bufSize;
    if (length) *length = 1;
    if (values && bufSize >= 1) values[0] = GL_SIGNALED;
}


void glGetMultisamplefv(GLenum pname, GLuint index, GLfloat *val) { (void)pname; (void)index; if(val){val[0]=0.5f;val[1]=0.5f;} }
void glSampleMaski(GLuint maskNumber, GLbitfield mask) { (void)maskNumber; (void)mask; }
void glTexImage2DMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei w, GLsizei h, GLboolean fixed) {
    (void)samples; (void)fixed; glTexImage2D(target, 0, internalformat, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
}
void glTexImage3DMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei w, GLsizei h, GLsizei d, GLboolean fixed) {
    (void)samples; (void)fixed; glTexImage3D(target, 0, internalformat, w, h, d, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
}


void glQueryCounter(GLuint id, GLenum target) { (void)id; (void)target; }
void glGetQueryObjecti64v(GLuint id, GLenum pname, GLint64 *params) { (void)id; (void)pname; if(params)*params=0; }
void glGetQueryObjectui64v(GLuint id, GLenum pname, GLuint64 *params) { (void)id; (void)pname; if(params)*params=0; }


void glVertexAttribP1ui(GLuint i, GLenum t, GLboolean n, GLuint v) { (void)t;(void)n; glVertexAttrib1f(i,(GLfloat)v); }
void glVertexAttribP2ui(GLuint i, GLenum t, GLboolean n, GLuint v) { (void)t;(void)n; glVertexAttrib2f(i,(GLfloat)v,0.0f); }
void glVertexAttribP3ui(GLuint i, GLenum t, GLboolean n, GLuint v) { (void)t;(void)n; glVertexAttrib3f(i,(GLfloat)v,0.0f,0.0f); }
void glVertexAttribP4ui(GLuint i, GLenum t, GLboolean n, GLuint v) { (void)t;(void)n; glVertexAttrib4f(i,(GLfloat)v,0.0f,0.0f,1.0f); }
void glVertexAttribP1uiv(GLuint i, GLenum t, GLboolean n, const GLuint *v) { if(v) glVertexAttribP1ui(i,t,n,*v); }
void glVertexAttribP2uiv(GLuint i, GLenum t, GLboolean n, const GLuint *v) { if(v) glVertexAttribP2ui(i,t,n,*v); }
void glVertexAttribP3uiv(GLuint i, GLenum t, GLboolean n, const GLuint *v) { if(v) glVertexAttribP3ui(i,t,n,*v); }
void glVertexAttribP4uiv(GLuint i, GLenum t, GLboolean n, const GLuint *v) { if(v) glVertexAttribP4ui(i,t,n,*v); }


void glTexBuffer(GLenum target, GLenum internalformat, GLuint buffer) { (void)target;(void)internalformat;(void)buffer; _gl_set_error(GL_INVALID_OPERATION); }


void glFramebufferTexture1D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) { (void)target;(void)attachment;(void)textarget;(void)texture;(void)level; }
void glFramebufferTexture3D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLint zoffset) { (void)target;(void)attachment;(void)textarget;(void)texture;(void)level;(void)zoffset; }
void glGetBooleani_v(GLenum target, GLuint index, GLboolean *data) {
    GLint v = 0; glGetIntegeri_v(target, index, &v); if (data) *data = v ? GL_TRUE : GL_FALSE;
}
void glGetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, GLvoid *data) { _gl_GetBufferSubData(target, offset, size, data); }
void glGetCompressedTexImage(GLenum target, GLint level, GLvoid *img) { (void)target;(void)level;(void)img; }
void glGetPointerv(GLenum pname, GLvoid **params) {
    if (!params) return;
    if (pname == 0x8449) { *params = NULL; return; }
    GLuint idx = (pname >= 0x8626 && pname <= 0x8641) ? pname - 0x8626 : 0;
    GLfloat dummy[4] = {0,0,0,0};
    glGetVertexAttribfv(idx, 0x8645, dummy);
    *params = NULL;
}
void glGetSamplerParameterIiv(GLuint sampler, GLenum pname, GLint *params) { _gl_GetSamplerParameterIiv(sampler, pname, params); }
void glGetSamplerParameterIuiv(GLuint sampler, GLenum pname, GLuint *params) { _gl_GetSamplerParameterIuiv(sampler, pname, params); }

void glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels) { (void)target;(void)level;(void)format;(void)type;(void)pixels; }
void glGetTexParameterIiv(GLenum target, GLenum pname, GLint *params) { _gl_GetTexParameterIiv(target, pname, params); }
void glGetTexParameterIuiv(GLenum target, GLenum pname, GLuint *params) { _gl_GetTexParameterIuiv(target, pname, params); }
void glGetVertexAttribdv(GLuint index, GLenum pname, GLdouble *params) {
    GLfloat v[4] = {0,0,0,0}; glGetVertexAttribfv(index, pname, v); if (params) *params = (GLdouble)v[0];
}
void glPixelStoref(GLenum pname, GLfloat param) { glPixelStorei(pname, (GLint)param); }
void glPointParameterf(GLenum pname, GLfloat param) { (void)pname;(void)param; }
void glPointParameterfv(GLenum pname, const GLfloat *params) { if (params) glPointParameterf(pname, params[0]); }
void glPointParameteri(GLenum pname, GLint param) { glPointParameterf(pname, (GLfloat)param); }
void glPointParameteriv(GLenum pname, const GLint *params) { if (params) glPointParameterf(pname, (GLfloat)params[0]); }
void glSamplerParameterIiv(GLuint sampler, GLenum pname, const GLint *param) { _gl_SamplerParameterIiv(sampler, pname, param); }
void glSamplerParameterIuiv(GLuint sampler, GLenum pname, const GLuint *param) { _gl_SamplerParameterIuiv(sampler, pname, param); }
void glTexParameterIiv(GLenum target, GLenum pname, const GLint *params) { _gl_TexParameterIiv(target, pname, params); }
void glTexParameterIuiv(GLenum target, GLenum pname, const GLuint *params) { _gl_TexParameterIuiv(target, pname, params); }

void glVertexAttrib1d(GLuint index, GLdouble x) { glVertexAttrib1f(index, (GLfloat)x); }
void glVertexAttrib1dv(GLuint index, const GLdouble *v) { if(v) glVertexAttrib1f(index, (GLfloat)v[0]); }
void glVertexAttrib1s(GLuint index, GLshort x) { glVertexAttrib1f(index, (GLfloat)x); }
void glVertexAttrib1sv(GLuint index, const GLshort *v) { if(v) glVertexAttrib1f(index, (GLfloat)v[0]); }
void glVertexAttrib2d(GLuint index, GLdouble x, GLdouble y) { glVertexAttrib2f(index, (GLfloat)x, (GLfloat)y); }
void glVertexAttrib2dv(GLuint index, const GLdouble *v) { if(v) glVertexAttrib2f(index, (GLfloat)v[0], (GLfloat)v[1]); }
void glVertexAttrib2s(GLuint index, GLshort x, GLshort y) { glVertexAttrib2f(index, (GLfloat)x, (GLfloat)y); }
void glVertexAttrib2sv(GLuint index, const GLshort *v) { if(v) glVertexAttrib2f(index, (GLfloat)v[0], (GLfloat)v[1]); }
void glVertexAttrib3d(GLuint index, GLdouble x, GLdouble y, GLdouble z) { glVertexAttrib3f(index, (GLfloat)x, (GLfloat)y, (GLfloat)z); }
void glVertexAttrib3dv(GLuint index, const GLdouble *v) { if(v) glVertexAttrib3f(index, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2]); }
void glVertexAttrib3s(GLuint index, GLshort x, GLshort y, GLshort z) { glVertexAttrib3f(index, (GLfloat)x, (GLfloat)y, (GLfloat)z); }
void glVertexAttrib3sv(GLuint index, const GLshort *v) { if(v) glVertexAttrib3f(index, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2]); }
void glVertexAttrib4Nbv(GLuint index, const GLbyte *v) { if(v) glVertexAttrib4f(index, v[0]/127.0f, v[1]/127.0f, v[2]/127.0f, v[3]/127.0f); }
void glVertexAttrib4Niv(GLuint index, const GLint *v) { if(v) glVertexAttrib4f(index, v[0]/2147483647.0f, v[1]/2147483647.0f, v[2]/2147483647.0f, v[3]/2147483647.0f); }
void glVertexAttrib4Nsv(GLuint index, const GLshort *v) { if(v) glVertexAttrib4f(index, v[0]/32767.0f, v[1]/32767.0f, v[2]/32767.0f, v[3]/32767.0f); }
void glVertexAttrib4Nub(GLuint index, GLubyte x, GLubyte y, GLubyte z, GLubyte w) { glVertexAttrib4f(index, x/255.0f, y/255.0f, z/255.0f, w/255.0f); }
void glVertexAttrib4Nubv(GLuint index, const GLubyte *v) { if(v) glVertexAttrib4f(index, v[0]/255.0f, v[1]/255.0f, v[2]/255.0f, v[3]/255.0f); }
void glVertexAttrib4Nuiv(GLuint index, const GLuint *v) { if(v) glVertexAttrib4f(index, v[0]/4294967295.0f, v[1]/4294967295.0f, v[2]/4294967295.0f, v[3]/4294967295.0f); }
void glVertexAttrib4Nusv(GLuint index, const GLushort *v) { if(v) glVertexAttrib4f(index, v[0]/65535.0f, v[1]/65535.0f, v[2]/65535.0f, v[3]/65535.0f); }
void glVertexAttrib4bv(GLuint index, const GLbyte *v) { if(v) glVertexAttrib4f(index, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]); }
void glVertexAttrib4d(GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w) { glVertexAttrib4f(index, (GLfloat)x, (GLfloat)y, (GLfloat)z, (GLfloat)w); }
void glVertexAttrib4dv(GLuint index, const GLdouble *v) { if(v) glVertexAttrib4f(index, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]); }
void glVertexAttrib4iv(GLuint index, const GLint *v) { if(v) glVertexAttrib4f(index, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]); }
void glVertexAttrib4s(GLuint index, GLshort x, GLshort y, GLshort z, GLshort w) { glVertexAttrib4f(index, (GLfloat)x, (GLfloat)y, (GLfloat)z, (GLfloat)w); }
void glVertexAttrib4sv(GLuint index, const GLshort *v) { if(v) glVertexAttrib4f(index, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]); }
void glVertexAttrib4ubv(GLuint index, const GLubyte *v) { if(v) glVertexAttrib4f(index, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]); }
void glVertexAttrib4uiv(GLuint index, const GLuint *v) { if(v) glVertexAttrib4f(index, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]); }
void glVertexAttrib4usv(GLuint index, const GLushort *v) { if(v) glVertexAttrib4f(index, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]); }
void glVertexAttribI4bv(GLuint index, const GLbyte *v) { if(v) glVertexAttribI4i(index, v[0], v[1], v[2], v[3]); }
void glVertexAttribI4sv(GLuint index, const GLshort *v) { if(v) glVertexAttribI4i(index, v[0], v[1], v[2], v[3]); }
void glVertexAttribI4ubv(GLuint index, const GLubyte *v) { if(v) glVertexAttribI4ui(index, v[0], v[1], v[2], v[3]); }
void glVertexAttribI4usv(GLuint index, const GLushort *v) { if(v) glVertexAttribI4ui(index, v[0], v[1], v[2], v[3]); }

#ifdef __cplusplus
}
#endif
