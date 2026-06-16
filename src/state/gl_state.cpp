#include "gl_state.h"
#include "core/gl_framebuffer.h"
#include "core/gl_shader.h"
#include "core/gl_texture.h"
#include "core/gl_vao.h"
#include "endian/endian.h"
#ifdef __cplusplus
extern "C" {
#endif
#include <gx2/state.h>
#include <gx2/registers.h>
#include <gx2/mem.h>
#include <gx2/clear.h>
#include <coreinit/cache.h>
#include <string.h>

static int stencil_face_index(GLenum face) {
    return (face == GL_BACK) ? 1 : 0;
}

static GX2CompareFunction map_compare_func(GLenum func, bool *valid) {
  *valid = true;
  switch (func) {
  case GL_NEVER: return GX2_COMPARE_FUNC_NEVER;
  case GL_LESS: return GX2_COMPARE_FUNC_LESS;
  case GL_EQUAL: return GX2_COMPARE_FUNC_EQUAL;
  case GL_LEQUAL: return GX2_COMPARE_FUNC_LEQUAL;
  case GL_GREATER: return GX2_COMPARE_FUNC_GREATER;
  case GL_NOTEQUAL: return GX2_COMPARE_FUNC_NOT_EQUAL;
  case GL_GEQUAL: return GX2_COMPARE_FUNC_GEQUAL;
  case GL_ALWAYS: return GX2_COMPARE_FUNC_ALWAYS;
  default: *valid = false; return GX2_COMPARE_FUNC_ALWAYS;
  }
}

static GX2BlendMode map_blend_factor(GLenum factor, bool *valid) {
  *valid = true;
  switch (factor) {
  case GL_ZERO: return GX2_BLEND_MODE_ZERO;
  case GL_ONE: return GX2_BLEND_MODE_ONE;
  case GL_SRC_COLOR: return GX2_BLEND_MODE_SRC_COLOR;
  case GL_ONE_MINUS_SRC_COLOR: return GX2_BLEND_MODE_INV_SRC_COLOR;
  case GL_SRC_ALPHA: return GX2_BLEND_MODE_SRC_ALPHA;
  case GL_ONE_MINUS_SRC_ALPHA: return GX2_BLEND_MODE_INV_SRC_ALPHA;
  case GL_DST_ALPHA: return GX2_BLEND_MODE_DST_ALPHA;
  case GL_ONE_MINUS_DST_ALPHA: return GX2_BLEND_MODE_INV_DST_ALPHA;
  case GL_DST_COLOR: return GX2_BLEND_MODE_DST_COLOR;
  case GL_ONE_MINUS_DST_COLOR: return GX2_BLEND_MODE_INV_DST_COLOR;
  case GL_SRC_ALPHA_SATURATE: return GX2_BLEND_MODE_SRC_ALPHA_SAT;
  case GL_CONSTANT_COLOR: return GX2_BLEND_MODE_BLEND_FACTOR;
  case GL_ONE_MINUS_CONSTANT_COLOR: return GX2_BLEND_MODE_INV_BLEND_FACTOR;
  case GL_CONSTANT_ALPHA: return GX2_BLEND_MODE_BLEND_FACTOR;
  case GL_ONE_MINUS_CONSTANT_ALPHA: return GX2_BLEND_MODE_INV_BLEND_FACTOR;
  default: *valid = false; return GX2_BLEND_MODE_ONE;
  }
}

static GX2BlendCombineMode map_blend_eq(GLenum mode, bool *valid) {
  *valid = true;
  switch (mode) {
  case GL_FUNC_ADD: return GX2_BLEND_COMBINE_MODE_ADD;
  case GL_FUNC_SUBTRACT: return GX2_BLEND_COMBINE_MODE_SUB;
  case GL_FUNC_REVERSE_SUBTRACT: return GX2_BLEND_COMBINE_MODE_REV_SUB;
  case GL_MIN: return GX2_BLEND_COMBINE_MODE_MIN;
  case GL_MAX: return GX2_BLEND_COMBINE_MODE_MAX;
  default: *valid = false; return GX2_BLEND_COMBINE_MODE_ADD;
  }
}

static GX2StencilFunction map_stencil_op(GLenum op, bool *valid) {
  *valid = true;
  switch (op) {
  case GL_KEEP: return GX2_STENCIL_FUNCTION_KEEP;
  case GL_ZERO: return GX2_STENCIL_FUNCTION_ZERO;
  case GL_REPLACE: return GX2_STENCIL_FUNCTION_REPLACE;
  case GL_INCR: return GX2_STENCIL_FUNCTION_INCR_CLAMP;
  case GL_DECR: return GX2_STENCIL_FUNCTION_DECR_CLAMP;
  case GL_INVERT: return GX2_STENCIL_FUNCTION_INV;
  case GL_INCR_WRAP: return GX2_STENCIL_FUNCTION_INCR_WRAP;
  case GL_DECR_WRAP: return GX2_STENCIL_FUNCTION_DECR_WRAP;
  default: *valid = false; return GX2_STENCIL_FUNCTION_KEEP;
  }
}

static GX2LogicOp map_logic_op(GLenum op) {
    switch (op) {
        case GL_CLEAR:         return GX2_LOGIC_OP_CLEAR;
        case GL_SET:           return GX2_LOGIC_OP_SET;
        case GL_COPY:          return GX2_LOGIC_OP_COPY;
        case GL_COPY_INVERTED: return GX2_LOGIC_OP_INV_COPY;
        case GL_NOOP:          return GX2_LOGIC_OP_NOP;
        case GL_INVERT:        return GX2_LOGIC_OP_INV;
        case GL_AND:           return GX2_LOGIC_OP_AND;
        case GL_NAND:          return GX2_LOGIC_OP_NOT_AND;
        case GL_OR:            return GX2_LOGIC_OP_OR;
        case GL_NOR:           return GX2_LOGIC_OP_NOR;
        case GL_XOR:           return GX2_LOGIC_OP_XOR;
        case GL_EQUIV:         return GX2_LOGIC_OP_EQUIV;
        case GL_AND_REVERSE:   return GX2_LOGIC_OP_REV_AND;
        case GL_AND_INVERTED:  return GX2_LOGIC_OP_INV_AND;
        case GL_OR_REVERSE:    return GX2_LOGIC_OP_REV_OR;
        case GL_OR_INVERTED:   return GX2_LOGIC_OP_INV_OR;
        default:               return GX2_LOGIC_OP_COPY;
    }
}

static GX2FrontFace map_front_face(GLenum mode) {
  return mode == GL_CW ? GX2_FRONT_FACE_CW : GX2_FRONT_FACE_CCW;
}

static uint8_t clamp_clear_unorm8(float value) {
  if (value <= 0.0f) return 0;
  if (value >= 1.0f) return 255;
  return (uint8_t)(value * 255.0f + 0.5f);
}

static bool cpu_clear_draw_color_buffer(GLuint index, const GLfloat *clear_color) {
  GX2ColorBuffer *cb;
  GX2Surface *surface;
  GLint x0;
  GLint y0;
  GLint x1;
  GLint y1;
  uint8_t r8;
  uint8_t g8;
  uint8_t b8;
  uint8_t a8;

  if (!g_gl_context || !clear_color || g_gl_context->bound_framebuffer == 0) {
    return false;
  }
  if (!gl_is_draw_color_buffer_enabled(index)) {
    return true;
  }

  cb = gl_get_draw_color_buffer(index);
  if (!cb || !cb->surface.image) {
    return false;
  }

  surface = &cb->surface;
  if (surface->tileMode != GX2_TILE_MODE_LINEAR_ALIGNED) {
    return false;
  }

  x0 = 0;
  y0 = 0;
  x1 = (GLint)surface->width;
  y1 = (GLint)surface->height;
  if (g_gl_context->scissor_test_enabled) {
    x0 = g_gl_context->scissor.x;
    y0 = g_gl_context->scissor.y;
    x1 = g_gl_context->scissor.x + g_gl_context->scissor.width;
    y1 = g_gl_context->scissor.y + g_gl_context->scissor.height;
  }

  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > (GLint)surface->width) x1 = (GLint)surface->width;
  if (y1 > (GLint)surface->height) y1 = (GLint)surface->height;
  if (x0 >= x1 || y0 >= y1) {
    return true;
  }

  r8 = clamp_clear_unorm8(clear_color[0]);
  g8 = clamp_clear_unorm8(clear_color[1]);
  b8 = clamp_clear_unorm8(clear_color[2]);
  a8 = clamp_clear_unorm8(clear_color[3]);

  for (GLint gl_y = y0; gl_y < y1; ++gl_y) {
    uint32_t surface_y = (uint32_t)gl_y;
    for (GLint gl_x = x0; gl_x < x1; ++gl_x) {
      switch (surface->format) {
      case GX2_SURFACE_FORMAT_UNORM_R8: {
        uint8_t *dst = (uint8_t *)surface->image +
                       (size_t)surface_y * (size_t)surface->pitch +
                       (size_t)gl_x;
        dst[0] = r8;
        break;
      }
      case GX2_SURFACE_FORMAT_UNORM_R8_G8: {
        uint8_t *dst = (uint8_t *)surface->image +
                       (((size_t)surface_y * (size_t)surface->pitch) +
                        (size_t)gl_x) * 2u;
        dst[0] = r8;
        dst[1] = g8;
        break;
      }
      case GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8: {
        uint32_t packed = CPU_TO_GPU_32(((uint32_t)r8 << 24) |
                                        ((uint32_t)g8 << 16) |
                                        ((uint32_t)b8 << 8) |
                                        (uint32_t)a8);
        uint8_t *dst = (uint8_t *)surface->image +
                       (((size_t)surface_y * (size_t)surface->pitch) +
                        (size_t)gl_x) * 4u;
        memcpy(dst, &packed, sizeof(packed));
        break;
      }
      default:
        return false;
      }
    }
  }

  DCFlushRange(surface->image, surface->imageSize);
  GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER |
                                    GX2_INVALIDATE_MODE_TEXTURE),
                surface->image, surface->imageSize);
  return true;
}

