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
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

gl_context_t *g_gl_context = NULL;

// Default no-op dispatch
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

static void gl_context_init_defaults(gl_context_t *ctx) {
  GX2ColorBuffer *tv_color;

  if (!ctx) {
    return;
  }

  ctx->active_texture = 0;
  ctx->blend_src_rgb = GL_ONE;
  ctx->blend_dst_rgb = GL_ZERO;
  ctx->blend_src_alpha = GL_ONE;
  ctx->blend_dst_alpha = GL_ZERO;
  ctx->blend_eq_rgb = GL_FUNC_ADD;
  ctx->blend_eq_alpha = GL_FUNC_ADD;
  ctx->blend_color[0] = 0.0f;
  ctx->blend_color[1] = 0.0f;
  ctx->blend_color[2] = 0.0f;
  ctx->blend_color[3] = 0.0f;
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
  ctx->stencil_ref[0] = 0;
  ctx->stencil_ref[1] = 0;
  ctx->cull_face_mode = GL_BACK;
  ctx->front_face = GL_CCW;
  ctx->polygon_mode = GL_FILL;
  ctx->polygon_offset_factor = 0.0f;
  ctx->polygon_offset_units = 0.0f;
  ctx->color_mask[0] = GL_TRUE;
  ctx->color_mask[1] = GL_TRUE;
  ctx->color_mask[2] = GL_TRUE;
  ctx->color_mask[3] = GL_TRUE;
  ctx->line_width = 1.0f;
  for (uint32_t i = 0; i < GL33_MAX_VERTEX_ATTRIBS; ++i) {
    ctx->current_vertex_attrib[i][0] = 0.0f;
    ctx->current_vertex_attrib[i][1] = 0.0f;
    ctx->current_vertex_attrib[i][2] = 0.0f;
    ctx->current_vertex_attrib[i][3] = 1.0f;
  }
  ctx->clear_color[0] = 0.0f;
  ctx->clear_color[1] = 0.0f;
  ctx->clear_color[2] = 0.0f;
  ctx->clear_color[3] = 0.0f;
  ctx->clear_depth = 1.0f;
  ctx->clear_stencil = 0;
  ctx->blend_enabled = GL_FALSE;
  ctx->depth_test_enabled = GL_FALSE;
  ctx->stencil_test_enabled = GL_FALSE;
  ctx->cull_face_enabled = GL_FALSE;
  ctx->scissor_test_enabled = GL_FALSE;
  ctx->sample_coverage_enabled = GL_FALSE;
  ctx->polygon_offset_point_enabled = GL_FALSE;
  ctx->polygon_offset_line_enabled = GL_FALSE;
  ctx->polygon_offset_fill_enabled = GL_FALSE;
  ctx->sample_coverage_invert = GL_FALSE;
  ctx->sample_coverage_value = 1.0f;
  ctx->generate_mipmap_hint = GL_DONT_CARE;

  tv_color = WHBGfxGetTVColourBuffer();
  if (tv_color) {
    ctx->viewport.width = (GLsizei)tv_color->surface.width;
    ctx->viewport.height = (GLsizei)tv_color->surface.height;
    ctx->scissor.width = (GLsizei)tv_color->surface.width;
    ctx->scissor.height = (GLsizei)tv_color->surface.height;
  }

  ctx->dirty_flags = GL_DIRTY_BLEND | GL_DIRTY_DEPTH_STENCIL | GL_DIRTY_CULL |
                     GL_DIRTY_SCISSOR | GL_DIRTY_VIEWPORT |
                     GL_DIRTY_COLOR_MASK |
                     GL_DIRTY_FRONT_FACE | GL_DIRTY_POLYGON_MODE |
                     GL_DIRTY_FRAMEBUFFER;
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
  // Allocate context storage
  gl_context_t *ctx =
      (gl_context_t *)gl_mem_alloc(GL_MEM_TYPE_MEM2, sizeof(gl_context_t), 4);
  if (!ctx)
    return NULL;

  memset(ctx, 0, sizeof(gl_context_t));
  OSInitMutex(&ctx->error_mutex);
  gl_context_init_defaults(ctx);

  gl_buffer_init();
  gl_texture_init();
  gl_shader_init();
  gl_vao_init();
  gl_framebuffer_init();

  // Wire core dispatch
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
  ctx->dispatch.glBindAttribLocation = _gl_BindAttribLocation;
  ctx->dispatch.glGetAttachedShaders = _gl_GetAttachedShaders;
  ctx->dispatch.glGetActiveAttrib = _gl_GetActiveAttrib;
  ctx->dispatch.glGetActiveUniform = _gl_GetActiveUniform;
  ctx->dispatch.glLinkProgram = _gl_LinkProgram;
  ctx->dispatch.glValidateProgram = _gl_ValidateProgram;
  ctx->dispatch.glUseProgram = _gl_UseProgram;
  ctx->dispatch.glGetShaderiv = _gl_GetShaderiv;
  ctx->dispatch.glGetProgramiv = _gl_GetProgramiv;
  ctx->dispatch.glGetShaderInfoLog = _gl_GetShaderInfoLog;
  ctx->dispatch.glGetProgramInfoLog = _gl_GetProgramInfoLog;
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
  ctx->dispatch.glGetAttribLocation = _gl_GetAttribLocation;
  ctx->dispatch.glGetUniformBlockIndex = _gl_GetUniformBlockIndex;
  ctx->dispatch.glUniformBlockBinding = _gl_UniformBlockBinding;
  ctx->dispatch.glWiiULoadShaderGroup = _gl_WiiULoadShaderGroup;
  ctx->dispatch.glWiiULoadShaderGroupGFD = _gl_WiiULoadShaderGroupGFD;
  
  ctx->dispatch.glGenVertexArrays = _gl_GenVertexArrays;
  ctx->dispatch.glDeleteVertexArrays = _gl_DeleteVertexArrays;
  ctx->dispatch.glIsVertexArray = _gl_IsVertexArray;
  ctx->dispatch.glBindVertexArray = _gl_BindVertexArray;
  ctx->dispatch.glEnableVertexAttribArray = _gl_EnableVertexAttribArray;
  ctx->dispatch.glDisableVertexAttribArray = _gl_DisableVertexAttribArray;
  ctx->dispatch.glGetVertexAttribiv = _gl_GetVertexAttribiv;
  ctx->dispatch.glGetVertexAttribPointerv = _gl_GetVertexAttribPointerv;
  ctx->dispatch.glVertexAttribPointer = _gl_VertexAttribPointer;
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
  ctx->dispatch.glFramebufferTexture2D = _gl_FramebufferTexture2D;
  ctx->dispatch.glFramebufferRenderbuffer = _gl_FramebufferRenderbuffer;
  ctx->dispatch.glRenderbufferStorage = _gl_RenderbufferStorage;
  ctx->dispatch.glGetRenderbufferParameteriv = _gl_GetRenderbufferParameteriv;
  ctx->dispatch.glGetFramebufferAttachmentParameteriv =
      _gl_GetFramebufferAttachmentParameteriv;
  ctx->dispatch.glDrawBuffer = _gl_DrawBuffer;
  ctx->dispatch.glDrawBuffers = _gl_DrawBuffers;
  ctx->dispatch.glReadBuffer = _gl_ReadBuffer;
  ctx->dispatch.glReadPixels = _gl_ReadPixels;

  ctx->dispatch.glDrawArrays = _gl_DrawArrays;
  ctx->dispatch.glDrawArraysInstanced = _gl_DrawArraysInstanced;
  ctx->dispatch.glDrawElements = _gl_DrawElements;
  ctx->dispatch.glDrawElementsInstanced = _gl_DrawElementsInstanced;

  // Wire state dispatch
  ctx->dispatch.glEnable = _gl_Enable;
  ctx->dispatch.glDisable = _gl_Disable;
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
  
  ctx->dispatch.glGetString = _gl_GetString;
  ctx->dispatch.glGetStringi = _gl_GetStringi;
  ctx->dispatch.glGetBooleanv = _gl_GetBooleanv;
  ctx->dispatch.glGetDoublev = _gl_GetDoublev;
  ctx->dispatch.glGetIntegerv = _gl_GetIntegerv;
  ctx->dispatch.glGetFloatv = _gl_GetFloatv;

  return ctx;
}

