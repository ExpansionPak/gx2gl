#include "gl_context.h"
#include "mem/gl_mem.h"
#include "state/gl_state.h"
#include "gl_buffer.h"
#include "gl_texture.h"
#include "gl_shader.h"
#include "gl_vao.h"
#include "gl_framebuffer.h"
#include "gl_draw.h"
#include <coreinit/mutex.h>
#include <coreinit/time.h>
#include <gx2/event.h>
#include <gx2/draw.h>
#include <whb/gfx.h>
#include <gx2/clear.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef GL_OBJECT_TYPE
#define GL_OBJECT_TYPE 0x9112
#endif
#ifndef GL_SYNC_CONDITION
#define GL_SYNC_CONDITION 0x9113
#endif
#ifndef GL_SYNC_STATUS
#define GL_SYNC_STATUS 0x9114
#endif
#ifndef GL_SYNC_FLAGS
#define GL_SYNC_FLAGS 0x9115
#endif
#ifndef GL_SYNC_FENCE
#define GL_SYNC_FENCE 0x9116
#endif
#ifndef GL_WAIT_FAILED
#ifdef GL_WAIT_FAILED_GL
#define GL_WAIT_FAILED GL_WAIT_FAILED_GL
#else
#define GL_WAIT_FAILED 0x911D
#endif
#endif
#ifndef GL_SAMPLE_POSITION
#define GL_SAMPLE_POSITION 0x8E50
#endif
#ifndef GL_TEXTURE_2D_MULTISAMPLE
#define GL_TEXTURE_2D_MULTISAMPLE 0x9100
#endif
#ifndef GL_PROXY_TEXTURE_2D_MULTISAMPLE
#define GL_PROXY_TEXTURE_2D_MULTISAMPLE 0x9101
#endif
#ifndef GL_TEXTURE_2D_MULTISAMPLE_ARRAY
#define GL_TEXTURE_2D_MULTISAMPLE_ARRAY 0x9102
#endif
#ifndef GL_PROXY_TEXTURE_2D_MULTISAMPLE_ARRAY
#define GL_PROXY_TEXTURE_2D_MULTISAMPLE_ARRAY 0x9103
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif
#ifndef GL_QUERY_COUNTER_BITS
#define GL_QUERY_COUNTER_BITS 0x8864
#endif
#ifndef GL_CURRENT_QUERY
#define GL_CURRENT_QUERY 0x8865
#endif
#ifndef GL_SAMPLES_PASSED
#define GL_SAMPLES_PASSED 0x8914
#endif
#ifndef GL_ANY_SAMPLES_PASSED
#define GL_ANY_SAMPLES_PASSED 0x8C2F
#endif
#ifndef GL_ANY_SAMPLES_PASSED_CONSERVATIVE
#define GL_ANY_SAMPLES_PASSED_CONSERVATIVE 0x8D6A
#endif
#ifndef GL_PRIMITIVES_GENERATED
#define GL_PRIMITIVES_GENERATED 0x8C87
#endif
#ifndef GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN
#define GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN 0x8C88
#endif
#ifndef GL_QUERY_WAIT
#define GL_QUERY_WAIT 0x8E13
#endif
#ifndef GL_QUERY_NO_WAIT
#define GL_QUERY_NO_WAIT 0x8E14
#endif
#ifndef GL_QUERY_BY_REGION_WAIT
#define GL_QUERY_BY_REGION_WAIT 0x8E15
#endif
#ifndef GL_QUERY_BY_REGION_NO_WAIT
#define GL_QUERY_BY_REGION_NO_WAIT 0x8E16
#endif
#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF
#endif
#ifndef GL_TIMESTAMP
#define GL_TIMESTAMP 0x8E28
#endif
#ifndef GL_POINT_SIZE_MIN
#define GL_POINT_SIZE_MIN 0x8126
#endif
#ifndef GL_POINT_SIZE_MAX
#define GL_POINT_SIZE_MAX 0x8127
#endif
#ifndef GL_POINT_FADE_THRESHOLD_SIZE
#define GL_POINT_FADE_THRESHOLD_SIZE 0x8128
#endif
#ifndef GL_POINT_DISTANCE_ATTENUATION
#define GL_POINT_DISTANCE_ATTENUATION 0x8129
#endif
#ifndef GL_TRANSFORM_FEEDBACK
#define GL_TRANSFORM_FEEDBACK 0x8E22
#endif
#ifndef GL_INTERLEAVED_ATTRIBS
#define GL_INTERLEAVED_ATTRIBS 0x8C8C
#endif
#ifndef GL_SEPARATE_ATTRIBS
#define GL_SEPARATE_ATTRIBS 0x8C8D
#endif
#ifndef GL_FIRST_VERTEX_CONVENTION
#define GL_FIRST_VERTEX_CONVENTION 0x8E4D
#endif
#ifndef GL_LAST_VERTEX_CONVENTION
#define GL_LAST_VERTEX_CONVENTION 0x8E4E
#endif
#ifndef GL_PROVOKING_VERTEX
#define GL_PROVOKING_VERTEX 0x8E4F
#endif
#ifndef GL_TEXTURE_CUBE_MAP_POSITIVE_X
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#endif
#ifndef GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Z 0x851A
#endif

