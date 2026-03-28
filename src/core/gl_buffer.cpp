#include "gl_buffer.h"
#include "gl_vao.h"
#include "endian/endian.h"
#include "mem/gl_mem.h"
#include <coreinit/cache.h>
#include <gx2/draw.h>
#include <gx2r/buffer.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_BUFFERS 4096

typedef struct {
  GX2RBuffer gx2_buffer;
  GLenum target;
  GLsizeiptr size;
  GLenum usage;
  void *mapped_ptr;
  void *mapped_shadow_ptr;
  void *mapped_gpu_ptr;
  GLintptr mapped_offset;
  GLsizeiptr mapped_length;
  GLboolean mapped_uniform_shadow;
  bool in_use;
  bool pending_delete;
  bool gpu_owned;
} GLBuffer;

static GLBuffer g_buffers[MAX_BUFFERS];

void gl_buffer_init(void) { memset(g_buffers, 0, sizeof(g_buffers)); }

static bool is_valid_buffer_target(GLenum target) {
  return target == GL_ARRAY_BUFFER || target == GL_ELEMENT_ARRAY_BUFFER ||
         target == GL_UNIFORM_BUFFER;
}

static GLBuffer *lookup_buffer(GLuint id) {
  if (id == 0 || id >= MAX_BUFFERS || !g_buffers[id].in_use) {
    return NULL;
  }
  return &g_buffers[id];
}

static void clear_uniform_buffer_bindings(GLuint buffer) {
  if (!g_gl_context || buffer == 0) {
    return;
  }

  for (uint32_t i = 0; i < GL33_MAX_UNIFORM_BUFFER_BINDINGS; ++i) {
    if (g_gl_context->uniform_buffer_bindings[i].buffer == buffer) {
      memset(&g_gl_context->uniform_buffer_bindings[i], 0,
             sizeof(g_gl_context->uniform_buffer_bindings[i]));
      g_gl_context->dirty_flags |= GL_DIRTY_UNIFORM_BINDINGS;
    }
  }
}

static void release_mapped_shadow(GLBuffer *buf) {
  if (buf && buf->mapped_shadow_ptr) {
    gl_mem_free(GL_MEM_TYPE_MEM2, buf->mapped_shadow_ptr);
    buf->mapped_shadow_ptr = NULL;
  }
}

void gl_buffer_end_frame(void) {
  for (int i = 1; i < MAX_BUFFERS; i++) {
    if (g_buffers[i].in_use && g_buffers[i].pending_delete) {
      release_mapped_shadow(&g_buffers[i]);
      if (g_buffers[i].gpu_owned) {
        GX2RDestroyBufferEx(&g_buffers[i].gx2_buffer, (GX2RResourceFlags)0);
      }
      memset(&g_buffers[i], 0, sizeof(GLBuffer));
    }
  }
}

