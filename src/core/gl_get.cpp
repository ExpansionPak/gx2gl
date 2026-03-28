#include <gl/gl.h>
#include "core/gl_context.h"
#include <gx2/enum.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

const GLubyte *_gl_GetString(GLenum name) {
    if (!g_gl_context) return NULL;
    switch (name) {
        case GL_VENDOR: return (const GLubyte *)"Nintendo / Homebrew";
        case GL_RENDERER: return (const GLubyte *)"Latte GX2 (Wii U)";
        case GL_VERSION: return (const GLubyte *)"3.3 Core Profile (GX2 Translation)";
        case GL_SHADING_LANGUAGE_VERSION: return (const GLubyte *)"3.30";
        case GL_EXTENSIONS: return (const GLubyte *)"GL_WIIU_shader_group GL_WIIU_cafeglsl_gfd";
        default: _gl_set_error(GL_INVALID_ENUM); return NULL;
    }
}

void _gl_GetBooleanv(GLenum pname, GLboolean *data) {
    if (!g_gl_context || !data) return;
    switch (pname) {
        case GL_BLEND:
            data[0] = g_gl_context->blend_enabled;
            break;
        case GL_DEPTH_TEST:
            data[0] = g_gl_context->depth_test_enabled;
            break;
        case GL_STENCIL_TEST:
            data[0] = g_gl_context->stencil_test_enabled;
            break;
        case GL_CULL_FACE:
            data[0] = g_gl_context->cull_face_enabled;
            break;
        case GL_SCISSOR_TEST:
            data[0] = g_gl_context->scissor_test_enabled;
            break;
        case GL_DEPTH_WRITEMASK:
            data[0] = g_gl_context->depth_mask ? GL_TRUE : GL_FALSE;
            break;
        case GL_COLOR_WRITEMASK:
            data[0] = g_gl_context->color_mask[0] ? GL_TRUE : GL_FALSE;
            data[1] = g_gl_context->color_mask[1] ? GL_TRUE : GL_FALSE;
            data[2] = g_gl_context->color_mask[2] ? GL_TRUE : GL_FALSE;
            data[3] = g_gl_context->color_mask[3] ? GL_TRUE : GL_FALSE;
            break;
        default: {
            GLint int_data = 0;
            _gl_GetIntegerv(pname, &int_data);
            data[0] = int_data ? GL_TRUE : GL_FALSE;
            break;
        }
    }
}

void _gl_GetIntegerv(GLenum pname, GLint *data) {
    if (!g_gl_context || !data) return;
    switch (pname) {
        case GL_CURRENT_PROGRAM: *data = (GLint)g_gl_context->bound_program; break;
        case GL_MAX_TEXTURE_SIZE: *data = 8192; break; // Latte maximum
        case GL_MAX_VERTEX_ATTRIBS: *data = 16; break;
        case GL_MAX_TEXTURE_IMAGE_UNITS: *data = 16; break;
        case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS: *data = 32; break;
        case GL_MAX_ARRAY_TEXTURE_LAYERS: *data = 2048; break;
        case GL_MAX_COLOR_ATTACHMENTS: *data = 8; break;
        case GL_MAX_DRAW_BUFFERS: *data = 8; break;
        case GL_MAX_RENDERBUFFER_SIZE: *data = 8192; break;
        case GL_MAX_UNIFORM_BUFFER_BINDINGS: *data = GL33_MAX_UNIFORM_BUFFER_BINDINGS; break;
        case GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT: *data = GX2_UNIFORM_BLOCK_ALIGNMENT; break;
        case GL_BLEND: *data = g_gl_context->blend_enabled ? 1 : 0; break;
        case GL_DEPTH_TEST: *data = g_gl_context->depth_test_enabled ? 1 : 0; break;
        case GL_STENCIL_TEST: *data = g_gl_context->stencil_test_enabled ? 1 : 0; break;
        case GL_CULL_FACE: *data = g_gl_context->cull_face_enabled ? 1 : 0; break;
        case GL_SCISSOR_TEST: *data = g_gl_context->scissor_test_enabled ? 1 : 0; break;
        case GL_DEPTH_WRITEMASK: *data = g_gl_context->depth_mask ? 1 : 0; break;
        case GL_STENCIL_WRITEMASK: *data = (GLint)g_gl_context->stencil_write_mask[0]; break;
        case GL_COLOR_WRITEMASK:
            data[0] = g_gl_context->color_mask[0] ? 1 : 0;
            data[1] = g_gl_context->color_mask[1] ? 1 : 0;
            data[2] = g_gl_context->color_mask[2] ? 1 : 0;
            data[3] = g_gl_context->color_mask[3] ? 1 : 0;
            break;
        case GL_STENCIL_CLEAR_VALUE: *data = g_gl_context->clear_stencil; break;
        default: _gl_set_error(GL_INVALID_ENUM); break;
    }
}

