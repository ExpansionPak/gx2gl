#include <gx2/common.h>
#include <gx2/state.h>
#include <whb/proc.h>
#include "mem/gl_mem.h"

int main(int argc, char **argv) {
    // Start process hooks
    WHBProcInit();

    // Init heap pools
    gl_mem_init();

    // Start GX2 core
    GX2Init(NULL);

    // Wait for exit
    while (WHBProcIsRunning()) {
        // Pump system loop
    }

    // Shut things down
    GX2Shutdown();
    gl_mem_shutdown();
    WHBProcShutdown();

    return 0;
}
