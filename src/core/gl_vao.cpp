#include "gl_vao.h"
#include "gl_buffer.h"
#include "gl_context.h"
#include "state/gl_state.h"
#include <gx2/draw.h>
#include <gx2/registers.h>
#include <gx2/shaders.h>
#include <gx2/mem.h>
#include <string.h>
#include <stdlib.h>

#define MAX_VAOS 128

typedef struct {
  GLuint buffer;
  GLint size;
  GLenum type;
  GLboolean normalized;
  bool is_integer;
  GLsizei stride;
  const GLvoid *pointer;
  GLuint divisor;
} GLVertexAttrib;

typedef struct {
  bool in_use;
  GLVertexAttrib attribs[GL33_MAX_VERTEX_ATTRIBS];
  GLboolean attrib_enabled[GL33_MAX_VERTEX_ATTRIBS];
  GLuint element_array_buffer;
  bool dirty;
} GLVAO;

static GLVAO g_vaos[MAX_VAOS];

void gl_vao_init(void) {
  memset(g_vaos, 0, sizeof(g_vaos));
  g_vaos[0].in_use = true;
  for (int i = 0; i < GL33_MAX_VERTEX_ATTRIBS; i++) {
    g_vaos[0].attribs[i].size = 4;
    g_vaos[0].attribs[i].type = GL_FLOAT;
  }
}