#ifdef __cplusplus
extern "C" {
#endif

gl_context_t *g_gl_context = NULL;

#define GL33_MAX_QUERY_OBJECTS 512
#define GL33_SYNC_MAGIC 0x33584E53u

typedef struct {
  bool reserved;
  bool in_use;
  bool active;
  GLenum target;
  GLuint64 result;
  GLboolean result_available;
  OSTime begin_ticks;
} gl_query_object_t;

struct __GLsync {
  uint32_t magic;
  GLenum condition;
  GLbitfield flags;
  GLboolean signaled;
  struct __GLsync *next;
};

static gl_query_object_t g_query_objects[GL33_MAX_QUERY_OBJECTS];
static struct __GLsync *g_sync_objects = NULL;

static void gl_context_reset_queries(void) {
  memset(g_query_objects, 0, sizeof(g_query_objects));
}

static void gl_context_default_framebuffer_size(GLsizei *width,
                                                GLsizei *height) {
  GX2ColorBuffer *tv_color;

  if (width) *width = 0;
  if (height) *height = 0;

  tv_color = WHBGfxGetTVColourBuffer();
  if (!tv_color) return;

  if (width) *width = (GLsizei)tv_color->surface.width;
  if (height) *height = (GLsizei)tv_color->surface.height;
}

static void gl_context_init_pixel_store(gl_context_t *ctx) {
  ctx->pack_alignment = 4;
  ctx->unpack_alignment = 4;
}

static void gl_context_init_blend_depth_stencil(gl_context_t *ctx) {
  ctx->blend_src_rgb = GL_ONE;
  ctx->blend_dst_rgb = GL_ZERO;
  ctx->blend_src_alpha = GL_ONE;
  ctx->blend_dst_alpha = GL_ZERO;
  ctx->blend_eq_rgb = GL_FUNC_ADD;
  ctx->blend_eq_alpha = GL_FUNC_ADD;
  ctx->depth_func = GL_LESS;
  ctx->depth_mask = GL_TRUE;
  ctx->stencil_compare_mask[0] = 0xFFFFFFFFu;
  ctx->stencil_compare_mask[1] = 0xFFFFFFFFu;
  ctx->stencil_write_mask[0] = 0xFFFFFFFFu;
  ctx->stencil_write_mask[1] = 0xFFFFFFFFu;
  ctx->stencil_func[0] = GL_ALWAYS;
  ctx->stencil_func[1] = GL_ALWAYS;
  ctx->stencil_fail[0] = GL_KEEP;
  ctx->stencil_fail[1] = GL_KEEP;
  ctx->stencil_zfail[0] = GL_KEEP;
  ctx->stencil_zfail[1] = GL_KEEP;
  ctx->stencil_zpass[0] = GL_KEEP;
  ctx->stencil_zpass[1] = GL_KEEP;
}

static void gl_context_init_raster_state(gl_context_t *ctx) {
  GLsizei default_width = 0;
  GLsizei default_height = 0;

  gl_context_default_framebuffer_size(&default_width, &default_height);

  ctx->active_texture = 0;
  ctx->viewport.x = 0;
  ctx->viewport.y = 0;
  ctx->viewport.width = default_width;
  ctx->viewport.height = default_height;
  ctx->viewport.near_z = 0.0f;
  ctx->viewport.far_z = 1.0f;
  ctx->scissor.x = 0;
  ctx->scissor.y = 0;
  ctx->scissor.width = default_width;
  ctx->scissor.height = default_height;
  ctx->cull_face_mode = GL_BACK;
  ctx->front_face = GL_CCW;
  ctx->polygon_mode = GL_FILL;
  ctx->provoking_vertex = GL_LAST_VERTEX_CONVENTION;
  ctx->clamp_read_color = GL_FIXED_ONLY;
  ctx->line_width = 1.0f;
  ctx->logic_op = GL_COPY;
  ctx->point_size = 1.0f;
  ctx->sample_coverage_value = 1.0f;
  ctx->generate_mipmap_hint = GL_DONT_CARE;
  ctx->primitive_restart_index = 0;
  ctx->color_mask[0] = GL_TRUE;
  ctx->color_mask[1] = GL_TRUE;
  ctx->color_mask[2] = GL_TRUE;
  ctx->color_mask[3] = GL_TRUE;
}

static void gl_context_init_vertex_defaults(gl_context_t *ctx) {
  for (uint32_t i = 0; i < GL33_MAX_VERTEX_ATTRIBS; ++i) {
    ctx->current_vertex_attrib[i][0] = 0.0f;
    ctx->current_vertex_attrib[i][1] = 0.0f;
    ctx->current_vertex_attrib[i][2] = 0.0f;
    ctx->current_vertex_attrib[i][3] = 1.0f;
  }
}

static void gl_context_init_defaults(gl_context_t *ctx) {
  if (!ctx) return;

  memset(ctx, 0, sizeof(gl_context_t));

  gl_context_init_pixel_store(ctx);
  gl_context_init_blend_depth_stencil(ctx);
  gl_context_init_raster_state(ctx);
  gl_context_init_vertex_defaults(ctx);

  ctx->clear_depth = 1.0f;
  ctx->clear_stencil = 0;
  ctx->error = GL_NO_ERROR;
  ctx->dirty_flags = 0xFFFFFFFF;
}

void _gl_set_error(GLenum error) {
  uint32_t next;

  if (!g_gl_context || error == GL_NO_ERROR) return;

  OSLockMutex(&g_gl_context->error_mutex);
  next = (g_gl_context->error_head + 1) % GL_ERROR_QUEUE_SIZE;
  if (next != g_gl_context->error_tail) {
    g_gl_context->error_queue[g_gl_context->error_head] = error;
    g_gl_context->error_head = next;
  }
  OSUnlockMutex(&g_gl_context->error_mutex);
}

GLenum glGetError(void) {
  GLenum error;

  if (!g_gl_context) return GL_NO_ERROR;

  OSLockMutex(&g_gl_context->error_mutex);
  if (g_gl_context->error_head == g_gl_context->error_tail) {
    OSUnlockMutex(&g_gl_context->error_mutex);
    return GL_NO_ERROR;
  }

  error = g_gl_context->error_queue[g_gl_context->error_tail];
  g_gl_context->error_tail = (g_gl_context->error_tail + 1) % GL_ERROR_QUEUE_SIZE;
  OSUnlockMutex(&g_gl_context->error_mutex);
  return error;
}

static void gl_context_signal_all_syncs(void) {
  for (struct __GLsync *sync = g_sync_objects; sync; sync = sync->next) {
    sync->signaled = GL_TRUE;
  }
}

void _gl_Flush(void) {
  gl_flush_state();
  GX2Flush();
}

void _gl_Finish(void) {
  GX2DrawDone();
  gl_context_signal_all_syncs();
}

static void gl_context_init_subsystems(void) {
  gl_buffer_init();
  gl_texture_init();
  gl_shader_init();
  gl_vao_init();
  gl_framebuffer_init();
  gl_context_reset_queries();
}

static void gl_context_init_dispatch(gl_context_t *ctx) {
  if (!ctx) return;

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
}

static void gl_context_delete_all_syncs(void) {
  struct __GLsync *sync = g_sync_objects;

  while (sync) {
    struct __GLsync *next = sync->next;
    sync->magic = 0;
    free(sync);
    sync = next;
  }

  g_sync_objects = NULL;
}

gl_context_t *gl_context_create(void) {
  gl_context_t *ctx = (gl_context_t *)gl_mem_alloc(GL_MEM_TYPE_MEM2,
                                                   sizeof(gl_context_t), 4);
  if (!ctx) return NULL;

  gl_context_init_defaults(ctx);
  OSInitMutex(&ctx->error_mutex);
  gl_context_init_subsystems();
  gl_context_init_dispatch(ctx);

  return ctx;
}

void gl_context_destroy(gl_context_t *ctx) {
  if (!ctx) return;

  if (g_gl_context == ctx) {
    _gl_Finish();
    g_gl_context = NULL;
  }

  gl_context_delete_all_syncs();
  gl_context_reset_queries();
  gl_mem_free(GL_MEM_TYPE_MEM2, ctx);
}

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
    if (!g_gl_context || !value) return;
    if (buffer == GL_STENCIL) {
        if (drawbuffer != 0) {
            _gl_set_error(GL_INVALID_VALUE);
            return;
        }
        gl_flush_state();
        GX2DepthBuffer *db = gl_get_draw_depth_buffer();
        if (db) GX2ClearDepthStencilEx(db, db->depthClear, (uint8_t)value[0], GX2_CLEAR_FLAGS_STENCIL);
        return;
    }
    if (buffer == GL_COLOR) {
        if (drawbuffer < 0 || drawbuffer >= 8 ||
            (g_gl_context->bound_framebuffer == 0 && drawbuffer != 0)) {
            _gl_set_error(GL_INVALID_VALUE);
            return;
        }
        /* gx2gl does not currently expose integer-format color clears. */
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    _gl_set_error(GL_INVALID_ENUM);
}
void glClearBufferuiv(GLenum buffer, GLint drawbuffer, const GLuint *value) {
    if (!g_gl_context || !value) return;
    if (buffer == GL_COLOR) {
        if (drawbuffer < 0 || drawbuffer >= 8 ||
            (g_gl_context->bound_framebuffer == 0 && drawbuffer != 0)) {
            _gl_set_error(GL_INVALID_VALUE);
            return;
        }
        /* gx2gl does not currently expose unsigned-integer color clears. */
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    _gl_set_error(GL_INVALID_ENUM);
}
void glClearBufferfv(GLenum buffer, GLint drawbuffer, const GLfloat *value) {
    if (!g_gl_context || !value) return;
    if (buffer == GL_COLOR) {
        if (drawbuffer < 0 || drawbuffer >= 8 ||
            (g_gl_context->bound_framebuffer == 0 && drawbuffer != 0)) {
            _gl_set_error(GL_INVALID_VALUE);
            return;
        }
        gl_flush_state();
        GX2ColorBuffer *cb = gl_get_draw_color_buffer((GLuint)drawbuffer);
        if (cb) {
            GX2ClearColor(cb, value[0], value[1], value[2], value[3]);
            gl_framebuffer_mark_bound_color_buffer_dirty((GLuint)drawbuffer);
        }
    } else if (buffer == GL_DEPTH) {
        if (drawbuffer != 0) {
            _gl_set_error(GL_INVALID_VALUE);
            return;
        }
        gl_flush_state();
        GX2DepthBuffer *db = gl_get_draw_depth_buffer();
        if (db) GX2ClearDepthStencilEx(db, value[0], (uint8_t)db->stencilClear, GX2_CLEAR_FLAGS_DEPTH);
    } else {
        _gl_set_error(GL_INVALID_ENUM);
    }
}
void glClearBufferfi(GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil) {
    if (!g_gl_context) return;
    if (buffer != GL_DEPTH_STENCIL) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (drawbuffer != 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    gl_flush_state();
    GX2DepthBuffer *db = gl_get_draw_depth_buffer();
    if (db) GX2ClearDepthStencilEx(db, depth, (uint8_t)stencil, GX2_CLEAR_FLAGS_BOTH);
}


void glEnablei(GLenum cap, GLuint index) {
    if (index != 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (cap != GL_BLEND && cap != GL_SCISSOR_TEST) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    glEnable(cap);
}
void glDisablei(GLenum cap, GLuint index) {
    if (index != 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (cap != GL_BLEND && cap != GL_SCISSOR_TEST) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    glDisable(cap);
}
GLboolean glIsEnabledi(GLenum cap, GLuint index) {
    if (index != 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return GL_FALSE;
    }
    if (cap != GL_BLEND && cap != GL_SCISSOR_TEST) {
        _gl_set_error(GL_INVALID_ENUM);
        return GL_FALSE;
    }
    return glIsEnabled(cap);
}
void glColorMaski(GLuint index, GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    if (index != 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    glColorMask(r, g, b, a);
}


void glGetIntegeri_v(GLenum target, GLuint index, GLint *data) {
    if (!g_gl_context || !data) return;
    if (target == GL_UNIFORM_BUFFER_BINDING && index < GL33_MAX_UNIFORM_BUFFER_BINDINGS)
        *data = (GLint)g_gl_context->uniform_buffer_bindings[index].buffer;
    else if (target == GL_UNIFORM_BUFFER_BINDING)
        _gl_set_error(GL_INVALID_VALUE);
    else if (target == GL_TRANSFORM_FEEDBACK_BUFFER_BINDING &&
             index < GL33_MAX_TRANSFORM_FEEDBACK_BUFFER_BINDINGS)
        *data = (GLint)g_gl_context->transform_feedback_buffer_bindings[index].buffer;
    else if (target == GL_TRANSFORM_FEEDBACK_BUFFER_BINDING)
        _gl_set_error(GL_INVALID_VALUE);
    else
        _gl_set_error(GL_INVALID_ENUM);
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
    gl_buffer_copy_sub_data(readTarget, writeTarget, readOffset, writeOffset, size);
}

static bool is_query_begin_target(GLenum target) {
    switch (target) {
    case GL_SAMPLES_PASSED:
    case GL_ANY_SAMPLES_PASSED:
    case GL_ANY_SAMPLES_PASSED_CONSERVATIVE:
    case GL_TIME_ELAPSED:
    case GL_PRIMITIVES_GENERATED:
    case GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN:
        return true;
    default:
        return false;
    }
}

static bool is_query_info_target(GLenum target) {
    return is_query_begin_target(target) || target == GL_TIMESTAMP;
}

static bool is_query_object_pname(GLenum pname) {
    return pname == GL_QUERY_RESULT ||
           pname == GL_QUERY_RESULT_AVAILABLE;
}

static bool is_conditional_render_mode(GLenum mode) {
    return mode == GL_QUERY_WAIT ||
           mode == GL_QUERY_NO_WAIT ||
           mode == GL_QUERY_BY_REGION_WAIT ||
           mode == GL_QUERY_BY_REGION_NO_WAIT;
}

static bool query_targets_compatible(GLenum object_target,
                                     GLenum requested_target) {
    if (object_target == requested_target) return true;
    if ((object_target == GL_TIME_ELAPSED || object_target == GL_TIMESTAMP) &&
        (requested_target == GL_TIME_ELAPSED || requested_target == GL_TIMESTAMP)) {
        return true;
    }
    return false;
}

static GLuint *active_query_binding(GLenum target) {
    if (!g_gl_context) return NULL;

    switch (target) {
    case GL_SAMPLES_PASSED:
        return &g_gl_context->active_query_samples_passed;
    case GL_ANY_SAMPLES_PASSED:
        return &g_gl_context->active_query_any_samples_passed;
    case GL_ANY_SAMPLES_PASSED_CONSERVATIVE:
        return &g_gl_context->active_query_any_samples_passed_conservative;
    case GL_TIME_ELAPSED:
        return &g_gl_context->active_query_time_elapsed;
    case GL_PRIMITIVES_GENERATED:
        return &g_gl_context->active_query_primitives_generated;
    case GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN:
        return &g_gl_context->active_query_transform_feedback_primitives_written;
    default:
        return NULL;
    }
}

static gl_query_object_t *query_object(GLuint id) {
    if (id == 0 || id >= GL33_MAX_QUERY_OBJECTS) return NULL;
    if (!g_query_objects[id].in_use) return NULL;
    return &g_query_objects[id];
}

static gl_query_object_t *create_or_get_query_object(GLuint id,
                                                     GLenum target) {
    gl_query_object_t *query;

    if (id == 0 || id >= GL33_MAX_QUERY_OBJECTS) {
        _gl_set_error(GL_INVALID_OPERATION);
        return NULL;
    }

    query = &g_query_objects[id];
    if (query->in_use) {
        if (!query_targets_compatible(query->target, target)) {
            _gl_set_error(GL_INVALID_OPERATION);
            return NULL;
        }
        return query;
    }

    memset(query, 0, sizeof(*query));
    query->in_use = true;
    query->target = target;
    query->result_available = GL_FALSE;
    return query;
}

static GLuint64 ticks_to_query_nanoseconds(OSTime ticks) {
    if (ticks <= 0) return 0;
    return (GLuint64)OSTicksToNanoseconds((uint64_t)ticks);
}

static void finish_query_object(gl_query_object_t *query) {
    OSTime end_ticks;

    if (!query) return;

    if (query->target == GL_TIME_ELAPSED) {
        end_ticks = OSGetTime();
        query->result = ticks_to_query_nanoseconds(end_ticks - query->begin_ticks);
    } else {
        query->result = 0;
    }

    query->active = false;
    query->result_available = GL_TRUE;
}

static bool allocate_query_names(GLsizei n, GLuint *ids) {
    GLsizei found = 0;

    for (GLuint id = 1; id < GL33_MAX_QUERY_OBJECTS && found < n; ++id) {
        if (!g_query_objects[id].reserved && !g_query_objects[id].in_use) {
            ids[found++] = id;
        }
    }

    if (found != n) {
        for (GLsizei i = 0; i < n; ++i) ids[i] = 0;
        _gl_set_error(GL_INVALID_OPERATION);
        return false;
    }

    for (GLsizei i = 0; i < n; ++i) {
        g_query_objects[ids[i]].reserved = true;
    }
    return true;
}

void _gl_GenQueries(GLsizei n, GLuint *ids) {
    if (n < 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (n > 0 && !ids) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (n == 0) return;

    allocate_query_names(n, ids);
}

void _gl_DeleteQueries(GLsizei n, const GLuint *ids) {
    if (n < 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (n > 0 && !ids) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }

    for (GLsizei i = 0; i < n; ++i) {
        GLuint id = ids[i];
        gl_query_object_t *query;

        if (id == 0 || id >= GL33_MAX_QUERY_OBJECTS) continue;

        query = &g_query_objects[id];
        if (query->in_use && query->active) {
            GLuint *binding = active_query_binding(query->target);
            finish_query_object(query);
            if (binding && *binding == id) *binding = 0;
        }
        memset(query, 0, sizeof(*query));
    }
}

GLboolean _gl_IsQuery(GLuint id) {
    return query_object(id) ? GL_TRUE : GL_FALSE;
}

void _gl_BeginQuery(GLenum target, GLuint id) {
    GLuint *binding;
    gl_query_object_t *query;

    if (!is_query_begin_target(target)) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }

    binding = active_query_binding(target);
    if (!binding || *binding != 0) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }

    query = create_or_get_query_object(id, target);
    if (!query) return;
    if (query->active) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }

    query->target = target;
    query->active = true;
    query->result = 0;
    query->result_available = GL_FALSE;
    query->begin_ticks = OSGetTime();
    *binding = id;
}

void _gl_EndQuery(GLenum target) {
    GLuint id;
    GLuint *binding;
    gl_query_object_t *query;

    if (!is_query_begin_target(target)) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }

    binding = active_query_binding(target);
    if (!binding || *binding == 0) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }

    id = *binding;
    query = query_object(id);
    if (!query || !query->active) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }

    finish_query_object(query);
    *binding = 0;
}

void _gl_GetQueryiv(GLenum target, GLenum pname, GLint *params) {
    if (!params) return;
    if (!is_query_info_target(target)) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }

    if (pname == GL_CURRENT_QUERY) {
        GLuint *binding;

        if (!is_query_begin_target(target)) {
            _gl_set_error(GL_INVALID_ENUM);
            return;
        }
        binding = active_query_binding(target);
        *params = binding ? (GLint)*binding : 0;
        return;
    }

    if (pname == GL_QUERY_COUNTER_BITS) {
        *params = (target == GL_TIME_ELAPSED || target == GL_TIMESTAMP) ? 64 : 32;
        return;
    }

    _gl_set_error(GL_INVALID_ENUM);
}

