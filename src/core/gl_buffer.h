#ifndef GL33_BUFFER_H
#define GL33_BUFFER_H

#include "core/gl_context.h"
#include <gx2/clear.h>
#include <gx2/mem.h>
#include <gx2r/buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize buffer subsytem
void gl_buffer_init(void);

// Called per-frame to clean up pending_delete buffers
void gl_buffer_end_frame(void);

// Mapped functions for dispatch
void _gl_GenBuffers(GLsizei n, GLuint *buffers);
void _gl_DeleteBuffers(GLsizei n, const GLuint *buffers);
void _gl_BindBuffer(GLenum target, GLuint buffer);
void _gl_BindBufferBase(GLenum target, GLuint index, GLuint buffer);
void _gl_BindBufferRange(GLenum target, GLuint index, GLuint buffer,
                         GLintptr offset, GLsizeiptr size);
void _gl_BufferData(GLenum target, GLsizeiptr size, const GLvoid *data,
                    GLenum usage);
void _gl_BufferSubData(GLenum target, GLintptr offset, GLsizeiptr size,
                       const GLvoid *data);
void *_gl_MapBuffer(GLenum target, GLenum access);
void *_gl_MapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length,
                         GLbitfield access);
GLboolean _gl_UnmapBuffer(GLenum target);

void *gl_buffer_get_data(GLuint buffer);
GLsizeiptr gl_buffer_get_size(GLuint buffer);
GX2RBuffer *gl_buffer_get_gx2r_buffer(GLuint buffer);

#ifdef __cplusplus
}
#endif

#endif // GL33_BUFFER_H
