#include "gl_mem.h"
#include "Platform/WiiU_Log.hpp"

#include <malloc.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <coreinit/cache.h>
#include <gx2/draw.h>
#include <gx2/event.h>
#include <gx2/mem.h>

#ifndef GX2GL_ENABLE_VERBOSE_FILE_LOGS
#define GX2GL_ENABLE_VERBOSE_FILE_LOGS 0
#endif

typedef struct {
    uint32_t init_count;
    uint32_t live_allocations;
} GLMemState;

static GLMemState g_mem_state;

static void log_mem_step(const char *format, ...) {
#if GX2GL_ENABLE_VERBOSE_FILE_LOGS
    char buffer[512];
    va_list args;
    FILE *log_file;

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    WiiU_Log("%s", buffer);

    log_file = fopen(WiiU_GetGX2GLInitLogPath(), "a");
    if (log_file) {
        fprintf(log_file, "[gx2gl-mem] %s\n", buffer);
        fclose(log_file);
    }
#else
    (void)format;
#endif
}

static uint32_t normalize_alignment(uint32_t align) {
    uint32_t normalized = align < sizeof(void *) ? (uint32_t)sizeof(void *) : align;

    if ((normalized & (normalized - 1u)) == 0) {
        return normalized;
    }

    normalized--;
    normalized |= normalized >> 1;
    normalized |= normalized >> 2;
    normalized |= normalized >> 4;
    normalized |= normalized >> 8;
    normalized |= normalized >> 16;
    normalized++;
    return normalized;
}

static const char *mem_type_name(gl_mem_type_t type) {
    switch (type) {
    case GL_MEM_TYPE_MEM1: return "MEM1";
    case GL_MEM_TYPE_MEM2: return "MEM2";
    default: return "unknown";
    }
}

void gl_mem_init(void) {
    g_mem_state.init_count++;
    log_mem_step("gl_mem_init: count=%u", (unsigned int)g_mem_state.init_count);
}

void gl_mem_shutdown(void) {
    if (g_mem_state.init_count == 0) {
        return;
    }

    g_mem_state.init_count--;
    log_mem_step("gl_mem_shutdown: count=%u live=%u",
                 (unsigned int)g_mem_state.init_count,
                 (unsigned int)g_mem_state.live_allocations);
}

void* gl_mem_alloc(gl_mem_type_t type, size_t size, uint32_t align) {
    uint32_t normalized_align;
    void *ptr;

    if (size == 0) {
        return NULL;
    }

    normalized_align = normalize_alignment(align);
    if (normalized_align == 0) {
        log_mem_step("gl_mem_alloc: alignment overflow type=%s size=%u align=%u",
                     mem_type_name(type), (unsigned int)size, (unsigned int)align);
        return NULL;
    }

    ptr = memalign((size_t)normalized_align, size);
    if (!ptr) {
        log_mem_step("gl_mem_alloc: failed type=%s size=%u align=%u normalized=%u",
                     mem_type_name(type), (unsigned int)size,
                     (unsigned int)align, (unsigned int)normalized_align);
        return NULL;
    }

    g_mem_state.live_allocations++;
    return ptr;
}

void gl_mem_free(gl_mem_type_t type, void* ptr) {
    (void)type;

    if (!ptr) {
        return;
    }

    if (g_mem_state.live_allocations > 0) {
        g_mem_state.live_allocations--;
    }
    free(ptr);
}

/* ---- per-frame copies and deferred frees ---- */

typedef struct {
    void (*fn)(void *arg);
    void *arg;
} DeferredCall;

/* Copies for one frame. A full ring waits for the GPU and starts over. */
static const uint32_t kFrameRingSize = 8u * 1024u * 1024u;
static uint8_t *g_frame_ring;
static uint32_t g_frame_ring_used;

static DeferredCall *g_deferred;
static uint32_t g_deferred_count;
static uint32_t g_deferred_capacity;

static void run_deferred(void) {
    /* A call may defer more work, which then waits for the next sync. */
    DeferredCall *calls = g_deferred;
    uint32_t count = g_deferred_count;

    g_deferred = NULL;
    g_deferred_count = 0;
    g_deferred_capacity = 0;

    for (uint32_t i = 0; i < count; ++i) {
        calls[i].fn(calls[i].arg);
    }
    free(calls);
}

void gl_mem_gpu_sync(void) {
    GX2DrawDone();
    g_frame_ring_used = 0;
    run_deferred();
}

void gl_mem_frame_end(void) {
    /* WHBGfxFinishRender() has waited for the GPU. */
    g_frame_ring_used = 0;
    run_deferred();
}

void gl_mem_defer(void (*fn)(void *arg), void *arg) {
    if (!fn) return;

    if (g_deferred_count == g_deferred_capacity) {
        /* Many thousands pending: wait for the GPU instead of growing. */
        if (g_deferred_capacity >= 65536u) {
            gl_mem_gpu_sync();
        }
        uint32_t capacity = g_deferred_capacity ? g_deferred_capacity * 2u : 256u;
        DeferredCall *calls =
            (DeferredCall *)realloc(g_deferred, capacity * sizeof(DeferredCall));
        if (!calls) {
            /* No memory to wait with: wait now. */
            GX2DrawDone();
            fn(arg);
            return;
        }
        g_deferred = calls;
        g_deferred_capacity = capacity;
    }

    g_deferred[g_deferred_count].fn = fn;
    g_deferred[g_deferred_count].arg = arg;
    g_deferred_count++;
}

static void free_mem2(void *ptr) {
    gl_mem_free(GL_MEM_TYPE_MEM2, ptr);
}

static void free_mem1(void *ptr) {
    gl_mem_free(GL_MEM_TYPE_MEM1, ptr);
}

void gl_mem_free_deferred(gl_mem_type_t type, void *ptr) {
    if (!ptr) return;
    gl_mem_defer(type == GL_MEM_TYPE_MEM1 ? free_mem1 : free_mem2, ptr);
}

void *gl_mem_frame_copy(const void *data, uint32_t size, uint32_t align) {
    uint32_t start;
    uint8_t *copy;

    if (!data || size == 0) return NULL;

    if (!g_frame_ring) {
        g_frame_ring = (uint8_t *)memalign(256, kFrameRingSize);
        if (!g_frame_ring) return NULL;
    }

    align = normalize_alignment(align < 256u ? 256u : align);
    if (size > kFrameRingSize) return NULL;

    start = (g_frame_ring_used + align - 1u) & ~(align - 1u);
    if (start + size > kFrameRingSize) {
        /* Out of room for this frame: let the GPU catch up. */
        gl_mem_gpu_sync();
        start = 0;
    }

    copy = g_frame_ring + start;
    memcpy(copy, data, size);
    g_frame_ring_used = start + size;

    DCFlushRange(copy, size);
    GX2Invalidate(GX2_INVALIDATE_MODE_UNIFORM_BLOCK, copy, size);
    return copy;
}
