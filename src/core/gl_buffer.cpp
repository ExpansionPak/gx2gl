#include "gl_buffer.h"
#include "gl_vao.h"
#include "endian/endian.h"
#include "mem/gl_mem.h"
#include <coreinit/cache.h>
#include <gx2/draw.h>
#include <gx2r/buffer.h>
#include <string.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_BUFFERS 1024

typedef struct {
  bool in_use;
  bool gpu_owned;
  GLenum target;
  GLsizeiptr size;
  GLenum usage;
  GX2RBuffer gx2_buffer;
  void *mapped_ptr;
  GLenum mapped_access;
  GLboolean mapped_persistent;
  GLboolean mapped_uniform_shadow;
} GLBuffer;

static GLBuffer g_buffers[MAX_BUFFERS];

void gl_buffer_init(void) { memset(g_buffers, 0, sizeof(g_buffers)); }

static GLuint get_bound_buffer(GLenum target) {
  if (!g_gl_context) return 0;
  switch (target) {
  case GL_ARRAY_BUFFER: return g_gl_context->bound_array_buffer;
  case GL_ELEMENT_ARRAY_BUFFER: return gl_vao_get_element_array_buffer();
  case GL_UNIFORM_BUFFER: return g_gl_context->bound_uniform_buffer;
  default: return 0;
  }
}