void _gl_ClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha) {
  if (g_gl_context) {
    g_gl_context->clear_color[0] = red; g_gl_context->clear_color[1] = green;
    g_gl_context->clear_color[2] = blue; g_gl_context->clear_color[3] = alpha;
  }
}

void _gl_ClearDepth(GLclampd depth) { if (g_gl_context) g_gl_context->clear_depth = (GLfloat)depth; }
void _gl_ClearStencil(GLint s) { if (g_gl_context) g_gl_context->clear_stencil = s; }

void _gl_Clear(GLbitfield mask) {
  if (!g_gl_context) return;
  if ((mask & ~(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) != 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  gl_flush_state();
  if (mask & GL_COLOR_BUFFER_BIT) {
    for (GLuint i = 0; i < 8; ++i) {
      GX2ColorBuffer *cb;
      if (!gl_is_draw_color_buffer_enabled(i)) continue;
      if (!cpu_clear_draw_color_buffer(i, g_gl_context->clear_color)) {
        cb = gl_get_draw_color_buffer(i);
        if (!cb) continue;
        GX2ClearColor(cb, g_gl_context->clear_color[0], g_gl_context->clear_color[1],
                      g_gl_context->clear_color[2], g_gl_context->clear_color[3]);
      }
    }
    gl_framebuffer_mark_bound_color_dirty();
  }
  if ((mask & GL_DEPTH_BUFFER_BIT) || (mask & GL_STENCIL_BUFFER_BIT)) {
    GX2ClearFlags flags = GX2_CLEAR_FLAGS_BOTH;
    GX2DepthBuffer *db = gl_get_draw_depth_buffer();
    if (mask & GL_DEPTH_BUFFER_BIT) {
      flags = (mask & GL_STENCIL_BUFFER_BIT) ? GX2_CLEAR_FLAGS_BOTH : GX2_CLEAR_FLAGS_DEPTH;
    } else {
      flags = GX2_CLEAR_FLAGS_STENCIL;
    }
    if (db) GX2ClearDepthStencilEx(db, (mask & GL_DEPTH_BUFFER_BIT) ? g_gl_context->clear_depth : db->depthClear,
                                 (mask & GL_STENCIL_BUFFER_BIT) ? (uint8_t)g_gl_context->clear_stencil : (uint8_t)db->stencilClear,
                                 flags);
  }
}

void _gl_Enable(GLenum cap) {
  if (!g_gl_context) return;
  switch (cap) {
  case GL_DEPTH_TEST: g_gl_context->depth_test_enabled = GL_TRUE; break;
  case GL_STENCIL_TEST: g_gl_context->stencil_test_enabled = GL_TRUE; break;
  case GL_BLEND: g_gl_context->blend_enabled = GL_TRUE; break;
  case GL_CULL_FACE: g_gl_context->cull_face_enabled = GL_TRUE; break;
  case GL_SCISSOR_TEST: g_gl_context->scissor_test_enabled = GL_TRUE; break;
  case GL_SAMPLE_COVERAGE: g_gl_context->sample_coverage_enabled = GL_TRUE; break;
  case GL_PRIMITIVE_RESTART: g_gl_context->primitive_restart_enabled = GL_TRUE; break;
  case GL_POLYGON_OFFSET_FILL: g_gl_context->polygon_offset_fill_enabled = GL_TRUE; break;
  case GL_POLYGON_OFFSET_LINE: g_gl_context->polygon_offset_line_enabled = GL_TRUE; break;
  case GL_POLYGON_OFFSET_POINT: g_gl_context->polygon_offset_point_enabled = GL_TRUE; break;
  default: _gl_set_error(GL_INVALID_ENUM); return;
  }
  g_gl_context->dirty_flags |= 0xFFFFFFFF;
}

void _gl_Disable(GLenum cap) {
  if (!g_gl_context) return;
  switch (cap) {
  case GL_DEPTH_TEST: g_gl_context->depth_test_enabled = GL_FALSE; break;
  case GL_STENCIL_TEST: g_gl_context->stencil_test_enabled = GL_FALSE; break;
  case GL_BLEND: g_gl_context->blend_enabled = GL_FALSE; break;
  case GL_CULL_FACE: g_gl_context->cull_face_enabled = GL_FALSE; break;
  case GL_SCISSOR_TEST: g_gl_context->scissor_test_enabled = GL_FALSE; break;
  case GL_SAMPLE_COVERAGE: g_gl_context->sample_coverage_enabled = GL_FALSE; break;
  case GL_PRIMITIVE_RESTART: g_gl_context->primitive_restart_enabled = GL_FALSE; break;
  case GL_POLYGON_OFFSET_FILL: g_gl_context->polygon_offset_fill_enabled = GL_FALSE; break;
  case GL_POLYGON_OFFSET_LINE: g_gl_context->polygon_offset_line_enabled = GL_FALSE; break;
  case GL_POLYGON_OFFSET_POINT: g_gl_context->polygon_offset_point_enabled = GL_FALSE; break;
  default: _gl_set_error(GL_INVALID_ENUM); return;
  }
  g_gl_context->dirty_flags |= 0xFFFFFFFF;
}

GLboolean _gl_IsEnabled(GLenum cap) {
  if (!g_gl_context) return GL_FALSE;
  switch (cap) {
  case GL_DEPTH_TEST: return g_gl_context->depth_test_enabled;
  case GL_STENCIL_TEST: return g_gl_context->stencil_test_enabled;
  case GL_BLEND: return g_gl_context->blend_enabled;
  case GL_CULL_FACE: return g_gl_context->cull_face_enabled;
  case GL_SCISSOR_TEST: return g_gl_context->scissor_test_enabled;
  case GL_SAMPLE_COVERAGE: return g_gl_context->sample_coverage_enabled;
  case GL_PRIMITIVE_RESTART: return g_gl_context->primitive_restart_enabled;
  case GL_POLYGON_OFFSET_FILL: return g_gl_context->polygon_offset_fill_enabled;
  case GL_POLYGON_OFFSET_LINE: return g_gl_context->polygon_offset_line_enabled;
  case GL_POLYGON_OFFSET_POINT: return g_gl_context->polygon_offset_point_enabled;
  default: _gl_set_error(GL_INVALID_ENUM); return GL_FALSE;
  }
}

void _gl_LogicOp(GLenum opcode) {
    if (!g_gl_context) return;
    g_gl_context->logic_op = opcode;
    g_gl_context->dirty_flags |= GL_DIRTY_LOGIC_OP;
}

void _gl_PointSize(GLfloat size) {
    if (!g_gl_context) return;
    if (size <= 0.0f) { _gl_set_error(GL_INVALID_VALUE); return; }
    g_gl_context->point_size = size;
    g_gl_context->dirty_flags |= GL_DIRTY_POINT_SIZE;
}

void _gl_LineWidth(GLfloat width) {
  if (g_gl_context) { g_gl_context->line_width = width; g_gl_context->dirty_flags |= GL_DIRTY_LINE_WIDTH; }
}

void _gl_Viewport(GLint x, GLint y, GLsizei width, GLsizei height) {
  if (!g_gl_context) return;
  if (width < 0 || height < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
  g_gl_context->viewport.x = x; g_gl_context->viewport.y = y;
  g_gl_context->viewport.width = width; g_gl_context->viewport.height = height;
  g_gl_context->dirty_flags |= GL_DIRTY_VIEWPORT;
}

void _gl_Scissor(GLint x, GLint y, GLsizei width, GLsizei height) {
  if (!g_gl_context) return;
  if (width < 0 || height < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
  g_gl_context->scissor.x = x; g_gl_context->scissor.y = y;
  g_gl_context->scissor.width = width; g_gl_context->scissor.height = height;
  g_gl_context->dirty_flags |= GL_DIRTY_SCISSOR;
}

void _gl_ColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) {
  if (g_gl_context) {
    g_gl_context->color_mask[0] = red; g_gl_context->color_mask[1] = green;
    g_gl_context->color_mask[2] = blue; g_gl_context->color_mask[3] = alpha;
    g_gl_context->dirty_flags |= GL_DIRTY_COLOR_MASK;
  }
}

void _gl_Hint(GLenum target, GLenum mode) {
  if (!g_gl_context) return;
  if (mode != GL_DONT_CARE && mode != GL_FASTEST && mode != GL_NICEST) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  switch (target) {
  case GL_GENERATE_MIPMAP_HINT:
    g_gl_context->generate_mipmap_hint = mode;
    break;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    break;
  }
}
void _gl_PixelStorei(GLenum pname, GLint param) {
  if (!g_gl_context) return;
  switch (pname) {
  case GL_PACK_ALIGNMENT:
    if (param != 1 && param != 2 && param != 4 && param != 8) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
    g_gl_context->pack_alignment = param;
    break;
  case GL_UNPACK_ALIGNMENT:
    if (param != 1 && param != 2 && param != 4 && param != 8) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
    g_gl_context->unpack_alignment = param;
    break;
  case GL_PACK_ROW_LENGTH:
    if (param < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    g_gl_context->pack_row_length = param;
    break;
  case GL_PACK_SKIP_ROWS:
    if (param < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    g_gl_context->pack_skip_rows = param;
    break;
  case GL_PACK_SKIP_PIXELS:
    if (param < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    g_gl_context->pack_skip_pixels = param;
    break;
  case GL_PACK_IMAGE_HEIGHT:
    if (param < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    g_gl_context->pack_image_height = param;
    break;
  case GL_PACK_SKIP_IMAGES:
    if (param < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    g_gl_context->pack_skip_images = param;
    break;
  case GL_UNPACK_ROW_LENGTH:
    if (param < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    g_gl_context->unpack_row_length = param;
    break;
  case GL_UNPACK_SKIP_ROWS:
    if (param < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    g_gl_context->unpack_skip_rows = param;
    break;
  case GL_UNPACK_SKIP_PIXELS:
    if (param < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    g_gl_context->unpack_skip_pixels = param;
    break;
  case GL_UNPACK_IMAGE_HEIGHT:
    if (param < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    g_gl_context->unpack_image_height = param;
    break;
  case GL_UNPACK_SKIP_IMAGES:
    if (param < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    g_gl_context->unpack_skip_images = param;
    break;
  default: _gl_set_error(GL_INVALID_ENUM); break;
  }
}
void _gl_SampleCoverage(GLclampf value, GLboolean invert) {
  if (g_gl_context) { g_gl_context->sample_coverage_value = value; g_gl_context->sample_coverage_invert = invert; }
}

void _gl_BlendFunc(GLenum s, GLenum d) { if(g_gl_context) { g_gl_context->blend_src_rgb = s; g_gl_context->blend_dst_rgb = d; g_gl_context->blend_src_alpha = s; g_gl_context->blend_dst_alpha = d; g_gl_context->dirty_flags |= GL_DIRTY_BLEND; } }
void _gl_BlendEquation(GLenum m) { if(g_gl_context) { g_gl_context->blend_eq_rgb = m; g_gl_context->blend_eq_alpha = m; g_gl_context->dirty_flags |= GL_DIRTY_BLEND; } }
void _gl_BlendEquationSeparate(GLenum r, GLenum a) { if(g_gl_context) { g_gl_context->blend_eq_rgb = r; g_gl_context->blend_eq_alpha = a; g_gl_context->dirty_flags |= GL_DIRTY_BLEND; } }
void _gl_BlendFuncSeparate(GLenum r, GLenum d, GLenum a, GLenum e) { if(g_gl_context) { g_gl_context->blend_src_rgb = r; g_gl_context->blend_dst_rgb = d; g_gl_context->blend_src_alpha = a; g_gl_context->blend_dst_alpha = e; g_gl_context->dirty_flags |= GL_DIRTY_BLEND; } }
void _gl_BlendColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) { if(g_gl_context) { g_gl_context->blend_color[0]=r; g_gl_context->blend_color[1]=g; g_gl_context->blend_color[2]=b; g_gl_context->blend_color[3]=a; g_gl_context->dirty_flags |= GL_DIRTY_BLEND; } }
void _gl_DepthFunc(GLenum f) { if(g_gl_context) { g_gl_context->depth_func = f; g_gl_context->dirty_flags |= GL_DIRTY_DEPTH_STENCIL; } }
void _gl_DepthMask(GLboolean f) { if(g_gl_context) { g_gl_context->depth_mask = f; g_gl_context->dirty_flags |= GL_DIRTY_DEPTH_STENCIL; } }
void _gl_DepthRange(GLclampd n, GLclampd f) { if(g_gl_context) { g_gl_context->viewport.near_z = (GLfloat)n; g_gl_context->viewport.far_z = (GLfloat)f; g_gl_context->dirty_flags |= GL_DIRTY_VIEWPORT; } }
void _gl_StencilFunc(GLenum f, GLint r, GLuint m) {
  if (!g_gl_context) return;
  g_gl_context->stencil_func[0] = f; g_gl_context->stencil_func[1] = f;
  g_gl_context->stencil_ref[0] = r; g_gl_context->stencil_ref[1] = r;
  g_gl_context->stencil_compare_mask[0] = m; g_gl_context->stencil_compare_mask[1] = m;
  g_gl_context->dirty_flags |= GL_DIRTY_DEPTH_STENCIL;
}
void _gl_StencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask) {
  if (!g_gl_context) return;
  int i0 = stencil_face_index(face);
  bool both = (face == GL_FRONT_AND_BACK);
  g_gl_context->stencil_func[i0] = func; g_gl_context->stencil_ref[i0] = ref; g_gl_context->stencil_compare_mask[i0] = mask;
  if (both) { g_gl_context->stencil_func[1] = func; g_gl_context->stencil_ref[1] = ref; g_gl_context->stencil_compare_mask[1] = mask; }
  g_gl_context->dirty_flags |= GL_DIRTY_DEPTH_STENCIL;
}
void _gl_StencilOp(GLenum f, GLenum z, GLenum p) {
  if (!g_gl_context) return;
  g_gl_context->stencil_fail[0] = f; g_gl_context->stencil_zfail[0] = z; g_gl_context->stencil_zpass[0] = p;
  g_gl_context->stencil_fail[1] = f; g_gl_context->stencil_zfail[1] = z; g_gl_context->stencil_zpass[1] = p;
  g_gl_context->dirty_flags |= GL_DIRTY_DEPTH_STENCIL;
}
void _gl_StencilOpSeparate(GLenum face, GLenum sfail, GLenum zfail, GLenum zpass) {
  if (!g_gl_context) return;
  int i0 = stencil_face_index(face);
  bool both = (face == GL_FRONT_AND_BACK);
  g_gl_context->stencil_fail[i0] = sfail; g_gl_context->stencil_zfail[i0] = zfail; g_gl_context->stencil_zpass[i0] = zpass;
  if (both) { g_gl_context->stencil_fail[1] = sfail; g_gl_context->stencil_zfail[1] = zfail; g_gl_context->stencil_zpass[1] = zpass; }
  g_gl_context->dirty_flags |= GL_DIRTY_DEPTH_STENCIL;
}
void _gl_StencilMask(GLuint m) {
  if (!g_gl_context) return;
  g_gl_context->stencil_write_mask[0] = m; g_gl_context->stencil_write_mask[1] = m;
  g_gl_context->dirty_flags |= GL_DIRTY_DEPTH_STENCIL;
}
void _gl_StencilMaskSeparate(GLenum face, GLuint m) {
  if (!g_gl_context) return;
  int i0 = stencil_face_index(face);
  bool both = (face == GL_FRONT_AND_BACK);
  g_gl_context->stencil_write_mask[i0] = m;
  if (both) g_gl_context->stencil_write_mask[1] = m;
  g_gl_context->dirty_flags |= GL_DIRTY_DEPTH_STENCIL;
}
void _gl_CullFace(GLenum m) { if(g_gl_context) { g_gl_context->cull_face_mode = m; g_gl_context->dirty_flags |= GL_DIRTY_CULL; } }
void _gl_FrontFace(GLenum m) { if(g_gl_context) { g_gl_context->front_face = m; g_gl_context->dirty_flags |= GL_DIRTY_CULL; } }
void _gl_PolygonMode(GLenum face, GLenum mode) { if(g_gl_context) { g_gl_context->polygon_mode = mode; g_gl_context->dirty_flags |= GL_DIRTY_POLYGON_MODE; } }
void _gl_PolygonOffset(GLfloat f, GLfloat u) { if(g_gl_context) { g_gl_context->polygon_offset_factor = f; g_gl_context->polygon_offset_units = u; g_gl_context->dirty_flags |= GL_DIRTY_POLYGON_MODE; } }

static GX2PolygonMode map_polygon_mode(GLenum mode) {
  switch (mode) {
  case GL_POINT: return GX2_POLYGON_MODE_POINT;
  case GL_LINE:  return GX2_POLYGON_MODE_LINE;
  default:       return GX2_POLYGON_MODE_TRIANGLE;
  }
}

void gl_flush_state(void) {
  if (!g_gl_context) return;
  bool valid;

  // gx2gl currently does not expose legacy alpha-test state, so keep it
  // explicitly disabled to avoid inheriting a previous GX2 context's discard
  // configuration and silently dropping every fragment.
  GX2SetAlphaTest(GX2_DISABLE, GX2_COMPARE_FUNC_ALWAYS, 0.0f);
  GX2SetAlphaToMask(GX2_DISABLE, GX2_ALPHA_TO_MASK_MODE_NON_DITHERED);

  if (g_gl_context->dirty_flags & GL_DIRTY_FRAMEBUFFER) {
    gl_bind_framebuffers();
  }
  if (g_gl_context->dirty_flags & GL_DIRTY_VIEWPORT) {
    GX2ColorBuffer *cb = gl_get_draw_color_buffer(0);
    float rt_height = cb ? (float)cb->surface.height : (float)g_gl_context->viewport.height;
    float flipped_y = rt_height - (float)g_gl_context->viewport.y - (float)g_gl_context->viewport.height;
    GX2SetViewport((float)g_gl_context->viewport.x, flipped_y,
                   (float)g_gl_context->viewport.width, (float)g_gl_context->viewport.height,
                   g_gl_context->viewport.near_z, g_gl_context->viewport.far_z);
  }
  if (g_gl_context->dirty_flags & (GL_DIRTY_SCISSOR | GL_DIRTY_FRAMEBUFFER)) {
    GX2ColorBuffer *cb = gl_get_draw_color_buffer(0);
    if (g_gl_context->scissor_test_enabled) {
      uint32_t rt_height = cb ? cb->surface.height : (uint32_t)g_gl_context->scissor.height;
      uint32_t flipped_y = rt_height - (uint32_t)g_gl_context->scissor.y - (uint32_t)g_gl_context->scissor.height;
      GX2SetScissor((uint32_t)g_gl_context->scissor.x, flipped_y,
                    (uint32_t)g_gl_context->scissor.width, (uint32_t)g_gl_context->scissor.height);
    } else {
      uint32_t rt_width = cb ? cb->surface.width : (uint32_t)g_gl_context->viewport.width;
      uint32_t rt_height = cb ? cb->surface.height : (uint32_t)g_gl_context->viewport.height;
      GX2SetScissor(0, 0, rt_width, rt_height);
    }
  }
  if (g_gl_context->dirty_flags & GL_DIRTY_BLEND) {
    GX2SetColorControl(GX2_LOGIC_OP_COPY,
                       g_gl_context->blend_enabled ? 0xFF : 0x00,
                       GX2_FALSE,
                       GX2_TRUE);
    if (g_gl_context->blend_enabled) {
      GX2BlendMode src_rgb   = map_blend_factor(g_gl_context->blend_src_rgb,   &valid);
      GX2BlendMode dst_rgb   = map_blend_factor(g_gl_context->blend_dst_rgb,   &valid);
      GX2BlendMode src_alpha = map_blend_factor(g_gl_context->blend_src_alpha, &valid);
      GX2BlendMode dst_alpha = map_blend_factor(g_gl_context->blend_dst_alpha, &valid);
      GX2BlendCombineMode eq_rgb   = map_blend_eq(g_gl_context->blend_eq_rgb,   &valid);
      GX2BlendCombineMode eq_alpha = map_blend_eq(g_gl_context->blend_eq_alpha, &valid);
      GX2SetBlendControl(GX2_RENDER_TARGET_0,
                         src_rgb, dst_rgb, eq_rgb,
                         GX2_ENABLE,
                         src_alpha, dst_alpha, eq_alpha);
      GX2SetBlendConstantColor(g_gl_context->blend_color[0], g_gl_context->blend_color[1],
                               g_gl_context->blend_color[2], g_gl_context->blend_color[3]);
    }
  }
  if (g_gl_context->dirty_flags & GL_DIRTY_COLOR_MASK) {
    uint8_t mask = 0;
    if (g_gl_context->color_mask[0]) mask |= GX2_CHANNEL_MASK_R;
    if (g_gl_context->color_mask[1]) mask |= GX2_CHANNEL_MASK_G;
    if (g_gl_context->color_mask[2]) mask |= GX2_CHANNEL_MASK_B;
    if (g_gl_context->color_mask[3]) mask |= GX2_CHANNEL_MASK_A;
    GX2SetTargetChannelMasks((GX2ChannelMask)mask, (GX2ChannelMask)mask,
                             (GX2ChannelMask)mask, (GX2ChannelMask)mask,
                             (GX2ChannelMask)mask, (GX2ChannelMask)mask,
                             (GX2ChannelMask)mask, (GX2ChannelMask)mask);
  }
  if (g_gl_context->dirty_flags & GL_DIRTY_LOGIC_OP) {
    GX2SetColorControl(map_logic_op(g_gl_context->logic_op),
                       g_gl_context->blend_enabled ? 0xFF : 0x00,
                       GX2_FALSE,
                       GX2_TRUE);
  }
  if (g_gl_context->dirty_flags & GL_DIRTY_DEPTH_STENCIL) {
    GX2CompareFunction depth_func = map_compare_func(g_gl_context->depth_func, &valid);
    GX2CompareFunction sf_front   = map_compare_func(g_gl_context->stencil_func[0], &valid);
    GX2CompareFunction sf_back    = map_compare_func(g_gl_context->stencil_func[1], &valid);
    GX2StencilFunction ss_front_fail  = map_stencil_op(g_gl_context->stencil_fail[0],  &valid);
    GX2StencilFunction ss_front_zfail = map_stencil_op(g_gl_context->stencil_zfail[0], &valid);
    GX2StencilFunction ss_front_zpass = map_stencil_op(g_gl_context->stencil_zpass[0], &valid);
    GX2StencilFunction ss_back_fail   = map_stencil_op(g_gl_context->stencil_fail[1],  &valid);
    GX2StencilFunction ss_back_zfail  = map_stencil_op(g_gl_context->stencil_zfail[1], &valid);
    GX2StencilFunction ss_back_zpass  = map_stencil_op(g_gl_context->stencil_zpass[1], &valid);
    GX2SetDepthStencilControl(
      g_gl_context->depth_test_enabled    ? GX2_ENABLE : GX2_DISABLE,
      g_gl_context->depth_mask            ? GX2_ENABLE : GX2_DISABLE,
      depth_func,
      g_gl_context->stencil_test_enabled  ? GX2_ENABLE : GX2_DISABLE,
      g_gl_context->stencil_test_enabled  ? GX2_ENABLE : GX2_DISABLE,
      sf_front, ss_front_fail, ss_front_zfail, ss_front_zpass,
      sf_back,  ss_back_fail,  ss_back_zfail,  ss_back_zpass);
    GX2SetStencilMask(
      (uint8_t)g_gl_context->stencil_compare_mask[0], (uint8_t)g_gl_context->stencil_write_mask[0], (uint8_t)g_gl_context->stencil_ref[0],
      (uint8_t)g_gl_context->stencil_compare_mask[1], (uint8_t)g_gl_context->stencil_write_mask[1], (uint8_t)g_gl_context->stencil_ref[1]);
  }
  if (g_gl_context->dirty_flags & GL_DIRTY_POINT_SIZE) {
    GX2SetPointSize(g_gl_context->point_size, g_gl_context->point_size);
  }
  if (g_gl_context->dirty_flags & GL_DIRTY_LINE_WIDTH) {
    GX2SetLineWidth(g_gl_context->line_width);
  }
  if (g_gl_context->dirty_flags & GL_DIRTY_CULL) {
    GX2SetCullOnlyControl(map_front_face(g_gl_context->front_face),
      g_gl_context->cull_face_enabled && (g_gl_context->cull_face_mode == GL_FRONT || g_gl_context->cull_face_mode == GL_FRONT_AND_BACK),
      g_gl_context->cull_face_enabled && (g_gl_context->cull_face_mode == GL_BACK  || g_gl_context->cull_face_mode == GL_FRONT_AND_BACK));
  }
  if (g_gl_context->dirty_flags & GL_DIRTY_POLYGON_MODE) {
    GX2PolygonMode pm = map_polygon_mode(g_gl_context->polygon_mode);
    bool offset_en = g_gl_context->polygon_offset_fill_enabled ||
                     g_gl_context->polygon_offset_line_enabled ||
                     g_gl_context->polygon_offset_point_enabled;
    GX2SetPolygonControl(map_front_face(g_gl_context->front_face),
                         g_gl_context->cull_face_enabled && (g_gl_context->cull_face_mode == GL_FRONT || g_gl_context->cull_face_mode == GL_FRONT_AND_BACK),
                         g_gl_context->cull_face_enabled && (g_gl_context->cull_face_mode == GL_BACK  || g_gl_context->cull_face_mode == GL_FRONT_AND_BACK),
                         GX2_ENABLE, pm, pm,
                         offset_en ? GX2_ENABLE : GX2_DISABLE,
                         offset_en ? GX2_ENABLE : GX2_DISABLE,
                         GX2_DISABLE);
    if (offset_en)
      GX2SetPolygonOffset(g_gl_context->polygon_offset_factor, g_gl_context->polygon_offset_units,
                          g_gl_context->polygon_offset_factor, g_gl_context->polygon_offset_units, 0.0f);
  }
  if (!(g_gl_context->dirty_flags & GL_DIRTY_FRAMEBUFFER)) {
    gl_bind_framebuffers();
  }
  gl_bind_shaders();
  gl_bind_program_fetch_shader();
  gl_bind_textures();
  gl_bind_vao();
  g_gl_context->dirty_flags = 0;
}

#ifdef __cplusplus
}
#endif
