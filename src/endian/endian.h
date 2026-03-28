#ifndef GL33_ENDIAN_H
#define GL33_ENDIAN_H

#include <stdint.h>

// Wii U CPU is Big-Endian.
// Wii U GPU is Little-Endian.
// I don't know why they did this.

#define swap16(v) __builtin_bswap16(v)
#define swap32(v) __builtin_bswap32(v)
#define swap64(v) __builtin_bswap64(v)

/* CPU (BE) -> GPU (LE) */
#define CPU_TO_GPU_16(v) swap16(v)
#define CPU_TO_GPU_32(v) swap32(v)
#define CPU_TO_GPU_64(v) swap64(v)

/* GPU (LE) -> CPU (BE) */
#define GPU_TO_CPU_16(v) swap16(v)
#define GPU_TO_CPU_32(v) swap32(v)
#define GPU_TO_CPU_64(v) swap64(v)

#endif /* GL33_ENDIAN_H */