void _gl_GenBuffers(GLsizei n, GLuint *buffers) {
  if (n < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
  int count = 0;
  for (int i = 1; i < MAX_BUFFERS && count < n; i++) {
    if (!g_buffers[i].in_use) {
      memset(&g_buffers[i], 0, sizeof(GLBuffer));
      g_buffers[i].in_use = true;
      buffers[count++] = i;
    }
  }
}

void _gl_DeleteBuffers(GLsizei n, const GLuint *buffers) {
  if (n < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
  for (int i = 0; i < n; i++) {
    GLuint id = buffers[i];
    if (id > 0 && id < MAX_BUFFERS && g_buffers[id].in_use) {
      if (g_buffers[id].gpu_owned) GX2RDestroyBufferEx(&g_buffers[id].gx2_buffer, (GX2RResourceFlags)0);
      g_buffers[id].in_use = false;
    }
  }
}

GLboolean _gl_IsBuffer(GLuint buffer) { return (buffer < MAX_BUFFERS && g_buffers[buffer].in_use) ? GL_TRUE : GL_FALSE; }

void _gl_BindBuffer(GLenum target, GLuint buffer) {
  if (!g_gl_context) return;
  if (buffer >= MAX_BUFFERS || (buffer > 0 && !g_buffers[buffer].in_use)) { _gl_set_error(GL_INVALID_OPERATION); return; }
  switch (target) {
  case GL_ARRAY_BUFFER: g_gl_context->bound_array_buffer = buffer; break;
  case GL_ELEMENT_ARRAY_BUFFER: gl_vao_set_element_array_buffer(buffer); break;
  case GL_UNIFORM_BUFFER: g_gl_context->bound_uniform_buffer = buffer; break;
  default: _gl_set_error(GL_INVALID_ENUM); return;
  }
}

void _gl_BindBufferBase(GLenum target, GLuint index, GLuint buffer) {
    if (target != GL_UNIFORM_BUFFER) { _gl_set_error(GL_INVALID_ENUM); return; }
    if (index >= GL33_MAX_UNIFORM_BUFFER_BINDINGS) { _gl_set_error(GL_INVALID_VALUE); return; }
    g_gl_context->uniform_buffer_bindings[index].buffer = buffer;
    g_gl_context->uniform_buffer_bindings[index].whole_buffer = GL_TRUE;
    g_gl_context->dirty_flags |= GL_DIRTY_UNIFORM_BINDINGS;
}

void _gl_BindBufferRange(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size) {
    if (target != GL_UNIFORM_BUFFER) { _gl_set_error(GL_INVALID_ENUM); return; }
    if (index >= GL33_MAX_UNIFORM_BUFFER_BINDINGS) { _gl_set_error(GL_INVALID_VALUE); return; }
    g_gl_context->uniform_buffer_bindings[index].buffer = buffer;
    g_gl_context->uniform_buffer_bindings[index].offset = offset;
    g_gl_context->uniform_buffer_bindings[index].size = size;
    g_gl_context->uniform_buffer_bindings[index].whole_buffer = GL_FALSE;
    g_gl_context->dirty_flags |= GL_DIRTY_UNIFORM_BINDINGS;
}

void _gl_BufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage) {
  GLuint id = get_bound_buffer(target);
  if (id == 0) { _gl_set_error(GL_INVALID_OPERATION); return; }
  GLBuffer *buf = &g_buffers[id];
  if (buf->gpu_owned) GX2RDestroyBufferEx(&buf->gx2_buffer, (GX2RResourceFlags)0);
  buf->size = size; buf->usage = usage;
  GX2RResourceFlags bind = GX2R_RESOURCE_BIND_NONE;
  if (target == GL_ARRAY_BUFFER) bind = GX2R_RESOURCE_BIND_VERTEX_BUFFER;
  else if (target == GL_ELEMENT_ARRAY_BUFFER) bind = GX2R_RESOURCE_BIND_INDEX_BUFFER;
  else if (target == GL_UNIFORM_BUFFER) bind = GX2R_RESOURCE_BIND_UNIFORM_BLOCK;
  buf->gx2_buffer.flags = (GX2RResourceFlags)(bind | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ);
  buf->gx2_buffer.elemSize = 1; buf->gx2_buffer.elemCount = (uint32_t)size;
  GX2RCreateBuffer(&buf->gx2_buffer);
  buf->gpu_owned = true;
  if (data) {
    void *ptr = GX2RLockBufferEx(&buf->gx2_buffer, (GX2RResourceFlags)0);
    memcpy(ptr, data, size);
    GX2RUnlockBufferEx(&buf->gx2_buffer, (GX2RResourceFlags)0);
  }
}

void _gl_BufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data) {
  GLuint id = get_bound_buffer(target);
  if (id == 0) { _gl_set_error(GL_INVALID_OPERATION); return; }
  GLBuffer *buf = &g_buffers[id];
  if (!buf->gpu_owned || offset + size > buf->size) { _gl_set_error(GL_INVALID_VALUE); return; }
  void *ptr = GX2RLockBufferEx(&buf->gx2_buffer, (GX2RResourceFlags)0);
  memcpy((uint8_t *)ptr + offset, data, size);
  GX2RUnlockBufferEx(&buf->gx2_buffer, (GX2RResourceFlags)0);
}

void _gl_GetBufferParameteriv(GLenum target, GLenum pname, GLint *params) {
  GLuint id = get_bound_buffer(target);
  if (id == 0) { _gl_set_error(GL_INVALID_OPERATION); return; }
  GLBuffer *buf = &g_buffers[id];
  switch (pname) {
  case GL_BUFFER_SIZE: *params = (GLint)buf->size; break;
  case GL_BUFFER_USAGE: *params = (GLint)buf->usage; break;
  case GL_BUFFER_ACCESS: *params = (GLint)buf->mapped_access; break;
  case GL_BUFFER_MAPPED: *params = buf->mapped_ptr ? GL_TRUE : GL_FALSE; break;
  default: _gl_set_error(GL_INVALID_ENUM); break;
  }
}

void _gl_GetBufferPointerv(GLenum target, GLenum pname, GLvoid **params) {
  GLuint id = get_bound_buffer(target);
  if (id == 0) { _gl_set_error(GL_INVALID_OPERATION); return; }
  if (pname != GL_BUFFER_MAP_POINTER) { _gl_set_error(GL_INVALID_ENUM); return; }
  *params = g_buffers[id].mapped_ptr;
}