#ifdef __cplusplus
extern "C" {
#endif

static void set_current_vertex_attrib(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
  if (index >= GL33_MAX_VERTEX_ATTRIBS) { _gl_set_error(GL_INVALID_VALUE); return; }
  g_gl_context->current_vertex_attrib[index][0] = x;
  g_gl_context->current_vertex_attrib[index][1] = y;
  g_gl_context->current_vertex_attrib[index][2] = z;
  g_gl_context->current_vertex_attrib[index][3] = w;
  g_gl_context->dirty_flags |= GL_DIRTY_VAO;
}

void _gl_GenVertexArrays(GLsizei n, GLuint *arrays) {
  if (n < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
  int count = 0;
  for (int i = 1; i < MAX_VAOS && count < n; i++) {
    if (!g_vaos[i].in_use) {
      memset(&g_vaos[i], 0, sizeof(GLVAO));
      g_vaos[i].in_use = true;
      for (int j = 0; j < GL33_MAX_VERTEX_ATTRIBS; j++) {
        g_vaos[i].attribs[j].size = 4;
        g_vaos[i].attribs[j].type = GL_FLOAT;
      }
      arrays[count++] = i;
    }
  }
}

void _gl_DeleteVertexArrays(GLsizei n, const GLuint *arrays) {
  if (n < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
  for (int i = 0; i < n; i++) {
    GLuint id = arrays[i];
    if (id > 0 && id < MAX_VAOS) g_vaos[id].in_use = false;
  }
}

GLboolean _gl_IsVertexArray(GLuint array) { return (array < MAX_VAOS && g_vaos[array].in_use) ? GL_TRUE : GL_FALSE; }

void _gl_BindVertexArray(GLuint array) {
  if (array >= MAX_VAOS || (array > 0 && !g_vaos[array].in_use)) { _gl_set_error(GL_INVALID_OPERATION); return; }
  if (g_gl_context) {
    g_gl_context->bound_vao = array;
    g_gl_context->dirty_flags |= GL_DIRTY_VAO;
  }
}

void _gl_EnableVertexAttribArray(GLuint index) {
  if (index >= GL33_MAX_VERTEX_ATTRIBS) { _gl_set_error(GL_INVALID_VALUE); return; }
  GLuint vao = g_gl_context->bound_vao;
  g_vaos[vao].attrib_enabled[index] = GL_TRUE;
  g_vaos[vao].dirty = true;
  g_gl_context->dirty_flags |= GL_DIRTY_VAO;
}

void _gl_DisableVertexAttribArray(GLuint index) {
  if (index >= GL33_MAX_VERTEX_ATTRIBS) { _gl_set_error(GL_INVALID_VALUE); return; }
  GLuint vao = g_gl_context->bound_vao;
  g_vaos[vao].attrib_enabled[index] = GL_FALSE;
  g_vaos[vao].dirty = true;
  g_gl_context->dirty_flags |= GL_DIRTY_VAO;
}

void _gl_GetVertexAttribfv(GLuint index, GLenum pname, GLfloat *params) {
  if (index >= GL33_MAX_VERTEX_ATTRIBS) { _gl_set_error(GL_INVALID_VALUE); return; }
  GLuint vao = g_gl_context->bound_vao;
  switch (pname) {
  case GL_CURRENT_VERTEX_ATTRIB:
    memcpy(params, g_gl_context->current_vertex_attrib[index], 4 * sizeof(GLfloat));
    break;
  case GL_VERTEX_ATTRIB_ARRAY_ENABLED:
    *params = (GLfloat)g_vaos[vao].attrib_enabled[index];
    break;
  case GL_VERTEX_ATTRIB_ARRAY_SIZE:
    *params = (GLfloat)g_vaos[vao].attribs[index].size;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_STRIDE:
    *params = (GLfloat)g_vaos[vao].attribs[index].stride;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_TYPE:
    *params = (GLfloat)g_vaos[vao].attribs[index].type;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
    *params = (GLfloat)g_vaos[vao].attribs[index].normalized;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING:
    *params = (GLfloat)g_vaos[vao].attribs[index].buffer;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_DIVISOR:
    *params = (GLfloat)g_vaos[vao].attribs[index].divisor;
    break;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    break;
  }
}

void _gl_GetVertexAttribiv(GLuint index, GLenum pname, GLint *params) {
    GLfloat fparams[4];
    _gl_GetVertexAttribfv(index, pname, fparams);
    if (pname == GL_CURRENT_VERTEX_ATTRIB) {
        for(int i=0; i<4; i++) params[i] = (GLint)fparams[i];
    } else {
        *params = (GLint)fparams[0];
    }
}

void _gl_GetVertexAttribPointerv(GLuint index, GLenum pname, GLvoid **pointer) {
  if (index >= GL33_MAX_VERTEX_ATTRIBS) { _gl_set_error(GL_INVALID_VALUE); return; }
  if (pname != GL_VERTEX_ATTRIB_ARRAY_POINTER) { _gl_set_error(GL_INVALID_ENUM); return; }
  GLuint vao = g_gl_context->bound_vao;
  *pointer = (GLvoid *)g_vaos[vao].attribs[index].pointer;
}

void _gl_VertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid *pointer) {
  if (!g_gl_context || index >= GL33_MAX_VERTEX_ATTRIBS) { _gl_set_error(GL_INVALID_VALUE); return; }
  if (size < 1 || size > 4) { _gl_set_error(GL_INVALID_VALUE); return; }
  GLuint vao = g_gl_context->bound_vao;
  g_vaos[vao].attribs[index].buffer = g_gl_context->bound_array_buffer;
  g_vaos[vao].attribs[index].size = size;
  g_vaos[vao].attribs[index].type = type;
  g_vaos[vao].attribs[index].normalized = normalized;
  g_vaos[vao].attribs[index].is_integer = false;
  g_vaos[vao].attribs[index].stride = stride;
  g_vaos[vao].attribs[index].pointer = pointer;
  g_vaos[vao].dirty = true;
  g_gl_context->dirty_flags |= GL_DIRTY_VAO;
}

void _gl_VertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {
  if (!g_gl_context || index >= GL33_MAX_VERTEX_ATTRIBS) { _gl_set_error(GL_INVALID_VALUE); return; }
  if (size < 1 || size > 4) { _gl_set_error(GL_INVALID_VALUE); return; }
  GLuint vao = g_gl_context->bound_vao;
  g_vaos[vao].attribs[index].buffer = g_gl_context->bound_array_buffer;
  g_vaos[vao].attribs[index].size = size;
  g_vaos[vao].attribs[index].type = type;
  g_vaos[vao].attribs[index].normalized = GL_FALSE;
  g_vaos[vao].attribs[index].is_integer = true;
  g_vaos[vao].attribs[index].stride = stride;
  g_vaos[vao].attribs[index].pointer = pointer;
  g_vaos[vao].dirty = true;
  g_gl_context->dirty_flags |= GL_DIRTY_VAO;
}

void _gl_GetVertexAttribIiv(GLuint index, GLenum pname, GLint *params) { _gl_GetVertexAttribiv(index, pname, params); }
void _gl_GetVertexAttribIuiv(GLuint index, GLenum pname, GLuint *params) { _gl_GetVertexAttribiv(index, pname, (GLint *)params); }

void _gl_VertexAttrib1f(GLuint i, GLfloat x)                                    { set_current_vertex_attrib(i, x, 0.0f, 0.0f, 1.0f); }
void _gl_VertexAttrib2f(GLuint i, GLfloat x, GLfloat y)                          { set_current_vertex_attrib(i, x, y, 0.0f, 1.0f); }
void _gl_VertexAttrib3f(GLuint i, GLfloat x, GLfloat y, GLfloat z)              { set_current_vertex_attrib(i, x, y, z, 1.0f); }
void _gl_VertexAttrib4f(GLuint i, GLfloat x, GLfloat y, GLfloat z, GLfloat w)  { set_current_vertex_attrib(i, x, y, z, w); }
void _gl_VertexAttrib1fv(GLuint i, const GLfloat *v) { if(v) _gl_VertexAttrib1f(i,v[0]); }
void _gl_VertexAttrib2fv(GLuint i, const GLfloat *v) { if(v) _gl_VertexAttrib2f(i,v[0],v[1]); }
void _gl_VertexAttrib3fv(GLuint i, const GLfloat *v) { if(v) _gl_VertexAttrib3f(i,v[0],v[1],v[2]); }
void _gl_VertexAttrib4fv(GLuint i, const GLfloat *v) { if(v) _gl_VertexAttrib4f(i,v[0],v[1],v[2],v[3]); }
void _gl_VertexAttribI1i(GLuint index, GLint x) { set_current_vertex_attrib(index, (GLfloat)x, 0.0f, 0.0f, 1.0f); }
void _gl_VertexAttribI2i(GLuint index, GLint x, GLint y) { set_current_vertex_attrib(index, (GLfloat)x, (GLfloat)y, 0.0f, 1.0f); }
void _gl_VertexAttribI3i(GLuint index, GLint x, GLint y, GLint z) { set_current_vertex_attrib(index, (GLfloat)x, (GLfloat)y, (GLfloat)z, 1.0f); }
void _gl_VertexAttribI4i(GLuint index, GLint x, GLint y, GLint z, GLint w) { set_current_vertex_attrib(index, (GLfloat)x, (GLfloat)y, (GLfloat)z, (GLfloat)w); }
void _gl_VertexAttribI1ui(GLuint index, GLuint x) { set_current_vertex_attrib(index, (GLfloat)x, 0.0f, 0.0f, 1.0f); }
void _gl_VertexAttribI2ui(GLuint index, GLuint x, GLuint y) { set_current_vertex_attrib(index, (GLfloat)x, (GLfloat)y, 0.0f, 1.0f); }
void _gl_VertexAttribI3ui(GLuint index, GLuint x, GLuint y, GLuint z) { set_current_vertex_attrib(index, (GLfloat)x, (GLfloat)y, (GLfloat)z, 1.0f); }
void _gl_VertexAttribI4ui(GLuint index, GLuint x, GLuint y, GLuint z, GLuint w) { set_current_vertex_attrib(index, (GLfloat)x, (GLfloat)y, (GLfloat)z, (GLfloat)w); }

void _gl_VertexAttribI1iv(GLuint index, const GLint *v) { if(v) _gl_VertexAttribI1i(index, v[0]); else _gl_set_error(GL_INVALID_VALUE); }
void _gl_VertexAttribI2iv(GLuint index, const GLint *v) { if(v) _gl_VertexAttribI2i(index, v[0], v[1]); else _gl_set_error(GL_INVALID_VALUE); }
void _gl_VertexAttribI3iv(GLuint index, const GLint *v) { if(v) _gl_VertexAttribI3i(index, v[0], v[1], v[2]); else _gl_set_error(GL_INVALID_VALUE); }
void _gl_VertexAttribI4iv(GLuint index, const GLint *v) { if(v) _gl_VertexAttribI4i(index, v[0], v[1], v[2], v[3]); else _gl_set_error(GL_INVALID_VALUE); }
void _gl_VertexAttribI1uiv(GLuint index, const GLuint *v) { if(v) _gl_VertexAttribI1ui(index, v[0]); else _gl_set_error(GL_INVALID_VALUE); }
void _gl_VertexAttribI2uiv(GLuint index, const GLuint *v) { if(v) _gl_VertexAttribI2ui(index, v[0], v[1]); else _gl_set_error(GL_INVALID_VALUE); }
void _gl_VertexAttribI3uiv(GLuint index, const GLuint *v) { if(v) _gl_VertexAttribI3ui(index, v[0], v[1], v[2]); else _gl_set_error(GL_INVALID_VALUE); }
void _gl_VertexAttribI4uiv(GLuint index, const GLuint *v) { if(v) _gl_VertexAttribI4ui(index, v[0], v[1], v[2], v[3]); else _gl_set_error(GL_INVALID_VALUE); }

void _gl_VertexAttribDivisor(GLuint index, GLuint divisor) {
  if (index >= GL33_MAX_VERTEX_ATTRIBS) { _gl_set_error(GL_INVALID_VALUE); return; }
  GLuint vao = g_gl_context->bound_vao;
  g_vaos[vao].attribs[index].divisor = divisor;
  g_vaos[vao].dirty = true;
  g_gl_context->dirty_flags |= GL_DIRTY_VAO;
}

static uint32_t gl_type_size(GLenum type) {
  switch (type) {
  case GL_FLOAT:          return 4;
  case GL_HALF_FLOAT:     return 2;
  case GL_UNSIGNED_BYTE:  return 1;
  case GL_BYTE:           return 1;
  case GL_UNSIGNED_SHORT: return 2;
  case GL_SHORT:          return 2;
  case GL_UNSIGNED_INT:   return 4;
  case GL_INT:            return 4;
  default:                return 4;
  }
}

void gl_bind_vao(void) {
  if (!g_gl_context) return;
  GLuint vao_id = g_gl_context->bound_vao;
  if (vao_id >= MAX_VAOS || !g_vaos[vao_id].in_use) return;
  GLVAO *vao = &g_vaos[vao_id];
  for (uint32_t i = 0; i < GL33_MAX_VERTEX_ATTRIBS; ++i) {
    if (!vao->attrib_enabled[i]) continue;
    GLVertexAttrib *a = &vao->attribs[i];
    GX2RBuffer *gx2r = gl_buffer_get_gx2r_buffer(a->buffer);
    if (!gx2r || !gx2r->buffer) continue;
    uint32_t elem_bytes = (uint32_t)gl_type_size(a->type) * (uint32_t)a->size;
    uint32_t stride = a->stride > 0 ? (uint32_t)a->stride : elem_bytes;
    uint32_t offset = (uint32_t)(uintptr_t)a->pointer;
    uint8_t *base = (uint8_t*)gx2r->buffer + offset;
    uint32_t avail = gx2r->elemCount > offset ? gx2r->elemCount - offset : 0;
    GX2SetAttribBuffer(i, avail, stride, base);
  }
}

void gl_vao_set_element_array_buffer(GLuint buffer) {
  if (!g_gl_context) return;
  GLuint v = g_gl_context->bound_vao;
  if (v < MAX_VAOS && g_vaos[v].in_use) g_vaos[v].element_array_buffer = buffer;
}
GLuint gl_vao_get_element_array_buffer(void) {
  if (!g_gl_context) return 0;
  GLuint v = g_gl_context->bound_vao;
  return (v < MAX_VAOS && g_vaos[v].in_use) ? g_vaos[v].element_array_buffer : 0;
}

#ifdef __cplusplus
}
#endif
