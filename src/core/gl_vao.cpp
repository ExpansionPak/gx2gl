#include "gl_vao.h"
#include "gl_buffer.h"
#include "mem/gl_mem.h"
#include "state/gl_state.h"
#include <coreinit/cache.h>
#include <gx2/draw.h>
#include <gx2/enum.h>
#include <gx2/mem.h>
#include <gx2/state.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_VAOS 512
#define MAX_VERTEX_ATTRIBS 16

typedef struct {
  bool enabled;
  GLuint buffer;
  GLint size;
  GLenum type;
  GLboolean normalized;
  GLsizei stride;
  const GLvoid *pointer;
  GLuint divisor;
} GLVertexAttrib;

typedef struct {
  bool in_use;
  GLVertexAttrib attribs[MAX_VERTEX_ATTRIBS];
  GLuint element_array_buffer; // Element array bound to this VAO

  bool dirty; // Recompile fetch shader
  GX2FetchShader fetch_shader;
  void *fetch_program;
} GLVertexArray;

static GLVertexArray g_vaos[MAX_VAOS];

void gl_vao_init(void) {
  memset(g_vaos, 0, sizeof(g_vaos));
  // VAO 0 is the default VAO
  g_vaos[0].in_use = true;
  g_vaos[0].dirty = true;
}