void *_gl_MapBuffer(GLenum target, GLenum access) {
  GLuint id = get_bound_buffer(target);
  if (id == 0) { _gl_set_error(GL_INVALID_OPERATION); return NULL; }
  GLBuffer *buf = &g_buffers[id];
  if (buf->mapped_ptr) { _gl_set_error(GL_INVALID_OPERATION); return NULL; }
  buf->mapped_ptr = GX2RLockBufferEx(&buf->gx2_buffer, (GX2RResourceFlags)0);
  buf->mapped_access = access;
  return buf->mapped_ptr;
}

void *_gl_MapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) {
  GLuint id = get_bound_buffer(target);
  if (id == 0) { _gl_set_error(GL_INVALID_OPERATION); return NULL; }
  GLBuffer *buf = &g_buffers[id];
  if (buf->mapped_ptr) { _gl_set_error(GL_INVALID_OPERATION); return NULL; }
  if (offset < 0 || length < 0 || offset + length > buf->size) { _gl_set_error(GL_INVALID_VALUE); return NULL; }
  void *base = GX2RLockBufferEx(&buf->gx2_buffer, (GX2RResourceFlags)0);
  buf->mapped_ptr   = (uint8_t*)base + offset;
  buf->mapped_access = (access & GL_MAP_WRITE_BIT) ? GL_WRITE_ONLY : GL_READ_ONLY;
  return buf->mapped_ptr;
}

GLboolean _gl_UnmapBuffer(GLenum target) {
  GLuint id = get_bound_buffer(target);
  if (id == 0) { _gl_set_error(GL_INVALID_OPERATION); return GL_FALSE; }
  GLBuffer *buf = &g_buffers[id];
  if (!buf->mapped_ptr) return GL_FALSE;
  GX2RUnlockBufferEx(&buf->gx2_buffer, (GX2RResourceFlags)0);
  buf->mapped_ptr = NULL;
  return GL_TRUE;
}

void _gl_FlushMappedBufferRange(GLenum target, GLintptr offset, GLsizeiptr length) {
  GLuint id = get_bound_buffer(target);
  if (id == 0) { _gl_set_error(GL_INVALID_OPERATION); return; }
  GLBuffer *buf = &g_buffers[id];
  if (!buf->mapped_ptr) { _gl_set_error(GL_INVALID_OPERATION); return; }
  DCFlushRange((uint8_t *)buf->mapped_ptr + offset, length);
}

void _gl_GetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, GLvoid *data) {
  GLuint id = get_bound_buffer(target);
  if (id == 0 || !data) { _gl_set_error(GL_INVALID_OPERATION); return; }
  GLBuffer *buf = &g_buffers[id];
  if (!buf->gpu_owned || offset < 0 || size < 0 || offset + size > buf->size) { _gl_set_error(GL_INVALID_VALUE); return; }
  void *ptr = GX2RLockBufferEx(&buf->gx2_buffer, (GX2RResourceFlags)0);
  if (ptr) { DCInvalidateRange((uint8_t*)ptr + offset, size); memcpy(data, (uint8_t*)ptr + offset, size); }
  GX2RUnlockBufferEx(&buf->gx2_buffer, (GX2RResourceFlags)0);
}

void *gl_buffer_get_data(GLuint id) {
  if (id > 0 && id < MAX_BUFFERS && g_buffers[id].in_use && g_buffers[id].gpu_owned) return g_buffers[id].gx2_buffer.buffer;
  return NULL;
}

GLsizeiptr gl_buffer_get_size(GLuint id) {
  if (id > 0 && id < MAX_BUFFERS && g_buffers[id].in_use) return g_buffers[id].size;
  return 0;
}

GX2RBuffer *gl_buffer_get_gx2r_buffer(GLuint id) {
  if (id > 0 && id < MAX_BUFFERS && g_buffers[id].in_use && g_buffers[id].gpu_owned) return &g_buffers[id].gx2_buffer;
  return NULL;
}

#ifdef __cplusplus
}
#endif