void _gl_GetFloatv(GLenum pname, GLfloat *data) {
    if (!data || !g_gl_context) return;
    switch (pname) {
        case GL_BLEND_COLOR:
            data[0] = g_gl_context->blend_color[0];
            data[1] = g_gl_context->blend_color[1];
            data[2] = g_gl_context->blend_color[2];
            data[3] = g_gl_context->blend_color[3];
            break;
        case GL_DEPTH_RANGE:
            data[0] = g_gl_context->viewport.near_z;
            data[1] = g_gl_context->viewport.far_z;
            break;
        case GL_COLOR_CLEAR_VALUE:
            data[0] = g_gl_context->clear_color[0];
            data[1] = g_gl_context->clear_color[1];
            data[2] = g_gl_context->clear_color[2];
            data[3] = g_gl_context->clear_color[3];
            break;
        case GL_DEPTH_CLEAR_VALUE:
            data[0] = g_gl_context->clear_depth;
            break;
        case GL_LINE_WIDTH:
            data[0] = g_gl_context->line_width;
            break;
        default: {
            GLint d = 0;
            g_gl_context->dispatch.glGetIntegerv(pname, &d);
            data[0] = (GLfloat)d;
            break;
        }
    }
}

void _gl_GetDoublev(GLenum pname, GLdouble *data) {
    if (!data || !g_gl_context) return;
    switch (pname) {
        case GL_BLEND_COLOR: {
            GLfloat floats[4];
            _gl_GetFloatv(pname, floats);
            data[0] = (GLdouble)floats[0];
            data[1] = (GLdouble)floats[1];
            data[2] = (GLdouble)floats[2];
            data[3] = (GLdouble)floats[3];
            break;
        }
        case GL_DEPTH_RANGE: {
            GLfloat floats[2];
            _gl_GetFloatv(pname, floats);
            data[0] = (GLdouble)floats[0];
            data[1] = (GLdouble)floats[1];
            break;
        }
        case GL_COLOR_CLEAR_VALUE: {
            GLfloat floats[4];
            _gl_GetFloatv(pname, floats);
            data[0] = (GLdouble)floats[0];
            data[1] = (GLdouble)floats[1];
            data[2] = (GLdouble)floats[2];
            data[3] = (GLdouble)floats[3];
            break;
        }
        case GL_COLOR_WRITEMASK: {
            GLboolean mask[4];
            _gl_GetBooleanv(pname, mask);
            data[0] = mask[0] ? 1.0 : 0.0;
            data[1] = mask[1] ? 1.0 : 0.0;
            data[2] = mask[2] ? 1.0 : 0.0;
            data[3] = mask[3] ? 1.0 : 0.0;
            break;
        }
        case GL_DEPTH_CLEAR_VALUE:
        case GL_LINE_WIDTH: {
            GLfloat value = 0.0f;
            _gl_GetFloatv(pname, &value);
            data[0] = (GLdouble)value;
            break;
        }
        default: {
            GLint value = 0;
            _gl_GetIntegerv(pname, &value);
            data[0] = (GLdouble)value;
            break;
        }
    }
}

#ifdef __cplusplus
}
#endif

/* glGetError is implemented in gl_context.cpp using the queue */
