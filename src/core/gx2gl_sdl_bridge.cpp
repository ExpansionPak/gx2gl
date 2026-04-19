#include "gx2gl/sdl_bridge.h"

#include "core/gl_context.h"
#include "mem/gl_mem.h"
#include "whb/gfx.h"
#include "whb/proc.h"
#include "gx2gl/proc.h"

#include <proc_ui/procui.h>

typedef struct
{
    int library_loaded;
    int context_refs;
    int proc_owned;
    int gfx_owned;
    int mem_ready;
    int render_active;
    int render_tv_active;
    int render_drc_active;
    int swap_interval;
    gl_context_t *context;
} GX2GLSDLState;

static GX2GLSDLState g_gx2gl_sdl_state = {0};

static int gx2glEnsureGraphicsReady(void)
{
    if (!ProcUIIsRunning()) {
        WHBProcInit();
        g_gx2gl_sdl_state.proc_owned = 1;
    }

    if (!WHBGfxGetTVColourBuffer()) {
        if (!WHBGfxInit()) {
            return -1;
        }
        g_gx2gl_sdl_state.gfx_owned = 1;
    }

    if (!g_gx2gl_sdl_state.mem_ready) {
        gl_mem_init();
        g_gx2gl_sdl_state.mem_ready = 1;
    }

    return 0;
}

static void gx2glBeginFrameIfNeeded(void)
{
    if (g_gx2gl_sdl_state.render_active) {
        return;
    }

    WHBGfxBeginRender();
    if (WHBGfxGetTVColourBuffer()) {
        WHBGfxBeginRenderTV();
        g_gx2gl_sdl_state.render_tv_active = 1;
    }
    if (WHBGfxGetDRCColourBuffer()) {
        WHBGfxBeginRenderDRC();
        g_gx2gl_sdl_state.render_drc_active = 1;
    }
    g_gx2gl_sdl_state.render_active = 1;
}

static void gx2glEndFrameIfNeeded(void)
{
    if (!g_gx2gl_sdl_state.render_active) {
        return;
    }

    if (g_gx2gl_sdl_state.render_drc_active) {
        WHBGfxFinishRenderDRC();
        g_gx2gl_sdl_state.render_drc_active = 0;
    }
    if (g_gx2gl_sdl_state.render_tv_active) {
        WHBGfxFinishRenderTV();
        g_gx2gl_sdl_state.render_tv_active = 0;
    }
    WHBGfxFinishRender();
    g_gx2gl_sdl_state.render_active = 0;
}

int GX2GL_LoadLibrary(const char *path)
{
    (void) path;

    if (gx2glEnsureGraphicsReady() != 0) {
        return -1;
    }

    g_gx2gl_sdl_state.library_loaded = 1;
    return 0;
}

void GX2GL_UnloadLibrary(void)
{
    gx2glEndFrameIfNeeded();

    if (g_gx2gl_sdl_state.context_refs > 0) {
        return;
    }

    g_gx2gl_sdl_state.library_loaded = 0;

    if (g_gx2gl_sdl_state.mem_ready) {
        gl_mem_shutdown();
        g_gx2gl_sdl_state.mem_ready = 0;
    }

    if (g_gx2gl_sdl_state.gfx_owned) {
        WHBGfxShutdown();
        g_gx2gl_sdl_state.gfx_owned = 0;
    }

    if (g_gx2gl_sdl_state.proc_owned) {
        WHBProcShutdown();
        g_gx2gl_sdl_state.proc_owned = 0;
    }
}

void* GX2GL_GetSDLProcAddress(const char *proc)
{
    return GX2GL_GetProcAddress(proc);
}

GX2GL_Context GX2GL_CreateContext(void)
{
    if (GX2GL_LoadLibrary(NULL) != 0) {
        return NULL;
    }

    if (!g_gx2gl_sdl_state.context) {
        g_gx2gl_sdl_state.context = gl_context_create();
        if (!g_gx2gl_sdl_state.context) {
            return NULL;
        }
    }

    g_gx2gl_sdl_state.context_refs += 1;
    g_gl_context = g_gx2gl_sdl_state.context;
    gx2glBeginFrameIfNeeded();
    return (GX2GL_Context) g_gx2gl_sdl_state.context;
}

int GX2GL_MakeCurrent(GX2GL_Context context)
{
    if (context && (gl_context_t*) context != g_gx2gl_sdl_state.context) {
        return -1;
    }

    g_gl_context = (gl_context_t*) context;

    if (g_gl_context) {
        gx2glBeginFrameIfNeeded();
    } else {
        gx2glEndFrameIfNeeded();
    }

    return 0;
}

void GX2GL_DeleteContext(GX2GL_Context context)
{
    if ((gl_context_t*) context == g_gx2gl_sdl_state.context) {
        if (g_gx2gl_sdl_state.context_refs > 0) {
            g_gx2gl_sdl_state.context_refs -= 1;
        }

        if (g_gl_context == g_gx2gl_sdl_state.context) {
            g_gl_context = NULL;
        }

        if (g_gx2gl_sdl_state.context_refs == 0) {
            gx2glEndFrameIfNeeded();
            g_gx2gl_sdl_state.context = NULL;
            g_gl_context = NULL;
        }
    }

    if (g_gx2gl_sdl_state.context_refs == 0) {
        GX2GL_UnloadLibrary();
    }
}

int GX2GL_SetSwapInterval(int interval)
{
    g_gx2gl_sdl_state.swap_interval = interval;
    return 0;
}

int GX2GL_GetSwapInterval(void)
{
    return g_gx2gl_sdl_state.swap_interval;
}

int GX2GL_SwapWindow(void)
{
    if (!g_gl_context) {
        return -1;
    }

    _gl_Flush();
    gx2glEndFrameIfNeeded();
    gx2glBeginFrameIfNeeded();
    return 0;
}
