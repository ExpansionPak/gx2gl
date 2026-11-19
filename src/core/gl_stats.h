#ifndef GX2GL_GL_STATS_H
#define GX2GL_GL_STATS_H

#include "gx2gl/stats.h"

#ifdef __WIIU__
#include <coreinit/time.h>
#define GL_STATS_NOW() OSGetTime()
#define GL_STATS_US(t) ((uint32_t)OSTicksToMicroseconds(t))
typedef OSTime gl_stats_time_t;
#else
#define GL_STATS_NOW() 0
#define GL_STATS_US(t) 0u
typedef long long gl_stats_time_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif
extern GX2GL_FrameStats g_gl_frame_stats;
void gl_stats_end_frame(uint32_t finish_us);
#ifdef __cplusplus
}
#endif

/* Runs stmt, counting it in g_gl_frame_stats.count and its time in .us. */
#define GL_STATS_TIMED(count, us, stmt)                                     \
    do {                                                                    \
        gl_stats_time_t gl_stats_start_ = GL_STATS_NOW();                   \
        stmt;                                                               \
        g_gl_frame_stats.count++;                                           \
        g_gl_frame_stats.us += GL_STATS_US(GL_STATS_NOW() - gl_stats_start_); \
    } while (0)

#endif
