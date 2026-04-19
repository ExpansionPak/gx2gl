#include "gx2gl_cafeglsl.h"

extern "C" {
#include <coreinit/dynload.h>
}

typedef void (*gx2gl_cafeglsl_init_fn)(void);
typedef GX2VertexShader *(*gx2gl_cafeglsl_compile_vs_fn)(const char *, char *, int, gx2gl_cafeglsl_flag_t);
typedef GX2PixelShader *(*gx2gl_cafeglsl_compile_ps_fn)(const char *, char *, int, gx2gl_cafeglsl_flag_t);
typedef void (*gx2gl_cafeglsl_free_vs_fn)(GX2VertexShader *);
typedef void (*gx2gl_cafeglsl_free_ps_fn)(GX2PixelShader *);
typedef void (*gx2gl_cafeglsl_destroy_fn)(void);

typedef struct {
    OSDynLoad_Module module;
    gx2gl_cafeglsl_init_fn init;
    gx2gl_cafeglsl_compile_vs_fn compile_vertex_shader;
    gx2gl_cafeglsl_compile_ps_fn compile_pixel_shader;
    gx2gl_cafeglsl_free_vs_fn free_vertex_shader;
    gx2gl_cafeglsl_free_ps_fn free_pixel_shader;
    gx2gl_cafeglsl_destroy_fn destroy;
    bool attempted_init;
    bool available;
} GX2GLCafeGLSLState;

static GX2GLCafeGLSLState g_cafeglsl_state = {0};

static bool gx2gl_cafeglsl_load_exports(OSDynLoad_Module module) {
    if (OSDynLoad_FindExport(module, OS_DYNLOAD_EXPORT_FUNC, "InitGLSLCompiler",
                             (void **)&g_cafeglsl_state.init) != OS_DYNLOAD_OK) {
        return false;
    }
    if (OSDynLoad_FindExport(module, OS_DYNLOAD_EXPORT_FUNC, "CompileVertexShader",
                             (void **)&g_cafeglsl_state.compile_vertex_shader) != OS_DYNLOAD_OK) {
        return false;
    }
    if (OSDynLoad_FindExport(module, OS_DYNLOAD_EXPORT_FUNC, "CompilePixelShader",
                             (void **)&g_cafeglsl_state.compile_pixel_shader) != OS_DYNLOAD_OK) {
        return false;
    }
    if (OSDynLoad_FindExport(module, OS_DYNLOAD_EXPORT_FUNC, "FreeVertexShader",
                             (void **)&g_cafeglsl_state.free_vertex_shader) != OS_DYNLOAD_OK) {
        return false;
    }
    if (OSDynLoad_FindExport(module, OS_DYNLOAD_EXPORT_FUNC, "FreePixelShader",
                             (void **)&g_cafeglsl_state.free_pixel_shader) != OS_DYNLOAD_OK) {
        return false;
    }
    if (OSDynLoad_FindExport(module, OS_DYNLOAD_EXPORT_FUNC, "DestroyGLSLCompiler",
                             (void **)&g_cafeglsl_state.destroy) != OS_DYNLOAD_OK) {
        return false;
    }

    return true;
}

bool gx2gl_cafeglsl_init(void) {
    static const char *const candidates[] = {
        "glslcompiler",
        "glslcompiler.rpl",
        "/vol/content/glslcompiler.rpl",
        "./glslcompiler.rpl",
        "~/wiiu/libs/glslcompiler.rpl",
    };

    if (g_cafeglsl_state.available) {
        return true;
    }
    if (g_cafeglsl_state.attempted_init) {
        return false;
    }

    g_cafeglsl_state.attempted_init = true;

    for (unsigned i = 0; i < (sizeof(candidates) / sizeof(candidates[0])); ++i) {
        if (OSDynLoad_Acquire(candidates[i], &g_cafeglsl_state.module) == OS_DYNLOAD_OK) {
            if (gx2gl_cafeglsl_load_exports(g_cafeglsl_state.module)) {
                g_cafeglsl_state.init();
                g_cafeglsl_state.available = true;
                return true;
            }

            OSDynLoad_Release(g_cafeglsl_state.module);
            g_cafeglsl_state.module = NULL;
        }
    }

    return false;
}

void gx2gl_cafeglsl_shutdown(void) {
    if (!g_cafeglsl_state.available) {
        return;
    }

    g_cafeglsl_state.destroy();
    OSDynLoad_Release(g_cafeglsl_state.module);
    g_cafeglsl_state = (GX2GLCafeGLSLState){0};
}

bool gx2gl_cafeglsl_is_available(void) {
    return g_cafeglsl_state.available;
}

GX2VertexShader *gx2gl_cafeglsl_compile_vertex_shader(const char *shader_source,
                                                      char *info_log_out,
                                                      int info_log_max_length,
                                                      gx2gl_cafeglsl_flag_t flags) {
    if (!gx2gl_cafeglsl_init()) {
        return NULL;
    }

    return g_cafeglsl_state.compile_vertex_shader(shader_source, info_log_out,
                                                  info_log_max_length, flags);
}

GX2PixelShader *gx2gl_cafeglsl_compile_pixel_shader(const char *shader_source,
                                                    char *info_log_out,
                                                    int info_log_max_length,
                                                    gx2gl_cafeglsl_flag_t flags) {
    if (!gx2gl_cafeglsl_init()) {
        return NULL;
    }

    return g_cafeglsl_state.compile_pixel_shader(shader_source, info_log_out,
                                                 info_log_max_length, flags);
}

void gx2gl_cafeglsl_free_vertex_shader(GX2VertexShader *shader) {
    if (g_cafeglsl_state.available && shader) {
        g_cafeglsl_state.free_vertex_shader(shader);
    }
}

void gx2gl_cafeglsl_free_pixel_shader(GX2PixelShader *shader) {
    if (g_cafeglsl_state.available && shader) {
        g_cafeglsl_state.free_pixel_shader(shader);
    }
}
