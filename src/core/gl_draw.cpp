#include "gl_draw.h"
#include "state/gl_state.h"
#include "core/gl_context.h"
#include "core/gl_vao.h"
#include "core/gl_buffer.h"
#include "core/gl_framebuffer.h"
#include <gx2/draw.h>
#include <gx2/registers.h>
#include <gx2/enum.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

static bool validate_draw_mode(GLenum mode, GX2PrimitiveMode *prim) {
    switch (mode) {
        case GL_TRIANGLES: *prim = GX2_PRIMITIVE_MODE_TRIANGLES; return true;
        case GL_TRIANGLE_STRIP: *prim = GX2_PRIMITIVE_MODE_TRIANGLE_STRIP; return true;
        case GL_TRIANGLE_FAN: *prim = GX2_PRIMITIVE_MODE_TRIANGLE_FAN; return true;
        case GL_LINES: *prim = GX2_PRIMITIVE_MODE_LINES; return true;
        case GL_LINE_STRIP: *prim = GX2_PRIMITIVE_MODE_LINE_STRIP; return true;
        case GL_POINTS: *prim = GX2_PRIMITIVE_MODE_POINTS; return true;
        default: return false;
    }
}

static GX2IndexType map_index_type(GLenum type) {
    switch (type) {
        case GL_UNSIGNED_SHORT: return GX2_INDEX_TYPE_U16;
        case GL_UNSIGNED_INT: return GX2_INDEX_TYPE_U32;
        default: return (GX2IndexType)0xFFFF;
    }
}

static bool validate_draw_buffer_mappings(GLuint element_buffer) {
    if (!g_gl_context) return false;

    for (GLuint i = 0; i < GL33_MAX_VERTEX_ATTRIBS; ++i) {
        gl_vao_attrib_state_t state;
        if (!gl_vao_get_attrib_state(i, &state)) continue;
        if (state.enabled && state.buffer != 0 &&
            gl_buffer_is_mapped(state.buffer) == GL_TRUE) {
            _gl_set_error(GL_INVALID_OPERATION);
            return false;
        }
    }

    if (element_buffer != 0 && gl_buffer_is_mapped(element_buffer) == GL_TRUE) {
        _gl_set_error(GL_INVALID_OPERATION);
        return false;
    }

    for (uint32_t i = 0; i < GL33_MAX_UNIFORM_BUFFER_BINDINGS; ++i) {
        GLuint buffer = g_gl_context->uniform_buffer_bindings[i].buffer;
        if (buffer != 0 && gl_buffer_is_mapped(buffer) == GL_TRUE) {
            _gl_set_error(GL_INVALID_OPERATION);
            return false;
        }
    }

    return true;
}

void _gl_DrawArrays(GLenum mode, GLint first, GLsizei count) { _gl_DrawArraysInstanced(mode, first, count, 1); }

void _gl_DrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instancecount) {
    GX2PrimitiveMode prim;
    if (!g_gl_context || !g_gl_context->bound_program) { _gl_set_error(GL_INVALID_OPERATION); return; }
    if (!gl_vao_has_bound_array()) { _gl_set_error(GL_INVALID_OPERATION); return; }
    if (first < 0 || count < 0 || instancecount < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    if (!validate_draw_mode(mode, &prim)) { _gl_set_error(GL_INVALID_ENUM); return; }
    if (count == 0 || instancecount == 0) return;
    if (!validate_draw_buffer_mappings(0)) return;
    gl_flush_state();
    GX2DrawEx(prim, count, first, (uint32_t)instancecount);
    gl_framebuffer_mark_bound_color_dirty();
}

void _gl_DrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices) { _gl_DrawElementsInstanced(mode, count, type, indices, 1); }

