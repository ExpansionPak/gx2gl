#include <coreinit/debug.h>
#include <whb/gfx.h>
#include <whb/proc.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "core/gl_context.h"
#include "mem/gl_mem.h"
#include "piglit_lite.h"

namespace {

static const char *kResultsPath = "/vol/external01/gx2gl_results.txt";
static const char *kProgressPath = "/vol/external01/gx2gl_progress.txt";
static const char *kCaseLogPath = "/vol/external01/gx2gl_piglit.txt";
static const char *kDonePath = "/vol/external01/gx2gl_done.flag";

struct RunnerState {
    PiglitRunStats stats;
    FILE *case_log;
};

static void report(const char *format, ...) {
    char buffer[1024];
    va_list args;

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    OSReport("%s", buffer);
}

static const char *result_name(PiglitResult result) {
    switch (result) {
        case PIGLIT_RESULT_PASS:
            return "pass";
        case PIGLIT_RESULT_FAIL:
            return "fail";
        case PIGLIT_RESULT_SKIP:
            return "skip";
    }
    return "fail";
}

static void write_summary(const char *path, const char *status,
                          const PiglitRunStats &stats) {
    FILE *file = fopen(path, "wb");
    if (!file) {
        return;
    }

    fprintf(file, "status=%s\n", status);
    fprintf(file, "suite=piglit\n");
    fprintf(file, "total=%u\n", stats.total);
    fprintf(file, "pass=%u\n", stats.pass);
    fprintf(file, "fail=%u\n", stats.fail);
    fprintf(file, "skip=%u\n", stats.skip);
    fclose(file);
}

static void record_result(const char *name, PiglitResult result,
                          const char *detail, void *user_data) {
    RunnerState *state = static_cast<RunnerState *>(user_data);

    ++state->stats.total;
    switch (result) {
        case PIGLIT_RESULT_PASS:
            ++state->stats.pass;
            break;
        case PIGLIT_RESULT_FAIL:
            ++state->stats.fail;
            break;
        case PIGLIT_RESULT_SKIP:
            ++state->stats.skip;
            break;
    }

    if (state->case_log) {
        fprintf(state->case_log, "result=%s\tname=%s\tdetail=%s\n",
                result_name(result), name, detail ? detail : "");
        fflush(state->case_log);
    }
    write_summary(kProgressPath, "running", state->stats);
}

static void write_done_flag(void) {
    FILE *file = fopen(kDonePath, "wb");
    if (file) {
        fputs("done\n", file);
        fclose(file);
    }
}

} // namespace

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    remove(kResultsPath);
    remove(kProgressPath);
    remove(kCaseLogPath);
    remove(kDonePath);

    WHBProcInit();
    WHBGfxInit();
    gl_mem_init();
    g_gl_context = gl_context_create();

    if (!g_gl_context) {
        PiglitRunStats empty = {};
        report("PIGLIT: context creation failed\n");
        write_summary(kResultsPath, "failed", empty);
        write_done_flag();
        gl_mem_shutdown();
        WHBGfxShutdown();
        WHBProcShutdown();
        return 1;
    }

    report("PIGLIT: gx2gl OpenGL 3.3 runner\n");
    RunnerState state = {};
    state.case_log = fopen(kCaseLogPath, "wb");
    write_summary(kProgressPath, "running", state.stats);

    const PiglitRunStats returned =
        run_piglit_tests(report, record_result, &state);
    if (returned.total != state.stats.total ||
        returned.pass != state.stats.pass ||
        returned.fail != state.stats.fail ||
        returned.skip != state.stats.skip) {
        report("PIGLIT: internal result accounting mismatch\n");
        ++state.stats.fail;
        ++state.stats.total;
    }

    if (state.case_log) {
        fclose(state.case_log);
    }
    write_summary(kResultsPath,
                  state.stats.fail == 0 ? "complete" : "failed",
                  state.stats);
    write_done_flag();

    report("PIGLIT: total=%u pass=%u fail=%u skip=%u\n",
           state.stats.total, state.stats.pass, state.stats.fail,
           state.stats.skip);

    gl_context_destroy(g_gl_context);
    g_gl_context = NULL;
    gl_mem_shutdown();
    WHBGfxShutdown();
    WHBProcShutdown();
    return state.stats.fail == 0 ? 0 : 1;
}
