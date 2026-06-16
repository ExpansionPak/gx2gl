#include "vk/gx2vk_internal.h"

#include <gx2r/buffer.h>
#include <gx2r/mem.h>
#include <malloc.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "endian/endian.h"

static bool g_gx2vk_allocator_ready = false;

static uint32_t gx2vk_normalize_alignment(uint32_t alignment) {
    uint32_t normalized = alignment < sizeof(void *) ? (uint32_t)sizeof(void *) : alignment;

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

static void *gx2vk_gx2r_alloc(GX2RResourceFlags flags, uint32_t size, uint32_t alignment) {
    (void)flags;
    if (size == 0) return NULL;
    return memalign((size_t)gx2vk_normalize_alignment(alignment), size);
}

static void gx2vk_gx2r_free(GX2RResourceFlags flags, void *ptr) {
    (void)flags;
    free(ptr);
}

static void gx2vk_ensure_allocator_ready(void) {
    if (g_gx2vk_allocator_ready) {
        return;
    }
    GX2RSetAllocator(gx2vk_gx2r_alloc, gx2vk_gx2r_free);
    g_gx2vk_allocator_ready = true;
}

static GX2RResourceFlags gx2vk_buffer_usage_to_gx2r_flags(VkBufferUsageFlags usage) {
    GX2RResourceFlags flags = GX2R_RESOURCE_USAGE_CPU_WRITE |
                              GX2R_RESOURCE_USAGE_GPU_READ |
                              GX2R_RESOURCE_USAGE_FORCE_MEM2;

    if (usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) {
        flags = (GX2RResourceFlags)(flags | GX2R_RESOURCE_BIND_VERTEX_BUFFER);
    }
    if (usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) {
        flags = (GX2RResourceFlags)(flags | GX2R_RESOURCE_BIND_INDEX_BUFFER);
    }
    if (usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) {
        flags = (GX2RResourceFlags)(flags | GX2R_RESOURCE_BIND_UNIFORM_BLOCK);
    }
    if (usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) {
        flags = (GX2RResourceFlags)(flags | GX2R_RESOURCE_USAGE_CPU_READ | GX2R_RESOURCE_USAGE_DMA_READ);
    }
    if (usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) {
        flags = (GX2RResourceFlags)(flags | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_DMA_WRITE);
    }

    return flags;
}

static uint64_t gx2vk_compute_image_size(const VkImageCreateInfo *create_info) {
    uint32_t mip_level;
    uint64_t total_size = 0;
    uint64_t width = create_info->extent.width ? create_info->extent.width : 1;
    uint64_t height = create_info->extent.height ? create_info->extent.height : 1;
    uint64_t depth = create_info->extent.depth ? create_info->extent.depth : 1;
    uint32_t bpp = 4;

    switch (create_info->format) {
        case VK_FORMAT_R8_UNORM:
        case VK_FORMAT_S8_UINT:
            bpp = 1;
            break;
        case VK_FORMAT_R8G8_UNORM:
        case VK_FORMAT_D16_UNORM:
            bpp = 2;
            break;
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_D24_UNORM_S8_UINT:
            bpp = 4;
            break;
        default:
            bpp = 4;
            break;
    }

    for (mip_level = 0; mip_level < create_info->mipLevels; ++mip_level) {
        total_size += width * height * depth * bpp;
        if (width > 1) {
            width >>= 1;
        }
        if (height > 1) {
            height >>= 1;
        }
        if (depth > 1) {
            depth >>= 1;
        }
    }

    total_size *= create_info->arrayLayers ? create_info->arrayLayers : 1;
    return total_size;
}

static void gx2vk_fill_memory_requirements(VkMemoryRequirements *requirements,
                                           uint64_t size,
                                           VkDeviceSize alignment) {
    requirements->size = size;
    requirements->alignment = alignment;
    requirements->memoryTypeBits = 0x1;
}

static void gx2vk_fill_memory_requirements2(VkMemoryRequirements2 *requirements2,
                                            uint64_t size,
                                            VkDeviceSize alignment) {
    if (!requirements2) {
        return;
    }
    requirements2->sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    requirements2->pNext = NULL;
    gx2vk_fill_memory_requirements(&requirements2->memoryRequirements, size, alignment);
}

static void gx2vk_fill_buffer_memory_requirements(VkDeviceSize size,
                                                  VkBufferUsageFlags usage,
                                                  VkMemoryRequirements *requirements) {
    VkDeviceSize alignment = 16;

    if (!requirements) {
        return;
    }
    if (usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) {
        alignment = 256;
    } else if (usage & (VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
        alignment = 64;
    } else if (usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) {
        alignment = 256;
    }

    gx2vk_fill_memory_requirements(requirements, size, alignment);
}

static void gx2vk_fill_buffer_memory_requirements2(const gx2vk_buffer_t *buffer,
                                                   VkMemoryRequirements2 *requirements2) {
    if (!buffer || !requirements2) {
        return;
    }
    requirements2->sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    requirements2->pNext = NULL;
    gx2vk_fill_buffer_memory_requirements(buffer->size,
                                          buffer->usage,
                                          &requirements2->memoryRequirements);
}

static void gx2vk_copy_range_to_gpu(gx2vk_memory_t *memory,
                                    VkDeviceSize offset,
                                    VkDeviceSize size) {
    gx2vk_buffer_t *buffer = NULL;
    uint8_t *dst = NULL;
    uint8_t *src = NULL;
    uint32_t i;

    if (!memory || !memory->bound_buffer || !memory->bound_buffer->gx2r_created || !memory->host_storage || size == 0) {
        return;
    }

    buffer = memory->bound_buffer;
    src = memory->host_storage + buffer->bound_offset + offset;
    dst = (uint8_t *)GX2RLockBufferEx(&buffer->gx2r_buffer, GX2R_RESOURCE_USAGE_CPU_WRITE);
    if (!dst) {
        return;
    }
    dst += offset;

    if (buffer->needs_uniform_word_swap) {
        uint32_t word_count = (uint32_t)(size / sizeof(uint32_t));
        const uint32_t *src_words = (const uint32_t *)src;
        uint32_t *dst_words = (uint32_t *)dst;
        for (i = 0; i < word_count; ++i) {
            dst_words[i] = CPU_TO_GPU_32(src_words[i]);
        }
        if ((size % sizeof(uint32_t)) != 0) {
            memcpy(dst + (word_count * sizeof(uint32_t)),
                   src + (word_count * sizeof(uint32_t)),
                   (size_t)(size % sizeof(uint32_t)));
        }
    } else {
        memcpy(dst, src, (size_t)size);
    }

    GX2RUnlockBufferEx(&buffer->gx2r_buffer, GX2R_RESOURCE_USAGE_CPU_WRITE);
    GX2RInvalidateBuffer(&buffer->gx2r_buffer,
                         (GX2RResourceFlags)(GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ));
}

static VkResult gx2vk_bind_memory_to_buffer(gx2vk_buffer_t *buffer,
                                            gx2vk_memory_t *memory,
                                            VkDeviceSize memory_offset) {
    VkMemoryRequirements requirements;

    if (!buffer || !memory) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    gx2vk_fill_buffer_memory_requirements(buffer->size, buffer->usage, &requirements);
    if (memory_offset != 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (memory->allocation_size < requirements.size) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (memory->memory_type_index != 0) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (memory->bound_buffer && memory->bound_buffer != buffer) {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    gx2vk_ensure_allocator_ready();

    if (buffer->gx2r_created) {
        GX2RDestroyBufferEx(&buffer->gx2r_buffer, (GX2RResourceFlags)0);
        memset(&buffer->gx2r_buffer, 0, sizeof(buffer->gx2r_buffer));
        buffer->gx2r_created = false;
    }

    buffer->gx2r_buffer.flags = buffer->gx2r_flags;
    buffer->gx2r_buffer.elemSize = 1;
    buffer->gx2r_buffer.elemCount = (uint32_t)buffer->size;
    if (!GX2RCreateBuffer(&buffer->gx2r_buffer)) {
        memset(&buffer->gx2r_buffer, 0, sizeof(buffer->gx2r_buffer));
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    buffer->gx2r_created = true;
    buffer->bound_memory = memory;
    buffer->bound_offset = memory_offset;
    memory->bound_buffer = buffer;

    return VK_SUCCESS;
}

extern "C" {

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice device,
                                              const VkBufferCreateInfo *pCreateInfo,
                                              const VkAllocationCallbacks *pAllocator,
                                              VkBuffer *pBuffer) {
    gx2vk_buffer_t *buffer = NULL;
    (void)pAllocator;

    if (!device || !pCreateInfo || !pBuffer || pCreateInfo->sType != VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (pCreateInfo->sharingMode != VK_SHARING_MODE_EXCLUSIVE) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    buffer = (gx2vk_buffer_t *)calloc(1, sizeof(*buffer));
    if (!buffer) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    buffer->device = (gx2vk_device_t *)device;
    buffer->size = pCreateInfo->size;
    buffer->usage = pCreateInfo->usage;
    buffer->sharing_mode = pCreateInfo->sharingMode;
    buffer->gx2r_flags = gx2vk_buffer_usage_to_gx2r_flags(pCreateInfo->usage);
    buffer->needs_uniform_word_swap =
        (pCreateInfo->usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) != 0 &&
        (pCreateInfo->usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) == 0;
    *pBuffer = (VkBuffer)buffer;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice device,
                                           VkBuffer buffer,
                                           const VkAllocationCallbacks *pAllocator) {
    gx2vk_buffer_t *gx2vk_buffer = (gx2vk_buffer_t *)buffer;
    (void)device;
    (void)pAllocator;

    if (!gx2vk_buffer) {
        return;
    }
    if (gx2vk_buffer->bound_memory) {
        gx2vk_buffer->bound_memory->bound_buffer = NULL;
        gx2vk_buffer->bound_memory = NULL;
    }
    if (gx2vk_buffer->gx2r_created) {
        GX2RDestroyBufferEx(&gx2vk_buffer->gx2r_buffer, (GX2RResourceFlags)0);
    }
    free(gx2vk_buffer);
}

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice device,
                                                         VkBuffer buffer,
                                                         VkMemoryRequirements *pMemoryRequirements) {
    gx2vk_buffer_t *gx2vk_buffer = (gx2vk_buffer_t *)buffer;
    (void)device;

    if (!gx2vk_buffer || !pMemoryRequirements) {
        return;
    }
    gx2vk_fill_buffer_memory_requirements(gx2vk_buffer->size,
                                          gx2vk_buffer->usage,
                                          pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements2(VkDevice device,
                                                          const VkBufferMemoryRequirementsInfo2 *pInfo,
                                                          VkMemoryRequirements2 *pMemoryRequirements) {
    gx2vk_buffer_t *gx2vk_buffer = NULL;
    (void)device;

    if (!pInfo || !pMemoryRequirements) {
        return;
    }
    gx2vk_buffer = (gx2vk_buffer_t *)pInfo->buffer;
    gx2vk_fill_buffer_memory_requirements2(gx2vk_buffer, pMemoryRequirements);
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice device,
                                                const VkMemoryAllocateInfo *pAllocateInfo,
                                                const VkAllocationCallbacks *pAllocator,
                                                VkDeviceMemory *pMemory) {
    gx2vk_memory_t *memory = NULL;
    (void)pAllocator;

    if (!device || !pAllocateInfo || !pMemory || pAllocateInfo->sType != VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (pAllocateInfo->memoryTypeIndex != 0) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    memory = (gx2vk_memory_t *)calloc(1, sizeof(*memory));
    if (!memory) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memory->host_storage = (uint8_t *)calloc(1, (size_t)pAllocateInfo->allocationSize);
    if (!memory->host_storage && pAllocateInfo->allocationSize > 0) {
        free(memory);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    memory->device = (gx2vk_device_t *)device;
    memory->allocation_size = pAllocateInfo->allocationSize;
    memory->memory_type_index = pAllocateInfo->memoryTypeIndex;
    *pMemory = (VkDeviceMemory)memory;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice device,
                                        VkDeviceMemory memory,
                                        const VkAllocationCallbacks *pAllocator) {
    gx2vk_memory_t *gx2vk_memory = (gx2vk_memory_t *)memory;
    (void)device;
    (void)pAllocator;

    if (!gx2vk_memory) {
        return;
    }
    if (gx2vk_memory->bound_buffer) {
        gx2vk_memory->bound_buffer->bound_memory = NULL;
        if (gx2vk_memory->bound_buffer->gx2r_created) {
            GX2RDestroyBufferEx(&gx2vk_memory->bound_buffer->gx2r_buffer, (GX2RResourceFlags)0);
            memset(&gx2vk_memory->bound_buffer->gx2r_buffer, 0, sizeof(gx2vk_memory->bound_buffer->gx2r_buffer));
            gx2vk_memory->bound_buffer->gx2r_created = false;
        }
        gx2vk_memory->bound_buffer = NULL;
    }
    free(gx2vk_memory->host_storage);
    free(gx2vk_memory);
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice device,
                                                  VkBuffer buffer,
                                                  VkDeviceMemory memory,
                                                  VkDeviceSize memoryOffset) {
    (void)device;
    return gx2vk_bind_memory_to_buffer((gx2vk_buffer_t *)buffer,
                                       (gx2vk_memory_t *)memory,
                                       memoryOffset);
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory2(VkDevice device,
                                                   uint32_t bindInfoCount,
                                                   const VkBindBufferMemoryInfo *pBindInfos) {
    uint32_t i;
    (void)device;

    if (bindInfoCount > 0 && !pBindInfos) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    for (i = 0; i < bindInfoCount; ++i) {
        VkResult result = gx2vk_bind_memory_to_buffer((gx2vk_buffer_t *)pBindInfos[i].buffer,
                                                      (gx2vk_memory_t *)pBindInfos[i].memory,
                                                      pBindInfos[i].memoryOffset);
        if (result != VK_SUCCESS) {
            return result;
        }
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice device,
                                           VkDeviceMemory memory,
                                           VkDeviceSize offset,
                                           VkDeviceSize size,
                                           VkMemoryMapFlags flags,
                                           void **ppData) {
    gx2vk_memory_t *gx2vk_memory = (gx2vk_memory_t *)memory;
    (void)device;
    (void)flags;

    if (!gx2vk_memory || !ppData) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (gx2vk_memory->mapped) {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    if (offset > gx2vk_memory->allocation_size) {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    if (size == VK_WHOLE_SIZE) {
        size = gx2vk_memory->allocation_size - offset;
    }
    if (size > (gx2vk_memory->allocation_size - offset)) {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    gx2vk_memory->mapped = true;
    gx2vk_memory->mapped_offset = offset;
    gx2vk_memory->mapped_size = size;
    *ppData = gx2vk_memory->host_storage + offset;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice device,
                                         VkDeviceMemory memory) {
    gx2vk_memory_t *gx2vk_memory = (gx2vk_memory_t *)memory;
    (void)device;

    if (!gx2vk_memory || !gx2vk_memory->mapped) {
        return;
    }
    if (gx2vk_memory->bound_buffer && gx2vk_memory->mapped_size > 0) {
        gx2vk_copy_range_to_gpu(gx2vk_memory,
                                gx2vk_memory->mapped_offset,
                                gx2vk_memory->mapped_size);
    }

    gx2vk_memory->mapped = false;
    gx2vk_memory->mapped_offset = 0;
    gx2vk_memory->mapped_size = 0;
}

VKAPI_ATTR VkResult VKAPI_CALL vkFlushMappedMemoryRanges(VkDevice device,
                                                         uint32_t memoryRangeCount,
                                                         const VkMappedMemoryRange *pMemoryRanges) {
    uint32_t i;
    (void)device;

    if (memoryRangeCount > 0 && !pMemoryRanges) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    for (i = 0; i < memoryRangeCount; ++i) {
        gx2vk_memory_t *gx2vk_memory = (gx2vk_memory_t *)pMemoryRanges[i].memory;
        VkDeviceSize size = pMemoryRanges[i].size;
        if (!gx2vk_memory) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        if (pMemoryRanges[i].offset > gx2vk_memory->allocation_size) {
            return VK_ERROR_MEMORY_MAP_FAILED;
        }
        if (size == VK_WHOLE_SIZE) {
            size = gx2vk_memory->allocation_size - pMemoryRanges[i].offset;
        }
        if (size > (gx2vk_memory->allocation_size - pMemoryRanges[i].offset)) {
            return VK_ERROR_MEMORY_MAP_FAILED;
        }
        if (gx2vk_memory->bound_buffer && size > 0) {
            gx2vk_copy_range_to_gpu(gx2vk_memory, pMemoryRanges[i].offset, size);
        }
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(VkDevice device,
                                                              uint32_t memoryRangeCount,
                                                              const VkMappedMemoryRange *pMemoryRanges) {
    (void)device;
    (void)memoryRangeCount;
    (void)pMemoryRanges;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceBufferMemoryRequirements(VkDevice device,
                                                               const VkDeviceBufferMemoryRequirements *pInfo,
                                                               VkMemoryRequirements2 *pMemoryRequirements) {
    VkDeviceSize alignment = 16;
    uint64_t size = 0;
    (void)device;

    if (!pInfo || !pInfo->pCreateInfo || !pMemoryRequirements) {
        return;
    }

    size = pInfo->pCreateInfo->size;
    if (pInfo->pCreateInfo->usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) {
        alignment = 256;
    } else if (pInfo->pCreateInfo->usage & (VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
        alignment = 64;
    } else if (pInfo->pCreateInfo->usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) {
        alignment = 256;
    }

    gx2vk_fill_memory_requirements2(pMemoryRequirements, size, alignment);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageMemoryRequirements(VkDevice device,
                                                              const VkDeviceImageMemoryRequirements *pInfo,
                                                              VkMemoryRequirements2 *pMemoryRequirements) {
    uint64_t size = 0;
    (void)device;

    if (!pInfo || !pInfo->pCreateInfo || !pMemoryRequirements) {
        return;
    }

    size = gx2vk_compute_image_size(pInfo->pCreateInfo);
    gx2vk_fill_memory_requirements2(pMemoryRequirements, size, 64);
}

}