void gl_context_destroy(gl_context_t *ctx) {
  if (!ctx)
    return;
  gl_mem_free(GL_MEM_TYPE_MEM2, ctx);
}

// Public API wrappers
void glGenBuffers(GLsizei n, GLuint *buffers) {
  if (g_gl_context)
    g_gl_context->dispatch.glGenBuffers(n, buffers);
}
void glDeleteBuffers(GLsizei n, const GLuint *buffers) {
  if (g_gl_context)
    g_gl_context->dispatch.glDeleteBuffers(n, buffers);
}
GLboolean glIsBuffer(GLuint buffer) {
  return g_gl_context ? g_gl_context->dispatch.glIsBuffer(buffer) : GL_FALSE;
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
void glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params) {
  if (g_gl_context)
    g_gl_context->dispatch.glGetBufferParameteriv(target, pname, params);
}
void glGetBufferPointerv(GLenum target, GLenum pname, GLvoid **params) {
  if (g_gl_context)
    g_gl_context->dispatch.glGetBufferPointerv(target, pname, params);
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
void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha) {
  if (g_gl_context)
    g_gl_context->dispatch.glClearColor(red, green, blue, alpha);
}
void glClearDepth(GLclampd depth) {
  if (g_gl_context)
    g_gl_context->dispatch.glClearDepth(depth);
}
void glClearDepthf(GLclampf depth) { glClearDepth((GLclampd)depth); }
void glClearStencil(GLint s) {
  if (g_gl_context)
    g_gl_context->dispatch.glClearStencil(s);
}
void glClear(GLbitfield mask) {
  if (g_gl_context)
    g_gl_context->dispatch.glClear(mask);
}
void glGenTextures(GLsizei n, GLuint *textures) { if(g_gl_context) g_gl_context->dispatch.glGenTextures(n, textures); }
void glDeleteTextures(GLsizei n, const GLuint *textures) { if(g_gl_context) g_gl_context->dispatch.glDeleteTextures(n, textures); }
GLboolean glIsTexture(GLuint texture) { return g_gl_context ? g_gl_context->dispatch.glIsTexture(texture) : GL_FALSE; }
void glGenSamplers(GLsizei n, GLuint *samplers) { if(g_gl_context) g_gl_context->dispatch.glGenSamplers(n, samplers); }
void glDeleteSamplers(GLsizei n, const GLuint *samplers) { if(g_gl_context) g_gl_context->dispatch.glDeleteSamplers(n, samplers); }
GLboolean glIsSampler(GLuint sampler) { return g_gl_context ? g_gl_context->dispatch.glIsSampler(sampler) : GL_FALSE; }
void glBindTexture(GLenum target, GLuint texture) { if(g_gl_context) g_gl_context->dispatch.glBindTexture(target, texture); }
void glBindSampler(GLuint unit, GLuint sampler) { if(g_gl_context) g_gl_context->dispatch.glBindSampler(unit, sampler); }
void glActiveTexture(GLenum texture) { if(g_gl_context) g_gl_context->dispatch.glActiveTexture(texture); }
void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels) { if(g_gl_context) g_gl_context->dispatch.glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels); }
void glTexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid *pixels) { if(g_gl_context) g_gl_context->dispatch.glTexImage3D(target, level, internalformat, width, height, depth, border, format, type, pixels); }
void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels) { if(g_gl_context) g_gl_context->dispatch.glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels); }
void glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const GLvoid *pixels) { if(g_gl_context) g_gl_context->dispatch.glTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels); }
void glTexParameteri(GLenum target, GLenum pname, GLint param) { if(g_gl_context) g_gl_context->dispatch.glTexParameteri(target, pname, param); }
void glTexParameterf(GLenum target, GLenum pname, GLfloat param) { if(g_gl_context) g_gl_context->dispatch.glTexParameterf(target, pname, param); }
void glTexParameteriv(GLenum target, GLenum pname, const GLint *params) { if(g_gl_context) g_gl_context->dispatch.glTexParameteriv(target, pname, params); }
void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params) { if(g_gl_context) g_gl_context->dispatch.glTexParameterfv(target, pname, params); }
void glGetTexParameteriv(GLenum target, GLenum pname, GLint *params) { if(g_gl_context) g_gl_context->dispatch.glGetTexParameteriv(target, pname, params); }
void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params) { if(g_gl_context) g_gl_context->dispatch.glGetTexParameterfv(target, pname, params); }
void glSamplerParameteriv(GLuint sampler, GLenum pname, const GLint *param) { if(g_gl_context) g_gl_context->dispatch.glSamplerParameteriv(sampler, pname, param); }
void glSamplerParameterfv(GLuint sampler, GLenum pname, const GLfloat *param) { if(g_gl_context) g_gl_context->dispatch.glSamplerParameterfv(sampler, pname, param); }
void glSamplerParameteri(GLuint sampler, GLenum pname, GLint param) { if(g_gl_context) g_gl_context->dispatch.glSamplerParameteri(sampler, pname, param); }
void glSamplerParameterf(GLuint sampler, GLenum pname, GLfloat param) { if(g_gl_context) g_gl_context->dispatch.glSamplerParameterf(sampler, pname, param); }
void glGetSamplerParameteriv(GLuint sampler, GLenum pname, GLint *params) { if(g_gl_context) g_gl_context->dispatch.glGetSamplerParameteriv(sampler, pname, params); }
void glGetSamplerParameterfv(GLuint sampler, GLenum pname, GLfloat *params) { if(g_gl_context) g_gl_context->dispatch.glGetSamplerParameterfv(sampler, pname, params); }
void glGenerateMipmap(GLenum target) { if(g_gl_context) g_gl_context->dispatch.glGenerateMipmap(target); }

