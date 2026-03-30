#include "gl_mem.h"
#include <coreinit/memory.h>
#include <coreinit/memexpheap.h>

static MEMHeapHandle mem1_heap = NULL;
static MEMHeapHandle mem2_heap = NULL;

void gl_mem_init(void) {
    uint32_t mem1_start = 0, mem1_end = 0;
    uint32_t mem2_start = 0, mem2_end = 0;

    // Grab MEM1 bounds
    OSGetMemBound(OS_MEM1, &mem1_start, &mem1_end);
    if (mem1_start != 0 && mem1_end != 0) {
        uint32_t size = mem1_end - mem1_start;
        mem1_heap = MEMCreateExpHeapEx((void*)mem1_start, size, 0);
    }

    // Grab MEM2 bounds
    OSGetMemBound(OS_MEM2, &mem2_start, &mem2_end);
    if (mem2_start != 0 && mem2_end != 0) {
        uint32_t size = mem2_end - mem2_start;
        mem2_heap = MEMCreateExpHeapEx((void*)mem2_start, size, 0);
    }
}

void gl_mem_shutdown(void) {
    if (mem1_heap) {
        MEMDestroyExpHeap(mem1_heap);
        mem1_heap = NULL;
    }
    if (mem2_heap) {
        MEMDestroyExpHeap(mem2_heap);
        mem2_heap = NULL;
    }
}

void* gl_mem_alloc(gl_mem_type_t type, size_t size, uint32_t align) {
    MEMHeapHandle heap = (type == GL_MEM_TYPE_MEM1) ? mem1_heap : mem2_heap;
    if (!heap) return NULL;
    
    return MEMAllocFromExpHeapEx(heap, (uint32_t)size, (int32_t)align);
}

void gl_mem_free(gl_mem_type_t type, void* ptr) {
    MEMHeapHandle heap = (type == GL_MEM_TYPE_MEM1) ? mem1_heap : mem2_heap;
    if (!heap || !ptr) return;

    MEMFreeToExpHeap(heap, ptr);
}
