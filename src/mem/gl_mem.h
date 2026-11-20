#ifndef GL33_MEM_H
#define GL33_MEM_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    GL_MEM_TYPE_MEM1 = 0,
    GL_MEM_TYPE_MEM2 = 1
} gl_mem_type_t;

void gl_mem_init(void);
void gl_mem_shutdown(void);

void* gl_mem_alloc(gl_mem_type_t type, size_t size, uint32_t align);
void gl_mem_free(gl_mem_type_t type, void* ptr);

/*
 * GPU commands run after the CPU has queued them, often after it moved on.
 * Memory the queued draws read must not change or be reused until they
 * have run, which is at the latest the end of the frame (WHBGfxFinishRender
 * waits for the GPU).
 *
 * gl_mem_frame_copy() copies data that the CPU keeps changing (uniform
 * shadows, uniform buffers) to memory that stays untouched until the end
 * of the frame, and returns the copy, flushed for the GPU.
 *
 * gl_mem_free_deferred() frees memory once the frame's draws are done.
 * gl_mem_defer() runs any cleanup then.
 *
 * gl_mem_frame_end() is called after the frame is presented; it reclaims
 * both. gl_mem_gpu_sync() does the same mid-frame, after waiting for the
 * GPU, when they grow too large.
 */
void* gl_mem_frame_copy(const void* data, uint32_t size, uint32_t align);
void gl_mem_free_deferred(gl_mem_type_t type, void* ptr);
void gl_mem_defer(void (*fn)(void* arg), void* arg);
void gl_mem_frame_end(void);
void gl_mem_gpu_sync(void);

#endif