GLuint glCreateShader(GLenum type) { return g_gl_context ? g_gl_context->dispatch.glCreateShader(type) : 0; }
void glDeleteShader(GLuint shader) { if(g_gl_context) g_gl_context->dispatch.glDeleteShader(shader); }
GLboolean glIsShader(GLuint shader) { return g_gl_context ? g_gl_context->dispatch.glIsShader(shader) : GL_FALSE; }
void glShaderSource(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length) { if(g_gl_context) g_gl_context->dispatch.glShaderSource(shader, count, string, length); }
void glGetShaderSource(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *source) { if(g_gl_context) g_gl_context->dispatch.glGetShaderSource(shader, bufSize, length, source); }
void glCompileShader(GLuint shader) { if(g_gl_context) g_gl_context->dispatch.glCompileShader(shader); }
GLuint glCreateProgram(void) { return g_gl_context ? g_gl_context->dispatch.glCreateProgram() : 0; }
void glDeleteProgram(GLuint program) { if(g_gl_context) g_gl_context->dispatch.glDeleteProgram(program); }
GLboolean glIsProgram(GLuint program) { return g_gl_context ? g_gl_context->dispatch.glIsProgram(program) : GL_FALSE; }
void glAttachShader(GLuint program, GLuint shader) { if(g_gl_context) g_gl_context->dispatch.glAttachShader(program, shader); }
void glDetachShader(GLuint program, GLuint shader) { if(g_gl_context) g_gl_context->dispatch.glDetachShader(program, shader); }
void glBindAttribLocation(GLuint program, GLuint index, const GLchar *name) { if(g_gl_context) g_gl_context->dispatch.glBindAttribLocation(program, index, name); }
void glGetAttachedShaders(GLuint program, GLsizei maxCount, GLsizei *count, GLuint *shaders) { if(g_gl_context) g_gl_context->dispatch.glGetAttachedShaders(program, maxCount, count, shaders); }
void glGetActiveAttrib(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name) { if(g_gl_context) g_gl_context->dispatch.glGetActiveAttrib(program, index, bufSize, length, size, type, name); }
void glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name) { if(g_gl_context) g_gl_context->dispatch.glGetActiveUniform(program, index, bufSize, length, size, type, name); }
void glLinkProgram(GLuint program) { if(g_gl_context) g_gl_context->dispatch.glLinkProgram(program); }
void glValidateProgram(GLuint program) { if(g_gl_context) g_gl_context->dispatch.glValidateProgram(program); }
void glUseProgram(GLuint program) { if(g_gl_context) g_gl_context->dispatch.glUseProgram(program); }
void glGetShaderiv(GLuint shader, GLenum pname, GLint *params) { if(g_gl_context) g_gl_context->dispatch.glGetShaderiv(shader, pname, params); }
void glGetProgramiv(GLuint program, GLenum pname, GLint *params) { if(g_gl_context) g_gl_context->dispatch.glGetProgramiv(program, pname, params); }
void glGetShaderInfoLog(GLuint shader, GLsizei maxLength, GLsizei *length, GLchar *infoLog) { if(g_gl_context) g_gl_context->dispatch.glGetShaderInfoLog(shader, maxLength, length, infoLog); }
void glGetProgramInfoLog(GLuint program, GLsizei maxLength, GLsizei *length, GLchar *infoLog) { if(g_gl_context) g_gl_context->dispatch.glGetProgramInfoLog(program, maxLength, length, infoLog); }
void glGetUniformfv(GLuint program, GLint location, GLfloat *params) { if(g_gl_context) _gl_GetUniformfv(program, location, params); }
void glGetUniformiv(GLuint program, GLint location, GLint *params) { if(g_gl_context) _gl_GetUniformiv(program, location, params); }
void glUniform1f(GLint location, GLfloat v0) { if(g_gl_context) g_gl_context->dispatch.glUniform1f(location, v0); }
void glUniform1fv(GLint location, GLsizei count, const GLfloat *value) { if(g_gl_context) g_gl_context->dispatch.glUniform1fv(location, count, value); }
void glUniform1i(GLint location, GLint v0) { if(g_gl_context) g_gl_context->dispatch.glUniform1i(location, v0); }
void glUniform1iv(GLint location, GLsizei count, const GLint *value) { if(g_gl_context) _gl_Uniform1iv(location, count, value); }
void glUniform2f(GLint location, GLfloat v0, GLfloat v1) { if(g_gl_context) g_gl_context->dispatch.glUniform2f(location, v0, v1); }
void glUniform2fv(GLint location, GLsizei count, const GLfloat *value) { if(g_gl_context) g_gl_context->dispatch.glUniform2fv(location, count, value); }
void glUniform2i(GLint location, GLint v0, GLint v1) { if(g_gl_context) _gl_Uniform2i(location, v0, v1); }
void glUniform2iv(GLint location, GLsizei count, const GLint *value) { if(g_gl_context) _gl_Uniform2iv(location, count, value); }
void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) { if(g_gl_context) g_gl_context->dispatch.glUniform3f(location, v0, v1, v2); }
void glUniform3fv(GLint location, GLsizei count, const GLfloat *value) { if(g_gl_context) g_gl_context->dispatch.glUniform3fv(location, count, value); }
void glUniform3i(GLint location, GLint v0, GLint v1, GLint v2) { if(g_gl_context) _gl_Uniform3i(location, v0, v1, v2); }
void glUniform3iv(GLint location, GLsizei count, const GLint *value) { if(g_gl_context) _gl_Uniform3iv(location, count, value); }
void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) { if(g_gl_context) g_gl_context->dispatch.glUniform4f(location, v0, v1, v2, v3); }
void glUniform4fv(GLint location, GLsizei count, const GLfloat *value) { if(g_gl_context) g_gl_context->dispatch.glUniform4fv(location, count, value); }
void glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3) { if(g_gl_context) _gl_Uniform4i(location, v0, v1, v2, v3); }
void glUniform4iv(GLint location, GLsizei count, const GLint *value) { if(g_gl_context) _gl_Uniform4iv(location, count, value); }
void glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { _gl_UniformMatrix2fv(location, count, transpose, value); }
void glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { _gl_UniformMatrix3fv(location, count, transpose, value); }
void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { if(g_gl_context) g_gl_context->dispatch.glUniformMatrix4fv(location, count, transpose, value); }
GLint glGetUniformLocation(GLuint program, const GLchar *name) { return g_gl_context ? g_gl_context->dispatch.glGetUniformLocation(program, name) : -1; }
GLint glGetAttribLocation(GLuint program, const GLchar *name) { return g_gl_context ? g_gl_context->dispatch.glGetAttribLocation(program, name) : -1; }
GLuint glGetUniformBlockIndex(GLuint program, const GLchar *uniformBlockName) { return g_gl_context ? g_gl_context->dispatch.glGetUniformBlockIndex(program, uniformBlockName) : GL_INVALID_INDEX; }
void glUniformBlockBinding(GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding) { if(g_gl_context) g_gl_context->dispatch.glUniformBlockBinding(program, uniformBlockIndex, uniformBlockBinding); }
void glWiiULoadShaderGroup(GLuint program, const void* shaderGroup) { if(g_gl_context) g_gl_context->dispatch.glWiiULoadShaderGroup(program, shaderGroup); }
void glWiiULoadShaderGroupGFD(GLuint program, GLuint index, const void* gfdData) { if(g_gl_context) g_gl_context->dispatch.glWiiULoadShaderGroupGFD(program, index, gfdData); }