static bool get_query_result(GLuint id, GLenum pname, GLuint64 *result) {
    gl_query_object_t *query;

    if (!result) return false;
    if (!is_query_object_pname(pname)) {
        _gl_set_error(GL_INVALID_ENUM);
        return false;
    }

    query = query_object(id);
    if (!query) {
        _gl_set_error(GL_INVALID_OPERATION);
        return false;
    }
    if (query->active) {
        _gl_set_error(GL_INVALID_OPERATION);
        return false;
    }

    if (pname == GL_QUERY_RESULT_AVAILABLE) {
        *result = query->result_available ? GL_TRUE : GL_FALSE;
    } else {
        *result = query->result;
    }
    return true;
}

void _gl_GetQueryObjectiv(GLuint id, GLenum pname, GLint *params) {
    GLuint64 result = 0;

    if (!params) return;
    if (!get_query_result(id, pname, &result)) return;
    *params = (GLint)result;
}

void _gl_GetQueryObjectuiv(GLuint id, GLenum pname, GLuint *params) {
    GLuint64 result = 0;

    if (!params) return;
    if (!get_query_result(id, pname, &result)) return;
    *params = (GLuint)result;
}

void _gl_BeginConditionalRender(GLuint id, GLenum mode) {
    gl_query_object_t *query;

    if (!is_conditional_render_mode(mode)) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (!g_gl_context || g_gl_context->conditional_render_active) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }

    query = query_object(id);
    if (!query || query->active) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }

    g_gl_context->conditional_render_query = id;
    g_gl_context->conditional_render_mode = mode;
    g_gl_context->conditional_render_active = GL_TRUE;
}

