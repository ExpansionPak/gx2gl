#include "gl_context.h"
#include "gl_framebuffer.h"
#include "state/gl_state.h"

#include <gx2/clear.h>

void glClearBufferiv(GLenum buffer, GLint drawbuffer, const GLint *value) {
  if (!g_gl_context || !value) return;
  if (buffer == GL_STENCIL) {
    if (drawbuffer != 0) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
    gl_flush_state();
    GX2DepthBuffer *depth = gl_get_draw_depth_buffer();
    if (depth) {
      GX2ClearDepthStencilEx(depth, depth->depthClear, (uint8_t)value[0],
                             GX2_CLEAR_FLAGS_STENCIL);
    }
    return;
  }
  if (buffer == GL_COLOR) {
    if (drawbuffer < 0 || drawbuffer >= 8 ||
        (g_gl_context->bound_framebuffer == 0 && drawbuffer != 0)) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
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
    GX2ColorBuffer *color = gl_get_draw_color_buffer((GLuint)drawbuffer);
    if (color) {
      GX2ClearColor(color, value[0], value[1], value[2], value[3]);
      gl_framebuffer_mark_bound_color_buffer_dirty((GLuint)drawbuffer);
    }
  } else if (buffer == GL_DEPTH) {
    if (drawbuffer != 0) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
    gl_flush_state();
    GX2DepthBuffer *depth = gl_get_draw_depth_buffer();
    if (depth) {
      GX2ClearDepthStencilEx(depth, value[0], (uint8_t)depth->stencilClear,
                             GX2_CLEAR_FLAGS_DEPTH);
    }
  } else {
    _gl_set_error(GL_INVALID_ENUM);
  }
}

void glClearBufferfi(GLenum buffer, GLint drawbuffer, GLfloat depth,
                     GLint stencil) {
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
  GX2DepthBuffer *depth_buffer = gl_get_draw_depth_buffer();
  if (depth_buffer) {
    GX2ClearDepthStencilEx(depth_buffer, depth, (uint8_t)stencil,
                           GX2_CLEAR_FLAGS_BOTH);
  }
}