void glGenVertexArrays(GLsizei n, GLuint *arrays) { if(g_gl_context) g_gl_context->dispatch.glGenVertexArrays(n, arrays); }
void glDeleteVertexArrays(GLsizei n, const GLuint *arrays) { if(g_gl_context) g_gl_context->dispatch.glDeleteVertexArrays(n, arrays); }
GLboolean glIsVertexArray(GLuint array) { return g_gl_context ? g_gl_context->dispatch.glIsVertexArray(array) : GL_FALSE; }
void glBindVertexArray(GLuint array) { if(g_gl_context) g_gl_context->dispatch.glBindVertexArray(array); }
void glEnableVertexAttribArray(GLuint index) { if(g_gl_context) g_gl_context->dispatch.glEnableVertexAttribArray(index); }
void glDisableVertexAttribArray(GLuint index) { if(g_gl_context) g_gl_context->dispatch.glDisableVertexAttribArray(index); }
void glGetVertexAttribfv(GLuint index, GLenum pname, GLfloat *params) { if(g_gl_context) _gl_GetVertexAttribfv(index, pname, params); }
void glGetVertexAttribiv(GLuint index, GLenum pname, GLint *params) { if(g_gl_context) g_gl_context->dispatch.glGetVertexAttribiv(index, pname, params); }
void glGetVertexAttribPointerv(GLuint index, GLenum pname, GLvoid **pointer) { if(g_gl_context) g_gl_context->dispatch.glGetVertexAttribPointerv(index, pname, pointer); }
void glVertexAttrib1f(GLuint index, GLfloat x) { if(g_gl_context) _gl_VertexAttrib1f(index, x); }
void glVertexAttrib1fv(GLuint index, const GLfloat *v) { if(g_gl_context) _gl_VertexAttrib1fv(index, v); }
void glVertexAttrib2f(GLuint index, GLfloat x, GLfloat y) { if(g_gl_context) _gl_VertexAttrib2f(index, x, y); }
void glVertexAttrib2fv(GLuint index, const GLfloat *v) { if(g_gl_context) _gl_VertexAttrib2fv(index, v); }
void glVertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z) { if(g_gl_context) _gl_VertexAttrib3f(index, x, y, z); }
void glVertexAttrib3fv(GLuint index, const GLfloat *v) { if(g_gl_context) _gl_VertexAttrib3fv(index, v); }
void glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { if(g_gl_context) _gl_VertexAttrib4f(index, x, y, z, w); }
void glVertexAttrib4fv(GLuint index, const GLfloat *v) { if(g_gl_context) _gl_VertexAttrib4fv(index, v); }
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid *pointer) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribPointer(index, size, type, normalized, stride, pointer); }
void glVertexAttribDivisor(GLuint index, GLuint divisor) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribDivisor(index, divisor); }

