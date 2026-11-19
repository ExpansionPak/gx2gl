#include "gl_stats.h"

#include <string.h>

extern "C" {

GX2GL_FrameStats g_gl_frame_stats;
static GX2GL_FrameStats g_gl_last_frame_stats;

void gl_stats_end_frame(uint32_t finish_us)
{
    g_gl_frame_stats.finish_us = finish_us;
    g_gl_last_frame_stats = g_gl_frame_stats;
    memset(&g_gl_frame_stats, 0, sizeof(g_gl_frame_stats));
}

void gl_stats_set_vsync(uint32_t vsync_us)
{
    g_gl_last_frame_stats.vsync_us = vsync_us;
}

void GX2GL_GetFrameStats(GX2GL_FrameStats *stats)
{
    if (stats) *stats = g_gl_last_frame_stats;
}

}
