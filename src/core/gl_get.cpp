#include "gl_context.h"
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

static const char *g_vendor = "Wii U Homebrew";
static const char *g_renderer = "AMD Latte";
static const char *g_version = "3.3.0 (gx2gl)";
static const char *g_sl_version = "330";
static const char *g_extensions[] = {
    "GL_ARB_vertex_array_object",
    "GL_ARB_framebuffer_object",
    "GL_ARB_uniform_buffer_object",
    "GL_ARB_instanced_arrays",
    NULL
};
static const uint32_t g_extension_count = 4;

const GLubyte *_gl_GetString(GLenum name) {
    switch (name) {
    case GL_VENDOR: return (const GLubyte *)g_vendor;
    case GL_RENDERER: return (const GLubyte *)g_renderer;
    case GL_VERSION: return (const GLubyte *)g_version;
    case GL_SHADING_LANGUAGE_VERSION: return (const GLubyte *)g_sl_version;
    default: _gl_set_error(GL_INVALID_ENUM); return NULL;
    }
}

const GLubyte *_gl_GetStringi(GLenum name, GLuint index) {
    if (name != GL_EXTENSIONS) { _gl_set_error(GL_INVALID_ENUM); return NULL; }
    if (index >= g_extension_count) { _gl_set_error(GL_INVALID_VALUE); return NULL; }
    return (const GLubyte *)g_extensions[index];
}

void _gl_GetBooleanv(GLenum pname, GLboolean *data) {
    if (!g_gl_context || !data) return;
    switch (pname) {
    case GL_DEPTH_TEST: *data = g_gl_context->depth_test_enabled; break;
    case GL_STENCIL_TEST: *data = g_gl_context->stencil_test_enabled; break;
    case GL_BLEND: *data = g_gl_context->blend_enabled; break;
    case GL_CULL_FACE: *data = g_gl_context->cull_face_enabled; break;
    case GL_SCISSOR_TEST: *data = g_gl_context->scissor_test_enabled; break;
    case GL_DEPTH_WRITEMASK: *data = g_gl_context->depth_mask; break;
    case GL_COLOR_WRITEMASK:
        data[0] = g_gl_context->color_mask[0];
        data[1] = g_gl_context->color_mask[1];
        data[2] = g_gl_context->color_mask[2];
        data[3] = g_gl_context->color_mask[3];
        break;
    default: _gl_set_error(GL_INVALID_ENUM); break;
    }
}

void _gl_GetIntegerv(GLenum pname, GLint *data) {
    if (!g_gl_context || !data) return;
    switch (pname) {
    case GL_CURRENT_PROGRAM: *data = (GLint)g_gl_context->bound_program; break;
    case GL_MAX_TEXTURE_SIZE: *data = 8192; break;
    case GL_MAX_VERTEX_ATTRIBS: *data = 16; break;
    case GL_MAX_TEXTURE_IMAGE_UNITS: *data = 16; break;
    case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS: *data = 32; break;
    case GL_MAX_ARRAY_TEXTURE_LAYERS: *data = 2048; break;
    case GL_MAX_COLOR_ATTACHMENTS: *data = 8; break;
    case GL_MAX_DRAW_BUFFERS: *data = 8; break;
    case GL_MAX_RENDERBUFFER_SIZE: *data = 8192; break;
    case GL_MAX_UNIFORM_BUFFER_BINDINGS: *data = GL33_MAX_UNIFORM_BUFFER_BINDINGS; break;
    case GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT: *data = 256; break;
    case GL_NUM_EXTENSIONS: *data = (GLint)g_extension_count; break;
    case GL_SHADER_COMPILER: *data = 1; break;
    case GL_NUM_SHADER_BINARY_FORMATS: *data = 0; break;
    case GL_NUM_COMPRESSED_TEXTURE_FORMATS: *data = 0; break;
    case GL_PACK_ALIGNMENT: *data = g_gl_context->pack_alignment; break;
    case GL_UNPACK_ALIGNMENT: *data = g_gl_context->unpack_alignment; break;
    case GL_VIEWPORT:
        data[0] = g_gl_context->viewport.x; data[1] = g_gl_context->viewport.y;
        data[2] = (GLint)g_gl_context->viewport.width; data[3] = (GLint)g_gl_context->viewport.height;
        break;
    case GL_SCISSOR_BOX:
        data[0] = g_gl_context->scissor.x; data[1] = g_gl_context->scissor.y;
        data[2] = (GLint)g_gl_context->scissor.width; data[3] = (GLint)g_gl_context->scissor.height;
        break;
    default: _gl_set_error(GL_INVALID_ENUM); break;
    }
}

void _gl_GetFloatv(GLenum pname, GLfloat *data) {
    if (!g_gl_context || !data) return;
    switch (pname) {
    case GL_DEPTH_RANGE: data[0] = g_gl_context->viewport.near_z; data[1] = g_gl_context->viewport.far_z; break;
    case GL_COLOR_CLEAR_VALUE:
        data[0] = g_gl_context->clear_color[0]; data[1] = g_gl_context->clear_color[1];
        data[2] = g_gl_context->clear_color[2]; data[3] = g_gl_context->clear_color[3];
        break;
    case GL_DEPTH_CLEAR_VALUE: *data = g_gl_context->clear_depth; break;
    case GL_LINE_WIDTH: *data = g_gl_context->line_width; break;
    case GL_SAMPLE_COVERAGE_VALUE: *data = g_gl_context->sample_coverage_value; break;
    default: _gl_set_error(GL_INVALID_ENUM); break;
    }
}

void _gl_GetDoublev(GLenum pname, GLdouble *data) {
    GLfloat fdata[4];
    _gl_GetFloatv(pname, fdata);
    if (!data) return;
    for (int i = 0; i < 4; i++) data[i] = (GLdouble)fdata[i];
}

#ifdef __cplusplus
}
#endif