void glGenFramebuffers(GLsizei n, GLuint *framebuffers) { if(g_gl_context) g_gl_context->dispatch.glGenFramebuffers(n, framebuffers); }
void glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers) { if(g_gl_context) g_gl_context->dispatch.glDeleteFramebuffers(n, framebuffers); }
GLboolean glIsFramebuffer(GLuint framebuffer) { return g_gl_context ? g_gl_context->dispatch.glIsFramebuffer(framebuffer) : GL_FALSE; }
void glGenRenderbuffers(GLsizei n, GLuint *renderbuffers) { if(g_gl_context) g_gl_context->dispatch.glGenRenderbuffers(n, renderbuffers); }
void glDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers) { if(g_gl_context) g_gl_context->dispatch.glDeleteRenderbuffers(n, renderbuffers); }
GLboolean glIsRenderbuffer(GLuint renderbuffer) { return g_gl_context ? g_gl_context->dispatch.glIsRenderbuffer(renderbuffer) : GL_FALSE; }
void glBindFramebuffer(GLenum target, GLuint framebuffer) { if(g_gl_context) g_gl_context->dispatch.glBindFramebuffer(target, framebuffer); }
void glBindRenderbuffer(GLenum target, GLuint renderbuffer) { if(g_gl_context) g_gl_context->dispatch.glBindRenderbuffer(target, renderbuffer); }
GLenum glCheckFramebufferStatus(GLenum target) { return g_gl_context ? g_gl_context->dispatch.glCheckFramebufferStatus(target) : GL_FRAMEBUFFER_UNSUPPORTED; }
void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) { if(g_gl_context) g_gl_context->dispatch.glFramebufferTexture2D(target, attachment, textarget, texture, level); }
void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) { if(g_gl_context) g_gl_context->dispatch.glFramebufferRenderbuffer(target, attachment, renderbuffertarget, renderbuffer); }
void glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) { if(g_gl_context) g_gl_context->dispatch.glRenderbufferStorage(target, internalformat, width, height); }
void glGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params) { if(g_gl_context) g_gl_context->dispatch.glGetRenderbufferParameteriv(target, pname, params); }
void glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname, GLint *params) { if(g_gl_context) g_gl_context->dispatch.glGetFramebufferAttachmentParameteriv(target, attachment, pname, params); }
void glDrawBuffer(GLenum buf) { if(g_gl_context) g_gl_context->dispatch.glDrawBuffer(buf); }
void glDrawBuffers(GLsizei n, const GLenum *bufs) { if(g_gl_context) g_gl_context->dispatch.glDrawBuffers(n, bufs); }
void glReadBuffer(GLenum src) { if(g_gl_context) g_gl_context->dispatch.glReadBuffer(src); }
void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type, GLvoid *pixels) {
  if (g_gl_context) {
    g_gl_context->dispatch.glReadPixels(x, y, width, height, format, type,
                                        pixels);
  }
}

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
GLboolean glIsEnabled(GLenum cap) {
  return g_gl_context ? g_gl_context->dispatch.glIsEnabled(cap) : GL_FALSE;
}

