#include "gl_vao.h"
#include "gl_buffer.h"
#include "mem/gl_mem.h"
#include "state/gl_state.h"
#include <coreinit/cache.h>
#include <gx2/draw.h>
#include <gx2/enum.h>
#include <gx2/mem.h>
#include <gx2/state.h>
#include <gx2/utils.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_VAOS 512

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
  GLVertexAttrib attribs[GL33_MAX_VERTEX_ATTRIBS];
  GLuint element_array_buffer; // EBO binding id

  bool dirty; // Fetch state dirty
  GX2FetchShader fetch_shader;
  void *fetch_program;
} GLVertexArray;

static GLVertexArray g_vaos[MAX_VAOS];

void gl_vao_init(void) {
  memset(g_vaos, 0, sizeof(g_vaos));
  // Reserve default VAO
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
        g_gl_context->bound_vao = 0; // Reset to default
        g_gl_context->dirty_flags |= GL_DIRTY_VAO;
      }
    }
  }
}

GLboolean _gl_IsVertexArray(GLuint array) {
  return (array < MAX_VAOS && g_vaos[array].in_use) ? GL_TRUE : GL_FALSE;
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
  if (!g_gl_context || index >= GL33_MAX_VERTEX_ATTRIBS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  GLuint vao = g_gl_context->bound_vao;
  g_vaos[vao].attribs[index].enabled = true;
  g_vaos[vao].dirty = true;
  g_gl_context->dirty_flags |= GL_DIRTY_VAO;
}

void _gl_DisableVertexAttribArray(GLuint index) {
  if (!g_gl_context || index >= GL33_MAX_VERTEX_ATTRIBS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  GLuint vao = g_gl_context->bound_vao;
  g_vaos[vao].attribs[index].enabled = false;
  g_vaos[vao].dirty = true;
  g_gl_context->dirty_flags |= GL_DIRTY_VAO;
}

static void set_current_vertex_attrib(GLuint index, GLfloat x, GLfloat y,
                                      GLfloat z, GLfloat w) {
  if (!g_gl_context || index >= GL33_MAX_VERTEX_ATTRIBS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  g_gl_context->current_vertex_attrib[index][0] = x;
  g_gl_context->current_vertex_attrib[index][1] = y;
  g_gl_context->current_vertex_attrib[index][2] = z;
  g_gl_context->current_vertex_attrib[index][3] = w;
}

void _gl_GetVertexAttribfv(GLuint index, GLenum pname, GLfloat *params) {
  GLVertexAttrib *attrib;

  if (!g_gl_context || !params) {
    return;
  }
  if (index >= GL33_MAX_VERTEX_ATTRIBS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  attrib = &g_vaos[g_gl_context->bound_vao].attribs[index];
  switch (pname) {
  case GL_CURRENT_VERTEX_ATTRIB:
    params[0] = g_gl_context->current_vertex_attrib[index][0];
    params[1] = g_gl_context->current_vertex_attrib[index][1];
    params[2] = g_gl_context->current_vertex_attrib[index][2];
    params[3] = g_gl_context->current_vertex_attrib[index][3];
    break;
  case GL_VERTEX_ATTRIB_ARRAY_ENABLED:
    params[0] = attrib->enabled ? 1.0f : 0.0f;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_SIZE:
    params[0] = (GLfloat)attrib->size;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_STRIDE:
    params[0] = (GLfloat)attrib->stride;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_TYPE:
    params[0] = (GLfloat)attrib->type;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
    params[0] = attrib->normalized ? 1.0f : 0.0f;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING:
    params[0] = (GLfloat)attrib->buffer;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_INTEGER:
    params[0] = 0.0f;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_DIVISOR:
    params[0] = (GLfloat)attrib->divisor;
    break;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    break;
  }
}

void _gl_GetVertexAttribiv(GLuint index, GLenum pname, GLint *params) {
  GLVertexAttrib *attrib;

  if (!g_gl_context || !params) {
    return;
  }
  if (index >= GL33_MAX_VERTEX_ATTRIBS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  attrib = &g_vaos[g_gl_context->bound_vao].attribs[index];
  switch (pname) {
  case GL_CURRENT_VERTEX_ATTRIB:
    params[0] = (GLint)g_gl_context->current_vertex_attrib[index][0];
    params[1] = (GLint)g_gl_context->current_vertex_attrib[index][1];
    params[2] = (GLint)g_gl_context->current_vertex_attrib[index][2];
    params[3] = (GLint)g_gl_context->current_vertex_attrib[index][3];
    break;
  case GL_VERTEX_ATTRIB_ARRAY_ENABLED:
    *params = attrib->enabled ? GL_TRUE : GL_FALSE;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_SIZE:
    *params = attrib->size;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_STRIDE:
    *params = attrib->stride;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_TYPE:
    *params = (GLint)attrib->type;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
    *params = attrib->normalized ? GL_TRUE : GL_FALSE;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING:
    *params = (GLint)attrib->buffer;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_INTEGER:
    *params = GL_FALSE;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_DIVISOR:
    *params = (GLint)attrib->divisor;
    break;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    break;
  }
}

void _gl_GetVertexAttribPointerv(GLuint index, GLenum pname, GLvoid **pointer) {
  if (!g_gl_context || !pointer) {
    return;
  }
  if (index >= GL33_MAX_VERTEX_ATTRIBS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (pname != GL_VERTEX_ATTRIB_ARRAY_POINTER) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  *pointer = (GLvoid *)g_vaos[g_gl_context->bound_vao].attribs[index].pointer;
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

static uint32_t get_attrib_selector_mask(GLint size) {
  switch (size) {
  case 1:
    return GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_0, GX2_SQ_SEL_0,
                        GX2_SQ_SEL_1);
  case 2:
    return GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0,
                        GX2_SQ_SEL_1);
  case 3:
    return GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z,
                        GX2_SQ_SEL_1);
  case 4:
    return GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z,
                        GX2_SQ_SEL_W);
  default:
    return GX2_SEL_MASK(GX2_SQ_SEL_0, GX2_SQ_SEL_0, GX2_SQ_SEL_0,
                        GX2_SQ_SEL_1);
  }
}

void _gl_VertexAttrib1f(GLuint index, GLfloat x) {
  set_current_vertex_attrib(index, x, 0.0f, 0.0f, 1.0f);
}

void _gl_VertexAttrib1fv(GLuint index, const GLfloat *v) {
  if (!v) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  set_current_vertex_attrib(index, v[0], 0.0f, 0.0f, 1.0f);
}

void _gl_VertexAttrib2f(GLuint index, GLfloat x, GLfloat y) {
  set_current_vertex_attrib(index, x, y, 0.0f, 1.0f);
}

void _gl_VertexAttrib2fv(GLuint index, const GLfloat *v) {
  if (!v) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  set_current_vertex_attrib(index, v[0], v[1], 0.0f, 1.0f);
}

void _gl_VertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z) {
  set_current_vertex_attrib(index, x, y, z, 1.0f);
}

void _gl_VertexAttrib3fv(GLuint index, const GLfloat *v) {
  if (!v) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  set_current_vertex_attrib(index, v[0], v[1], v[2], 1.0f);
}

void _gl_VertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z,
                        GLfloat w) {
  set_current_vertex_attrib(index, x, y, z, w);
}

void _gl_VertexAttrib4fv(GLuint index, const GLfloat *v) {
  if (!v) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  set_current_vertex_attrib(index, v[0], v[1], v[2], v[3]);
}

void _gl_VertexAttribPointer(GLuint index, GLint size, GLenum type,
                             GLboolean normalized, GLsizei stride,
                             const GLvoid *pointer) {
  if (!g_gl_context || index >= GL33_MAX_VERTEX_ATTRIBS) {
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

  if (!g_gl_context || index >= GL33_MAX_VERTEX_ATTRIBS) {
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
    // Rebuild fetch shader
    GX2AttribStream attribs[GL33_MAX_VERTEX_ATTRIBS];
    uint32_t attrib_count = 0;

    for (uint32_t i = 0; i < GL33_MAX_VERTEX_ATTRIBS; i++) {
      if (vao->attribs[i].enabled) {
        bool valid = false;
        GX2AttribFormat format =
            map_attrib_format(vao->attribs[i].size, vao->attribs[i].type,
                              vao->attribs[i].normalized, &valid);
        if (!valid) {
          // Fallback float format
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
            i; 
        attribs[attrib_count].offset = (uint32_t)vao->attribs[i].pointer;
        attribs[attrib_count].format = format;
        attribs[attrib_count].type =
            vao->attribs[i].divisor > 0 ? GX2_ATTRIB_INDEX_PER_INSTANCE
                                        : GX2_ATTRIB_INDEX_PER_VERTEX;
        attribs[attrib_count].aluDivisor = vao->attribs[i].divisor;
        attribs[attrib_count].mask =
            get_attrib_selector_mask(vao->attribs[i].size);
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

  for (uint32_t i = 0; i < GL33_MAX_VERTEX_ATTRIBS; i++) {
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