void _gl_GenBuffers(GLsizei n, GLuint *buffers) {
  if (!g_gl_context || n < 0) {
    if (n < 0)
      _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  int generated = 0;
  for (int i = 1; i < MAX_BUFFERS && generated < n; i++) {
    if (!g_buffers[i].in_use) {
      g_buffers[i].in_use = true;
      g_buffers[i].target = 0;
      g_buffers[i].size = 0;
      g_buffers[i].mapped_ptr = NULL;
      g_buffers[i].pending_delete = false;
      g_buffers[i].gpu_owned = false;
      buffers[generated++] = i;
    }
  }
  if (generated < n) {
    _gl_set_error(GL_OUT_OF_MEMORY);
  }
}

void _gl_DeleteBuffers(GLsizei n, const GLuint *buffers) {
  if (!g_gl_context || n < 0) {
    if (n < 0)
      _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  for (int i = 0; i < n; i++) {
    GLuint id = buffers[i];
    if (id > 0 && id < MAX_BUFFERS && g_buffers[id].in_use) {
      /* If bound, unbind from context state targets */
      if (g_gl_context->bound_array_buffer == id)
        g_gl_context->bound_array_buffer = 0;
      if (g_gl_context->bound_element_array_buffer == id)
        g_gl_context->bound_element_array_buffer = 0;
      if (g_gl_context->bound_uniform_buffer == id)
        g_gl_context->bound_uniform_buffer = 0;

      clear_uniform_buffer_bindings(id);
      g_buffers[id].pending_delete = true;
    }
  }
}

void _gl_BindBuffer(GLenum target, GLuint buffer) {
  if (!g_gl_context)
    return;
  if (buffer >= MAX_BUFFERS || (buffer > 0 && !g_buffers[buffer].in_use)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  switch (target) {
  case GL_ARRAY_BUFFER:
    g_gl_context->bound_array_buffer = buffer;
    break;
  case GL_ELEMENT_ARRAY_BUFFER:
    gl_vao_set_element_array_buffer(buffer);
    break;
  case GL_UNIFORM_BUFFER:
    g_gl_context->bound_uniform_buffer = buffer;
    break;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
}

void _gl_BindBufferBase(GLenum target, GLuint index, GLuint buffer) {
  GLBuffer *buf = NULL;

  if (!g_gl_context)
    return;
  if (target != GL_UNIFORM_BUFFER) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (index >= GL33_MAX_UNIFORM_BUFFER_BINDINGS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (buffer > 0) {
    buf = lookup_buffer(buffer);
    if (!buf) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
  }

  g_gl_context->bound_uniform_buffer = buffer;
  g_gl_context->uniform_buffer_bindings[index].buffer = buffer;
  g_gl_context->uniform_buffer_bindings[index].offset = 0;
  g_gl_context->uniform_buffer_bindings[index].size =
      buf ? gl_buffer_get_size(buffer) : 0;
  g_gl_context->uniform_buffer_bindings[index].whole_buffer = GL_TRUE;
  g_gl_context->dirty_flags |= GL_DIRTY_UNIFORM_BINDINGS;
}

void _gl_BindBufferRange(GLenum target, GLuint index, GLuint buffer,
                         GLintptr offset, GLsizeiptr size) {
  GLBuffer *buf;

  if (!g_gl_context)
    return;
  if (target != GL_UNIFORM_BUFFER) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (index >= GL33_MAX_UNIFORM_BUFFER_BINDINGS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (buffer == 0) {
    if (offset != 0 || size != 0) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
    g_gl_context->bound_uniform_buffer = 0;
    memset(&g_gl_context->uniform_buffer_bindings[index], 0,
           sizeof(g_gl_context->uniform_buffer_bindings[index]));
    g_gl_context->dirty_flags |= GL_DIRTY_UNIFORM_BINDINGS;
    return;
  }

  buf = lookup_buffer(buffer);
  if (!buf) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (offset < 0 || size <= 0 || offset + size > buf->size ||
      (offset % GX2_UNIFORM_BLOCK_ALIGNMENT) != 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  g_gl_context->bound_uniform_buffer = buffer;
  g_gl_context->uniform_buffer_bindings[index].buffer = buffer;
  g_gl_context->uniform_buffer_bindings[index].offset = offset;
  g_gl_context->uniform_buffer_bindings[index].size = size;
  g_gl_context->uniform_buffer_bindings[index].whole_buffer = GL_FALSE;
  g_gl_context->dirty_flags |= GL_DIRTY_UNIFORM_BINDINGS;
}

static GLuint get_bound_buffer(GLenum target) {
  switch (target) {
  case GL_ARRAY_BUFFER:
    return g_gl_context->bound_array_buffer;
  case GL_ELEMENT_ARRAY_BUFFER:
    return g_gl_context->bound_element_array_buffer;
  case GL_UNIFORM_BUFFER:
    return g_gl_context->bound_uniform_buffer;
  default:
    return 0;
  }
}

void _gl_BufferData(GLenum target, GLsizeiptr size, const GLvoid *data,
                    GLenum usage) {
  if (!g_gl_context)
    return;
  if (!is_valid_buffer_target(target)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  GLuint id = get_bound_buffer(target);
  if (id == 0 || size < 0) {
    _gl_set_error(id == 0 ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
    return;
  }

  GLBuffer *buf = &g_buffers[id];
  if (buf->mapped_ptr) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  buf->target = target;
  buf->size = size;
  buf->usage = usage;

  release_mapped_shadow(buf);
  buf->mapped_ptr = NULL;
  buf->mapped_gpu_ptr = NULL;
  buf->mapped_offset = 0;
  buf->mapped_length = 0;
  buf->mapped_uniform_shadow = GL_FALSE;

  if (buf->gpu_owned) {
    GX2RDestroyBufferEx(&buf->gx2_buffer, (GX2RResourceFlags)0);
    buf->gpu_owned = false;
  }

  GX2RResourceFlags flags = GX2R_RESOURCE_BIND_NONE;
  if (target == GL_ARRAY_BUFFER)
    flags |= GX2R_RESOURCE_BIND_VERTEX_BUFFER;
  else if (target == GL_ELEMENT_ARRAY_BUFFER)
    flags |= GX2R_RESOURCE_BIND_INDEX_BUFFER;
  else if (target == GL_UNIFORM_BUFFER)
    flags |= GX2R_RESOURCE_BIND_UNIFORM_BLOCK;

  flags |= GX2R_RESOURCE_USAGE_GPU_READ;
  if (usage == GL_DYNAMIC_DRAW || usage == GL_STREAM_DRAW) {
    flags |= GX2R_RESOURCE_USAGE_CPU_WRITE;
  }

  buf->gx2_buffer.flags = flags;
  buf->gx2_buffer.elemCount = size;
  buf->gx2_buffer.elemSize = 1;

  // No raw malloc/free, we must ensure memory comes from MEM2 or GX2R itself.

  if (!GX2RCreateBuffer(&buf->gx2_buffer)) {
    _gl_set_error(GL_OUT_OF_MEMORY);
    return;
  }
  buf->gpu_owned = true;
  g_gl_context->dirty_flags |= GL_DIRTY_VAO;
  if (target == GL_UNIFORM_BUFFER) {
    g_gl_context->dirty_flags |= GL_DIRTY_UNIFORM_BINDINGS;
  }

  if (data) {
    glBufferSubData(target, 0, size, data);
  }
}

void _gl_BufferSubData(GLenum target, GLintptr offset, GLsizeiptr size,
                       const GLvoid *data) {
  if (!g_gl_context || !data)
    return;
  if (!is_valid_buffer_target(target)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  GLuint id = get_bound_buffer(target);
  if (id == 0) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  GLBuffer *buf = &g_buffers[id];
  if (offset < 0 || size < 0 || offset + size > buf->size) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (buf->mapped_ptr) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  void *ptr = GX2RLockBufferEx(&buf->gx2_buffer, (GX2RResourceFlags)0);
  if (!ptr) {
    _gl_set_error(GL_OUT_OF_MEMORY);
    return;
  }

  uint8_t *dst = (uint8_t *)ptr + offset;

  if (target == GL_UNIFORM_BUFFER) {
    /*
     * Each scalar field written into a locked uniform buffer must be
     * individually swapped using CPU_TO_GPU_32. We assume incoming data
     * is packed 32-bit scalars (std140 layout usually guarantees this).
     */
    const uint32_t *src32 = (const uint32_t *)data;
    uint32_t *dst32 = (uint32_t *)dst;
    size_t count = size / 4;
    for (size_t i = 0; i < count; i++) {
      dst32[i] = CPU_TO_GPU_32(src32[i]);
    }
    size_t remainder = size % 4;
    if (remainder > 0) {
      /* If not multiple of 4, safely fallback to memcpy.
         This shouldn't happen for valid UBOs per spec. */
      memcpy(dst + count * 4, (const uint8_t *)data + count * 4, remainder);
    }
  } else {
    /* GL_ARRAY_BUFFER / GL_ELEMENT_ARRAY_BUFFER: No swap needed */
    memcpy(dst, data, size);
  }

  DCFlushRange(dst, size);
  GX2RUnlockBufferEx(&buf->gx2_buffer, (GX2RResourceFlags)0);
  if (target == GL_UNIFORM_BUFFER) {
    GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_CPU |
                                      GX2_INVALIDATE_MODE_UNIFORM_BLOCK),
                  dst, size);
  } else {
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, dst, size);
  }
}

void *_gl_MapBuffer(GLenum target, GLenum access) {
  return _gl_MapBufferRange(target, 0, 0,
                            access); /* Length handled inside if 0 */
}

void *_gl_MapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length,
                         GLbitfield access) {
  if (!g_gl_context)
    return NULL;
  if (!is_valid_buffer_target(target)) {
    _gl_set_error(GL_INVALID_ENUM);
    return NULL;
  }
  GLuint id = get_bound_buffer(target);
  if (id == 0) {
    _gl_set_error(GL_INVALID_OPERATION);
    return NULL;
  }

  GLBuffer *buf = &g_buffers[id];
  if (buf->mapped_ptr) {
    _gl_set_error(GL_INVALID_OPERATION);
    return NULL;
  }

  if (length == 0)
    length = buf->size; // Handling for MapBuffer wrapper

  if (offset < 0 || length < 0 || offset + length > buf->size) {
    _gl_set_error(GL_INVALID_VALUE);
    return NULL;
  }

  void *ptr = GX2RLockBufferEx(&buf->gx2_buffer, (GX2RResourceFlags)0);
  if (!ptr) {
    _gl_set_error(GL_OUT_OF_MEMORY);
    return NULL;
  }

  buf->mapped_offset = offset;
  buf->mapped_length = length;
  buf->mapped_gpu_ptr = (uint8_t *)ptr + offset;
  buf->mapped_uniform_shadow = GL_FALSE;

  if (target == GL_UNIFORM_BUFFER) {
    uint8_t *shadow =
        (uint8_t *)gl_mem_alloc(GL_MEM_TYPE_MEM2, (uint32_t)length, 4);
    if (!shadow) {
      GX2RUnlockBufferEx(&buf->gx2_buffer, (GX2RResourceFlags)0);
      buf->mapped_gpu_ptr = NULL;
      buf->mapped_offset = 0;
      buf->mapped_length = 0;
      _gl_set_error(GL_OUT_OF_MEMORY);
      return NULL;
    }

    memcpy(shadow, buf->mapped_gpu_ptr, (size_t)length);
    for (GLsizeiptr i = 0; i + 4 <= length; i += 4) {
      uint32_t word;
      memcpy(&word, shadow + i, sizeof(word));
      word = GPU_TO_CPU_32(word);
      memcpy(shadow + i, &word, sizeof(word));
    }

    buf->mapped_shadow_ptr = shadow;
    buf->mapped_uniform_shadow = GL_TRUE;
    buf->mapped_ptr = shadow;
    return buf->mapped_ptr;
  }

  buf->mapped_ptr = buf->mapped_gpu_ptr;
  return buf->mapped_ptr;
}

GLboolean _gl_UnmapBuffer(GLenum target) {
  if (!g_gl_context)
    return GL_FALSE;
  if (!is_valid_buffer_target(target)) {
    _gl_set_error(GL_INVALID_ENUM);
    return GL_FALSE;
  }
  GLuint id = get_bound_buffer(target);
  if (id == 0) {
    _gl_set_error(GL_INVALID_OPERATION);
    return GL_FALSE;
  }

  GLBuffer *buf = &g_buffers[id];
  if (!buf->mapped_ptr) {
    _gl_set_error(GL_INVALID_OPERATION);
    return GL_FALSE;
  }

  if (buf->mapped_uniform_shadow) {
    uint8_t *src = (uint8_t *)buf->mapped_shadow_ptr;
    uint8_t *dst = (uint8_t *)buf->mapped_gpu_ptr;

    for (GLsizeiptr i = 0; i + 4 <= buf->mapped_length; i += 4) {
      uint32_t word;
      memcpy(&word, src + i, sizeof(word));
      word = CPU_TO_GPU_32(word);
      memcpy(dst + i, &word, sizeof(word));
    }
    if ((buf->mapped_length % 4) != 0) {
      GLsizeiptr tail = buf->mapped_length - (buf->mapped_length & ~((GLsizeiptr)3));
      memcpy(dst + (buf->mapped_length - tail),
             src + (buf->mapped_length - tail), (size_t)tail);
    }
    DCFlushRange(dst, (uint32_t)buf->mapped_length);
  } else {
    DCFlushRange(buf->mapped_ptr,
                 (uint32_t)buf->mapped_length);
  }
  GX2RUnlockBufferEx(&buf->gx2_buffer, (GX2RResourceFlags)0);
  if (target == GL_UNIFORM_BUFFER) {
    GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_CPU |
                                      GX2_INVALIDATE_MODE_UNIFORM_BLOCK),
                  buf->mapped_gpu_ptr, (uint32_t)buf->mapped_length);
  } else {
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER,
                  buf->gx2_buffer.buffer, buf->size);
  }

  release_mapped_shadow(buf);
  buf->mapped_ptr = NULL;
  buf->mapped_gpu_ptr = NULL;
  buf->mapped_offset = 0;
  buf->mapped_length = 0;
  buf->mapped_uniform_shadow = GL_FALSE;
  return GL_TRUE;
}

void *gl_buffer_get_data(GLuint id) {
  if (id > 0 && id < MAX_BUFFERS && g_buffers[id].in_use && g_buffers[id].gpu_owned) {
    return g_buffers[id].gx2_buffer.buffer;
  }
  return NULL;
}

GLsizeiptr gl_buffer_get_size(GLuint id) {
  if (id > 0 && id < MAX_BUFFERS && g_buffers[id].in_use && g_buffers[id].gpu_owned) {
    return g_buffers[id].size;
  }
  return 0;
}

GX2RBuffer *gl_buffer_get_gx2r_buffer(GLuint id) {
  if (id > 0 && id < MAX_BUFFERS && g_buffers[id].in_use &&
      g_buffers[id].gpu_owned) {
    return &g_buffers[id].gx2_buffer;
  }
  return NULL;
}

#ifdef __cplusplus
}
#endif