void glBlendFunc(GLenum sfactor, GLenum dfactor) {
  if (g_gl_context)
    g_gl_context->dispatch.glBlendFunc(sfactor, dfactor);
}
void glBlendEquation(GLenum mode) {
  if (g_gl_context)
    g_gl_context->dispatch.glBlendEquation(mode);
}
void glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha) {
  if (g_gl_context)
    g_gl_context->dispatch.glBlendEquationSeparate(modeRGB, modeAlpha);
}
void glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB,
                         GLenum sfactorAlpha, GLenum dfactorAlpha) {
  if (g_gl_context)
    g_gl_context->dispatch.glBlendFuncSeparate(sfactorRGB, dfactorRGB,
                                               sfactorAlpha, dfactorAlpha);
}
void glBlendColor(GLclampf red, GLclampf green, GLclampf blue,
                  GLclampf alpha) {
  if (g_gl_context)
    g_gl_context->dispatch.glBlendColor(red, green, blue, alpha);
}
void glDepthFunc(GLenum func) {
  if (g_gl_context)
    g_gl_context->dispatch.glDepthFunc(func);
}
void glDepthMask(GLboolean flag) {
  if (g_gl_context)
    g_gl_context->dispatch.glDepthMask(flag);
}
void glDepthRange(GLclampd nearVal, GLclampd farVal) {
  if (g_gl_context)
    g_gl_context->dispatch.glDepthRange(nearVal, farVal);
}
void glDepthRangef(GLclampf nearVal, GLclampf farVal) {
  glDepthRange((GLclampd)nearVal, (GLclampd)farVal);
}
void glStencilFunc(GLenum func, GLint ref, GLuint mask) {
  if (g_gl_context)
    g_gl_context->dispatch.glStencilFunc(func, ref, mask);
}
void glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask) {
  if (g_gl_context)
    g_gl_context->dispatch.glStencilFuncSeparate(face, func, ref, mask);
}
void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass) {
  if (g_gl_context)
    g_gl_context->dispatch.glStencilOp(fail, zfail, zpass);
}
void glStencilOpSeparate(GLenum face, GLenum fail, GLenum zfail,
                         GLenum zpass) {
  if (g_gl_context)
    g_gl_context->dispatch.glStencilOpSeparate(face, fail, zfail, zpass);
}
void glStencilMask(GLuint mask) {
  if (g_gl_context)
    g_gl_context->dispatch.glStencilMask(mask);
}
void glStencilMaskSeparate(GLenum face, GLuint mask) {
  if (g_gl_context)
    g_gl_context->dispatch.glStencilMaskSeparate(face, mask);
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
void glPolygonOffset(GLfloat factor, GLfloat units) {
  if (g_gl_context)
    g_gl_context->dispatch.glPolygonOffset(factor, units);
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
void glHint(GLenum target, GLenum mode) { _gl_Hint(target, mode); }
void glSampleCoverage(GLclampf value, GLboolean invert) {
  _gl_SampleCoverage(value, invert);
}
void glPixelStorei(GLenum pname, GLint param) {
  if (g_gl_context)
    g_gl_context->dispatch.glPixelStorei(pname, param);
}

const GLubyte *glGetString(GLenum name) {
    return g_gl_context ? g_gl_context->dispatch.glGetString(name) : NULL;
}

const GLubyte *glGetStringi(GLenum name, GLuint index) {
    return g_gl_context ? g_gl_context->dispatch.glGetStringi(name, index) : NULL;
}

void glGetBooleanv(GLenum pname, GLboolean *data) {
    if (g_gl_context)
        g_gl_context->dispatch.glGetBooleanv(pname, data);
}

void glGetDoublev(GLenum pname, GLdouble *data) {
    if (g_gl_context)
        g_gl_context->dispatch.glGetDoublev(pname, data);
}

void glGetIntegerv(GLenum pname, GLint *data) {
    if (g_gl_context)
        g_gl_context->dispatch.glGetIntegerv(pname, data);
}

void glGetFloatv(GLenum pname, GLfloat *data) {
    if (g_gl_context)
        g_gl_context->dispatch.glGetFloatv(pname, data);
}

void glReleaseShaderCompiler(void) { _gl_ReleaseShaderCompiler(); }

void glShaderBinary(GLsizei count, const GLuint *shaders, GLenum binaryFormat,
                    const GLvoid *binary, GLsizei length) {
  _gl_ShaderBinary(count, shaders, binaryFormat, binary, length);
}

void glGetShaderPrecisionFormat(GLenum shadertype, GLenum precisiontype,
                                GLint *range, GLint *precision) {
  _gl_GetShaderPrecisionFormat(shadertype, precisiontype, range, precision);
}

void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat,
                            GLsizei width, GLsizei height, GLint border,
                            GLsizei imageSize, const GLvoid *data) {
  _gl_CompressedTexImage2D(target, level, internalformat, width, height,
                           border, imageSize, data);
}

void glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat,
                      GLint x, GLint y, GLsizei width, GLsizei height,
                      GLint border) {
  _gl_CopyTexImage2D(target, level, internalformat, x, y, width, height,
                     border);
}

void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                         GLint yoffset, GLint x, GLint y, GLsizei width,
                         GLsizei height) {
  _gl_CopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width, height);
}

void glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                               GLint yoffset, GLsizei width, GLsizei height,
                               GLenum format, GLsizei imageSize,
                               const GLvoid *data) {
  _gl_CompressedTexSubImage2D(target, level, xoffset, yoffset, width, height,
                              format, imageSize, data);
}

#ifdef __cplusplus
}
#endif