void _gl_EndConditionalRender(void) {
    if (!g_gl_context || !g_gl_context->conditional_render_active) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }

    g_gl_context->conditional_render_query = 0;
    g_gl_context->conditional_render_mode = 0;
    g_gl_context->conditional_render_active = GL_FALSE;
}

void _gl_BeginQueryIndexed(GLenum target, GLuint index, GLuint id) {
    if (index != 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    _gl_BeginQuery(target, id);
}

void _gl_EndQueryIndexed(GLenum target, GLuint index) {
    if (index != 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    _gl_EndQuery(target);
}

void _gl_GetQueryIndexediv(GLenum target, GLuint index, GLenum pname, GLint *params) {
    if (index != 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    _gl_GetQueryiv(target, pname, params);
}

void _gl_GenTransformFeedbacks(GLsizei n, GLuint *ids) {
    if (n < 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (n > 0 && !ids) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (ids) {
        for (GLsizei i = 0; i < n; ++i) ids[i] = 0;
    }
    if (n > 0) _gl_set_error(GL_INVALID_OPERATION);
}

void _gl_DeleteTransformFeedbacks(GLsizei n, const GLuint *ids) {
    if (n < 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (n > 0 && !ids) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
}


void glGetUniformIndices(GLuint program, GLsizei count, const GLchar *const *names, GLuint *indices) {
    GLint active_uniforms = 0;
    GLchar active_name[256];
    GLsizei active_name_length = 0;

    if (!indices || !names || count < 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (!_gl_IsProgram(program)) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }

    for (GLsizei i = 0; i < count; i++) indices[i] = GL_INVALID_INDEX;
    _gl_GetProgramiv(program, GL_ACTIVE_UNIFORMS, &active_uniforms);

    for (GLsizei i = 0; i < count; ++i) {
        if (!names[i]) {
            _gl_set_error(GL_INVALID_VALUE);
            return;
        }
        for (GLint uniform_index = 0; uniform_index < active_uniforms; ++uniform_index) {
            memset(active_name, 0, sizeof(active_name));
            active_name_length = 0;
            _gl_GetActiveUniformName(program, (GLuint)uniform_index, (GLsizei)sizeof(active_name),
                                     &active_name_length, active_name);
            if (strcmp(active_name, names[i]) == 0) {
                indices[i] = (GLuint)uniform_index;
                break;
            }
        }
    }
}

static bool is_wrapper_framebuffer_target(GLenum target) {
    return target == GL_FRAMEBUFFER ||
           target == GL_DRAW_FRAMEBUFFER ||
           target == GL_READ_FRAMEBUFFER;
}

static bool is_wrapper_framebuffer_attachment(GLenum attachment) {
    return (attachment >= GL_COLOR_ATTACHMENT0 &&
            attachment <= GL_COLOR_ATTACHMENT7) ||
           attachment == GL_DEPTH_ATTACHMENT ||
           attachment == GL_STENCIL_ATTACHMENT ||
           attachment == GL_DEPTH_STENCIL_ATTACHMENT;
}

static bool is_cube_map_face_wrapper_target(GLenum target) {
    return target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
           target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z;
}

static bool is_transform_feedback_primitive_mode(GLenum mode) {
    return mode == GL_POINTS || mode == GL_LINES || mode == GL_TRIANGLES;
}


void glFramebufferTextureLayer(GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer) {
    _gl_FramebufferTextureLayer(target, attachment, texture, level, layer);
}


void glBindFragDataLocation(GLuint program, GLuint color, const GLchar *name) {
    if (!_gl_IsProgram(program)) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (color >= 8 || !name) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    _gl_set_error(GL_INVALID_OPERATION);
}
GLint glGetFragDataLocation(GLuint program, const GLchar *name) {
    if (!_gl_IsProgram(program) || !name) {
        _gl_set_error(GL_INVALID_VALUE);
        return -1;
    }
    _gl_set_error(GL_INVALID_OPERATION);
    return -1;
}
void glBindFragDataLocationIndexed(GLuint program, GLuint colorNumber, GLuint index, const GLchar *name) {
    if (!_gl_IsProgram(program)) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (colorNumber >= 8 || index > 0 || !name) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    _gl_set_error(GL_INVALID_OPERATION);
}
GLint glGetFragDataIndex(GLuint program, const GLchar *name) {
    if (!_gl_IsProgram(program) || !name) {
        _gl_set_error(GL_INVALID_VALUE);
        return -1;
    }
    _gl_set_error(GL_INVALID_OPERATION);
    return -1;
}


void glBeginTransformFeedback(GLenum primitiveMode) {
    if (!is_transform_feedback_primitive_mode(primitiveMode)) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    _gl_set_error(GL_INVALID_OPERATION);
}
void glEndTransformFeedback(void) { _gl_set_error(GL_INVALID_OPERATION); }
void glPauseTransformFeedback(void) { _gl_set_error(GL_INVALID_OPERATION); }
void glResumeTransformFeedback(void) { _gl_set_error(GL_INVALID_OPERATION); }
void glBindTransformFeedback(GLenum target, GLuint id) {
    (void)id;
    if (target != GL_TRANSFORM_FEEDBACK) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    _gl_set_error(GL_INVALID_OPERATION);
}
GLboolean glIsTransformFeedback(GLuint id) { (void)id; return GL_FALSE; }
void glTransformFeedbackVaryings(GLuint p, GLsizei c, const GLchar *const *v, GLenum m) {
    if (!_gl_IsProgram(p) || c < 0 || (c > 0 && !v)) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (m != GL_INTERLEAVED_ATTRIBS && m != GL_SEPARATE_ATTRIBS) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    _gl_set_error(GL_INVALID_OPERATION);
}
void glGetTransformFeedbackVarying(GLuint p, GLuint i, GLsizei bs, GLsizei *l, GLsizei *s, GLenum *t, GLchar *n) {
    (void)i;
    if (!_gl_IsProgram(p) || bs < 0 || (bs > 0 && !n)) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (l) *l = 0;
    if (n && bs > 0) n[0] = '\0';
    if (s) *s = 0;
    if (t) *t = 0;
    _gl_set_error(GL_INVALID_OPERATION);
}
void glDrawTransformFeedback(GLenum mode, GLuint id) {
    (void)id;
    if (!is_transform_feedback_primitive_mode(mode)) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    _gl_set_error(GL_INVALID_OPERATION);
}
void glClampColor(GLenum target, GLenum clamp) {
    if (target != GL_CLAMP_READ_COLOR) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (clamp != GL_TRUE && clamp != GL_FALSE && clamp != GL_FIXED_ONLY) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (!g_gl_context) return;
    g_gl_context->clamp_read_color = clamp;
}
void glProvokingVertex(GLenum mode) {
    if (mode != GL_FIRST_VERTEX_CONVENTION &&
        mode != GL_LAST_VERTEX_CONVENTION) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (!g_gl_context) return;
    if (g_gl_context->provoking_vertex != mode) {
        g_gl_context->provoking_vertex = mode;
        g_gl_context->dirty_flags |= GL_DIRTY_PROVOKING_VERTEX;
    }
}


static struct __GLsync *find_sync_object(GLsync sync) {
    struct __GLsync *candidate = (struct __GLsync *)sync;

    if (!candidate) return NULL;
    for (struct __GLsync *it = g_sync_objects; it; it = it->next) {
        if (it == candidate && it->magic == GL33_SYNC_MAGIC) {
            return it;
        }
    }
    return NULL;
}

GLsync glFenceSync(GLenum condition, GLbitfield flags) {
    struct __GLsync *sync;

    if (condition != GL_SYNC_GPU_COMMANDS_COMPLETE) {
        _gl_set_error(GL_INVALID_ENUM);
        return NULL;
    }
    if (flags != 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return NULL;
    }

    sync = (struct __GLsync *)malloc(sizeof(*sync));
    if (!sync) {
        _gl_set_error(GL_OUT_OF_MEMORY);
        return NULL;
    }

    sync->magic = GL33_SYNC_MAGIC;
    sync->condition = condition;
    sync->flags = flags;
    sync->signaled = GL_FALSE;
    sync->next = g_sync_objects;
    g_sync_objects = sync;

    GX2Flush();
    return (GLsync)sync;
}

GLboolean glIsSync(GLsync sync) {
    return find_sync_object(sync) ? GL_TRUE : GL_FALSE;
}

void glDeleteSync(GLsync sync) {
    struct __GLsync *target = (struct __GLsync *)sync;
    struct __GLsync **link = &g_sync_objects;

    while (*link && *link != target) {
        link = &(*link)->next;
    }

    if (!*link || (*link)->magic != GL33_SYNC_MAGIC) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }

    *link = target->next;
    target->magic = 0;
    free(target);
}

GLenum glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
    struct __GLsync *object = find_sync_object(sync);
    GLboolean was_signaled;

    if (!object) {
        _gl_set_error(GL_INVALID_VALUE);
        return GL_WAIT_FAILED;
    }
    if (flags & ~GL_SYNC_FLUSH_COMMANDS_BIT) {
        _gl_set_error(GL_INVALID_VALUE);
        return GL_WAIT_FAILED;
    }

    was_signaled = object->signaled;
    if (!object->signaled) {
        if ((flags & GL_SYNC_FLUSH_COMMANDS_BIT) != 0) {
            GX2Flush();
        }
        if (timeout == 0) {
            return GL_TIMEOUT_EXPIRED;
        }
        GX2DrawDone();
        gl_context_signal_all_syncs();
    }

    return was_signaled ? GL_ALREADY_SIGNALED : GL_CONDITION_SATISFIED;
}

void glWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
    struct __GLsync *object = find_sync_object(sync);

    if (!object) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (flags != 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (timeout != GL_TIMEOUT_IGNORED) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (!object->signaled) {
        GX2DrawDone();
        gl_context_signal_all_syncs();
    }
}

void glGetSynciv(GLsync sync, GLenum pname, GLsizei bufSize, GLsizei *length, GLint *values) {
    struct __GLsync *object = find_sync_object(sync);
    GLint value;

    if (!object) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (bufSize < 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (bufSize > 0 && !values) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }

    switch (pname) {
    case GL_OBJECT_TYPE:
        value = GL_SYNC_FENCE;
        break;
    case GL_SYNC_CONDITION:
        value = GL_SYNC_GPU_COMMANDS_COMPLETE;
        break;
    case GL_SYNC_STATUS:
        value = object->signaled ? GL_SIGNALED : GL_UNSIGNALED;
        break;
    case GL_SYNC_FLAGS:
        value = (GLint)object->flags;
        break;
    default:
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }

    if (length) *length = bufSize > 0 ? 1 : 0;
    if (values && bufSize > 0) values[0] = value;
}


void glGetMultisamplefv(GLenum pname, GLuint index, GLfloat *val) {
    (void)val;
    if (pname != GL_SAMPLE_POSITION) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (index != 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    _gl_set_error(GL_INVALID_OPERATION);
}
void glSampleMaski(GLuint maskNumber, GLbitfield mask) {
    (void)mask;
    if (maskNumber != 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    _gl_set_error(GL_INVALID_OPERATION);
}
void glTexImage2DMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei w, GLsizei h, GLboolean fixed) {
    (void)internalformat; (void)fixed;
    if (target != GL_TEXTURE_2D_MULTISAMPLE &&
        target != GL_PROXY_TEXTURE_2D_MULTISAMPLE) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (samples <= 0 || w < 0 || h < 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    _gl_set_error(GL_INVALID_OPERATION);
}
void glTexImage3DMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei w, GLsizei h, GLsizei d, GLboolean fixed) {
    (void)internalformat; (void)fixed;
    if (target != GL_TEXTURE_2D_MULTISAMPLE_ARRAY &&
        target != GL_PROXY_TEXTURE_2D_MULTISAMPLE_ARRAY) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (samples <= 0 || w < 0 || h < 0 || d < 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    _gl_set_error(GL_INVALID_OPERATION);
}


void glQueryCounter(GLuint id, GLenum target) {
    gl_query_object_t *query;
    OSTime now;

    if (target != GL_TIMESTAMP) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }

    query = create_or_get_query_object(id, target);
    if (!query) return;
    if (query->active) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }

    GX2Flush();
    now = OSGetTime();
    query->target = GL_TIMESTAMP;
    query->active = false;
    query->result = ticks_to_query_nanoseconds(now);
    query->result_available = GL_TRUE;
}

void glGetQueryObjecti64v(GLuint id, GLenum pname, GLint64 *params) {
    GLuint64 result = 0;

    if (!params) return;
    if (!get_query_result(id, pname, &result)) return;
    *params = (GLint64)result;
}

void glGetQueryObjectui64v(GLuint id, GLenum pname, GLuint64 *params) {
    GLuint64 result = 0;

    if (!params) return;
    if (!get_query_result(id, pname, &result)) return;
    *params = result;
}


static GLint sign_extend_packed(GLuint value, uint32_t bits) {
    const GLuint sign_bit = 1u << (bits - 1u);
    const GLuint mask = (1u << bits) - 1u;
    value &= mask;
    return (GLint)((value ^ sign_bit) - sign_bit);
}

static GLfloat normalize_signed_vertex_value(GLint value, uint32_t bits) {
    const double max_encoded = (double)(((uint64_t)1 << bits) - 1u);
    return (GLfloat)((((double)value * 2.0) + 1.0) / max_encoded);
}

static GLfloat normalize_unsigned_vertex_value(GLuint value, uint32_t bits) {
    const double max_encoded = (double)(((uint64_t)1 << bits) - 1u);
    return (GLfloat)((double)value / max_encoded);
}

static GLfloat unpack_signed_component(GLuint value, uint32_t bits, GLboolean normalized) {
    const GLint signed_value = sign_extend_packed(value, bits);

    if (!normalized) {
        return (GLfloat)signed_value;
    }

    return normalize_signed_vertex_value(signed_value, bits);
}

static GLfloat unpack_unsigned_component(GLuint value, uint32_t bits, GLboolean normalized) {
    const GLuint mask = (1u << bits) - 1u;
    value &= mask;

    if (!normalized) {
        return (GLfloat)value;
    }

    return normalize_unsigned_vertex_value(value, bits);
}

static void set_vertex_attrib_packed(GLuint index, GLuint packed, GLenum type,
                                     GLboolean normalized, GLuint component_count) {
    GLfloat components[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    if (type == GL_INT_2_10_10_10_REV) {
        components[0] = unpack_signed_component(packed >> 0u, 10u, normalized);
        components[1] = unpack_signed_component(packed >> 10u, 10u, normalized);
        components[2] = unpack_signed_component(packed >> 20u, 10u, normalized);
        components[3] = unpack_signed_component(packed >> 30u, 2u, normalized);
    } else if (type == GL_UNSIGNED_INT_2_10_10_10_REV) {
        components[0] = unpack_unsigned_component(packed >> 0u, 10u, normalized);
        components[1] = unpack_unsigned_component(packed >> 10u, 10u, normalized);
        components[2] = unpack_unsigned_component(packed >> 20u, 10u, normalized);
        components[3] = unpack_unsigned_component(packed >> 30u, 2u, normalized);
    } else {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }

    switch (component_count) {
    case 1:
        _gl_VertexAttrib4f(index, components[0], 0.0f, 0.0f, 1.0f);
        break;
    case 2:
        _gl_VertexAttrib4f(index, components[0], components[1], 0.0f, 1.0f);
        break;
    case 3:
        _gl_VertexAttrib4f(index, components[0], components[1], components[2], 1.0f);
        break;
    case 4:
        _gl_VertexAttrib4f(index, components[0], components[1], components[2], components[3]);
        break;
    default:
        _gl_set_error(GL_INVALID_VALUE);
        break;
    }
}

void glVertexAttribP1ui(GLuint i, GLenum t, GLboolean n, GLuint v) { set_vertex_attrib_packed(i, v, t, n, 1); }
void glVertexAttribP2ui(GLuint i, GLenum t, GLboolean n, GLuint v) { set_vertex_attrib_packed(i, v, t, n, 2); }
void glVertexAttribP3ui(GLuint i, GLenum t, GLboolean n, GLuint v) { set_vertex_attrib_packed(i, v, t, n, 3); }
void glVertexAttribP4ui(GLuint i, GLenum t, GLboolean n, GLuint v) { set_vertex_attrib_packed(i, v, t, n, 4); }
void glVertexAttribP1uiv(GLuint i, GLenum t, GLboolean n, const GLuint *v) { if (!v) { _gl_set_error(GL_INVALID_VALUE); return; } glVertexAttribP1ui(i, t, n, *v); }
void glVertexAttribP2uiv(GLuint i, GLenum t, GLboolean n, const GLuint *v) { if (!v) { _gl_set_error(GL_INVALID_VALUE); return; } glVertexAttribP2ui(i, t, n, *v); }
void glVertexAttribP3uiv(GLuint i, GLenum t, GLboolean n, const GLuint *v) { if (!v) { _gl_set_error(GL_INVALID_VALUE); return; } glVertexAttribP3ui(i, t, n, *v); }
void glVertexAttribP4uiv(GLuint i, GLenum t, GLboolean n, const GLuint *v) { if (!v) { _gl_set_error(GL_INVALID_VALUE); return; } glVertexAttribP4ui(i, t, n, *v); }


void glTexBuffer(GLenum target, GLenum internalformat, GLuint buffer) {
    (void)internalformat;
    if (target != GL_TEXTURE_BUFFER) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (buffer != 0 && !_gl_IsBuffer(buffer)) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    _gl_set_error(GL_INVALID_OPERATION);
}


void glFramebufferTexture1D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    if (!is_wrapper_framebuffer_target(target) ||
        !is_wrapper_framebuffer_attachment(attachment) ||
        textarget != GL_TEXTURE_1D) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (level < 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (texture != 0 && !_gl_IsTexture(texture)) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    /* gx2gl does not support GL_TEXTURE_1D attachments. */
    _gl_set_error(GL_INVALID_OPERATION);
}
void glFramebufferTexture3D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLint zoffset) {
    if (!is_wrapper_framebuffer_target(target) ||
        !is_wrapper_framebuffer_attachment(attachment) ||
        textarget != GL_TEXTURE_3D) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (level < 0 || zoffset < 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (texture != 0 && !_gl_IsTexture(texture)) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    _gl_FramebufferTextureLayer(target, attachment, texture, level, zoffset);
}
void glGetBooleani_v(GLenum target, GLuint index, GLboolean *data) {
    if (!g_gl_context || !data) return;

    switch (target) {
    case GL_BLEND:
        if (index != 0) {
            _gl_set_error(GL_INVALID_VALUE);
            return;
        }
        *data = g_gl_context->blend_enabled;
        break;
    case GL_SCISSOR_TEST:
        if (index != 0) {
            _gl_set_error(GL_INVALID_VALUE);
            return;
        }
        *data = g_gl_context->scissor_test_enabled;
        break;
    case GL_COLOR_WRITEMASK:
        if (index != 0) {
            _gl_set_error(GL_INVALID_VALUE);
            return;
        }
        data[0] = g_gl_context->color_mask[0];
        data[1] = g_gl_context->color_mask[1];
        data[2] = g_gl_context->color_mask[2];
        data[3] = g_gl_context->color_mask[3];
        break;
    default:
        _gl_set_error(GL_INVALID_ENUM);
        break;
    }
}
void glGetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, GLvoid *data) {
    gl_buffer_get_sub_data(target, offset, size, data);
}
void glGetCompressedTexImage(GLenum target, GLint level, GLvoid *img) {
    (void)img;
    if (target != GL_TEXTURE_1D &&
        target != GL_TEXTURE_2D &&
        target != GL_TEXTURE_3D &&
        !is_cube_map_face_wrapper_target(target)) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (level < 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    _gl_set_error(GL_INVALID_OPERATION);
}
void glGetPointerv(GLenum pname, GLvoid **params) {
    (void)pname;
    if (params) *params = NULL;
    _gl_set_error(GL_INVALID_OPERATION);
}
void glGetSamplerParameterIiv(GLuint sampler, GLenum pname, GLint *params) {
    if (!params) return;
    glGetSamplerParameteriv(sampler, pname, params);
}
void glGetSamplerParameterIuiv(GLuint sampler, GLenum pname, GLuint *params) {
    GLint value = 0;

    if (!params) return;
    glGetSamplerParameteriv(sampler, pname, &value);
    *params = (GLuint)value;
}

void glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels) {
    _gl_GetTexImage(target, level, format, type, pixels);
}
void glGetTexParameterIiv(GLenum target, GLenum pname, GLint *params) {
    if (!params) return;
    glGetTexParameteriv(target, pname, params);
}
void glGetTexParameterIuiv(GLenum target, GLenum pname, GLuint *params) {
    GLint value = 0;

    if (!params) return;
    glGetTexParameteriv(target, pname, &value);
    *params = (GLuint)value;
}
void glGetVertexAttribdv(GLuint index, GLenum pname, GLdouble *params) {
    GLfloat values[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    if (!params) return;
    glGetVertexAttribfv(index, pname, values);
    if (pname == GL_CURRENT_VERTEX_ATTRIB) {
        params[0] = (GLdouble)values[0];
        params[1] = (GLdouble)values[1];
        params[2] = (GLdouble)values[2];
        params[3] = (GLdouble)values[3];
    } else {
        params[0] = (GLdouble)values[0];
    }
}
void glPixelStoref(GLenum pname, GLfloat param) { glPixelStorei(pname, (GLint)param); }
void glPointParameterf(GLenum pname, GLfloat param) {
    (void)param;
    if (pname != GL_POINT_SIZE_MIN &&
        pname != GL_POINT_SIZE_MAX &&
        pname != GL_POINT_FADE_THRESHOLD_SIZE) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    _gl_set_error(GL_INVALID_OPERATION);
}
void glPointParameterfv(GLenum pname, const GLfloat *params) {
    (void)params;
    if (pname != GL_POINT_DISTANCE_ATTENUATION) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    _gl_set_error(GL_INVALID_OPERATION);
}
void glPointParameteri(GLenum pname, GLint param) {
    glPointParameterf(pname, (GLfloat)param);
}
void glPointParameteriv(GLenum pname, const GLint *params) {
    (void)params;
    if (pname != GL_POINT_DISTANCE_ATTENUATION) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    _gl_set_error(GL_INVALID_OPERATION);
}
void glSamplerParameterIiv(GLuint sampler, GLenum pname, const GLint *param) {
    if (!param) return;
    glSamplerParameteriv(sampler, pname, param);
}
void glSamplerParameterIuiv(GLuint sampler, GLenum pname, const GLuint *param) {
    GLint value;

    if (!param) return;
    value = (GLint)param[0];
    glSamplerParameteriv(sampler, pname, &value);
}
void glTexParameterIiv(GLenum target, GLenum pname, const GLint *params) {
    if (!params) return;
    glTexParameteriv(target, pname, params);
}
void glTexParameterIuiv(GLenum target, GLenum pname, const GLuint *params) {
    GLint value;

    if (!params) return;
    value = (GLint)params[0];
    glTexParameteriv(target, pname, &value);
}
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
void glVertexAttrib4Nbv(GLuint index, const GLbyte *v) {
    if (v) glVertexAttrib4f(index,
                            normalize_signed_vertex_value(v[0], 8),
                            normalize_signed_vertex_value(v[1], 8),
                            normalize_signed_vertex_value(v[2], 8),
                            normalize_signed_vertex_value(v[3], 8));
}
void glVertexAttrib4Niv(GLuint index, const GLint *v) {
    if (v) glVertexAttrib4f(index,
                            normalize_signed_vertex_value(v[0], 32),
                            normalize_signed_vertex_value(v[1], 32),
                            normalize_signed_vertex_value(v[2], 32),
                            normalize_signed_vertex_value(v[3], 32));
}
void glVertexAttrib4Nsv(GLuint index, const GLshort *v) {
    if (v) glVertexAttrib4f(index,
                            normalize_signed_vertex_value(v[0], 16),
                            normalize_signed_vertex_value(v[1], 16),
                            normalize_signed_vertex_value(v[2], 16),
                            normalize_signed_vertex_value(v[3], 16));
}
void glVertexAttrib4Nub(GLuint index, GLubyte x, GLubyte y, GLubyte z, GLubyte w) {
    glVertexAttrib4f(index,
                     normalize_unsigned_vertex_value(x, 8),
                     normalize_unsigned_vertex_value(y, 8),
                     normalize_unsigned_vertex_value(z, 8),
                     normalize_unsigned_vertex_value(w, 8));
}
void glVertexAttrib4Nubv(GLuint index, const GLubyte *v) {
    if (v) glVertexAttrib4Nub(index, v[0], v[1], v[2], v[3]);
}
void glVertexAttrib4Nuiv(GLuint index, const GLuint *v) {
    if (v) glVertexAttrib4f(index,
                            normalize_unsigned_vertex_value(v[0], 32),
                            normalize_unsigned_vertex_value(v[1], 32),
                            normalize_unsigned_vertex_value(v[2], 32),
                            normalize_unsigned_vertex_value(v[3], 32));
}
void glVertexAttrib4Nusv(GLuint index, const GLushort *v) {
    if (v) glVertexAttrib4f(index,
                            normalize_unsigned_vertex_value(v[0], 16),
                            normalize_unsigned_vertex_value(v[1], 16),
                            normalize_unsigned_vertex_value(v[2], 16),
                            normalize_unsigned_vertex_value(v[3], 16));
}
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
