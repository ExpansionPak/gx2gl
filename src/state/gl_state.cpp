#include "gl_state.h"
#include "core/gl_framebuffer.h"
#include "core/gl_shader.h"
#include "core/gl_texture.h"
#include "core/gl_vao.h"
#ifdef __cplusplus
extern "C" {
#endif
#include <gx2/enum.h>
#include <gx2/registers.h>
#include <gx2/state.h>
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
// TODO: explain what's going on here (besides the obvious)
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
  case GL_CULL_FACE:
    g_gl_context->cull_face_enabled = GL_TRUE;
    g_gl_context->dirty_flags |= GL_DIRTY_CULL;
    break;
  case GL_SCISSOR_TEST:
    g_gl_context->scissor_test_enabled = GL_TRUE;
    g_gl_context->dirty_flags |= GL_DIRTY_SCISSOR;
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
  case GL_CULL_FACE:
    g_gl_context->cull_face_enabled = GL_FALSE;
    g_gl_context->dirty_flags |= GL_DIRTY_CULL;
    break;
  case GL_SCISSOR_TEST:
    g_gl_context->scissor_test_enabled = GL_FALSE;
    g_gl_context->dirty_flags |= GL_DIRTY_SCISSOR;
    break;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    break;
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
  if (!g_gl_context)
    return;
  bool v;
  map_blend_eq(mode, &v);
  if (!v) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  g_gl_context->blend_eq_rgb = mode;
  g_gl_context->blend_eq_alpha = mode;
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

void _gl_StencilFunc(GLenum func, GLint ref, GLuint mask) {
  if (!g_gl_context)
    return;
  bool v;
  map_compare_func(func, &v);
  if (!v) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  g_gl_context->stencil_func[0] = func;
  g_gl_context->stencil_func[1] = func;
  g_gl_context->stencil_ref[0] = ref;
  g_gl_context->stencil_ref[1] = ref;
  g_gl_context->stencil_mask[0] = mask;
  g_gl_context->stencil_mask[1] = mask;
  g_gl_context->dirty_flags |= GL_DIRTY_DEPTH_STENCIL;
}

void _gl_StencilOp(GLenum fail, GLenum zfail, GLenum zpass) {
  if (!g_gl_context)
    return;
  bool v1, v2, v3;
  map_stencil_op(fail, &v1);
  map_stencil_op(zfail, &v2);
  map_stencil_op(zpass, &v3);
  if (!v1 || !v2 || !v3) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  g_gl_context->stencil_fail[0] = fail;
  g_gl_context->stencil_fail[1] = fail;
  g_gl_context->stencil_zfail[0] = zfail;
  g_gl_context->stencil_zfail[1] = zfail;
  g_gl_context->stencil_zpass[0] = zpass;
  g_gl_context->stencil_zpass[1] = zpass;
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

void gl_flush_state(void) {
  if (!g_gl_context || !g_gl_context->dirty_flags)
    return;

  bool ignored;

  if (g_gl_context->dirty_flags & GL_DIRTY_BLEND) {
    for (uint32_t rt = 0; rt < 8; ++rt) {
      GX2BlendControlReg blendReg;
      GX2InitBlendControlReg(
          &blendReg, (GX2RenderTarget)rt,
          (GX2BlendMode)map_blend_factor(g_gl_context->blend_src_rgb, &ignored),
          (GX2BlendMode)map_blend_factor(g_gl_context->blend_dst_rgb, &ignored),
          (GX2BlendCombineMode)map_blend_eq(g_gl_context->blend_eq_rgb, &ignored),
          g_gl_context->blend_enabled ? GX2_TRUE : GX2_FALSE,
          (GX2BlendMode)map_blend_factor(g_gl_context->blend_src_alpha, &ignored),
          (GX2BlendMode)map_blend_factor(g_gl_context->blend_dst_alpha, &ignored),
          (GX2BlendCombineMode)map_blend_eq(g_gl_context->blend_eq_alpha, &ignored));
      GX2SetBlendControlReg(&blendReg);
    }
  }

  if (g_gl_context->dirty_flags & GL_DIRTY_DEPTH_STENCIL) {
    GX2DepthStencilControlReg dsReg;
    GX2InitDepthStencilControlReg(&dsReg,
                                  g_gl_context->depth_test_enabled ? GX2_ENABLE : GX2_DISABLE,
                                  g_gl_context->depth_mask ? GX2_ENABLE : GX2_DISABLE,
                                  (GX2CompareFunction)map_compare_func(g_gl_context->depth_func, &ignored),
                                  GX2_DISABLE, GX2_DISABLE,
                                  (GX2CompareFunction)0, (GX2StencilFunction)0, (GX2StencilFunction)0, (GX2StencilFunction)0,
                                  (GX2CompareFunction)0, (GX2StencilFunction)0, (GX2StencilFunction)0, (GX2StencilFunction)0);
    GX2SetDepthStencilControlReg(&dsReg);
  }

  if (g_gl_context->dirty_flags & GL_DIRTY_VIEWPORT) {
    GX2SetViewport(g_gl_context->viewport.x, g_gl_context->viewport.y,
                   g_gl_context->viewport.width, g_gl_context->viewport.height,
                   0.0f, 1.0f);
  }

  if (g_gl_context->dirty_flags & GL_DIRTY_SCISSOR) {
    if (g_gl_context->scissor_test_enabled) {
      GX2SetScissor(g_gl_context->scissor.x, g_gl_context->scissor.y,
                    g_gl_context->scissor.width, g_gl_context->scissor.height);
    }
  }

  if (g_gl_context->dirty_flags & GL_DIRTY_LINE_WIDTH) {
    GX2SetLineWidth(g_gl_context->line_width);
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
