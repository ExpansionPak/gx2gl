#include "gl_state.h"
#include "core/gl_framebuffer.h"
#include "core/gl_shader.h"
#include "core/gl_texture.h"
#include "core/gl_vao.h"
#include "endian/endian.h"
#ifdef __cplusplus
extern "C" {
#endif
#include <coreinit/cache.h>
#include <gx2/clear.h>
#include <gx2/enum.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/state.h>
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
static GLfloat clamp_float(GLfloat value, GLfloat min_value, GLfloat max_value) {
  if (value < min_value) {
    return min_value;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

static GLdouble clamp_double(GLdouble value, GLdouble min_value,
                             GLdouble max_value) {
  if (value < min_value) {
    return min_value;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

static GX2BlendMode map_blend_factor(GLenum factor, bool *valid) {
  *valid = true;
  switch (factor) {
  case GL_ZERO:
    return GX2_BLEND_MODE_ZERO;
  case GL_ONE:
    return GX2_BLEND_MODE_ONE;
  case GL_SRC_COLOR:
    return GX2_BLEND_MODE_SRC_COLOR;
  case GL_ONE_MINUS_SRC_COLOR:
    return GX2_BLEND_MODE_INV_SRC_COLOR;
  case GL_SRC_ALPHA:
    return GX2_BLEND_MODE_SRC_ALPHA;
  case GL_ONE_MINUS_SRC_ALPHA:
    return GX2_BLEND_MODE_INV_SRC_ALPHA;
  case GL_DST_ALPHA:
    return GX2_BLEND_MODE_DST_ALPHA;
  case GL_ONE_MINUS_DST_ALPHA:
    return GX2_BLEND_MODE_INV_DST_ALPHA;
  case GL_DST_COLOR:
    return GX2_BLEND_MODE_DST_COLOR;
  case GL_ONE_MINUS_DST_COLOR:
    return GX2_BLEND_MODE_INV_DST_COLOR;
  case GL_SRC_ALPHA_SATURATE:
    return GX2_BLEND_MODE_SRC_ALPHA_SAT;
  case GL_CONSTANT_COLOR:
    return GX2_BLEND_MODE_BLEND_FACTOR;
  case GL_ONE_MINUS_CONSTANT_COLOR:
    return GX2_BLEND_MODE_INV_BLEND_FACTOR;
  case GL_CONSTANT_ALPHA:
    return GX2_BLEND_MODE_BLEND_FACTOR;
  case GL_ONE_MINUS_CONSTANT_ALPHA:
    return GX2_BLEND_MODE_INV_BLEND_FACTOR;
  default:
    *valid = false;
    return GX2_BLEND_MODE_ZERO;
  }
}

static GX2BlendCombineMode map_blend_eq(GLenum eq, bool *valid) {
  *valid = true;
  switch (eq) {
  case GL_FUNC_ADD:
    return GX2_BLEND_COMBINE_MODE_ADD;
  case GL_FUNC_SUBTRACT:
    return GX2_BLEND_COMBINE_MODE_SUB;
  case GL_FUNC_REVERSE_SUBTRACT:
    return GX2_BLEND_COMBINE_MODE_REV_SUB;
  case GL_MIN:
    return GX2_BLEND_COMBINE_MODE_MIN;
  case GL_MAX:
    return GX2_BLEND_COMBINE_MODE_MAX;
  default:
    *valid = false;
    return GX2_BLEND_COMBINE_MODE_ADD;
  }
}

static GX2CompareFunction map_compare_func(GLenum func, bool *valid) {
  *valid = true;
  switch (func) {
  case GL_NEVER:
    return GX2_COMPARE_FUNC_NEVER;
  case GL_LESS:
    return GX2_COMPARE_FUNC_LESS;
  case GL_EQUAL:
    return GX2_COMPARE_FUNC_EQUAL;
  case GL_LEQUAL:
    return GX2_COMPARE_FUNC_LEQUAL;
  case GL_GREATER:
    return GX2_COMPARE_FUNC_GREATER;
  case GL_NOTEQUAL:
    return GX2_COMPARE_FUNC_NOT_EQUAL;
  case GL_GEQUAL:
    return GX2_COMPARE_FUNC_GEQUAL;
  case GL_ALWAYS:
    return GX2_COMPARE_FUNC_ALWAYS;
  default:
    *valid = false;
    return GX2_COMPARE_FUNC_ALWAYS;
  }
}

static GX2StencilFunction map_stencil_op(GLenum op, bool *valid) {
  *valid = true;
  switch (op) {
  case GL_KEEP:
    return GX2_STENCIL_FUNCTION_KEEP;
  case GL_REPLACE:
    return GX2_STENCIL_FUNCTION_REPLACE;
  case GL_INCR:
    return GX2_STENCIL_FUNCTION_INCR_CLAMP;
  case GL_DECR:
    return GX2_STENCIL_FUNCTION_DECR_CLAMP;
  case GL_INVERT:
    return GX2_STENCIL_FUNCTION_INV;
  case GL_INCR_WRAP:
    return GX2_STENCIL_FUNCTION_INCR_WRAP;
  case GL_DECR_WRAP:
    return GX2_STENCIL_FUNCTION_DECR_WRAP;
  default:
    *valid = false;
    return GX2_STENCIL_FUNCTION_KEEP;
  }
}

static GX2PolygonMode map_polygon_mode(GLenum mode, bool *valid) {
  *valid = true;
  switch (mode) {
  case GL_POINT:
    return GX2_POLYGON_MODE_POINT;
  case GL_LINE:
    return GX2_POLYGON_MODE_LINE;
  case GL_FILL:
    return GX2_POLYGON_MODE_TRIANGLE;
  default:
    *valid = false;
    return GX2_POLYGON_MODE_TRIANGLE;
  }
}

static GX2FrontFace map_front_face(GLenum mode) {
  return mode == GL_CW ? GX2_FRONT_FACE_CW : GX2_FRONT_FACE_CCW;
}

static bool stencil_face_range(GLenum face, uint32_t *first, uint32_t *last) {
  if (!first || !last) {
    return false;
  }

  switch (face) {
  case GL_FRONT:
    *first = 0;
    *last = 0;
    return true;
  case GL_BACK:
    *first = 1;
    *last = 1;
    return true;
  case GL_FRONT_AND_BACK:
    *first = 0;
    *last = 1;
    return true;
  default:
    return false;
  }
}

static bool is_polygon_offset_enabled(void) {
  if (!g_gl_context) {
    return false;
  }

  switch (g_gl_context->polygon_mode) {
  case GL_POINT:
    return g_gl_context->polygon_offset_point_enabled == GL_TRUE;
  case GL_LINE:
    return g_gl_context->polygon_offset_line_enabled == GL_TRUE;
  case GL_FILL:
  default:
    return g_gl_context->polygon_offset_fill_enabled == GL_TRUE;
  }
}

static uint16_t float_to_half(float value) {
  uint32_t bits;
  uint32_t sign;
  int32_t exponent;
  uint32_t mantissa;

  memcpy(&bits, &value, sizeof(bits));
  sign = (bits >> 16) & 0x8000u;
  exponent = (int32_t)((bits >> 23) & 0xFFu) - 127 + 15;
  mantissa = bits & 0x7FFFFFu;

  if (exponent <= 0) {
    if (exponent < -10) {
      return (uint16_t)sign;
    }
    mantissa = (mantissa | 0x800000u) >> (uint32_t)(1 - exponent);
    if (mantissa & 0x00001000u) {
      mantissa += 0x00002000u;
    }
    return (uint16_t)(sign | (mantissa >> 13));
  }

  if (exponent >= 31) {
    return (uint16_t)(sign | 0x7C00u);
  }

  if (mantissa & 0x00001000u) {
    mantissa += 0x00002000u;
    if (mantissa & 0x00800000u) {
      mantissa = 0;
      ++exponent;
      if (exponent >= 31) {
        return (uint16_t)(sign | 0x7C00u);
      }
    }
  }

  return (uint16_t)(sign | ((uint32_t)exponent << 10) | (mantissa >> 13));
}

static void cpu_clear_color_buffer(GX2ColorBuffer *color_buffer) {
  GX2Surface *surface;
  uint8_t *image;
  uint32_t row_bytes;
  uint32_t clear_x0;
  uint32_t clear_y0;
  uint32_t clear_x1;
  uint32_t clear_y1;

  if (!g_gl_context || !color_buffer) {
    return;
  }

  surface = &color_buffer->surface;
  if (!surface->image || surface->tileMode != GX2_TILE_MODE_LINEAR_ALIGNED) {
    return;
  }

  image = (uint8_t *)surface->image;
  clear_x0 = 0;
  clear_y0 = 0;
  clear_x1 = surface->width;
  clear_y1 = surface->height;

  if (g_gl_context->scissor_test_enabled) {
    int64_t scissor_x0 = g_gl_context->scissor.x;
    int64_t scissor_y0 = g_gl_context->scissor.y;
    int64_t scissor_x1 = scissor_x0 + (int64_t)g_gl_context->scissor.width;
    int64_t scissor_y1 = scissor_y0 + (int64_t)g_gl_context->scissor.height;

    if (scissor_x0 > (int64_t)clear_x0) {
      clear_x0 = (uint32_t)scissor_x0;
    }
    if (scissor_y0 > (int64_t)clear_y0) {
      clear_y0 = (uint32_t)scissor_y0;
    }
    if (scissor_x1 < (int64_t)clear_x1) {
      clear_x1 = scissor_x1 <= 0 ? 0u : (uint32_t)scissor_x1;
    }
    if (scissor_y1 < (int64_t)clear_y1) {
      clear_y1 = scissor_y1 <= 0 ? 0u : (uint32_t)scissor_y1;
    }
  }

  if (clear_x1 <= clear_x0 || clear_y1 <= clear_y0) {
    return;
  }

  switch (surface->format) {
  case GX2_SURFACE_FORMAT_UNORM_R8: {
    uint8_t clear_r =
        (uint8_t)(g_gl_context->clear_color[0] * 255.0f + 0.5f);
    row_bytes = surface->pitch;
    for (uint32_t y = clear_y0; y < clear_y1; ++y) {
      uint8_t *row = image + y * row_bytes;
      for (uint32_t x = clear_x0; x < clear_x1; ++x) {
        if (g_gl_context->color_mask[0]) {
          row[x] = clear_r;
        }
      }
    }
    break;
  }
  case GX2_SURFACE_FORMAT_UNORM_R8_G8: {
    uint8_t clear_r =
        (uint8_t)(g_gl_context->clear_color[0] * 255.0f + 0.5f);
    uint8_t clear_g =
        (uint8_t)(g_gl_context->clear_color[1] * 255.0f + 0.5f);
    row_bytes = surface->pitch * 2u;
    for (uint32_t y = clear_y0; y < clear_y1; ++y) {
      uint8_t *row = image + y * row_bytes;
      for (uint32_t x = clear_x0; x < clear_x1; ++x) {
        uint8_t *pixel = row + x * 2u;
        if (g_gl_context->color_mask[0]) {
          pixel[0] = clear_r;
        }
        if (g_gl_context->color_mask[1]) {
          pixel[1] = clear_g;
        }
      }
    }
    break;
  }
  case GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8: {
    uint8_t clear_rgba[4] = {
        (uint8_t)(g_gl_context->clear_color[0] * 255.0f + 0.5f),
        (uint8_t)(g_gl_context->clear_color[1] * 255.0f + 0.5f),
        (uint8_t)(g_gl_context->clear_color[2] * 255.0f + 0.5f),
        (uint8_t)(g_gl_context->clear_color[3] * 255.0f + 0.5f)};
    row_bytes = surface->pitch * 4u;
    for (uint32_t y = clear_y0; y < clear_y1; ++y) {
      uint8_t *row = image + y * row_bytes;
      for (uint32_t x = clear_x0; x < clear_x1; ++x) {
        uint8_t *pixel = row + x * 4u;
        for (uint32_t c = 0; c < 4; ++c) {
          if (g_gl_context->color_mask[c]) {
            pixel[c] = clear_rgba[c];
          }
        }
      }
    }
    break;
  }
  case GX2_SURFACE_FORMAT_FLOAT_R16_G16_B16_A16: {
    uint16_t clear_rgba[4] = {
        CPU_TO_GPU_16(float_to_half(g_gl_context->clear_color[0])),
        CPU_TO_GPU_16(float_to_half(g_gl_context->clear_color[1])),
        CPU_TO_GPU_16(float_to_half(g_gl_context->clear_color[2])),
        CPU_TO_GPU_16(float_to_half(g_gl_context->clear_color[3]))};
    row_bytes = surface->pitch * 8u;
    for (uint32_t y = clear_y0; y < clear_y1; ++y) {
      uint16_t *row = (uint16_t *)(image + y * row_bytes);
      for (uint32_t x = clear_x0; x < clear_x1; ++x) {
        uint16_t *pixel = row + x * 4u;
        for (uint32_t c = 0; c < 4; ++c) {
          if (g_gl_context->color_mask[c]) {
            pixel[c] = clear_rgba[c];
          }
        }
      }
    }
    break;
  }
  case GX2_SURFACE_FORMAT_FLOAT_R32: {
    uint32_t clear_r_bits;
    memcpy(&clear_r_bits, &g_gl_context->clear_color[0], sizeof(clear_r_bits));
    clear_r_bits = CPU_TO_GPU_32(clear_r_bits);
    row_bytes = surface->pitch * 4u;
    for (uint32_t y = clear_y0; y < clear_y1; ++y) {
      uint32_t *row = (uint32_t *)(image + y * row_bytes);
      for (uint32_t x = clear_x0; x < clear_x1; ++x) {
        if (g_gl_context->color_mask[0]) {
          row[x] = clear_r_bits;
        }
      }
    }
    break;
  }
  case GX2_SURFACE_FORMAT_FLOAT_R32_G32_B32_A32: {
    uint32_t clear_rgba[4];
    row_bytes = surface->pitch * 16u;
    for (uint32_t c = 0; c < 4; ++c) {
      memcpy(&clear_rgba[c], &g_gl_context->clear_color[c],
             sizeof(clear_rgba[c]));
      clear_rgba[c] = CPU_TO_GPU_32(clear_rgba[c]);
    }
    for (uint32_t y = clear_y0; y < clear_y1; ++y) {
      uint32_t *row = (uint32_t *)(image + y * row_bytes);
      for (uint32_t x = clear_x0; x < clear_x1; ++x) {
        uint32_t *pixel = row + x * 4u;
        for (uint32_t c = 0; c < 4; ++c) {
          if (g_gl_context->color_mask[c]) {
            pixel[c] = clear_rgba[c];
          }
        }
      }
    }
    break;
  }
  default:
    return;
  }

  DCFlushRange(image, surface->imageSize);
  GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, image, surface->imageSize);
}

void _gl_ClearColor(GLclampf red, GLclampf green, GLclampf blue,
                    GLclampf alpha) {
  if (!g_gl_context) {
    return;
  }

  g_gl_context->clear_color[0] = clamp_float(red, 0.0f, 1.0f);
  g_gl_context->clear_color[1] = clamp_float(green, 0.0f, 1.0f);
  g_gl_context->clear_color[2] = clamp_float(blue, 0.0f, 1.0f);
  g_gl_context->clear_color[3] = clamp_float(alpha, 0.0f, 1.0f);
}

void _gl_ClearDepth(GLclampd depth) {
  if (!g_gl_context) {
    return;
  }

  g_gl_context->clear_depth = (GLfloat)clamp_double(depth, 0.0, 1.0);
}

void _gl_ClearStencil(GLint s) {
  if (!g_gl_context) {
    return;
  }

  g_gl_context->clear_stencil = s;
}

void _gl_Clear(GLbitfield mask) {
  const GLbitfield valid_mask =
      GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
  GX2DepthBuffer *depth_buffer;

  if (!g_gl_context) {
    return;
  }
  if (mask & ~valid_mask) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  gl_flush_state();

  if (mask & GL_COLOR_BUFFER_BIT) {
    for (GLuint i = 0; i < 8; ++i) {
      GX2ColorBuffer *color_buffer;

      if (!gl_is_draw_color_buffer_enabled(i)) {
        continue;
      }

      color_buffer = gl_get_draw_color_buffer(i);
      if (!color_buffer) {
        continue;
      }

      GX2ClearColor(color_buffer, g_gl_context->clear_color[0],
                    g_gl_context->clear_color[1], g_gl_context->clear_color[2],
                    g_gl_context->clear_color[3]);
      cpu_clear_color_buffer(color_buffer);
    }
  }

  if (!(mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT))) {
    return;
  }

  depth_buffer = gl_get_draw_depth_buffer();
  if (!depth_buffer) {
    return;
  }

  GX2ClearFlags clear_flags = (GX2ClearFlags)0;
  if ((mask & GL_DEPTH_BUFFER_BIT) && g_gl_context->depth_mask) {
    clear_flags = (GX2ClearFlags)(clear_flags | GX2_CLEAR_FLAGS_DEPTH);
  }
  if ((mask & GL_STENCIL_BUFFER_BIT) &&
      (g_gl_context->stencil_write_mask[0] != 0 ||
       g_gl_context->stencil_write_mask[1] != 0)) {
    clear_flags = (GX2ClearFlags)(clear_flags | GX2_CLEAR_FLAGS_STENCIL);
  }

  if (clear_flags == 0) {
    return;
  }

  depth_buffer->depthClear = g_gl_context->clear_depth;
  depth_buffer->stencilClear = (uint8_t)g_gl_context->clear_stencil;
  GX2ClearDepthStencilEx(depth_buffer, g_gl_context->clear_depth,
                         (uint8_t)g_gl_context->clear_stencil, clear_flags);
}

void _gl_Enable(GLenum cap) {
  if (!g_gl_context)
    return;
  switch (cap) {
  case GL_BLEND:
    g_gl_context->blend_enabled = GL_TRUE;
    g_gl_context->dirty_flags |= GL_DIRTY_BLEND;
    break;
  case GL_DEPTH_TEST:
    g_gl_context->depth_test_enabled = GL_TRUE;
    g_gl_context->dirty_flags |= GL_DIRTY_DEPTH_STENCIL;
    break;
  case GL_STENCIL_TEST:
    g_gl_context->stencil_test_enabled = GL_TRUE;
    g_gl_context->dirty_flags |= GL_DIRTY_DEPTH_STENCIL;
    break;
  case GL_CULL_FACE:
    g_gl_context->cull_face_enabled = GL_TRUE;
    g_gl_context->dirty_flags |= GL_DIRTY_CULL;
    break;
  case GL_SCISSOR_TEST:
    g_gl_context->scissor_test_enabled = GL_TRUE;
    g_gl_context->dirty_flags |= GL_DIRTY_SCISSOR;
    break;
  case GL_SAMPLE_COVERAGE:
    g_gl_context->sample_coverage_enabled = GL_TRUE;
    break;
  case GL_POLYGON_OFFSET_POINT:
    g_gl_context->polygon_offset_point_enabled = GL_TRUE;
    g_gl_context->dirty_flags |= GL_DIRTY_POLYGON_MODE;
    break;
  case GL_POLYGON_OFFSET_LINE:
    g_gl_context->polygon_offset_line_enabled = GL_TRUE;
    g_gl_context->dirty_flags |= GL_DIRTY_POLYGON_MODE;
    break;
  case GL_POLYGON_OFFSET_FILL:
    g_gl_context->polygon_offset_fill_enabled = GL_TRUE;
    g_gl_context->dirty_flags |= GL_DIRTY_POLYGON_MODE;
    break;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    break;
  }
}

void _gl_Disable(GLenum cap) {
  if (!g_gl_context)
    return;
  switch (cap) {
  case GL_BLEND:
    g_gl_context->blend_enabled = GL_FALSE;
    g_gl_context->dirty_flags |= GL_DIRTY_BLEND;
    break;
  case GL_DEPTH_TEST:
    g_gl_context->depth_test_enabled = GL_FALSE;
    g_gl_context->dirty_flags |= GL_DIRTY_DEPTH_STENCIL;
    break;
  case GL_STENCIL_TEST:
    g_gl_context->stencil_test_enabled = GL_FALSE;
    g_gl_context->dirty_flags |= GL_DIRTY_DEPTH_STENCIL;
    break;
  case GL_CULL_FACE:
    g_gl_context->cull_face_enabled = GL_FALSE;
    g_gl_context->dirty_flags |= GL_DIRTY_CULL;
    break;
  case GL_SCISSOR_TEST:
    g_gl_context->scissor_test_enabled = GL_FALSE;
    g_gl_context->dirty_flags |= GL_DIRTY_SCISSOR;
    break;
  case GL_SAMPLE_COVERAGE:
    g_gl_context->sample_coverage_enabled = GL_FALSE;
    break;
  case GL_POLYGON_OFFSET_POINT:
    g_gl_context->polygon_offset_point_enabled = GL_FALSE;
    g_gl_context->dirty_flags |= GL_DIRTY_POLYGON_MODE;
    break;
  case GL_POLYGON_OFFSET_LINE:
    g_gl_context->polygon_offset_line_enabled = GL_FALSE;
    g_gl_context->dirty_flags |= GL_DIRTY_POLYGON_MODE;
    break;
  case GL_POLYGON_OFFSET_FILL:
    g_gl_context->polygon_offset_fill_enabled = GL_FALSE;
    g_gl_context->dirty_flags |= GL_DIRTY_POLYGON_MODE;
    break;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    break;
  }
}

GLboolean _gl_IsEnabled(GLenum cap) {
  if (!g_gl_context) {
    return GL_FALSE;
  }

  switch (cap) {
  case GL_BLEND:
    return g_gl_context->blend_enabled;
  case GL_DEPTH_TEST:
    return g_gl_context->depth_test_enabled;
  case GL_STENCIL_TEST:
    return g_gl_context->stencil_test_enabled;
  case GL_CULL_FACE:
    return g_gl_context->cull_face_enabled;
  case GL_SCISSOR_TEST:
    return g_gl_context->scissor_test_enabled;
  case GL_SAMPLE_COVERAGE:
    return g_gl_context->sample_coverage_enabled;
  case GL_POLYGON_OFFSET_POINT:
    return g_gl_context->polygon_offset_point_enabled;
  case GL_POLYGON_OFFSET_LINE:
    return g_gl_context->polygon_offset_line_enabled;
  case GL_POLYGON_OFFSET_FILL:
    return g_gl_context->polygon_offset_fill_enabled;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    return GL_FALSE;
  }
}

void _gl_BlendFunc(GLenum sfactor, GLenum dfactor) {
  _gl_BlendFuncSeparate(sfactor, dfactor, sfactor, dfactor);
}

void _gl_BlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB,
                             GLenum sfactorAlpha, GLenum dfactorAlpha) {
  if (!g_gl_context)
    return;
  bool v1, v2, v3, v4;
  map_blend_factor(sfactorRGB, &v1);
  map_blend_factor(dfactorRGB, &v2);
  map_blend_factor(sfactorAlpha, &v3);
  map_blend_factor(dfactorAlpha, &v4);
  if (!v1 || !v2 || !v3 || !v4) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  g_gl_context->blend_src_rgb = sfactorRGB;
  g_gl_context->blend_dst_rgb = dfactorRGB;
  g_gl_context->blend_src_alpha = sfactorAlpha;
  g_gl_context->blend_dst_alpha = dfactorAlpha;
  g_gl_context->dirty_flags |= GL_DIRTY_BLEND;
}

void _gl_BlendEquation(GLenum mode) {
  _gl_BlendEquationSeparate(mode, mode);
}

void _gl_BlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha) {
  if (!g_gl_context)
    return;
  bool rgb_valid;
  bool alpha_valid;
  map_blend_eq(modeRGB, &rgb_valid);
  map_blend_eq(modeAlpha, &alpha_valid);
  if (!rgb_valid || !alpha_valid) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  g_gl_context->blend_eq_rgb = modeRGB;
  g_gl_context->blend_eq_alpha = modeAlpha;
  g_gl_context->dirty_flags |= GL_DIRTY_BLEND;
}

void _gl_BlendColor(GLclampf red, GLclampf green, GLclampf blue,
                    GLclampf alpha) {
  if (!g_gl_context) {
    return;
  }

  g_gl_context->blend_color[0] = clamp_float(red, 0.0f, 1.0f);
  g_gl_context->blend_color[1] = clamp_float(green, 0.0f, 1.0f);
  g_gl_context->blend_color[2] = clamp_float(blue, 0.0f, 1.0f);
  g_gl_context->blend_color[3] = clamp_float(alpha, 0.0f, 1.0f);
  g_gl_context->dirty_flags |= GL_DIRTY_BLEND;
}

void _gl_DepthFunc(GLenum func) {
  if (!g_gl_context)
    return;
  bool v;
  map_compare_func(func, &v);
  if (!v) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  g_gl_context->depth_func = func;
  g_gl_context->dirty_flags |= GL_DIRTY_DEPTH_STENCIL;
}

void _gl_DepthMask(GLboolean flag) {
  if (!g_gl_context)
    return;
  g_gl_context->depth_mask = flag;
  g_gl_context->dirty_flags |= GL_DIRTY_DEPTH_STENCIL;
}

void _gl_DepthRange(GLclampd nearVal, GLclampd farVal) {
  if (!g_gl_context) {
    return;
  }

  g_gl_context->viewport.near_z = (GLfloat)clamp_double(nearVal, 0.0, 1.0);
  g_gl_context->viewport.far_z = (GLfloat)clamp_double(farVal, 0.0, 1.0);
  g_gl_context->dirty_flags |= GL_DIRTY_VIEWPORT;
}

void _gl_StencilFunc(GLenum func, GLint ref, GLuint mask) {
  _gl_StencilFuncSeparate(GL_FRONT_AND_BACK, func, ref, mask);
}

void _gl_StencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask) {
  uint32_t first;
  uint32_t last;

  if (!g_gl_context)
    return;
  bool v;
  map_compare_func(func, &v);
  if (!v || !stencil_face_range(face, &first, &last)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  for (uint32_t i = first; i <= last; ++i) {
    g_gl_context->stencil_func[i] = func;
    g_gl_context->stencil_ref[i] = ref;
    g_gl_context->stencil_compare_mask[i] = mask;
  }
  g_gl_context->dirty_flags |= GL_DIRTY_DEPTH_STENCIL;
}

void _gl_StencilOp(GLenum fail, GLenum zfail, GLenum zpass) {
  _gl_StencilOpSeparate(GL_FRONT_AND_BACK, fail, zfail, zpass);
}

void _gl_StencilOpSeparate(GLenum face, GLenum fail, GLenum zfail,
                           GLenum zpass) {
  uint32_t first;
  uint32_t last;

  if (!g_gl_context)
    return;
  bool v1, v2, v3;
  map_stencil_op(fail, &v1);
  map_stencil_op(zfail, &v2);
  map_stencil_op(zpass, &v3);
  if (!v1 || !v2 || !v3 || !stencil_face_range(face, &first, &last)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  for (uint32_t i = first; i <= last; ++i) {
    g_gl_context->stencil_fail[i] = fail;
    g_gl_context->stencil_zfail[i] = zfail;
    g_gl_context->stencil_zpass[i] = zpass;
  }
  g_gl_context->dirty_flags |= GL_DIRTY_DEPTH_STENCIL;
}

void _gl_StencilMask(GLuint mask) {
  _gl_StencilMaskSeparate(GL_FRONT_AND_BACK, mask);
}

void _gl_StencilMaskSeparate(GLenum face, GLuint mask) {
  uint32_t first;
  uint32_t last;

  if (!g_gl_context) {
    return;
  }
  if (!stencil_face_range(face, &first, &last)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  for (uint32_t i = first; i <= last; ++i) {
    g_gl_context->stencil_write_mask[i] = mask;
  }
  g_gl_context->dirty_flags |= GL_DIRTY_DEPTH_STENCIL;
}

void _gl_CullFace(GLenum mode) {
  if (!g_gl_context)
    return;
  if (mode != GL_FRONT && mode != GL_BACK && mode != GL_FRONT_AND_BACK) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  g_gl_context->cull_face_mode = mode;
  g_gl_context->dirty_flags |= GL_DIRTY_CULL;
}

void _gl_FrontFace(GLenum mode) {
  if (!g_gl_context)
    return;
  if (mode != GL_CW && mode != GL_CCW) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  g_gl_context->front_face = mode;
  g_gl_context->dirty_flags |= GL_DIRTY_FRONT_FACE;
}

void _gl_PolygonMode(GLenum face, GLenum mode) {
  if (!g_gl_context)
    return;
  bool v;
  map_polygon_mode(mode, &v);
  if (!v ||
      (face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  g_gl_context->polygon_mode = mode;
  g_gl_context->dirty_flags |= GL_DIRTY_POLYGON_MODE;
}

void _gl_PolygonOffset(GLfloat factor, GLfloat units) {
  if (!g_gl_context) {
    return;
  }

  g_gl_context->polygon_offset_factor = factor;
  g_gl_context->polygon_offset_units = units;
  g_gl_context->dirty_flags |= GL_DIRTY_POLYGON_MODE;
}

void _gl_Viewport(GLint x, GLint y, GLsizei width, GLsizei height) {
  if (!g_gl_context)
    return;
  if (width < 0 || height < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  g_gl_context->viewport.x = x;
  g_gl_context->viewport.y = y;
  g_gl_context->viewport.width = width;
  g_gl_context->viewport.height = height;
  g_gl_context->dirty_flags |= GL_DIRTY_VIEWPORT;
}

void _gl_Scissor(GLint x, GLint y, GLsizei width, GLsizei height) {
  if (!g_gl_context)
    return;
  if (width < 0 || height < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  g_gl_context->scissor.x = x;
  g_gl_context->scissor.y = y;
  g_gl_context->scissor.width = width;
  g_gl_context->scissor.height = height;
  g_gl_context->dirty_flags |= GL_DIRTY_SCISSOR;
}

void _gl_ColorMask(GLboolean red, GLboolean green, GLboolean blue,
                     GLboolean alpha) {
  if (!g_gl_context)
    return;
  g_gl_context->color_mask[0] = red;
  g_gl_context->color_mask[1] = green;
  g_gl_context->color_mask[2] = blue;
  g_gl_context->color_mask[3] = alpha;
  g_gl_context->dirty_flags |= GL_DIRTY_COLOR_MASK;
}

void _gl_LineWidth(GLfloat width) {
  if (!g_gl_context)
    return;
  if (width <= 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  g_gl_context->line_width = width;
  g_gl_context->dirty_flags |= GL_DIRTY_LINE_WIDTH;
}

void _gl_Hint(GLenum target, GLenum mode) {
  if (!g_gl_context) {
    return;
  }
  if (target != GL_GENERATE_MIPMAP_HINT) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (mode != GL_DONT_CARE && mode != GL_FASTEST && mode != GL_NICEST) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  g_gl_context->generate_mipmap_hint = mode;
}

void _gl_SampleCoverage(GLclampf value, GLboolean invert) {
  if (!g_gl_context) {
    return;
  }
  g_gl_context->sample_coverage_value = clamp_float(value, 0.0f, 1.0f);
  g_gl_context->sample_coverage_invert = invert ? GL_TRUE : GL_FALSE;
}

void _gl_PixelStorei(GLenum pname, GLint param) {
  if (!g_gl_context) {
    return;
  }

  switch (pname) {
  case GL_PACK_ALIGNMENT:
    if (param != 1 && param != 2 && param != 4 && param != 8) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
    g_gl_context->pack_alignment = param;
    return;
  case GL_UNPACK_ALIGNMENT:
    if (param != 1 && param != 2 && param != 4 && param != 8) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
    g_gl_context->unpack_alignment = param;
    return;
  case GL_PACK_ROW_LENGTH:
    if (param < 0) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
    g_gl_context->pack_row_length = param;
    return;
  case GL_PACK_SKIP_ROWS:
    if (param < 0) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
    g_gl_context->pack_skip_rows = param;
    return;
  case GL_PACK_SKIP_PIXELS:
    if (param < 0) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
    g_gl_context->pack_skip_pixels = param;
    return;
  case GL_PACK_IMAGE_HEIGHT:
    if (param < 0) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
    g_gl_context->pack_image_height = param;
    return;
  case GL_PACK_SKIP_IMAGES:
    if (param < 0) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
    g_gl_context->pack_skip_images = param;
    return;
  case GL_UNPACK_ROW_LENGTH:
    if (param < 0) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
    g_gl_context->unpack_row_length = param;
    return;
  case GL_UNPACK_SKIP_ROWS:
    if (param < 0) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
    g_gl_context->unpack_skip_rows = param;
    return;
  case GL_UNPACK_SKIP_PIXELS:
    if (param < 0) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
    g_gl_context->unpack_skip_pixels = param;
    return;
  case GL_UNPACK_IMAGE_HEIGHT:
    if (param < 0) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
    g_gl_context->unpack_image_height = param;
    return;
  case GL_UNPACK_SKIP_IMAGES:
    if (param < 0) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
    g_gl_context->unpack_skip_images = param;
    return;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
}

void gl_flush_state(void) {
  if (!g_gl_context || !g_gl_context->dirty_flags)
    return;

  bool ignored;

  if (g_gl_context->dirty_flags & GL_DIRTY_BLEND) {
    for (uint32_t rt = 0; rt < 8; ++rt) {
      GX2SetBlendControl(
          (GX2RenderTarget)rt,
          (GX2BlendMode)map_blend_factor(g_gl_context->blend_src_rgb, &ignored),
          (GX2BlendMode)map_blend_factor(g_gl_context->blend_dst_rgb, &ignored),
          (GX2BlendCombineMode)map_blend_eq(g_gl_context->blend_eq_rgb, &ignored),
          g_gl_context->blend_enabled ? GX2_TRUE : GX2_FALSE,
          (GX2BlendMode)map_blend_factor(g_gl_context->blend_src_alpha, &ignored),
          (GX2BlendMode)map_blend_factor(g_gl_context->blend_dst_alpha, &ignored),
          (GX2BlendCombineMode)map_blend_eq(g_gl_context->blend_eq_alpha, &ignored));
    }

    GX2SetBlendConstantColor(g_gl_context->blend_color[0],
                             g_gl_context->blend_color[1],
                             g_gl_context->blend_color[2],
                             g_gl_context->blend_color[3]);
  }

  if (g_gl_context->dirty_flags & GL_DIRTY_DEPTH_STENCIL) {
    GX2DepthStencilControlReg dsReg;
    GX2CompareFunction front_func =
        map_compare_func(g_gl_context->stencil_func[0], &ignored);
    GX2CompareFunction back_func =
        map_compare_func(g_gl_context->stencil_func[1], &ignored);
    GX2StencilFunction front_fail =
        map_stencil_op(g_gl_context->stencil_fail[0], &ignored);
    GX2StencilFunction front_zfail =
        map_stencil_op(g_gl_context->stencil_zfail[0], &ignored);
    GX2StencilFunction front_zpass =
        map_stencil_op(g_gl_context->stencil_zpass[0], &ignored);
    GX2StencilFunction back_fail =
        map_stencil_op(g_gl_context->stencil_fail[1], &ignored);
    GX2StencilFunction back_zfail =
        map_stencil_op(g_gl_context->stencil_zfail[1], &ignored);
    GX2StencilFunction back_zpass =
        map_stencil_op(g_gl_context->stencil_zpass[1], &ignored);

    GX2InitDepthStencilControlReg(&dsReg,
                                  g_gl_context->depth_test_enabled ? GX2_ENABLE
                                                                   : GX2_DISABLE,
                                  g_gl_context->depth_mask ? GX2_ENABLE
                                                           : GX2_DISABLE,
                                  (GX2CompareFunction)map_compare_func(
                                      g_gl_context->depth_func, &ignored),
                                  g_gl_context->stencil_test_enabled
                                      ? GX2_ENABLE
                                      : GX2_DISABLE,
                                  g_gl_context->stencil_test_enabled
                                      ? GX2_ENABLE
                                      : GX2_DISABLE,
                                  front_func, front_zpass, front_zfail,
                                  front_fail, back_func, back_zpass,
                                  back_zfail, back_fail);
    GX2SetDepthStencilControlReg(&dsReg);
    GX2SetStencilMask((uint8_t)g_gl_context->stencil_compare_mask[0],
                      (uint8_t)g_gl_context->stencil_write_mask[0],
                      (uint8_t)g_gl_context->stencil_ref[0],
                      (uint8_t)g_gl_context->stencil_compare_mask[1],
                      (uint8_t)g_gl_context->stencil_write_mask[1],
                      (uint8_t)g_gl_context->stencil_ref[1]);
  }

  if (g_gl_context->dirty_flags & GL_DIRTY_VIEWPORT) {
    GX2SetViewport(g_gl_context->viewport.x, g_gl_context->viewport.y,
                   g_gl_context->viewport.width, g_gl_context->viewport.height,
                   g_gl_context->viewport.near_z,
                   g_gl_context->viewport.far_z);
  }

  if (g_gl_context->dirty_flags & (GL_DIRTY_SCISSOR | GL_DIRTY_VIEWPORT)) {
    if (g_gl_context->scissor_test_enabled) {
      GX2SetScissor(g_gl_context->scissor.x, g_gl_context->scissor.y,
                    g_gl_context->scissor.width, g_gl_context->scissor.height);
    } else {
      GX2SetScissor(0, 0, g_gl_context->viewport.width,
                    g_gl_context->viewport.height);
    }
  }

  if (g_gl_context->dirty_flags & GL_DIRTY_LINE_WIDTH) {
    GX2SetLineWidth(g_gl_context->line_width);
  }

  if (g_gl_context->dirty_flags &
      (GL_DIRTY_CULL | GL_DIRTY_FRONT_FACE | GL_DIRTY_POLYGON_MODE)) {
    GX2PolygonMode gx2_polygon_mode =
        map_polygon_mode(g_gl_context->polygon_mode, &ignored);
    bool polygon_offset_enabled = is_polygon_offset_enabled();
    BOOL cull_front = GX2_FALSE;
    BOOL cull_back = GX2_FALSE;

    if (g_gl_context->cull_face_enabled) {
      if (g_gl_context->cull_face_mode == GL_FRONT ||
          g_gl_context->cull_face_mode == GL_FRONT_AND_BACK) {
        cull_front = GX2_TRUE;
      }
      if (g_gl_context->cull_face_mode == GL_BACK ||
          g_gl_context->cull_face_mode == GL_FRONT_AND_BACK) {
        cull_back = GX2_TRUE;
      }
    }

    GX2SetPolygonControl(
        map_front_face(g_gl_context->front_face), cull_front, cull_back,
        g_gl_context->polygon_mode != GL_FILL ? GX2_TRUE : GX2_FALSE,
        gx2_polygon_mode, gx2_polygon_mode,
        polygon_offset_enabled ? GX2_TRUE : GX2_FALSE,
        polygon_offset_enabled ? GX2_TRUE : GX2_FALSE,
        polygon_offset_enabled ? GX2_TRUE : GX2_FALSE);
    GX2SetPolygonOffset(g_gl_context->polygon_offset_units,
                        g_gl_context->polygon_offset_factor,
                        g_gl_context->polygon_offset_units,
                        g_gl_context->polygon_offset_factor, 0.0f);
  }

  if (g_gl_context->dirty_flags &
      (GL_DIRTY_VAO | GL_DIRTY_PROGRAM | GL_DIRTY_TEXTURE_BINDINGS |
       GL_DIRTY_UNIFORM_BINDINGS)) {
    gl_bind_shaders();
    gl_bind_textures();
    gl_bind_vao();
  }

  if (g_gl_context->dirty_flags &
      (GL_DIRTY_FRAMEBUFFER | GL_DIRTY_COLOR_MASK | GL_DIRTY_BLEND)) {
    gl_bind_framebuffers();
  }

  g_gl_context->dirty_flags = 0;
}

#ifdef __cplusplus
}
#endif
