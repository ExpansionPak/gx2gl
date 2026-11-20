#ifndef GX2GL_STATS_H
#define GX2GL_STATS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What a frame spent inside gx2gl: how many calls of each kind, and the CPU
 * time they took in microseconds. finish_us is GX2GL_Present() waiting for
 * the GPU to finish the frame, vsync_us waiting for it to be on screen
 * before the next frame starts.
 */
typedef struct GX2GL_FrameStats
{
    uint32_t draws, draw_us;
    uint32_t buffer_uploads, buffer_bytes, buffer_us;
    uint32_t uniform_calls, uniform_us;
    uint32_t texture_binds, texture_bind_us;
    uint32_t texture_uploads, texture_upload_us;
    uint32_t mipmap_generations, mipmap_us;
    uint32_t shader_compiles, program_links, shader_us;
    uint32_t program_switches, program_switch_us;
    uint32_t clears, clear_us;
    uint32_t other_calls, other_us; /* every other GL call */
    uint32_t finish_us;
    uint32_t vsync_us; /* GX2GL_Present() waiting for the previous frame to be shown */
} GX2GL_FrameStats;

/* The counts of the last frame GX2GL_Present() finished. */
void GX2GL_GetFrameStats(GX2GL_FrameStats *stats);

#ifdef __cplusplus
}
#endif

#endif