void _gl_GenVertexArrays(GLsizei n, GLuint *arrays) {
  if (!g_gl_context || n < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  int count = 0;
  for (int i = 1; i < MAX_VAOS && count < n; i++) {
    if (!g_vaos[i].in_use) {
      g_vaos[i].in_use = true;
      g_vaos[i].element_array_buffer = 0;
      g_vaos[i].dirty = true;
      g_vaos[i].fetch_program = NULL;
      memset(&g_vaos[i].fetch_shader, 0, sizeof(GX2FetchShader));
      memset(g_vaos[i].attribs, 0, sizeof(g_vaos[i].attribs));
      arrays[count++] = i;
    }
  }
}

void _gl_DeleteVertexArrays(GLsizei n, const GLuint *arrays) {
  if (!g_gl_context || n < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  for (int i = 0; i < n; i++) {
    GLuint id = arrays[i];
    if (id > 0 && id < MAX_VAOS && g_vaos[id].in_use) {
      if (g_vaos[id].fetch_program) {
        gl_mem_free(GL_MEM_TYPE_MEM2, g_vaos[id].fetch_program);
      }
      g_vaos[id].in_use = false;
      if (g_gl_context->bound_vao == id) {
        g_gl_context->bound_vao = 0; // Bind default VAO
        g_gl_context->dirty_flags |= GL_DIRTY_VAO;
      }
    }
  }
}

void _gl_BindVertexArray(GLuint array) {
  if (!g_gl_context)
    return;
  if (array >= MAX_VAOS || (!g_vaos[array].in_use && array > 0)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  g_gl_context->bound_vao = array;
  g_gl_context->dirty_flags |= GL_DIRTY_VAO;
}

void _gl_EnableVertexAttribArray(GLuint index) {
  if (!g_gl_context || index >= MAX_VERTEX_ATTRIBS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  GLuint vao = g_gl_context->bound_vao;
  g_vaos[vao].attribs[index].enabled = true;
  g_vaos[vao].dirty = true;
  g_gl_context->dirty_flags |= GL_DIRTY_VAO;
}

void _gl_DisableVertexAttribArray(GLuint index) {
  if (!g_gl_context || index >= MAX_VERTEX_ATTRIBS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  GLuint vao = g_gl_context->bound_vao;
  g_vaos[vao].attribs[index].enabled = false;
  g_vaos[vao].dirty = true;
  g_gl_context->dirty_flags |= GL_DIRTY_VAO;
}

static uint32_t get_attrib_stride_bytes(GLint size, GLenum type, bool *valid) {
  *valid = true;
  switch (type) {
  case GL_FLOAT:
    return (uint32_t)size * 4;
  case GL_HALF_FLOAT:
  case GL_UNSIGNED_SHORT:
    return (uint32_t)size * 2;
  case GL_UNSIGNED_BYTE:
    return (uint32_t)size;
  default:
    *valid = false;
    return 0;
  }
}

static GX2AttribFormat map_attrib_format(GLint size, GLenum type,
                                         GLboolean normalized, bool *valid) {
  *valid = true;
  switch (type) {
  case GL_FLOAT:
    if (size == 1)
      return GX2_ATTRIB_FORMAT_FLOAT_32;
    if (size == 2)
      return GX2_ATTRIB_FORMAT_FLOAT_32_32;
    if (size == 3)
      return GX2_ATTRIB_FORMAT_FLOAT_32_32_32;
    if (size == 4)
      return GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32;
    break;
  case GL_HALF_FLOAT:
    if (size == 1)
      return (GX2AttribFormat)(GX2_ATTRIB_FLAG_SCALED | GX2_ATTRIB_TYPE_16_FLOAT);
    if (size == 2)
      return (GX2AttribFormat)(GX2_ATTRIB_FLAG_SCALED | GX2_ATTRIB_TYPE_16_16_FLOAT);
    if (size == 4)
      return (GX2AttribFormat)(GX2_ATTRIB_FLAG_SCALED | GX2_ATTRIB_TYPE_16_16_16_16_FLOAT);
    break;
  case GL_UNSIGNED_BYTE:
    if (size == 1)
      return normalized ? GX2_ATTRIB_FORMAT_UNORM_8
                        : (GX2AttribFormat)(GX2_ATTRIB_FLAG_SCALED |
                                            GX2_ATTRIB_TYPE_8);
    if (size == 2)
      return normalized ? GX2_ATTRIB_FORMAT_UNORM_8_8
                        : (GX2AttribFormat)(GX2_ATTRIB_FLAG_SCALED |
                                            GX2_ATTRIB_TYPE_8_8);
    if (size == 4)
      return normalized ? GX2_ATTRIB_FORMAT_UNORM_8_8_8_8
                        : (GX2AttribFormat)(GX2_ATTRIB_FLAG_SCALED |
                                            GX2_ATTRIB_TYPE_8_8_8_8);
    break;
  case GL_UNSIGNED_SHORT:
    if (size == 1)
      return normalized ? GX2_ATTRIB_TYPE_16
                        : (GX2AttribFormat)(GX2_ATTRIB_FLAG_SCALED |
                                            GX2_ATTRIB_TYPE_16);
    if (size == 2)
      return normalized ? GX2_ATTRIB_TYPE_16_16
                        : (GX2AttribFormat)(GX2_ATTRIB_FLAG_SCALED |
                                            GX2_ATTRIB_TYPE_16_16);
    if (size == 4)
      return normalized ? GX2_ATTRIB_TYPE_16_16_16_16
                        : (GX2AttribFormat)(GX2_ATTRIB_FLAG_SCALED |
                                            GX2_ATTRIB_TYPE_16_16_16_16);
    break;
  }
  *valid = false;
  return GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32;
}

void _gl_VertexAttribPointer(GLuint index, GLint size, GLenum type,
                             GLboolean normalized, GLsizei stride,
                             const GLvoid *pointer) {
  if (!g_gl_context || index >= MAX_VERTEX_ATTRIBS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (size < 1 || size > 4) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  GLuint vao = g_gl_context->bound_vao;
  g_vaos[vao].attribs[index].buffer = g_gl_context->bound_array_buffer;
  g_vaos[vao].attribs[index].size = size;
  g_vaos[vao].attribs[index].type = type;
  g_vaos[vao].attribs[index].normalized = normalized;
  g_vaos[vao].attribs[index].stride = stride;
  g_vaos[vao].attribs[index].pointer = pointer;
  g_vaos[vao].dirty = true;
  g_gl_context->dirty_flags |= GL_DIRTY_VAO;
}

void _gl_VertexAttribDivisor(GLuint index, GLuint divisor) {
  GLuint vao;

  if (!g_gl_context || index >= MAX_VERTEX_ATTRIBS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  vao = g_gl_context->bound_vao;
  g_vaos[vao].attribs[index].divisor = divisor;
  g_vaos[vao].dirty = true;
  g_gl_context->dirty_flags |= GL_DIRTY_VAO;
}

void gl_bind_vao(void) {
  if (!g_gl_context || !(g_gl_context->dirty_flags & GL_DIRTY_VAO))
    return;

  GLuint vao_id = g_gl_context->bound_vao;
  GLVertexArray *vao = &g_vaos[vao_id];

  if (vao->dirty) {
    /* Recompile fetch shader */
    GX2AttribStream attribs[MAX_VERTEX_ATTRIBS];
    uint32_t attrib_count = 0;

    for (uint32_t i = 0; i < MAX_VERTEX_ATTRIBS; i++) {
      if (vao->attribs[i].enabled) {
        bool valid = false;
        GX2AttribFormat format =
            map_attrib_format(vao->attribs[i].size, vao->attribs[i].type,
                              vao->attribs[i].normalized, &valid);
        if (!valid) {
          /* Fallback to float format to prevent crashing */
          format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32;
        }

        uint32_t stride = vao->attribs[i].stride;
        if (stride == 0) {
          bool stride_valid = false;
          stride = get_attrib_stride_bytes(vao->attribs[i].size,
                                           vao->attribs[i].type,
                                           &stride_valid);
          if (!stride_valid) {
            stride = (uint32_t)vao->attribs[i].size * 4;
          }
        }

        attribs[attrib_count].location = i;
        attribs[attrib_count].buffer =
            i; /* Buffer index bindings map 1:1 with attribute index for now */
        attribs[attrib_count].offset = (uint32_t)vao->attribs[i].pointer;
        attribs[attrib_count].format = format;
        attribs[attrib_count].type =
            vao->attribs[i].divisor > 0 ? GX2_ATTRIB_INDEX_PER_INSTANCE
                                        : GX2_ATTRIB_INDEX_PER_VERTEX;
        attribs[attrib_count].aluDivisor = vao->attribs[i].divisor;
        attribs[attrib_count].mask = ((1u << vao->attribs[i].size) - 1u) << 16;
        attribs[attrib_count].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

        attrib_count++;
      }
    }

    if (vao->fetch_program) {
      gl_mem_free(GL_MEM_TYPE_MEM2, vao->fetch_program);
      vao->fetch_program = NULL;
    }

    uint32_t fs_size = GX2CalcFetchShaderSizeEx(
        attrib_count, GX2_FETCH_SHADER_TESSELLATION_NONE,
        GX2_TESSELLATION_MODE_DISCRETE);
    if (fs_size > 0 && attrib_count > 0) {
      vao->fetch_program = gl_mem_alloc(GL_MEM_TYPE_MEM2, fs_size, 256);
      GX2InitFetchShaderEx(&vao->fetch_shader, (uint8_t *)vao->fetch_program,
                           attrib_count, attribs,
                           GX2_FETCH_SHADER_TESSELLATION_NONE,
                           GX2_TESSELLATION_MODE_DISCRETE);
      DCFlushRange(vao->fetch_program, fs_size);
    }

    vao->dirty = false;
  }

  if (vao->fetch_program) {
    GX2SetFetchShader(&vao->fetch_shader);
  }

  for (uint32_t i = 0; i < MAX_VERTEX_ATTRIBS; i++) {
    if (!vao->attribs[i].enabled) {
      continue;
    }

    GLVertexAttrib *attrib = &vao->attribs[i];
    void *buffer_data = gl_buffer_get_data(attrib->buffer);
    GLsizeiptr buffer_size = gl_buffer_get_size(attrib->buffer);
    uintptr_t offset = (uintptr_t)attrib->pointer;
    if (!buffer_data || offset >= (uintptr_t)buffer_size) {
      continue;
    }

    uint32_t stride = attrib->stride;
    if (stride == 0) {
      bool stride_valid = false;
      stride = get_attrib_stride_bytes(attrib->size, attrib->type, &stride_valid);
      if (!stride_valid) {
        continue;
      }
    }

    GX2SetAttribBuffer(i, (uint32_t)(buffer_size - (GLsizeiptr)offset), stride,
                       (uint8_t *)buffer_data + offset);
  }
}
void gl_vao_set_element_array_buffer(GLuint buffer) {
  if (!g_gl_context)
    return;
  g_gl_context->bound_element_array_buffer = buffer;
  g_vaos[g_gl_context->bound_vao].element_array_buffer = buffer;
  g_gl_context->dirty_flags |= GL_DIRTY_VAO;
}

GLuint gl_vao_get_element_array_buffer(void) {
  if (!g_gl_context)
    return 0;
  return g_vaos[g_gl_context->bound_vao].element_array_buffer;
}

#ifdef __cplusplus
}
#endif