void _gl_DrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices, GLsizei instancecount) {
    GX2PrimitiveMode prim;
    if (!g_gl_context || !g_gl_context->bound_program) { _gl_set_error(GL_INVALID_OPERATION); return; }
    if (!gl_vao_has_bound_array()) { _gl_set_error(GL_INVALID_OPERATION); return; }
    if (count < 0 || instancecount < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    if (!validate_draw_mode(mode, &prim)) { _gl_set_error(GL_INVALID_ENUM); return; }
    if (count == 0 || instancecount == 0) return;
    GX2IndexType idx_type = map_index_type(type);
    if (idx_type == (GX2IndexType)0xFFFF) { _gl_set_error(GL_INVALID_ENUM); return; }
    GLuint element_buffer = gl_vao_get_element_array_buffer();
    if (element_buffer == 0) { _gl_set_error(GL_INVALID_OPERATION); return; }
    if (!validate_draw_buffer_mappings(element_buffer)) return;
    void *buffer_data = gl_buffer_get_data(element_buffer);
    if (!buffer_data) { _gl_set_error(GL_INVALID_OPERATION); return; }
    gl_flush_state();
    GX2DrawIndexedEx(prim, count, idx_type, (uint8_t *)buffer_data + (uintptr_t)indices, 0, (uint32_t)instancecount);
    gl_framebuffer_mark_bound_color_dirty();
}

void _gl_DrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *indices) { (void)start; (void)end; _gl_DrawElements(mode, count, type, indices); }
void _gl_DrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices, GLint basevertex) {
    GX2PrimitiveMode prim;
    if (!g_gl_context || !g_gl_context->bound_program) { _gl_set_error(GL_INVALID_OPERATION); return; }
    if (!gl_vao_has_bound_array()) { _gl_set_error(GL_INVALID_OPERATION); return; }
    if (count < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    if (!validate_draw_mode(mode, &prim)) { _gl_set_error(GL_INVALID_ENUM); return; }
    if (count == 0) return;
    GX2IndexType idx_type = map_index_type(type);
    if (idx_type == (GX2IndexType)0xFFFF) { _gl_set_error(GL_INVALID_ENUM); return; }
    GLuint element_buffer = gl_vao_get_element_array_buffer();
    if (element_buffer == 0) { _gl_set_error(GL_INVALID_OPERATION); return; }
    if (!validate_draw_buffer_mappings(element_buffer)) return;
    void *buffer_data = gl_buffer_get_data(element_buffer);
    if (!buffer_data) { _gl_set_error(GL_INVALID_OPERATION); return; }
    gl_flush_state();
    GX2DrawIndexedEx(prim, count, idx_type, (uint8_t *)buffer_data + (uintptr_t)indices, basevertex, 1);
    gl_framebuffer_mark_bound_color_dirty();
}

void _gl_DrawRangeElementsBaseVertex(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *indices, GLint basevertex) {
    (void)start; (void)end; _gl_DrawElementsBaseVertex(mode, count, type, indices, basevertex);
}

void _gl_DrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices, GLsizei instancecount, GLint basevertex) {
    GX2PrimitiveMode prim;
    if (!g_gl_context || !g_gl_context->bound_program) { _gl_set_error(GL_INVALID_OPERATION); return; }
    if (!gl_vao_has_bound_array()) { _gl_set_error(GL_INVALID_OPERATION); return; }
    if (count < 0 || instancecount < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    if (!validate_draw_mode(mode, &prim)) { _gl_set_error(GL_INVALID_ENUM); return; }
    if (count == 0 || instancecount == 0) return;
    GX2IndexType idx_type = map_index_type(type);
    if (idx_type == (GX2IndexType)0xFFFF) { _gl_set_error(GL_INVALID_ENUM); return; }
    GLuint element_buffer = gl_vao_get_element_array_buffer();
    if (element_buffer == 0) { _gl_set_error(GL_INVALID_OPERATION); return; }
    if (!validate_draw_buffer_mappings(element_buffer)) return;
    void *buffer_data = gl_buffer_get_data(element_buffer);
    if (!buffer_data) { _gl_set_error(GL_INVALID_OPERATION); return; }
    gl_flush_state();
    GX2DrawIndexedEx(prim, count, idx_type, (uint8_t *)buffer_data + (uintptr_t)indices, basevertex, instancecount);
    gl_framebuffer_mark_bound_color_dirty();
}

void _gl_MultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei drawcount) {
    for (GLsizei i = 0; i < drawcount; i++) _gl_DrawArrays(mode, first[i], count[i]);
}

void _gl_MultiDrawElements(GLenum mode, const GLsizei *count, GLenum type, const GLvoid *const*indices, GLsizei drawcount) {
    for (GLsizei i = 0; i < drawcount; i++) _gl_DrawElements(mode, count[i], type, indices[i]);
}

void _gl_MultiDrawElementsBaseVertex(GLenum mode, const GLsizei *count, GLenum type, const GLvoid *const*indices, GLsizei drawcount, const GLint *basevertex) {
    for (GLsizei i = 0; i < drawcount; i++) _gl_DrawElementsBaseVertex(mode, count[i], type, indices[i], basevertex[i]);
}

void _gl_PrimitiveRestartIndex(GLuint index) {
    GX2SetPrimitiveRestartIndex(index);
}

#ifdef __cplusplus
}
#endif
