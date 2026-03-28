#include "gl_draw.h"
#include "state/gl_state.h"
#include "core/gl_context.h"
#include "core/gl_vao.h"
#include "core/gl_buffer.h"
#include <gx2/draw.h>
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

void _gl_DrawArrays(GLenum mode, GLint first, GLsizei count) {
    _gl_DrawArraysInstanced(mode, first, count, 1);
}

void _gl_DrawArraysInstanced(GLenum mode, GLint first, GLsizei count,
                             GLsizei instancecount) {
    GX2PrimitiveMode prim;

    if (!g_gl_context || !g_gl_context->bound_program) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    if (first < 0 || count < 0 || instancecount < 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (!validate_draw_mode(mode, &prim)) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (count == 0 || instancecount == 0) {
        return;
    }

    gl_flush_state();
    GX2DrawEx(prim, count, first, (uint32_t)instancecount);
}

void _gl_DrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices) {
    _gl_DrawElementsInstanced(mode, count, type, indices, 1);
}

void _gl_DrawElementsInstanced(GLenum mode, GLsizei count, GLenum type,
                               const GLvoid *indices, GLsizei instancecount) {
    GX2PrimitiveMode prim;

    if (!g_gl_context || !g_gl_context->bound_program) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    if (count < 0 || instancecount < 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (!validate_draw_mode(mode, &prim)) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (count == 0 || instancecount == 0) {
        return;
    }

    gl_flush_state();

    GX2IndexType idx_type;
    switch (type) {
        case GL_UNSIGNED_SHORT: idx_type = GX2_INDEX_TYPE_U16; break;
        case GL_UNSIGNED_INT: idx_type = GX2_INDEX_TYPE_U32; break;
        default: _gl_set_error(GL_INVALID_ENUM); return;
    }

    GLuint element_buffer = gl_vao_get_element_array_buffer();
    if (element_buffer == 0) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    
    void *buffer_data = gl_buffer_get_data(element_buffer);
    GLsizeiptr buffer_size = gl_buffer_get_size(element_buffer);
    uintptr_t index_offset = (uintptr_t)indices;
    uint32_t index_size = type == GL_UNSIGNED_SHORT ? 2u : 4u;
    if (!buffer_data || index_offset > (uintptr_t)buffer_size ||
        index_offset + (uintptr_t)count * index_size > (uintptr_t)buffer_size) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    
    // Combine base pointer with offset
    void *final_indices = (void*)((uintptr_t)buffer_data + index_offset);
    
    // GX2DrawIndexedEx parameters: primitive_mode, count, indexFormat, indices, firstVertex, numInstances
    GX2DrawIndexedEx(prim, count, idx_type, final_indices, 0,
                     (uint32_t)instancecount);
}

#ifdef __cplusplus
}
#endif
