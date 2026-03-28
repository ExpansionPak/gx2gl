#include <gx2/common.h>
#include <gx2/state.h>
#include <whb/proc.h>
#include "mem/gl_mem.h"

int main(int argc, char **argv) {
    /* Initialize Wii U Homebrew help procedures */
    WHBProcInit();

    /* Initialize Custom Memory Allocators */
    gl_mem_init();

    /* Initialize GX2 with default attributes */
    GX2Init(NULL);

    /* Main Loop */
    while (WHBProcIsRunning()) {
        /*
         *   PHASE 9 - Draw calls will go here
         */
    }

    /* Shutdown Procedure */
    GX2Shutdown();
    gl_mem_shutdown();
    WHBProcShutdown();

    return 0;
}
