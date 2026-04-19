#ifndef GX2GL_SDL_BRIDGE_H
#define GX2GL_SDL_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void* GX2GL_Context;

int GX2GL_LoadLibrary(const char *path);
void GX2GL_UnloadLibrary(void);
void* GX2GL_GetSDLProcAddress(const char *proc);

GX2GL_Context GX2GL_CreateContext(void);
int GX2GL_MakeCurrent(GX2GL_Context context);
void GX2GL_DeleteContext(GX2GL_Context context);

int GX2GL_SetSwapInterval(int interval);
int GX2GL_GetSwapInterval(void);
int GX2GL_SwapWindow(void);

#ifdef __cplusplus
}
#endif

#endif
