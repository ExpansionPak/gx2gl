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

void _gl_GetIntegerv(GLenum pname, GLint *data) {
    if (!g_gl_context || !data) return;
    switch (pname) {
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
        default: _gl_set_error(GL_INVALID_ENUM); break;
    }
}

void _gl_GetFloatv(GLenum pname, GLfloat *data) {
    if (!data || !g_gl_context) return;
    GLint d = 0;
    g_gl_context->dispatch.glGetIntegerv(pname, &d);
    *data = (GLfloat)d;
}

#ifdef __cplusplus
}
#endif

/* glGetError is implemented in gl_context.cpp using the queue */
