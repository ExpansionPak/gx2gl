#include <coreinit/debug.h>
#include <whb/proc.h>
#include <whb/gfx.h>
#include <coreinit/memheap.h>
#include <coreinit/memexpheap.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif
#include <gx2/enum.h>
#include <gx2/draw.h>
#include <gx2/event.h>
#include <gx2/surface.h>
#include <gx2/registers.h>
#ifdef __cplusplus
}
#endif

#include "gl/gl.h"
#include "core/gl_context.h"
#include "core/gl_texture.h"
#include "mem/gl_mem.h"

static int g_pass_count = 0;
static int g_negative_pass_count = 0;
static int g_fail_count = 0;
static char g_failure_lines[12][128];
static uint32_t g_failure_line_count = 0;
typedef struct {
    int pass_count;
    int negative_pass_count;
    int fail_count;
} phase_stats_t;
enum {
    DIAG_PHASE_LIMITS = 0,
    DIAG_PHASE_LEGACY,
    DIAG_PHASE_BUFFERS,
    DIAG_PHASE_TEXTURES,
    DIAG_PHASE_SHADERS,
    DIAG_PHASE_VERTEX_ARRAYS,
    DIAG_PHASE_FRAMEBUFFERS,
    DIAG_PHASE_DRAW,
    DIAG_PHASE_DELETIONS,
    DIAG_PHASE_COUNT
};
static phase_stats_t g_phase_stats[DIAG_PHASE_COUNT];
static int g_active_phase = -1;

static int detect_phase_index(const char *line) {
    if (!line) {
        return -1;
    }
    if (strncmp(line, "-> Testing Limits & Capabilities", 32) == 0) {
        return DIAG_PHASE_LIMITS;
    }
    if (strncmp(line, "-> Testing Legacy State & Clears", 32) == 0) {
        return DIAG_PHASE_LEGACY;
    }
    if (strncmp(line, "-> Testing Buffer Objects", 25) == 0) {
        return DIAG_PHASE_BUFFERS;
    }
    if (strncmp(line, "-> Testing Texture Objects", 26) == 0) {
        return DIAG_PHASE_TEXTURES;
    }
    if (strncmp(line, "-> Testing Shaders & Programs", 29) == 0) {
        return DIAG_PHASE_SHADERS;
    }
    if (strncmp(line, "-> Testing Vertex Arrays", 24) == 0) {
        return DIAG_PHASE_VERTEX_ARRAYS;
    }
    if (strncmp(line, "-> Testing Framebuffers", 24) == 0) {
        return DIAG_PHASE_FRAMEBUFFERS;
    }
    if (strncmp(line, "-> Testing Draw Calls & Synchronization", 39) == 0) {
        return DIAG_PHASE_DRAW;
    }
    if (strncmp(line, "-> Testing Deletions", 20) == 0) {
        return DIAG_PHASE_DELETIONS;
    }
    return -1;
}

static void track_report_line(const char *line) {
    int phase_index;

    if (!line) {
        return;
    }
    phase_index = detect_phase_index(line);
    if (phase_index >= 0) {
        g_active_phase = phase_index;
        return;
    }

    if (strncmp(line, "[PASS]", 6) == 0) {
        ++g_pass_count;
        if (g_active_phase >= 0 && g_active_phase < DIAG_PHASE_COUNT) {
            ++g_phase_stats[g_active_phase].pass_count;
        }
        return;
    }
    if (strncmp(line, "   [PASS-NEGATIVE]", 18) == 0) {
        ++g_negative_pass_count;
        if (g_active_phase >= 0 && g_active_phase < DIAG_PHASE_COUNT) {
            ++g_phase_stats[g_active_phase].negative_pass_count;
        }
        return;
    }
    if (strncmp(line, "[FAIL]", 6) != 0 &&
        strncmp(line, "   [FAIL-NEGATIVE]", 18) != 0 &&
        strncmp(line, "[CRITICAL FAIL]", 15) != 0) {
        return;
    }

    ++g_fail_count;
    if (g_active_phase >= 0 && g_active_phase < DIAG_PHASE_COUNT) {
        ++g_phase_stats[g_active_phase].fail_count;
    }
    if (g_failure_line_count < (sizeof(g_failure_lines) / sizeof(g_failure_lines[0]))) {
        snprintf(g_failure_lines[g_failure_line_count],
                 sizeof(g_failure_lines[g_failure_line_count]), "%s", line);
        size_t length = strlen(g_failure_lines[g_failure_line_count]);
        while (length > 0 &&
               (g_failure_lines[g_failure_line_count][length - 1] == '\n' ||
                g_failure_lines[g_failure_line_count][length - 1] == '\r')) {
            g_failure_lines[g_failure_line_count][--length] = '\0';
        }
        ++g_failure_line_count;
    }
}

static void codex_report(const char *fmt, ...) {
    char buffer[1024];
    va_list args;

    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    track_report_line(buffer);
    OSConsoleWrite(buffer, (uint32_t)strlen(buffer));
}

#define OSReport codex_report

static void draw_diag_rect(GLsizei screen_width, GLsizei screen_height, int x,
                           int y, int width, int height, float r, float g,
                           float b, float a) {
    int clipped_x = x;
    int clipped_y = y;
    int clipped_width = width;
    int clipped_height = height;

    if (clipped_x < 0) {
        clipped_width += clipped_x;
        clipped_x = 0;
    }
    if (clipped_y < 0) {
        clipped_height += clipped_y;
        clipped_y = 0;
    }
    if (clipped_x + clipped_width > screen_width) {
        clipped_width = screen_width - clipped_x;
    }
    if (clipped_y + clipped_height > screen_height) {
        clipped_height = screen_height - clipped_y;
    }
    if (clipped_width <= 0 || clipped_height <= 0) {
        return;
    }

    glScissor(clipped_x, screen_height - (clipped_y + clipped_height),
              clipped_width, clipped_height);
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

typedef struct {
    char ch;
    uint8_t rows[7];
} diag_glyph_t;

static const diag_glyph_t g_diag_glyphs[] = {
    {' ', {0, 0, 0, 0, 0, 0, 0}},
    {'-', {0, 0, 0, 31, 0, 0, 0}},
    {'0', {14, 17, 19, 21, 25, 17, 14}},
    {'1', {4, 12, 4, 4, 4, 4, 14}},
    {'2', {14, 17, 1, 2, 4, 8, 31}},
    {'3', {30, 1, 1, 14, 1, 1, 30}},
    {'4', {2, 6, 10, 18, 31, 2, 2}},
    {'5', {31, 16, 16, 30, 1, 1, 30}},
    {'6', {14, 16, 16, 30, 17, 17, 14}},
    {'7', {31, 1, 2, 4, 8, 8, 8}},
    {'8', {14, 17, 17, 14, 17, 17, 14}},
    {'9', {14, 17, 17, 15, 1, 1, 14}},
    {'A', {14, 17, 17, 31, 17, 17, 17}},
    {'D', {30, 17, 17, 17, 17, 17, 30}},
    {'E', {31, 16, 16, 30, 16, 16, 31}},
    {'F', {31, 16, 16, 30, 16, 16, 16}},
    {'G', {14, 17, 16, 23, 17, 17, 14}},
    {'I', {31, 4, 4, 4, 4, 4, 31}},
    {'L', {16, 16, 16, 16, 16, 16, 31}},
    {'N', {17, 25, 21, 19, 17, 17, 17}},
    {'P', {30, 17, 17, 30, 16, 16, 16}},
    {'R', {30, 17, 17, 30, 20, 18, 17}},
    {'S', {15, 16, 16, 14, 1, 1, 30}},
    {'T', {31, 4, 4, 4, 4, 4, 4}},
    {'U', {17, 17, 17, 17, 17, 17, 14}},
};

static const uint8_t *diag_glyph_rows(char ch) {
    for (size_t i = 0; i < sizeof(g_diag_glyphs) / sizeof(g_diag_glyphs[0]); ++i) {
        if (g_diag_glyphs[i].ch == ch) {
            return g_diag_glyphs[i].rows;
        }
    }
    return g_diag_glyphs[0].rows;
}

static int diag_text_width(const char *text, int scale) {
    int width = 0;

    if (!text || scale <= 0) {
        return 0;
    }
    while (*text != '\0') {
        width += 6 * scale;
        ++text;
    }
    if (width > 0) {
        width -= scale;
    }
    return width;
}

static void draw_diag_text(GLsizei screen_width, GLsizei screen_height, int x,
                           int y, int scale, const char *text, float r,
                           float g, float b, float a) {
    int pen_x = x;

    if (!text || scale <= 0) {
        return;
    }

    while (*text != '\0') {
        const uint8_t *rows = diag_glyph_rows(*text);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if ((rows[row] & (1u << (4 - col))) == 0) {
                    continue;
                }
                draw_diag_rect(screen_width, screen_height,
                               pen_x + col * scale, y + row * scale, scale,
                               scale, r, g, b, a);
            }
        }
        pen_x += 6 * scale;
        ++text;
    }
}

static void draw_diag_text_shadow(GLsizei screen_width, GLsizei screen_height,
                                  int x, int y, int scale, const char *text,
                                  float r, float g, float b, float a) {
    draw_diag_text(screen_width, screen_height, x + scale / 2, y + scale / 2,
                   scale, text, 0.02f, 0.03f, 0.04f, a);
    draw_diag_text(screen_width, screen_height, x, y, scale, text, r, g, b, a);
}

static void draw_diag_card(GLsizei screen_width, GLsizei screen_height, int x,
                           int y, int width, int height, const char *label,
                           const char *value, float accent_r, float accent_g,
                           float accent_b) {
    int label_scale;
    int value_scale;
    int strip_height;
    int label_x;
    int label_y;
    int value_x;
    int value_y;

    if (width <= 0 || height <= 0) {
        return;
    }

    label_scale = height / 14;
    if (label_scale < 3) {
        label_scale = 3;
    }
    value_scale = height / 8;
    if (value_scale < 6) {
        value_scale = 6;
    }
    strip_height = height / 3;
    if (strip_height < label_scale * 8) {
        strip_height = label_scale * 8;
    }

    draw_diag_rect(screen_width, screen_height, x, y, width, height, 0.11f,
                   0.12f, 0.15f, 1.0f);
    draw_diag_rect(screen_width, screen_height, x + 2, y + 2, width - 4,
                   strip_height, accent_r, accent_g, accent_b, 1.0f);

    label_x = x + (width - diag_text_width(label, label_scale)) / 2;
    label_y = y + (strip_height - label_scale * 7) / 2;
    value_x = x + (width - diag_text_width(value, value_scale)) / 2;
    value_y = y + strip_height + (height - strip_height - value_scale * 7) / 2;

    draw_diag_text_shadow(screen_width, screen_height, label_x, label_y,
                          label_scale, label, 0.05f, 0.06f, 0.08f, 1.0f);
    draw_diag_text_shadow(screen_width, screen_height, value_x, value_y,
                          value_scale, value, 0.96f, 0.97f, 0.99f, 1.0f);
}

static void draw_diagnostic_screen(void) {
    static const char *phase_names[DIAG_PHASE_COUNT] = {
        "Limits",
        "Legacy",
        "Buffers",
        "Textures",
        "Shaders",
        "VAOs",
        "FBOs",
        "Draw",
        "Delete",
    };
    static const float accent_colors[DIAG_PHASE_COUNT][3] = {
        {0.32f, 0.73f, 0.55f},
        {0.90f, 0.72f, 0.30f},
        {0.35f, 0.65f, 0.92f},
        {0.80f, 0.56f, 0.30f},
        {0.76f, 0.40f, 0.90f},
        {0.30f, 0.78f, 0.88f},
        {0.88f, 0.52f, 0.62f},
        {0.55f, 0.78f, 0.35f},
        {0.72f, 0.72f, 0.72f},
    };
    GX2ColorBuffer *tv_color;
    GX2Texture *diag_gx2_texture = NULL;
    GLsizei screen_width;
    GLsizei screen_height;
    GLuint diag_texture = 0;
    GLuint diag_fbo = 0;
    GLboolean use_offscreen = GL_FALSE;
    char legend[256];

    if (!g_gl_context) {
        return;
    }
    tv_color = WHBGfxGetTVColourBuffer();
    if (!tv_color) {
        return;
    }
    screen_width = (GLsizei)tv_color->surface.width;
    screen_height = (GLsizei)tv_color->surface.height;

    glGenTextures(1, &diag_texture);
    if (diag_texture != 0) {
        glBindTexture(GL_TEXTURE_2D, diag_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, screen_width, screen_height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        if (glGetError() == GL_NO_ERROR) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glGenFramebuffers(1, &diag_fbo);
            if (diag_fbo != 0) {
                glBindFramebuffer(GL_FRAMEBUFFER, diag_fbo);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                       GL_TEXTURE_2D, diag_texture, 0);
                glDrawBuffer(GL_COLOR_ATTACHMENT0);
                glReadBuffer(GL_COLOR_ATTACHMENT0);
                if (glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
                    GL_FRAMEBUFFER_COMPLETE) {
                    diag_gx2_texture = gl_get_gx2_texture(diag_texture);
                    if (diag_gx2_texture) {
                        use_offscreen = GL_TRUE;
                    }
                }
            }
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    snprintf(legend, sizeof(legend),
             "Diagnostic tiles order: %s, %s, %s, %s, %s, %s, %s, %s, %s.\n",
             phase_names[0], phase_names[1], phase_names[2], phase_names[3],
             phase_names[4], phase_names[5], phase_names[6], phase_names[7],
             phase_names[8]);
    OSReport("%s", legend);

    while (WHBProcIsRunning()) {
        int margin = screen_width / 24;
        int gap = margin / 2;
        int header_height = screen_height / 4;
        int totals_height = screen_height / 16;
        int grid_top;
        int tile_width;
        int tile_height;
        int total_events;
        int bar_x;
        int bar_y;
        int bar_width;
        int status_height;
        int cards_x;
        int cards_y;
        int card_width;
        int card_height;
        int label_scale;
        int value_scale;
        char pass_text[16];
        char neg_text[16];
        char fail_text[16];
        const char *status_text;

        if (margin < 18) {
            margin = 18;
        }
        if (gap < 10) {
            gap = 10;
        }

        WHBGfxBeginRender();
        WHBGfxBeginRenderTV();

        glBindFramebuffer(GL_FRAMEBUFFER, use_offscreen ? diag_fbo : 0);
        glViewport(0, 0, screen_width, screen_height);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(g_fail_count == 0 ? 0.04f : 0.12f,
                     g_fail_count == 0 ? 0.08f : 0.04f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);

        status_height = header_height - gap;
        draw_diag_rect(screen_width, screen_height, margin, margin,
                       screen_width - margin * 2, status_height,
                       g_fail_count == 0 ? 0.10f : 0.55f,
                       g_fail_count == 0 ? 0.42f : 0.12f,
                       g_fail_count == 0 ? 0.18f : 0.12f, 1.0f);
        draw_diag_rect(screen_width, screen_height, margin + gap, margin + gap,
                       screen_width - (margin + gap) * 2, status_height / 3,
                       g_fail_count == 0 ? 0.32f : 0.88f,
                       g_fail_count == 0 ? 0.88f : 0.30f,
                       g_fail_count == 0 ? 0.44f : 0.30f, 1.0f);

        cards_x = margin + gap;
        cards_y = margin + gap * 2;
        card_width = (screen_width - margin * 2 - gap * 5) / 4;
        card_height = status_height - gap * 2;
        if (card_height < 72) {
            card_height = 72;
        }
        snprintf(pass_text, sizeof(pass_text), "%d", g_pass_count);
        snprintf(neg_text, sizeof(neg_text), "%d", g_negative_pass_count);
        snprintf(fail_text, sizeof(fail_text), "%d", g_fail_count);
        status_text = g_fail_count == 0 ? "GREEN" : "RED";
        draw_diag_card(screen_width, screen_height, cards_x, cards_y, card_width,
                       card_height, "STATUS", status_text,
                       g_fail_count == 0 ? 0.28f : 0.84f,
                       g_fail_count == 0 ? 0.74f : 0.28f,
                       g_fail_count == 0 ? 0.42f : 0.28f);
        draw_diag_card(screen_width, screen_height,
                       cards_x + (card_width + gap) * 1, cards_y, card_width,
                       card_height, "PASS", pass_text, 0.22f, 0.82f, 0.33f);
        draw_diag_card(screen_width, screen_height,
                       cards_x + (card_width + gap) * 2, cards_y, card_width,
                       card_height, "NEG", neg_text, 0.18f, 0.62f, 0.95f);
        draw_diag_card(screen_width, screen_height,
                       cards_x + (card_width + gap) * 3, cards_y, card_width,
                       card_height, "FAIL", fail_text, 0.92f, 0.22f, 0.22f);

        total_events = g_pass_count + g_negative_pass_count + g_fail_count;
        if (total_events <= 0) {
            total_events = 1;
        }
        bar_x = margin;
        bar_width = screen_width - margin * 2;
        bar_y = margin + header_height + gap;
        draw_diag_rect(screen_width, screen_height, bar_x, bar_y, bar_width,
                       totals_height, 0.16f, 0.18f, 0.21f, 1.0f);
        draw_diag_rect(screen_width, screen_height, bar_x + gap / 2,
                       bar_y + gap / 2,
                       ((bar_width - gap) * g_pass_count) / total_events,
                       totals_height - gap, 0.22f, 0.82f, 0.33f, 1.0f);
        draw_diag_rect(
            screen_width, screen_height,
            bar_x + gap / 2 +
                ((bar_width - gap) * g_pass_count) / total_events,
            bar_y + gap / 2,
            ((bar_width - gap) * g_negative_pass_count) / total_events,
            totals_height - gap, 0.18f, 0.62f, 0.95f, 1.0f);
        draw_diag_rect(
            screen_width, screen_height,
            bar_x + gap / 2 +
                ((bar_width - gap) * (g_pass_count + g_negative_pass_count)) /
                    total_events,
            bar_y + gap / 2,
            ((bar_width - gap) * g_fail_count) / total_events,
            totals_height - gap, 0.92f, 0.22f, 0.22f, 1.0f);

        bar_y += totals_height + gap;
        draw_diag_rect(screen_width, screen_height, bar_x, bar_y, bar_width,
                       totals_height, 0.16f, 0.18f, 0.21f, 1.0f);
        draw_diag_rect(screen_width, screen_height, bar_x + gap / 2,
                       bar_y + gap / 2,
                       ((bar_width - gap) * g_negative_pass_count) /
                           (g_negative_pass_count + g_fail_count + 1),
                       totals_height - gap, 0.16f, 0.55f, 0.88f, 1.0f);
        draw_diag_rect(
            screen_width, screen_height,
            bar_x + gap / 2 +
                ((bar_width - gap) * g_negative_pass_count) /
                    (g_negative_pass_count + g_fail_count + 1),
            bar_y + gap / 2,
            ((bar_width - gap) * g_fail_count) /
                (g_negative_pass_count + g_fail_count + 1),
            totals_height - gap, 0.88f, 0.18f, 0.18f, 1.0f);

        label_scale = totals_height / 5;
        value_scale = totals_height / 4;
        if (label_scale < 2) {
            label_scale = 2;
        }
        if (value_scale < 2) {
            value_scale = 2;
        }
        draw_diag_text(screen_width, screen_height, bar_x + gap,
                       margin + header_height + gap / 2, label_scale, "PASS",
                       0.88f, 0.92f, 0.90f, 1.0f);
        draw_diag_text(screen_width, screen_height, bar_x + gap,
                       margin + header_height + gap / 2 + label_scale * 8,
                       label_scale, "NEG", 0.88f, 0.92f, 0.96f, 1.0f);

        grid_top = bar_y + totals_height + gap * 2;
        tile_width = (screen_width - margin * 2 - gap * 2) / 3;
        tile_height = (screen_height - grid_top - margin - gap * 2) / 3;
        for (int i = 0; i < DIAG_PHASE_COUNT; ++i) {
            int tile_x = margin + (i % 3) * (tile_width + gap);
            int tile_y = grid_top + (i / 3) * (tile_height + gap);
            int pad = tile_width / 14;
            int phase_total = g_phase_stats[i].pass_count +
                              g_phase_stats[i].negative_pass_count +
                              g_phase_stats[i].fail_count;
            int inner_width;
            int inner_height;

            if (pad < 8) {
                pad = 8;
            }

            draw_diag_rect(screen_width, screen_height, tile_x, tile_y, tile_width,
                           tile_height, 0.12f, 0.13f, 0.16f, 1.0f);
            draw_diag_rect(screen_width, screen_height, tile_x + pad / 2,
                           tile_y + pad / 2, tile_width - pad,
                           tile_height / 6, accent_colors[i][0],
                           accent_colors[i][1], accent_colors[i][2], 1.0f);
            draw_diag_rect(screen_width, screen_height, tile_x + pad,
                           tile_y + tile_height - pad - tile_height / 5,
                           tile_width - pad * 2, tile_height / 6,
                           g_phase_stats[i].fail_count == 0 ? 0.10f : 0.32f,
                           g_phase_stats[i].fail_count == 0 ? 0.34f : 0.10f,
                           g_phase_stats[i].fail_count == 0 ? 0.18f : 0.10f,
                           1.0f);

            if (phase_total <= 0) {
                continue;
            }

            inner_width = tile_width - pad * 2;
            inner_height = tile_height / 4;
            draw_diag_rect(screen_width, screen_height, tile_x + pad,
                           tile_y + tile_height / 2 - inner_height / 2,
                           inner_width, inner_height, 0.18f, 0.19f, 0.22f, 1.0f);
            draw_diag_rect(screen_width, screen_height, tile_x + pad,
                           tile_y + tile_height / 2 - inner_height / 2,
                           (inner_width * g_phase_stats[i].pass_count) /
                               phase_total,
                           inner_height, 0.20f, 0.86f, 0.30f, 1.0f);
            draw_diag_rect(screen_width, screen_height,
                           tile_x + pad +
                               (inner_width * g_phase_stats[i].pass_count) /
                                   phase_total,
                           tile_y + tile_height / 2 - inner_height / 2,
                           (inner_width *
                            g_phase_stats[i].negative_pass_count) /
                               phase_total,
                           inner_height, 0.18f, 0.60f, 0.96f, 1.0f);
            draw_diag_rect(
                screen_width, screen_height,
                tile_x + pad +
                    (inner_width * (g_phase_stats[i].pass_count +
                                    g_phase_stats[i].negative_pass_count)) /
                        phase_total,
                tile_y + tile_height / 2 - inner_height / 2,
                (inner_width * g_phase_stats[i].fail_count) / phase_total,
                inner_height, 0.92f, 0.20f, 0.20f, 1.0f);
        }

        glDisable(GL_SCISSOR_TEST);
        glFinish();
        if (use_offscreen && diag_gx2_texture) {
            GX2Rect copy_rect;
            GX2Point copy_point;

            copy_rect.left = 0;
            copy_rect.top = 0;
            copy_rect.right = screen_width;
            copy_rect.bottom = screen_height;
            copy_point.x = 0;
            copy_point.y = 0;
            GX2CopySurfaceEx(&diag_gx2_texture->surface, 0, 0,
                             &tv_color->surface, 0, 0, 1, &copy_rect,
                             &copy_point);
            GX2DrawDone();
        }
        WHBGfxFinishRenderTV();
        WHBGfxFinishRender();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (diag_fbo != 0) {
        glDeleteFramebuffers(1, &diag_fbo);
    }
    if (diag_texture != 0) {
        glDeleteTextures(1, &diag_texture);
    }
}

// Log GL result
static void check_gl_error(const char* func_name) {
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        OSReport("[FAIL] %s resulted in error code 0x%04X\n", func_name, err);
    } else {
        OSReport("[PASS] %s completed successfully.\n", func_name);
    }
}

// Check expected error
static void expect_error(const char* test_name, GLenum expected_error) {
    GLenum actual = glGetError();
    if (actual == expected_error) {
        OSReport("   [PASS-NEGATIVE] %s correctly produced 0x%04X\n", test_name, expected_error);
    } else {
        OSReport("   [FAIL-NEGATIVE] %s expected 0x%04X but got 0x%04X\n", test_name, expected_error, actual);
    }
}

static GLboolean nearly_equal_double(GLdouble a, GLdouble b, GLdouble epsilon) {
    GLdouble diff = a - b;
    if (diff < 0.0) {
        diff = -diff;
    }
    return diff <= epsilon ? GL_TRUE : GL_FALSE;
}

static void init_fake_shader_group(
    WHBGfxShaderGroup *group, GX2VertexShader *vertex_shader,
    GX2PixelShader *pixel_shader, GX2UniformBlock *vertex_blocks,
    uint32_t vertex_block_count, GX2UniformVar *vertex_uniforms,
    uint32_t vertex_uniform_count, GX2SamplerVar *vertex_samplers,
    uint32_t vertex_sampler_count, GX2AttribVar *vertex_attribs,
    uint32_t vertex_attrib_count, GX2UniformBlock *pixel_blocks,
    uint32_t pixel_block_count, GX2UniformVar *pixel_uniforms,
    uint32_t pixel_uniform_count, GX2SamplerVar *pixel_samplers,
    uint32_t pixel_sampler_count) {
    memset(group, 0, sizeof(*group));
    memset(vertex_shader, 0, sizeof(*vertex_shader));
    memset(pixel_shader, 0, sizeof(*pixel_shader));

    vertex_shader->uniformBlockCount = vertex_block_count;
    vertex_shader->uniformBlocks = vertex_blocks;
    vertex_shader->uniformVarCount = vertex_uniform_count;
    vertex_shader->uniformVars = vertex_uniforms;
    vertex_shader->samplerVarCount = vertex_sampler_count;
    vertex_shader->samplerVars = vertex_samplers;
    vertex_shader->attribVarCount = vertex_attrib_count;
    vertex_shader->attribVars = vertex_attribs;

    pixel_shader->uniformBlockCount = pixel_block_count;
    pixel_shader->uniformBlocks = pixel_blocks;
    pixel_shader->uniformVarCount = pixel_uniform_count;
    pixel_shader->uniformVars = pixel_uniforms;
    pixel_shader->samplerVarCount = pixel_sampler_count;
    pixel_shader->samplerVars = pixel_samplers;

    group->vertexShader = vertex_shader;
    group->pixelShader = pixel_shader;
    group->numAttributes = vertex_attrib_count;
}

int main(int argc, char **argv) {
    WHBProcInit();
    WHBGfxInit();

    OSReport("----- GL 3.3 Translation Layer: Comprehensive Test App -----\n");

    OSReport("-> Initializing Memory and Context...\n");
    gl_mem_init();
    g_gl_context = gl_context_create();
    
    if (!g_gl_context) {
        OSReport("[CRITICAL FAIL] Context creation failed!\n");
        WHBGfxShutdown();
        WHBProcShutdown();
        return -1;
    }

    OSReport("-> Testing Limits & Capabilities (Phase 11)...\n");
    const GLubyte* vendor = glGetString(GL_VENDOR);
    check_gl_error("glGetString(GL_VENDOR)");
    
    OSReport("   Negative test: Invalid string query\n");
    glGetString(GL_INVALID_ENUM);
    expect_error("glGetString(GL_INVALID_ENUM)", GL_INVALID_ENUM);

    GLint max_tex_size = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex_size);
    check_gl_error("glGetIntegerv");

    GLint max_ubo_bindings = 0;
    glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &max_ubo_bindings);
    check_gl_error("glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS)");

    GLint ubo_alignment = 0;
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &ubo_alignment);
    check_gl_error("glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT)");
    GLint es_shader_compiler = 0;
    glGetIntegerv(GL_SHADER_COMPILER, &es_shader_compiler);
    check_gl_error("glGetIntegerv(GL_SHADER_COMPILER)");
    if (es_shader_compiler == GL_TRUE) {
        OSReport("[PASS] glGetIntegerv(GL_SHADER_COMPILER) returned true.\n");
    } else {
        OSReport("[FAIL] glGetIntegerv(GL_SHADER_COMPILER) returned %d\n",
                 es_shader_compiler);
    }
    GLint es_binary_formats = -1;
    glGetIntegerv(GL_NUM_SHADER_BINARY_FORMATS, &es_binary_formats);
    check_gl_error("glGetIntegerv(GL_NUM_SHADER_BINARY_FORMATS)");
    if (es_binary_formats == 0) {
        OSReport("[PASS] glGetIntegerv(GL_NUM_SHADER_BINARY_FORMATS) returned 0.\n");
    } else {
        OSReport("[FAIL] glGetIntegerv(GL_NUM_SHADER_BINARY_FORMATS) returned %d\n",
                 es_binary_formats);
    }
    GLint es_compressed_formats = -1;
    glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &es_compressed_formats);
    check_gl_error("glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS)");
    if (es_compressed_formats == 0) {
        OSReport("[PASS] glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS) returned 0.\n");
    } else {
        OSReport("[FAIL] glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS) returned %d\n",
                 es_compressed_formats);
    }
    GLint es_read_format = 0;
    glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT, &es_read_format);
    check_gl_error("glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT)");
    GLint es_read_type = 0;
    glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE, &es_read_type);
    check_gl_error("glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE)");
    if (es_read_format == GL_RGBA && es_read_type == GL_UNSIGNED_BYTE) {
        OSReport("[PASS] ES color read implementation enums returned expected values.\n");
    } else {
        OSReport("[FAIL] ES color read implementation enums returned 0x%04X / 0x%04X\n",
                 es_read_format, es_read_type);
    }
    GLint num_extensions = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &num_extensions);
    check_gl_error("glGetIntegerv(GL_NUM_EXTENSIONS)");
    const GLubyte* extension0 = glGetStringi(GL_EXTENSIONS, 0);
    check_gl_error("glGetStringi(GL_EXTENSIONS, 0)");
    if (num_extensions > 0 && extension0 && extension0[0] != '\0') {
        OSReport("[PASS] glGetStringi(GL_EXTENSIONS, 0) returned %s.\n",
                 extension0);
    } else {
        OSReport("[FAIL] glGetStringi(GL_EXTENSIONS, 0) returned null/empty.\n");
    }
    glGetStringi(GL_VENDOR, 0);
    expect_error("glGetStringi(GL_VENDOR, 0)", GL_INVALID_ENUM);
    glGetStringi(GL_EXTENSIONS, (GLuint)num_extensions);
    expect_error("glGetStringi(GL_EXTENSIONS, out_of_range)", GL_INVALID_VALUE);

    OSReport("-> Testing Legacy State & Clears (Phase 1/2)...\n");
    glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
    check_gl_error("glClearColor");
    glClearDepth(0.5);
    check_gl_error("glClearDepth");
    glClearStencil(7);
    check_gl_error("glClearStencil");
    glBlendColor(0.1f, 0.2f, 0.3f, 0.4f);
    check_gl_error("glBlendColor");
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_REVERSE_SUBTRACT);
    check_gl_error("glBlendEquationSeparate");
    glDepthRange(0.2, 0.8);
    check_gl_error("glDepthRange");
    glDepthRangef(0.25f, 0.75f);
    check_gl_error("glDepthRangef");
    GLfloat legacy_depth_range[2] = {0.0f, 0.0f};
    glGetFloatv(GL_DEPTH_RANGE, legacy_depth_range);
    check_gl_error("glGetFloatv(GL_DEPTH_RANGE)");
    GLdouble legacy_depth_range_double[2] = {0.0, 0.0};
    glGetDoublev(GL_DEPTH_RANGE, legacy_depth_range_double);
    check_gl_error("glGetDoublev(GL_DEPTH_RANGE)");
    if (nearly_equal_double(legacy_depth_range_double[0], 0.25, 0.000001) &&
        nearly_equal_double(legacy_depth_range_double[1], 0.75, 0.000001)) {
        OSReport("[PASS] glGetDoublev(GL_DEPTH_RANGE) returned expected values.\n");
    } else {
        OSReport("[FAIL] glGetDoublev(GL_DEPTH_RANGE) returned {%f, %f}\n",
                 legacy_depth_range_double[0], legacy_depth_range_double[1]);
    }
    glClearDepthf(0.25f);
    check_gl_error("glClearDepthf");
    GLfloat es_clear_depth = 0.0f;
    glGetFloatv(GL_DEPTH_CLEAR_VALUE, &es_clear_depth);
    check_gl_error("glGetFloatv(GL_DEPTH_CLEAR_VALUE)");
    if (es_clear_depth > 0.249f && es_clear_depth < 0.251f) {
        OSReport("[PASS] glClearDepthf updated the clear depth.\n");
    } else {
        OSReport("[FAIL] glClearDepthf produced %f\n", es_clear_depth);
    }
    GLfloat legacy_blend_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    glGetFloatv(GL_BLEND_COLOR, legacy_blend_color);
    check_gl_error("glGetFloatv(GL_BLEND_COLOR)");
    GLint precision_range[2] = {0, 0};
    GLint precision_bits = -1;
    glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT,
                               precision_range, &precision_bits);
    check_gl_error("glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT)");
    if (precision_range[0] == 127 && precision_range[1] == 127 &&
        precision_bits == 23) {
        OSReport("[PASS] glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT) returned expected values.\n");
    } else {
        OSReport("[FAIL] glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT) returned {%d, %d} precision %d\n",
                 precision_range[0], precision_range[1], precision_bits);
    }
    glGetShaderPrecisionFormat(GL_INVALID_ENUM, GL_HIGH_FLOAT,
                               precision_range, &precision_bits);
    expect_error("glGetShaderPrecisionFormat(GL_INVALID_ENUM, GL_HIGH_FLOAT)", GL_INVALID_ENUM);
    glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_INVALID_ENUM,
                               precision_range, &precision_bits);
    expect_error("glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_INVALID_ENUM)", GL_INVALID_ENUM);
    glReleaseShaderCompiler();
    check_gl_error("glReleaseShaderCompiler");
    glHint(GL_GENERATE_MIPMAP_HINT, GL_NICEST);
    check_gl_error("glHint(GL_GENERATE_MIPMAP_HINT)");
    GLint es_mipmap_hint = 0;
    glGetIntegerv(GL_GENERATE_MIPMAP_HINT, &es_mipmap_hint);
    check_gl_error("glGetIntegerv(GL_GENERATE_MIPMAP_HINT)");
    if (es_mipmap_hint == GL_NICEST) {
        OSReport("[PASS] glGetIntegerv(GL_GENERATE_MIPMAP_HINT) returned GL_NICEST.\n");
    } else {
        OSReport("[FAIL] glGetIntegerv(GL_GENERATE_MIPMAP_HINT) returned 0x%04X\n",
                 es_mipmap_hint);
    }
    glSampleCoverage(0.25f, GL_TRUE);
    check_gl_error("glSampleCoverage");
    glEnable(GL_SAMPLE_COVERAGE);
    check_gl_error("glEnable(GL_SAMPLE_COVERAGE)");
    GLfloat sample_coverage_value = 0.0f;
    glGetFloatv(GL_SAMPLE_COVERAGE_VALUE, &sample_coverage_value);
    check_gl_error("glGetFloatv(GL_SAMPLE_COVERAGE_VALUE)");
    GLboolean sample_coverage_invert = GL_FALSE;
    glGetBooleanv(GL_SAMPLE_COVERAGE_INVERT, &sample_coverage_invert);
    check_gl_error("glGetBooleanv(GL_SAMPLE_COVERAGE_INVERT)");
    if (sample_coverage_value > 0.249f && sample_coverage_value < 0.251f &&
        sample_coverage_invert == GL_TRUE &&
        glIsEnabled(GL_SAMPLE_COVERAGE) == GL_TRUE) {
        OSReport("[PASS] glSampleCoverage state returned expected values.\n");
    } else {
        OSReport("[FAIL] glSampleCoverage state returned %f invert=%d enabled=%d\n",
                 sample_coverage_value, sample_coverage_invert,
                 glIsEnabled(GL_SAMPLE_COVERAGE));
    }
    check_gl_error("glIsEnabled(GL_SAMPLE_COVERAGE)");
    glDisable(GL_SAMPLE_COVERAGE);
    check_gl_error("glDisable(GL_SAMPLE_COVERAGE)");
    glHint(GL_INVALID_ENUM, GL_NICEST);
    expect_error("glHint(GL_INVALID_ENUM)", GL_INVALID_ENUM);
    glHint(GL_GENERATE_MIPMAP_HINT, GL_INVALID_ENUM);
    expect_error("glHint(GL_GENERATE_MIPMAP_HINT, GL_INVALID_ENUM)", GL_INVALID_ENUM);
    GLint default_viewport_box[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, default_viewport_box);
    check_gl_error("glGetIntegerv(GL_VIEWPORT default)");
    glViewport(1, 2, 6, 5);
    check_gl_error("glViewport");
    GLint viewport_box[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport_box);
    check_gl_error("glGetIntegerv(GL_VIEWPORT)");
    if (viewport_box[0] == 1 && viewport_box[1] == 2 && viewport_box[2] == 6 &&
        viewport_box[3] == 5) {
        OSReport("[PASS] glGetIntegerv(GL_VIEWPORT) returned expected values.\n");
    } else {
        OSReport("[FAIL] glGetIntegerv(GL_VIEWPORT) returned {%d, %d, %d, %d}\n",
                 viewport_box[0], viewport_box[1], viewport_box[2],
                 viewport_box[3]);
    }
    GLint default_scissor_box[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_SCISSOR_BOX, default_scissor_box);
    check_gl_error("glGetIntegerv(GL_SCISSOR_BOX default)");
    glScissor(2, 1, 3, 2);
    check_gl_error("glScissor");
    GLint scissor_box[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_SCISSOR_BOX, scissor_box);
    check_gl_error("glGetIntegerv(GL_SCISSOR_BOX)");
    if (scissor_box[0] == 2 && scissor_box[1] == 1 && scissor_box[2] == 3 &&
        scissor_box[3] == 2) {
        OSReport("[PASS] glGetIntegerv(GL_SCISSOR_BOX) returned expected values.\n");
    } else {
        OSReport("[FAIL] glGetIntegerv(GL_SCISSOR_BOX) returned {%d, %d, %d, %d}\n",
                 scissor_box[0], scissor_box[1], scissor_box[2],
                 scissor_box[3]);
    }
    glViewport(default_viewport_box[0], default_viewport_box[1],
               default_viewport_box[2], default_viewport_box[3]);
    check_gl_error("glViewport(restore)");
    glScissor(default_scissor_box[0], default_scissor_box[1],
              default_scissor_box[2], default_scissor_box[3]);
    check_gl_error("glScissor(restore)");
    glViewport(0, 0, -1, 1);
    expect_error("glViewport(negative_width)", GL_INVALID_VALUE);
    glScissor(0, 0, -1, 1);
    expect_error("glScissor(negative_width)", GL_INVALID_VALUE);
    glEnable(GL_STENCIL_TEST);
    check_gl_error("glEnable(GL_STENCIL_TEST)");
    if (glIsEnabled(GL_STENCIL_TEST) == GL_TRUE) {
        OSReport("[PASS] glIsEnabled(GL_STENCIL_TEST) returned enabled.\n");
    } else {
        OSReport("[FAIL] glIsEnabled(GL_STENCIL_TEST) returned disabled.\n");
    }
    check_gl_error("glIsEnabled(GL_STENCIL_TEST)");
    glStencilFuncSeparate(GL_FRONT, GL_LEQUAL, 1, 0x0F);
    check_gl_error("glStencilFuncSeparate(GL_FRONT)");
    glStencilOpSeparate(GL_BACK, GL_KEEP, GL_INCR, GL_REPLACE);
    check_gl_error("glStencilOpSeparate(GL_BACK)");
    glStencilMask(0x3F);
    check_gl_error("glStencilMask");
    glStencilMaskSeparate(GL_FRONT, 0x0F);
    check_gl_error("glStencilMaskSeparate(GL_FRONT)");
    glPolygonOffset(1.5f, 2.0f);
    check_gl_error("glPolygonOffset");
    glEnable(GL_POLYGON_OFFSET_FILL);
    check_gl_error("glEnable(GL_POLYGON_OFFSET_FILL)");
    GLboolean legacy_depth_write_mask = GL_FALSE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &legacy_depth_write_mask);
    check_gl_error("glGetBooleanv(GL_DEPTH_WRITEMASK)");
    if (legacy_depth_write_mask == GL_TRUE) {
        OSReport("[PASS] glGetBooleanv(GL_DEPTH_WRITEMASK) returned enabled.\n");
    } else {
        OSReport("[FAIL] glGetBooleanv(GL_DEPTH_WRITEMASK) returned disabled.\n");
    }
    glColorMask(GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
    check_gl_error("glColorMask(custom)");
    GLboolean legacy_color_mask[4] = {GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE};
    glGetBooleanv(GL_COLOR_WRITEMASK, legacy_color_mask);
    check_gl_error("glGetBooleanv(GL_COLOR_WRITEMASK)");
    if (legacy_color_mask[0] == GL_TRUE && legacy_color_mask[1] == GL_FALSE &&
        legacy_color_mask[2] == GL_TRUE && legacy_color_mask[3] == GL_FALSE) {
        OSReport("[PASS] glGetBooleanv(GL_COLOR_WRITEMASK) returned expected values.\n");
    } else {
        OSReport("[FAIL] glGetBooleanv(GL_COLOR_WRITEMASK) returned {%u, %u, %u, %u}\n",
                 legacy_color_mask[0], legacy_color_mask[1],
                 legacy_color_mask[2], legacy_color_mask[3]);
    }
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    check_gl_error("glColorMask(restore)");
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    check_gl_error("glClear");
    glClear(0x80000000u);
    expect_error("glClear(invalid_mask)", GL_INVALID_VALUE);
    glIsEnabled(GL_INVALID_ENUM);
    expect_error("glIsEnabled(GL_INVALID_ENUM)", GL_INVALID_ENUM);
    glDisable(GL_POLYGON_OFFSET_FILL);
    check_gl_error("glDisable(GL_POLYGON_OFFSET_FILL)");
    glDisable(GL_STENCIL_TEST);
    check_gl_error("glDisable(GL_STENCIL_TEST)");

    OSReport("-> Testing Buffer Objects (Phase 4)...\n");
    GLuint buffers[2];
    glGenBuffers(2, buffers);
    check_gl_error("glGenBuffers");
    if (glIsBuffer(buffers[0]) == GL_TRUE) {
        OSReport("[PASS] glIsBuffer returned true.\n");
    } else {
        OSReport("[FAIL] glIsBuffer returned false.\n");
    }
    check_gl_error("glIsBuffer");
    
    // Try invalid count
    GLuint bad_buffers[1] = {0};
    glGenBuffers(-1, bad_buffers);
    expect_error("glGenBuffers(-1)", GL_INVALID_VALUE);

    glBindBuffer(GL_ARRAY_BUFFER, buffers[0]);
    check_gl_error("glBindBuffer(GL_ARRAY_BUFFER)");
    
    // Try bad target
    glBindBuffer(GL_INVALID_ENUM, buffers[0]);
    expect_error("glBindBuffer(GL_INVALID_ENUM)", GL_INVALID_ENUM);

    float vertices[] = { 0.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f };
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    check_gl_error("glBufferData(GL_ARRAY_BUFFER)");
    GLint array_buffer_size = 0;
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &array_buffer_size);
    check_gl_error("glGetBufferParameteriv(GL_BUFFER_SIZE)");
    if (array_buffer_size == (GLint)sizeof(vertices)) {
        OSReport("[PASS] glGetBufferParameteriv(GL_BUFFER_SIZE) returned %d.\n",
                 array_buffer_size);
    } else {
        OSReport("[FAIL] glGetBufferParameteriv(GL_BUFFER_SIZE) returned %d\n",
                 array_buffer_size);
    }

    // Try bad target
    glBufferData(GL_INVALID_ENUM, 32, vertices, GL_STATIC_DRAW);
    expect_error("glBufferData(GL_INVALID_ENUM)", GL_INVALID_ENUM);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers[1]);
    unsigned short indices[] = { 0, 1, 2 };
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    check_gl_error("glBufferData(GL_ELEMENT_ARRAY_BUFFER)");

    GLuint ubo;
    glGenBuffers(1, &ubo);
    check_gl_error("glGenBuffers(UBO)");
    glBindBuffer(GL_UNIFORM_BUFFER, ubo);
    check_gl_error("glBindBuffer(GL_UNIFORM_BUFFER)");
    glBufferData(GL_UNIFORM_BUFFER, 256, NULL, GL_DYNAMIC_DRAW);
    check_gl_error("glBufferData(GL_UNIFORM_BUFFER)");

    glBindBufferBase(GL_ARRAY_BUFFER, 0, ubo);
    expect_error("glBindBufferBase(GL_ARRAY_BUFFER)", GL_INVALID_ENUM);

    glBindBufferBase(GL_UNIFORM_BUFFER, (GLuint)max_ubo_bindings, ubo);
    expect_error("glBindBufferBase(out_of_range)", GL_INVALID_VALUE);

    glBindBufferRange(GL_UNIFORM_BUFFER, 0, ubo, 1, 64);
    expect_error("glBindBufferRange(unaligned_offset)", GL_INVALID_VALUE);

    glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);
    check_gl_error("glBindBufferBase(GL_UNIFORM_BUFFER)");

    void* mapped_ubo = glMapBufferRange(GL_UNIFORM_BUFFER, 0, 256, GL_WRITE_ONLY);
    if (!mapped_ubo) {
        OSReport("[FAIL] glMapBufferRange(GL_UNIFORM_BUFFER) returned NULL\n");
    }
    check_gl_error("glMapBufferRange(GL_UNIFORM_BUFFER)");
    GLint uniform_buffer_mapped = GL_FALSE;
    glGetBufferParameteriv(GL_UNIFORM_BUFFER, GL_BUFFER_MAPPED, &uniform_buffer_mapped);
    check_gl_error("glGetBufferParameteriv(GL_BUFFER_MAPPED)");
    if (uniform_buffer_mapped == GL_TRUE) {
        OSReport("[PASS] glGetBufferParameteriv(GL_BUFFER_MAPPED) returned true.\n");
    } else {
        OSReport("[FAIL] glGetBufferParameteriv(GL_BUFFER_MAPPED) returned %d\n",
                 uniform_buffer_mapped);
    }
    GLvoid* mapped_ubo_query = NULL;
    glGetBufferPointerv(GL_UNIFORM_BUFFER, GL_BUFFER_MAP_POINTER, &mapped_ubo_query);
    check_gl_error("glGetBufferPointerv(GL_BUFFER_MAP_POINTER)");
    if (mapped_ubo_query == mapped_ubo) {
        OSReport("[PASS] glGetBufferPointerv(GL_BUFFER_MAP_POINTER) returned mapped pointer.\n");
    } else {
        OSReport("[FAIL] glGetBufferPointerv(GL_BUFFER_MAP_POINTER) returned %p\n",
                 mapped_ubo_query);
    }
    glUnmapBuffer(GL_UNIFORM_BUFFER);
    check_gl_error("glUnmapBuffer(GL_UNIFORM_BUFFER)");
    mapped_ubo_query = (GLvoid*)0x1;
    glGetBufferPointerv(GL_UNIFORM_BUFFER, GL_BUFFER_MAP_POINTER, &mapped_ubo_query);
    check_gl_error("glGetBufferPointerv(GL_BUFFER_MAP_POINTER after unmap)");
    if (mapped_ubo_query == NULL) {
        OSReport("[PASS] glGetBufferPointerv(GL_BUFFER_MAP_POINTER after unmap) returned NULL.\n");
    } else {
        OSReport("[FAIL] glGetBufferPointerv(GL_BUFFER_MAP_POINTER after unmap) returned %p\n",
                 mapped_ubo_query);
    }
    glGetBufferPointerv(GL_UNIFORM_BUFFER, GL_INVALID_ENUM, &mapped_ubo_query);
    expect_error("glGetBufferPointerv(GL_INVALID_ENUM)", GL_INVALID_ENUM);
    glGetBufferParameteriv(GL_INVALID_ENUM, GL_BUFFER_SIZE, &uniform_buffer_mapped);
    expect_error("glGetBufferParameteriv(GL_INVALID_ENUM)", GL_INVALID_ENUM);

    OSReport("-> Testing Texture Objects (Phase 5)...\n");
    GLuint tex[2];
    glGenTextures(2, tex);
    check_gl_error("glGenTextures");
    if (glIsTexture(tex[0]) == GL_TRUE) {
        OSReport("[PASS] glIsTexture returned true.\n");
    } else {
        OSReport("[FAIL] glIsTexture returned false.\n");
    }
    check_gl_error("glIsTexture");
    
    // Try invalid count
    glGenTextures(-5, tex);
    expect_error("glGenTextures(-5)", GL_INVALID_VALUE);
    
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    check_gl_error("glBindTexture");
    
    // Try bad target
    glBindTexture(GL_INVALID_ENUM, tex[0]);
    expect_error("glBindTexture(GL_INVALID_ENUM)", GL_INVALID_ENUM);

    unsigned char pixels[4 * 4 * 4]; // White texture data
    memset(pixels, 255, sizeof(pixels));
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    check_gl_error("glTexImage2D");

    glBindTexture(GL_TEXTURE_2D, tex[1]);
    check_gl_error("glBindTexture(second)");
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    check_gl_error("glTexImage2D(second)");
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    check_gl_error("glBindTexture(restore)");

    unsigned char sub_pixels[2 * 2 * 4];
    memset(sub_pixels, 127, sizeof(sub_pixels));
    glTexSubImage2D(GL_TEXTURE_2D, 0, 1, 1, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, sub_pixels);
    check_gl_error("glTexSubImage2D");
    glTexSubImage2D(GL_TEXTURE_2D, 0, 3, 3, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, sub_pixels);
    expect_error("glTexSubImage2D(out_of_bounds)", GL_INVALID_VALUE);

    GLuint tex3d;
    glGenTextures(1, &tex3d);
    check_gl_error("glGenTextures(3D)");
    glBindTexture(GL_TEXTURE_3D, tex3d);
    check_gl_error("glBindTexture(GL_TEXTURE_3D)");
    unsigned char pixels3d[4 * 4 * 2 * 4];
    memset(pixels3d, 64, sizeof(pixels3d));
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA, 4, 4, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels3d);
    check_gl_error("glTexImage3D");
    unsigned char sub_pixels3d[2 * 2 * 1 * 4];
    memset(sub_pixels3d, 192, sizeof(sub_pixels3d));
    glTexSubImage3D(GL_TEXTURE_3D, 0, 1, 1, 0, 2, 2, 1, GL_RGBA, GL_UNSIGNED_BYTE, sub_pixels3d);
    check_gl_error("glTexSubImage3D");
    glTexSubImage3D(GL_TEXTURE_3D, 0, 3, 3, 1, 2, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, sub_pixels3d);
    expect_error("glTexSubImage3D(out_of_bounds)", GL_INVALID_VALUE);
    glGenerateMipmap(GL_TEXTURE_3D);
    check_gl_error("glGenerateMipmap(GL_TEXTURE_3D)");
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    check_gl_error("glBindTexture(restore_2d)");
    
    // Try negative width
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, -4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    expect_error("glTexImage2D(negative_width)", GL_INVALID_VALUE);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    check_gl_error("glTexParameteri");
    GLint texture_min_filter = 0;
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &texture_min_filter);
    check_gl_error("glGetTexParameteriv(GL_TEXTURE_MIN_FILTER)");
    if (texture_min_filter == GL_LINEAR) {
        OSReport("[PASS] glGetTexParameteriv(GL_TEXTURE_MIN_FILTER) returned GL_LINEAR.\n");
    } else {
        OSReport("[FAIL] glGetTexParameteriv(GL_TEXTURE_MIN_FILTER) returned 0x%04X\n",
                 texture_min_filter);
    }
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLfloat)GL_NEAREST);
    check_gl_error("glTexParameterf");
    GLint wrap_repeat = GL_REPEAT;
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &wrap_repeat);
    check_gl_error("glTexParameteriv");
    GLfloat wrap_clamp = (GLfloat)GL_CLAMP_TO_EDGE;
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &wrap_clamp);
    check_gl_error("glTexParameterfv");
    GLfloat queried_wrap_s = 0.0f;
    glGetTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &queried_wrap_s);
    check_gl_error("glGetTexParameterfv(GL_TEXTURE_WRAP_S)");
    if ((GLint)queried_wrap_s == GL_REPEAT) {
        OSReport("[PASS] glGetTexParameterfv(GL_TEXTURE_WRAP_S) returned GL_REPEAT.\n");
    } else {
        OSReport("[FAIL] glGetTexParameterfv(GL_TEXTURE_WRAP_S) returned %f\n",
                 queried_wrap_s);
    }
    
    // Try bad target
    glTexParameteri(GL_INVALID_ENUM, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    expect_error("glTexParameteri(GL_INVALID_ENUM)", GL_INVALID_ENUM);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    expect_error("glTexParameteri(invalid_mag_filter)", GL_INVALID_ENUM);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_INVALID_ENUM);
    expect_error("glTexParameteri(invalid_wrap)", GL_INVALID_ENUM);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, 64, pixels);
    expect_error("glCompressedTexImage2D(unsupported)", GL_INVALID_ENUM);
    glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 4, GL_RGBA, 64, pixels);
    expect_error("glCompressedTexSubImage2D(unsupported)", GL_INVALID_ENUM);

    glGenerateMipmap(GL_TEXTURE_2D);
    check_gl_error("glGenerateMipmap");
    glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
    check_gl_error("glPixelStorei(GL_UNPACK_ALIGNMENT)");
    GLint unpack_alignment = 0;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_alignment);
    check_gl_error("glGetIntegerv(GL_UNPACK_ALIGNMENT)");
    if (unpack_alignment == 8) {
        OSReport("[PASS] glGetIntegerv(GL_UNPACK_ALIGNMENT) returned 8.\n");
    } else {
        OSReport("[FAIL] glGetIntegerv(GL_UNPACK_ALIGNMENT) returned %d\n",
                 unpack_alignment);
    }
    GLuint pixel_store_tex = 0;
    GLuint pixel_store_fbo = 0;
    glGenTextures(1, &pixel_store_tex);
    check_gl_error("glGenTextures(pixel_store)");
    glBindTexture(GL_TEXTURE_2D, pixel_store_tex);
    check_gl_error("glBindTexture(pixel_store)");
    GLubyte padded_upload[16];
    memset(padded_upload, 0xCC, sizeof(padded_upload));
    padded_upload[0] = 10;
    padded_upload[1] = 20;
    padded_upload[2] = 30;
    padded_upload[3] = 40;
    padded_upload[8] = 50;
    padded_upload[9] = 60;
    padded_upload[10] = 70;
    padded_upload[11] = 80;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 padded_upload);
    check_gl_error("glTexImage2D(pixel_store_alignment)");
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    check_gl_error("glPixelStorei(GL_UNPACK_ALIGNMENT restore)");
    glGenFramebuffers(1, &pixel_store_fbo);
    check_gl_error("glGenFramebuffers(pixel_store)");
    glBindFramebuffer(GL_FRAMEBUFFER, pixel_store_fbo);
    check_gl_error("glBindFramebuffer(pixel_store)");
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           pixel_store_tex, 0);
    check_gl_error("glFramebufferTexture2D(pixel_store)");
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    check_gl_error("glReadBuffer(pixel_store)");
    GLubyte padded_readback[16];
    memset(padded_readback, 0xCD, sizeof(padded_readback));
    glPixelStorei(GL_PACK_ALIGNMENT, 8);
    check_gl_error("glPixelStorei(GL_PACK_ALIGNMENT)");
    GLint pack_alignment = 0;
    glGetIntegerv(GL_PACK_ALIGNMENT, &pack_alignment);
    check_gl_error("glGetIntegerv(GL_PACK_ALIGNMENT)");
    glReadPixels(0, 0, 1, 2, GL_RGBA, GL_UNSIGNED_BYTE, padded_readback);
    check_gl_error("glReadPixels(pixel_store_alignment)");
    if (padded_readback[0] == 10 && padded_readback[1] == 20 &&
        padded_readback[2] == 30 && padded_readback[3] == 40 &&
        padded_readback[4] == 0xCD && padded_readback[5] == 0xCD &&
        padded_readback[6] == 0xCD && padded_readback[7] == 0xCD &&
        padded_readback[8] == 50 && padded_readback[9] == 60 &&
        padded_readback[10] == 70 && padded_readback[11] == 80) {
        OSReport("[PASS] glPixelStorei alignment paths returned expected data.\n");
    } else {
        OSReport("[FAIL] glPixelStorei alignment paths returned {%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u}\n",
                 padded_readback[0], padded_readback[1], padded_readback[2],
                 padded_readback[3], padded_readback[4], padded_readback[5],
                 padded_readback[6], padded_readback[7], padded_readback[8],
                 padded_readback[9], padded_readback[10], padded_readback[11]);
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    check_gl_error("glPixelStorei(GL_PACK_ALIGNMENT restore)");
    GLubyte skipped_upload[16];
    memset(skipped_upload, 0, sizeof(skipped_upload));
    skipped_upload[12] = 90;
    skipped_upload[13] = 100;
    skipped_upload[14] = 110;
    skipped_upload[15] = 120;
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 2);
    check_gl_error("glPixelStorei(GL_UNPACK_ROW_LENGTH)");
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 1);
    check_gl_error("glPixelStorei(GL_UNPACK_SKIP_ROWS)");
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 1);
    check_gl_error("glPixelStorei(GL_UNPACK_SKIP_PIXELS)");
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 skipped_upload);
    check_gl_error("glTexImage2D(pixel_store_skip)");
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    check_gl_error("glPixelStorei(GL_UNPACK_ROW_LENGTH restore)");
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    check_gl_error("glPixelStorei(GL_UNPACK_SKIP_ROWS restore)");
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    check_gl_error("glPixelStorei(GL_UNPACK_SKIP_PIXELS restore)");
    GLubyte skipped_readback[24];
    memset(skipped_readback, 0xEE, sizeof(skipped_readback));
    glPixelStorei(GL_PACK_ROW_LENGTH, 3);
    check_gl_error("glPixelStorei(GL_PACK_ROW_LENGTH)");
    glPixelStorei(GL_PACK_SKIP_ROWS, 1);
    check_gl_error("glPixelStorei(GL_PACK_SKIP_ROWS)");
    glPixelStorei(GL_PACK_SKIP_PIXELS, 1);
    check_gl_error("glPixelStorei(GL_PACK_SKIP_PIXELS)");
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, skipped_readback);
    check_gl_error("glReadPixels(pixel_store_skip)");
    if (skipped_readback[16] == 90 && skipped_readback[17] == 100 &&
        skipped_readback[18] == 110 && skipped_readback[19] == 120) {
        OSReport("[PASS] glPixelStorei skip paths returned expected data.\n");
    } else {
        OSReport("[FAIL] glPixelStorei skip paths returned {%u,%u,%u,%u}\n",
                 skipped_readback[16], skipped_readback[17],
                 skipped_readback[18], skipped_readback[19]);
    }
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    check_gl_error("glPixelStorei(GL_PACK_ROW_LENGTH restore)");
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    check_gl_error("glPixelStorei(GL_PACK_SKIP_ROWS restore)");
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    check_gl_error("glPixelStorei(GL_PACK_SKIP_PIXELS restore)");
    glPixelStorei(GL_PACK_ALIGNMENT, 3);
    expect_error("glPixelStorei(GL_PACK_ALIGNMENT=3)", GL_INVALID_VALUE);
    glPixelStorei(GL_INVALID_ENUM, 4);
    expect_error("glPixelStorei(GL_INVALID_ENUM)", GL_INVALID_ENUM);
    GLuint es2_textures[3] = {0, 0, 0};
    glGenTextures(3, es2_textures);
    check_gl_error("glGenTextures(es2_formats)");
    GLubyte es2_alpha_texel[1] = {77};
    glBindTexture(GL_TEXTURE_2D, es2_textures[0]);
    check_gl_error("glBindTexture(es2_alpha)");
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 1, 1, 0, GL_ALPHA,
                 GL_UNSIGNED_BYTE, es2_alpha_texel);
    check_gl_error("glTexImage2D(GL_ALPHA)");
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           es2_textures[0], 0);
    check_gl_error("glFramebufferTexture2D(es2_alpha)");
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    check_gl_error("glReadBuffer(es2_alpha)");
    GLubyte es2_readback[4] = {0, 0, 0, 0};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, es2_readback);
    check_gl_error("glReadPixels(GL_ALPHA)");
    if (es2_readback[0] == 0 && es2_readback[1] == 0 &&
        es2_readback[2] == 0 && es2_readback[3] == 77) {
        OSReport("[PASS] glReadPixels(GL_ALPHA texture) returned expected data.\n");
    } else {
        OSReport("[FAIL] glReadPixels(GL_ALPHA texture) returned {%u, %u, %u, %u}\n",
                 es2_readback[0], es2_readback[1], es2_readback[2],
                 es2_readback[3]);
    }
    GLubyte es2_luminance_texel[1] = {90};
    glBindTexture(GL_TEXTURE_2D, es2_textures[1]);
    check_gl_error("glBindTexture(es2_luminance)");
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 1, 1, 0, GL_LUMINANCE,
                 GL_UNSIGNED_BYTE, es2_luminance_texel);
    check_gl_error("glTexImage2D(GL_LUMINANCE)");
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           es2_textures[1], 0);
    check_gl_error("glFramebufferTexture2D(es2_luminance)");
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    check_gl_error("glReadBuffer(es2_luminance)");
    memset(es2_readback, 0, sizeof(es2_readback));
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, es2_readback);
    check_gl_error("glReadPixels(GL_LUMINANCE)");
    if (es2_readback[0] == 90 && es2_readback[1] == 90 &&
        es2_readback[2] == 90 && es2_readback[3] == 255) {
        OSReport("[PASS] glReadPixels(GL_LUMINANCE texture) returned expected data.\n");
    } else {
        OSReport("[FAIL] glReadPixels(GL_LUMINANCE texture) returned {%u, %u, %u, %u}\n",
                 es2_readback[0], es2_readback[1], es2_readback[2],
                 es2_readback[3]);
    }
    GLubyte es2_luminance_alpha_texel[2] = {120, 33};
    glBindTexture(GL_TEXTURE_2D, es2_textures[2]);
    check_gl_error("glBindTexture(es2_luminance_alpha)");
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, 1, 1, 0,
                 GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE,
                 es2_luminance_alpha_texel);
    check_gl_error("glTexImage2D(GL_LUMINANCE_ALPHA)");
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           es2_textures[2], 0);
    check_gl_error("glFramebufferTexture2D(es2_luminance_alpha)");
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    check_gl_error("glReadBuffer(es2_luminance_alpha)");
    memset(es2_readback, 0, sizeof(es2_readback));
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, es2_readback);
    check_gl_error("glReadPixels(GL_LUMINANCE_ALPHA)");
    if (es2_readback[0] == 120 && es2_readback[1] == 120 &&
        es2_readback[2] == 120 && es2_readback[3] == 33) {
        OSReport("[PASS] glReadPixels(GL_LUMINANCE_ALPHA texture) returned expected data.\n");
    } else {
        OSReport("[FAIL] glReadPixels(GL_LUMINANCE_ALPHA texture) returned {%u, %u, %u, %u}\n",
                 es2_readback[0], es2_readback[1], es2_readback[2],
                 es2_readback[3]);
    }
    GLuint copy_textures[5] = {0, 0, 0, 0, 0};
    glGenTextures(5, copy_textures);
    check_gl_error("glGenTextures(copy_tex)");
    glBindTexture(GL_TEXTURE_2D, copy_textures[0]);
    check_gl_error("glBindTexture(copy_source)");
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    check_gl_error("glTexImage2D(copy_source)");
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           copy_textures[0], 0);
    check_gl_error("glFramebufferTexture2D(copy_source)");
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    check_gl_error("glReadBuffer(copy_source)");
    glClearColor(0.25f, 0.5f, 0.75f, 0.4f);
    glClear(GL_COLOR_BUFFER_BIT);
    check_gl_error("glClear(copy_source)");
    glBindTexture(GL_TEXTURE_2D, copy_textures[1]);
    check_gl_error("glBindTexture(copy_alpha)");
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 0, 0, 1, 1, 0);
    check_gl_error("glCopyTexImage2D(GL_ALPHA)");
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           copy_textures[1], 0);
    check_gl_error("glFramebufferTexture2D(copy_alpha)");
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    check_gl_error("glReadBuffer(copy_alpha)");
    memset(es2_readback, 0, sizeof(es2_readback));
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, es2_readback);
    check_gl_error("glReadPixels(glCopyTexImage2D GL_ALPHA)");
    if (es2_readback[0] == 0 && es2_readback[1] == 0 &&
        es2_readback[2] == 0 && es2_readback[3] == 102) {
        OSReport("[PASS] glCopyTexImage2D(GL_ALPHA) returned expected data.\n");
    } else {
        OSReport("[FAIL] glCopyTexImage2D(GL_ALPHA) returned {%u, %u, %u, %u}\n",
                 es2_readback[0], es2_readback[1], es2_readback[2],
                 es2_readback[3]);
    }
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           copy_textures[0], 0);
    check_gl_error("glFramebufferTexture2D(copy_source restore)");
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    check_gl_error("glReadBuffer(copy_source restore)");
    glBindTexture(GL_TEXTURE_2D, copy_textures[2]);
    check_gl_error("glBindTexture(copy_luminance)");
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 0, 0, 1, 1, 0);
    check_gl_error("glCopyTexImage2D(GL_LUMINANCE)");
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           copy_textures[2], 0);
    check_gl_error("glFramebufferTexture2D(copy_luminance)");
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    check_gl_error("glReadBuffer(copy_luminance)");
    memset(es2_readback, 0, sizeof(es2_readback));
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, es2_readback);
    check_gl_error("glReadPixels(glCopyTexImage2D GL_LUMINANCE)");
    if (es2_readback[0] == 64 && es2_readback[1] == 64 &&
        es2_readback[2] == 64 && es2_readback[3] == 255) {
        OSReport("[PASS] glCopyTexImage2D(GL_LUMINANCE) returned expected data.\n");
    } else {
        OSReport("[FAIL] glCopyTexImage2D(GL_LUMINANCE) returned {%u, %u, %u, %u}\n",
                 es2_readback[0], es2_readback[1], es2_readback[2],
                 es2_readback[3]);
    }
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           copy_textures[0], 0);
    check_gl_error("glFramebufferTexture2D(copy_source restore_luminance)");
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    check_gl_error("glReadBuffer(copy_source restore_luminance)");
    glBindTexture(GL_TEXTURE_2D, copy_textures[3]);
    check_gl_error("glBindTexture(copy_luminance_alpha)");
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, 0, 0, 1, 1, 0);
    check_gl_error("glCopyTexImage2D(GL_LUMINANCE_ALPHA)");
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           copy_textures[3], 0);
    check_gl_error("glFramebufferTexture2D(copy_luminance_alpha)");
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    check_gl_error("glReadBuffer(copy_luminance_alpha)");
    memset(es2_readback, 0, sizeof(es2_readback));
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, es2_readback);
    check_gl_error("glReadPixels(glCopyTexImage2D GL_LUMINANCE_ALPHA)");
    if (es2_readback[0] == 64 && es2_readback[1] == 64 &&
        es2_readback[2] == 64 && es2_readback[3] == 102) {
        OSReport("[PASS] glCopyTexImage2D(GL_LUMINANCE_ALPHA) returned expected data.\n");
    } else {
        OSReport("[FAIL] glCopyTexImage2D(GL_LUMINANCE_ALPHA) returned {%u, %u, %u, %u}\n",
                 es2_readback[0], es2_readback[1], es2_readback[2],
                 es2_readback[3]);
    }
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           copy_textures[0], 0);
    check_gl_error("glFramebufferTexture2D(copy_source restore_sub)");
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    check_gl_error("glReadBuffer(copy_source restore_sub)");
    glClearColor(0.8f, 0.2f, 0.4f, 0.6f);
    glClear(GL_COLOR_BUFFER_BIT);
    check_gl_error("glClear(copy_sub_source)");
    GLubyte copy_sub_upload[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    glBindTexture(GL_TEXTURE_2D, copy_textures[4]);
    check_gl_error("glBindTexture(copy_sub)");
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 1, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, copy_sub_upload);
    check_gl_error("glTexImage2D(copy_sub)");
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 1, 0, 0, 0, 1, 1);
    check_gl_error("glCopyTexSubImage2D");
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           copy_textures[4], 0);
    check_gl_error("glFramebufferTexture2D(copy_sub)");
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    check_gl_error("glReadBuffer(copy_sub)");
    GLubyte copy_sub_readback[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    glReadPixels(0, 0, 2, 1, GL_RGBA, GL_UNSIGNED_BYTE, copy_sub_readback);
    check_gl_error("glReadPixels(glCopyTexSubImage2D)");
    if (copy_sub_readback[0] == 1 && copy_sub_readback[1] == 2 &&
        copy_sub_readback[2] == 3 && copy_sub_readback[3] == 4 &&
        copy_sub_readback[4] == 204 && copy_sub_readback[5] == 51 &&
        copy_sub_readback[6] == 102 && copy_sub_readback[7] == 153) {
        OSReport("[PASS] glCopyTexSubImage2D returned expected data.\n");
    } else {
        OSReport("[FAIL] glCopyTexSubImage2D returned {%u, %u, %u, %u, %u, %u, %u, %u}\n",
                 copy_sub_readback[0], copy_sub_readback[1],
                 copy_sub_readback[2], copy_sub_readback[3],
                 copy_sub_readback[4], copy_sub_readback[5],
                 copy_sub_readback[6], copy_sub_readback[7]);
    }
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 2, 0, 0, 0, 1, 1);
    expect_error("glCopyTexSubImage2D(out_of_bounds)", GL_INVALID_VALUE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    check_gl_error("glBindFramebuffer(pixel_store restore_default)");
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    check_gl_error("glBindTexture(pixel_store restore_2d)");
    GLuint sampler = 0;
    glGenSamplers(1, &sampler);
    check_gl_error("glGenSamplers");
    if (glIsSampler(sampler) == GL_TRUE) {
        OSReport("[PASS] glIsSampler returned true.\n");
    } else {
        OSReport("[FAIL] glIsSampler returned false.\n");
    }
    check_gl_error("glIsSampler");
    glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    check_gl_error("glSamplerParameteri");
    glSamplerParameterf(sampler, GL_TEXTURE_WRAP_S, (GLfloat)GL_CLAMP_TO_EDGE);
    check_gl_error("glSamplerParameterf");
    GLint sampler_wrap_t_param = GL_REPEAT;
    glSamplerParameteriv(sampler, GL_TEXTURE_WRAP_T, &sampler_wrap_t_param);
    check_gl_error("glSamplerParameteriv");
    GLfloat sampler_mag_filter_param = (GLfloat)GL_LINEAR;
    glSamplerParameterfv(sampler, GL_TEXTURE_MAG_FILTER, &sampler_mag_filter_param);
    check_gl_error("glSamplerParameterfv");
    GLint sampler_min_filter = 0;
    glGetSamplerParameteriv(sampler, GL_TEXTURE_MIN_FILTER, &sampler_min_filter);
    check_gl_error("glGetSamplerParameteriv");
    if (sampler_min_filter == GL_NEAREST) {
        OSReport("[PASS] glGetSamplerParameteriv returned GL_NEAREST.\n");
    } else {
        OSReport("[FAIL] glGetSamplerParameteriv returned 0x%04X\n",
                 sampler_min_filter);
    }
    GLfloat sampler_wrap_s = 0.0f;
    glGetSamplerParameterfv(sampler, GL_TEXTURE_WRAP_S, &sampler_wrap_s);
    check_gl_error("glGetSamplerParameterfv");
    if ((GLint)sampler_wrap_s == GL_CLAMP_TO_EDGE) {
        OSReport("[PASS] glGetSamplerParameterfv returned GL_CLAMP_TO_EDGE.\n");
    } else {
        OSReport("[FAIL] glGetSamplerParameterfv returned %f\n", sampler_wrap_s);
    }
    glBindSampler(0, sampler);
    check_gl_error("glBindSampler");
    glBindSampler(32, sampler);
    expect_error("glBindSampler(out_of_range)", GL_INVALID_VALUE);

    OSReport("-> Testing Shaders & Programs (Phase 6)...\n");
    GLuint vshader = glCreateShader(GL_VERTEX_SHADER);
    check_gl_error("glCreateShader(GL_VERTEX_SHADER)");
    if (glIsShader(vshader) == GL_TRUE) {
        OSReport("[PASS] glIsShader returned true.\n");
    } else {
        OSReport("[FAIL] glIsShader returned false.\n");
    }
    check_gl_error("glIsShader");
    
    // Try bad shader
    glCreateShader(GL_INVALID_ENUM);
    expect_error("glCreateShader(GL_INVALID_ENUM)", GL_INVALID_ENUM);

    const char* vsrc = "#version 330 core\nvoid main() {}";
    glShaderSource(vshader, 1, &vsrc, NULL);
    check_gl_error("glShaderSource");

    GLuint pshader = glCreateShader(GL_FRAGMENT_SHADER);
    check_gl_error("glCreateShader(GL_FRAGMENT_SHADER)");

    const char* psrc = "#version 330 core\nout vec4 FragColor;\nvoid main() { FragColor = vec4(1.0); }";
    glShaderSource(pshader, 1, &psrc, NULL);
    check_gl_error("glShaderSource(fragment)");

    glCompileShader(vshader);
    check_gl_error("glCompileShader(vertex)");
    glShaderBinary(1, &vshader, 0xFFFFu, NULL, 0);
    expect_error("glShaderBinary(unsupported_format)", GL_INVALID_ENUM);
    GLint vshader_compile_status = GL_FALSE;
    glGetShaderiv(vshader, GL_COMPILE_STATUS, &vshader_compile_status);
    check_gl_error("glGetShaderiv(vertex, GL_COMPILE_STATUS)");
    if (vshader_compile_status == GL_TRUE) {
        OSReport("[PASS] glGetShaderiv(vertex, GL_COMPILE_STATUS) returned compiled.\n");
    } else {
        OSReport("[FAIL] glGetShaderiv(vertex, GL_COMPILE_STATUS) returned %d\n",
                 vshader_compile_status);
    }
    GLint vshader_source_length = 0;
    glGetShaderiv(vshader, GL_SHADER_SOURCE_LENGTH, &vshader_source_length);
    check_gl_error("glGetShaderiv(vertex, GL_SHADER_SOURCE_LENGTH)");
    if (vshader_source_length > 0) {
        OSReport("[PASS] glGetShaderiv(vertex, GL_SHADER_SOURCE_LENGTH) returned %d.\n",
                 vshader_source_length);
    } else {
        OSReport("[FAIL] glGetShaderiv(vertex, GL_SHADER_SOURCE_LENGTH) returned %d\n",
                 vshader_source_length);
    }
    GLchar shader_info_log[128];
    GLsizei shader_info_log_length = 0;
    glGetShaderInfoLog(vshader, sizeof(shader_info_log), &shader_info_log_length, shader_info_log);
    check_gl_error("glGetShaderInfoLog(vertex)");
    GLchar shader_source_copy[128];
    GLsizei shader_source_copy_length = 0;
    glGetShaderSource(vshader, sizeof(shader_source_copy), &shader_source_copy_length,
                      shader_source_copy);
    check_gl_error("glGetShaderSource(vertex)");
    if (shader_source_copy_length > 0) {
        OSReport("[PASS] glGetShaderSource(vertex) returned %d bytes.\n",
                 shader_source_copy_length);
    } else {
        OSReport("[FAIL] glGetShaderSource(vertex) returned %d bytes.\n",
                 shader_source_copy_length);
    }

    glCompileShader(pshader);
    check_gl_error("glCompileShader(fragment)");
    
    GLuint prog = glCreateProgram();
    check_gl_error("glCreateProgram");
    if (glIsProgram(prog) == GL_TRUE) {
        OSReport("[PASS] glIsProgram returned true.\n");
    } else {
        OSReport("[FAIL] glIsProgram returned false.\n");
    }
    check_gl_error("glIsProgram");
    
    glAttachShader(prog, vshader);
    check_gl_error("glAttachShader");

    glAttachShader(prog, pshader);
    check_gl_error("glAttachShader(fragment)");

    glBindAttribLocation(prog, 3, "aCustomPosition");
    check_gl_error("glBindAttribLocation");
    glBindAttribLocation(999, 0, "aCustomPosition");
    expect_error("glBindAttribLocation(999)", GL_INVALID_VALUE);
    GLuint shader_name_only = glCreateShader(GL_VERTEX_SHADER);
    glBindAttribLocation(shader_name_only, 0, "aCustomPosition");
    expect_error("glBindAttribLocation(shader_name)", GL_INVALID_OPERATION);
    glDeleteShader(shader_name_only);
    check_gl_error("glDeleteShader(shader_name)");
    glBindAttribLocation(prog, 999, "aCustomPosition");
    expect_error("glBindAttribLocation(out_of_range)", GL_INVALID_VALUE);
    glBindAttribLocation(prog, 0, "gl_reserved");
    expect_error("glBindAttribLocation(gl_reserved)", GL_INVALID_OPERATION);

    GLint attached_shader_count = 0;
    glGetProgramiv(prog, GL_ATTACHED_SHADERS, &attached_shader_count);
    check_gl_error("glGetProgramiv(GL_ATTACHED_SHADERS)");
    if (attached_shader_count == 2) {
        OSReport("[PASS] glGetProgramiv(GL_ATTACHED_SHADERS) returned 2.\n");
    } else {
        OSReport("[FAIL] glGetProgramiv(GL_ATTACHED_SHADERS) returned %d\n",
                 attached_shader_count);
    }
    GLuint attached_shaders[2] = {0, 0};
    GLsizei attached_shader_written = 0;
    glGetAttachedShaders(prog, 2, &attached_shader_written, attached_shaders);
    check_gl_error("glGetAttachedShaders");
    if (attached_shader_written == 2) {
        OSReport("[PASS] glGetAttachedShaders returned 2.\n");
    } else {
        OSReport("[FAIL] glGetAttachedShaders returned %d\n",
                 attached_shader_written);
    }
    
    // Try null program
    glAttachShader(0, vshader);
    expect_error("glAttachShader(0, ...)", GL_INVALID_OPERATION);
    
    glLinkProgram(prog);
    expect_error("glLinkProgram(source_only)", GL_INVALID_OPERATION);
    GLint program_link_status = GL_TRUE;
    glGetProgramiv(prog, GL_LINK_STATUS, &program_link_status);
    check_gl_error("glGetProgramiv(GL_LINK_STATUS)");
    if (program_link_status == GL_FALSE) {
        OSReport("[PASS] glGetProgramiv(GL_LINK_STATUS) returned unlinked.\n");
    } else {
        OSReport("[FAIL] glGetProgramiv(GL_LINK_STATUS) returned %d\n",
                 program_link_status);
    }
    GLchar program_info_log[256];
    GLsizei program_info_log_length = 0;
    glGetProgramInfoLog(prog, sizeof(program_info_log), &program_info_log_length, program_info_log);
    check_gl_error("glGetProgramInfoLog");

    glDetachShader(prog, pshader);
    check_gl_error("glDetachShader(fragment)");
    glGetProgramiv(prog, GL_ATTACHED_SHADERS, &attached_shader_count);
    check_gl_error("glGetProgramiv(GL_ATTACHED_SHADERS after detach)");
    if (attached_shader_count == 1) {
        OSReport("[PASS] glGetProgramiv(GL_ATTACHED_SHADERS after detach) returned 1.\n");
    } else {
        OSReport("[FAIL] glGetProgramiv(GL_ATTACHED_SHADERS after detach) returned %d\n",
                 attached_shader_count);
    }
    glAttachShader(prog, pshader);
    check_gl_error("glAttachShader(fragment restore)");

    GLuint reflect_prog = glCreateProgram();
    check_gl_error("glCreateProgram(reflect)");
    WHBGfxShaderGroup reflect_group;
    GX2VertexShader reflect_vertex_shader;
    GX2PixelShader reflect_pixel_shader;
    GX2UniformBlock reflect_vertex_blocks[1];
    GX2UniformBlock reflect_pixel_blocks[1];
    GX2UniformVar reflect_vertex_uniforms[2];
    GX2UniformVar reflect_pixel_uniforms[3];
    GX2SamplerVar reflect_pixel_samplers[1];
    GX2AttribVar reflect_vertex_attribs[2];
    memset(reflect_vertex_blocks, 0, sizeof(reflect_vertex_blocks));
    memset(reflect_pixel_blocks, 0, sizeof(reflect_pixel_blocks));
    memset(reflect_vertex_uniforms, 0, sizeof(reflect_vertex_uniforms));
    memset(reflect_pixel_uniforms, 0, sizeof(reflect_pixel_uniforms));
    memset(reflect_pixel_samplers, 0, sizeof(reflect_pixel_samplers));
    memset(reflect_vertex_attribs, 0, sizeof(reflect_vertex_attribs));
    reflect_vertex_blocks[0].name = "VSGlobals";
    reflect_vertex_blocks[0].size = 80;
    reflect_pixel_blocks[0].name = "PSGlobals";
    reflect_pixel_blocks[0].size = 48;
    reflect_vertex_uniforms[0].name = "uModelView";
    reflect_vertex_uniforms[0].type = GX2_SHADER_VAR_TYPE_FLOAT4X4;
    reflect_vertex_uniforms[0].count = 1;
    reflect_vertex_uniforms[0].block = 0;
    reflect_vertex_uniforms[0].offset = 0;
    reflect_vertex_uniforms[1].name = "uViewportSize";
    reflect_vertex_uniforms[1].type = GX2_SHADER_VAR_TYPE_INT2;
    reflect_vertex_uniforms[1].count = 1;
    reflect_vertex_uniforms[1].block = 0;
    reflect_vertex_uniforms[1].offset = 64;
    reflect_pixel_uniforms[0].name = "uTint";
    reflect_pixel_uniforms[0].type = GX2_SHADER_VAR_TYPE_FLOAT4;
    reflect_pixel_uniforms[0].count = 1;
    reflect_pixel_uniforms[0].block = 0;
    reflect_pixel_uniforms[0].offset = 0;
    reflect_pixel_uniforms[1].name = "uLightMask";
    reflect_pixel_uniforms[1].type = GX2_SHADER_VAR_TYPE_INT3;
    reflect_pixel_uniforms[1].count = 1;
    reflect_pixel_uniforms[1].block = 0;
    reflect_pixel_uniforms[1].offset = 16;
    reflect_pixel_uniforms[2].name = "uMask";
    reflect_pixel_uniforms[2].type = GX2_SHADER_VAR_TYPE_INT4;
    reflect_pixel_uniforms[2].count = 1;
    reflect_pixel_uniforms[2].block = 0;
    reflect_pixel_uniforms[2].offset = 32;
    reflect_pixel_samplers[0].name = "uTexture";
    reflect_pixel_samplers[0].type = GX2_SAMPLER_VAR_TYPE_SAMPLER_2D;
    reflect_pixel_samplers[0].location = 0;
    reflect_vertex_attribs[0].name = "aPosition";
    reflect_vertex_attribs[0].type = GX2_SHADER_VAR_TYPE_FLOAT3;
    reflect_vertex_attribs[0].count = 1;
    reflect_vertex_attribs[0].location = 0;
    reflect_vertex_attribs[1].name = "aTexCoord";
    reflect_vertex_attribs[1].type = GX2_SHADER_VAR_TYPE_FLOAT2;
    reflect_vertex_attribs[1].count = 1;
    reflect_vertex_attribs[1].location = 1;
    init_fake_shader_group(&reflect_group, &reflect_vertex_shader,
                           &reflect_pixel_shader, reflect_vertex_blocks, 1,
                           reflect_vertex_uniforms, 2, NULL, 0,
                           reflect_vertex_attribs, 2, reflect_pixel_blocks, 1,
                           reflect_pixel_uniforms, 3, reflect_pixel_samplers, 1);
    glWiiULoadShaderGroup(reflect_prog, &reflect_group);
    check_gl_error("glWiiULoadShaderGroup(reflect)");
    GLint reflect_link_status = GL_FALSE;
    glGetProgramiv(reflect_prog, GL_LINK_STATUS, &reflect_link_status);
    check_gl_error("glGetProgramiv(reflect, GL_LINK_STATUS)");
    if (reflect_link_status == GL_TRUE) {
        OSReport("[PASS] glGetProgramiv(reflect, GL_LINK_STATUS) returned linked.\n");
    } else {
        OSReport("[FAIL] glGetProgramiv(reflect, GL_LINK_STATUS) returned %d\n",
                 reflect_link_status);
    }
    GLint reflect_active_attributes = 0;
    glGetProgramiv(reflect_prog, GL_ACTIVE_ATTRIBUTES, &reflect_active_attributes);
    check_gl_error("glGetProgramiv(GL_ACTIVE_ATTRIBUTES)");
    if (reflect_active_attributes == 2) {
        OSReport("[PASS] glGetProgramiv(GL_ACTIVE_ATTRIBUTES) returned 2.\n");
    } else {
        OSReport("[FAIL] glGetProgramiv(GL_ACTIVE_ATTRIBUTES) returned %d\n",
                 reflect_active_attributes);
    }
    GLint reflect_active_attribute_max_length = 0;
    glGetProgramiv(reflect_prog, GL_ACTIVE_ATTRIBUTE_MAX_LENGTH,
                   &reflect_active_attribute_max_length);
    check_gl_error("glGetProgramiv(GL_ACTIVE_ATTRIBUTE_MAX_LENGTH)");
    if (reflect_active_attribute_max_length == 10) {
        OSReport("[PASS] glGetProgramiv(GL_ACTIVE_ATTRIBUTE_MAX_LENGTH) returned 10.\n");
    } else {
        OSReport("[FAIL] glGetProgramiv(GL_ACTIVE_ATTRIBUTE_MAX_LENGTH) returned %d\n",
                 reflect_active_attribute_max_length);
    }
    GLint reflect_active_uniforms = 0;
    glGetProgramiv(reflect_prog, GL_ACTIVE_UNIFORMS, &reflect_active_uniforms);
    check_gl_error("glGetProgramiv(GL_ACTIVE_UNIFORMS)");
    if (reflect_active_uniforms == 6) {
        OSReport("[PASS] glGetProgramiv(GL_ACTIVE_UNIFORMS) returned 6.\n");
    } else {
        OSReport("[FAIL] glGetProgramiv(GL_ACTIVE_UNIFORMS) returned %d\n",
                 reflect_active_uniforms);
    }
    GLint reflect_active_uniform_max_length = 0;
    glGetProgramiv(reflect_prog, GL_ACTIVE_UNIFORM_MAX_LENGTH,
                   &reflect_active_uniform_max_length);
    check_gl_error("glGetProgramiv(GL_ACTIVE_UNIFORM_MAX_LENGTH)");
    if (reflect_active_uniform_max_length == 14) {
        OSReport("[PASS] glGetProgramiv(GL_ACTIVE_UNIFORM_MAX_LENGTH) returned 14.\n");
    } else {
        OSReport("[FAIL] glGetProgramiv(GL_ACTIVE_UNIFORM_MAX_LENGTH) returned %d\n",
                 reflect_active_uniform_max_length);
    }
    GLchar active_name[64];
    GLsizei active_name_length = 0;
    GLint active_size = 0;
    GLenum active_type = 0;
    memset(active_name, 0, sizeof(active_name));
    glGetActiveAttrib(reflect_prog, 0, sizeof(active_name), &active_name_length,
                      &active_size, &active_type, active_name);
    check_gl_error("glGetActiveAttrib(index0)");
    if (strcmp(active_name, "aPosition") == 0 && active_size == 1 &&
        active_type == GL_FLOAT_VEC3) {
        OSReport("[PASS] glGetActiveAttrib(index0) returned expected values.\n");
    } else {
        OSReport("[FAIL] glGetActiveAttrib(index0) returned %s size %d type 0x%04X\n",
                 active_name, active_size, active_type);
    }
    memset(active_name, 0, sizeof(active_name));
    active_name_length = 0;
    active_size = 0;
    active_type = 0;
    glGetActiveUniform(reflect_prog, 5, sizeof(active_name), &active_name_length,
                       &active_size, &active_type, active_name);
    check_gl_error("glGetActiveUniform(index5)");
    if (strcmp(active_name, "uTexture") == 0 && active_size == 1 &&
        active_type == GL_SAMPLER_2D) {
        OSReport("[PASS] glGetActiveUniform(index5) returned expected values.\n");
    } else {
        OSReport("[FAIL] glGetActiveUniform(index5) returned %s size %d type 0x%04X\n",
                 active_name, active_size, active_type);
    }
    GLint reflect_viewport_location =
        glGetUniformLocation(reflect_prog, "uViewportSize");
    check_gl_error("glGetUniformLocation(reflect_int2)");
    GLint reflect_tint_location = glGetUniformLocation(reflect_prog, "uTint");
    check_gl_error("glGetUniformLocation(reflect_float4)");
    GLint reflect_lightmask_location =
        glGetUniformLocation(reflect_prog, "uLightMask");
    check_gl_error("glGetUniformLocation(reflect_int3)");
    GLint reflect_mask_location = glGetUniformLocation(reflect_prog, "uMask");
    check_gl_error("glGetUniformLocation(reflect_int4)");
    GLint reflect_attrib_location = glGetAttribLocation(reflect_prog, "aTexCoord");
    check_gl_error("glGetAttribLocation(reflect)");
    if (reflect_attrib_location == 1) {
        OSReport("[PASS] glGetAttribLocation(reflect) returned 1.\n");
    } else {
        OSReport("[FAIL] glGetAttribLocation(reflect) returned %d\n",
                 reflect_attrib_location);
    }
    GLint reflect_uniform_location = glGetUniformLocation(reflect_prog, "uTexture");
    check_gl_error("glGetUniformLocation(reflect)");
    if (reflect_uniform_location != -1) {
        OSReport("[PASS] glGetUniformLocation(reflect) returned a valid location.\n");
    } else {
        OSReport("[FAIL] glGetUniformLocation(reflect) returned %d\n",
                 reflect_uniform_location);
    }
    glValidateProgram(reflect_prog);
    check_gl_error("glValidateProgram(reflect)");
    GLint reflect_validate_status = GL_FALSE;
    glGetProgramiv(reflect_prog, GL_VALIDATE_STATUS, &reflect_validate_status);
    check_gl_error("glGetProgramiv(GL_VALIDATE_STATUS)");
    if (reflect_validate_status == GL_TRUE) {
        OSReport("[PASS] glGetProgramiv(GL_VALIDATE_STATUS) returned validated.\n");
    } else {
        OSReport("[FAIL] glGetProgramiv(GL_VALIDATE_STATUS) returned %d\n",
                 reflect_validate_status);
    }
    glUseProgram(reflect_prog);
    check_gl_error("glUseProgram(reflect)");
    GLint sampler_units[1] = {0};
    glUniform1iv(reflect_uniform_location, 1, sampler_units);
    check_gl_error("glUniform1iv(reflect_sampler)");
    glUniform4f(reflect_tint_location, 0.25f, 0.5f, 0.75f, 1.0f);
    check_gl_error("glUniform4f(reflect_tint)");
    glUniform2i(reflect_viewport_location, 640, 360);
    check_gl_error("glUniform2i(reflect)");
    GLint viewport_size[2] = {320, 180};
    glUniform2iv(reflect_viewport_location, 1, viewport_size);
    check_gl_error("glUniform2iv(reflect)");
    glUniform3i(reflect_lightmask_location, 1, 2, 3);
    check_gl_error("glUniform3i(reflect)");
    GLint light_mask[3] = {4, 5, 6};
    glUniform3iv(reflect_lightmask_location, 1, light_mask);
    check_gl_error("glUniform3iv(reflect)");
    glUniform4i(reflect_mask_location, 7, 8, 9, 10);
    check_gl_error("glUniform4i(reflect)");
    GLint mask_values[4] = {11, 12, 13, 14};
    glUniform4iv(reflect_mask_location, 1, mask_values);
    check_gl_error("glUniform4iv(reflect)");
    GLfloat reflect_tint_query[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    glGetUniformfv(reflect_prog, reflect_tint_location, reflect_tint_query);
    check_gl_error("glGetUniformfv(reflect)");
    if (reflect_tint_query[0] > 0.249f && reflect_tint_query[0] < 0.251f &&
        reflect_tint_query[1] > 0.499f && reflect_tint_query[1] < 0.501f &&
        reflect_tint_query[2] > 0.749f && reflect_tint_query[2] < 0.751f &&
        reflect_tint_query[3] > 0.999f && reflect_tint_query[3] < 1.001f) {
        OSReport("[PASS] glGetUniformfv(reflect) returned expected values.\n");
    } else {
        OSReport("[FAIL] glGetUniformfv(reflect) returned {%f, %f, %f, %f}\n",
                 reflect_tint_query[0], reflect_tint_query[1],
                 reflect_tint_query[2], reflect_tint_query[3]);
    }
    GLint reflect_viewport_query[2] = {0, 0};
    glGetUniformiv(reflect_prog, reflect_viewport_location, reflect_viewport_query);
    check_gl_error("glGetUniformiv(reflect_int2)");
    if (reflect_viewport_query[0] == 320 && reflect_viewport_query[1] == 180) {
        OSReport("[PASS] glGetUniformiv(reflect_int2) returned expected values.\n");
    } else {
        OSReport("[FAIL] glGetUniformiv(reflect_int2) returned {%d, %d}\n",
                 reflect_viewport_query[0], reflect_viewport_query[1]);
    }
    GLint reflect_mask_query[4] = {0, 0, 0, 0};
    glGetUniformiv(reflect_prog, reflect_mask_location, reflect_mask_query);
    check_gl_error("glGetUniformiv(reflect_int4)");
    if (reflect_mask_query[0] == 11 && reflect_mask_query[1] == 12 &&
        reflect_mask_query[2] == 13 && reflect_mask_query[3] == 14) {
        OSReport("[PASS] glGetUniformiv(reflect_int4) returned expected values.\n");
    } else {
        OSReport("[FAIL] glGetUniformiv(reflect_int4) returned {%d, %d, %d, %d}\n",
                 reflect_mask_query[0], reflect_mask_query[1],
                 reflect_mask_query[2], reflect_mask_query[3]);
    }
    GLint reflect_sampler_query = -1;
    glGetUniformiv(reflect_prog, reflect_uniform_location, &reflect_sampler_query);
    check_gl_error("glGetUniformiv(reflect_sampler)");
    if (reflect_sampler_query == 0) {
        OSReport("[PASS] glGetUniformiv(reflect_sampler) returned expected value.\n");
    } else {
        OSReport("[FAIL] glGetUniformiv(reflect_sampler) returned %d\n",
                 reflect_sampler_query);
    }
    glUseProgram(0);
    check_gl_error("glUseProgram(reflect_restore)");
    glGetActiveAttrib(reflect_prog, 9, sizeof(active_name), &active_name_length,
                      &active_size, &active_type, active_name);
    expect_error("glGetActiveAttrib(out_of_range)", GL_INVALID_VALUE);
    glGetActiveUniform(reflect_prog, 9, sizeof(active_name), &active_name_length,
                       &active_size, &active_type, active_name);
    expect_error("glGetActiveUniform(out_of_range)", GL_INVALID_VALUE);

    GLuint conflict_prog = glCreateProgram();
    check_gl_error("glCreateProgram(conflict)");
    WHBGfxShaderGroup conflict_group;
    GX2VertexShader conflict_vertex_shader;
    GX2PixelShader conflict_pixel_shader;
    GX2SamplerVar conflict_pixel_samplers[2];
    memset(conflict_pixel_samplers, 0, sizeof(conflict_pixel_samplers));
    conflict_pixel_samplers[0].name = "uTexture2D";
    conflict_pixel_samplers[0].type = GX2_SAMPLER_VAR_TYPE_SAMPLER_2D;
    conflict_pixel_samplers[0].location = 0;
    conflict_pixel_samplers[1].name = "uTextureCube";
    conflict_pixel_samplers[1].type = GX2_SAMPLER_VAR_TYPE_SAMPLER_CUBE;
    conflict_pixel_samplers[1].location = 1;
    init_fake_shader_group(&conflict_group, &conflict_vertex_shader,
                           &conflict_pixel_shader, NULL, 0, NULL, 0, NULL, 0,
                           NULL, 0, NULL, 0, NULL, 0, conflict_pixel_samplers,
                           2);
    glWiiULoadShaderGroup(conflict_prog, &conflict_group);
    check_gl_error("glWiiULoadShaderGroup(conflict)");
    glValidateProgram(conflict_prog);
    check_gl_error("glValidateProgram(conflict)");
    GLint conflict_validate_status = GL_TRUE;
    glGetProgramiv(conflict_prog, GL_VALIDATE_STATUS, &conflict_validate_status);
    check_gl_error("glGetProgramiv(conflict, GL_VALIDATE_STATUS)");
    if (conflict_validate_status == GL_FALSE) {
        OSReport("[PASS] glGetProgramiv(conflict, GL_VALIDATE_STATUS) returned invalid.\n");
    } else {
        OSReport("[FAIL] glGetProgramiv(conflict, GL_VALIDATE_STATUS) returned %d\n",
                 conflict_validate_status);
    }
    GLsizei conflict_log_length = 0;
    glGetProgramInfoLog(conflict_prog, sizeof(program_info_log),
                        &conflict_log_length, program_info_log);
    check_gl_error("glGetProgramInfoLog(conflict)");
    if (conflict_log_length > 0) {
        OSReport("[PASS] glGetProgramInfoLog(conflict) returned a validation log.\n");
    } else {
        OSReport("[FAIL] glGetProgramInfoLog(conflict) returned an empty validation log.\n");
    }

    glUseProgram(prog);
    expect_error("glUseProgram(source_only)", GL_INVALID_OPERATION);
    
    // Try bad program
    glUseProgram(999);
    expect_error("glUseProgram(999)", GL_INVALID_VALUE);
    
    // Drop current program
    glUseProgram(0);
    check_gl_error("glUseProgram(0)");
    
    // Uniform without program
    glUniform1f(0, 1.0f);
    expect_error("glUniform1f(unbound_prog)", GL_INVALID_OPERATION);
    glUniform4f(0, 1.0f, 0.5f, 0.25f, 1.0f);
    expect_error("glUniform4f(unbound_prog)", GL_INVALID_OPERATION);
    glUniform1i(0, 0);
    expect_error("glUniform1i(unbound_prog)", GL_INVALID_OPERATION);
    GLint sampler_value = 0;
    glUniform1iv(0, 1, &sampler_value);
    expect_error("glUniform1iv(unbound_prog)", GL_INVALID_OPERATION);
    glUniform2i(0, 1, 2);
    expect_error("glUniform2i(unbound_prog)", GL_INVALID_OPERATION);
    GLint int2_values[2] = {1, 2};
    glUniform2iv(0, 1, int2_values);
    expect_error("glUniform2iv(unbound_prog)", GL_INVALID_OPERATION);
    glUniform3i(0, 1, 2, 3);
    expect_error("glUniform3i(unbound_prog)", GL_INVALID_OPERATION);
    GLint int3_values[3] = {1, 2, 3};
    glUniform3iv(0, 1, int3_values);
    expect_error("glUniform3iv(unbound_prog)", GL_INVALID_OPERATION);
    glUniform4i(0, 1, 2, 3, 4);
    expect_error("glUniform4i(unbound_prog)", GL_INVALID_OPERATION);
    GLint int4_values[4] = {1, 2, 3, 4};
    glUniform4iv(0, 1, int4_values);
    expect_error("glUniform4iv(unbound_prog)", GL_INVALID_OPERATION);
    GLfloat matrix2[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    glUniformMatrix2fv(0, 1, GL_FALSE, matrix2);
    expect_error("glUniformMatrix2fv(unbound_prog)", GL_INVALID_OPERATION);
    GLfloat matrix3[9] = {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f};
    glUniformMatrix3fv(0, 1, GL_FALSE, matrix3);
    expect_error("glUniformMatrix3fv(unbound_prog)", GL_INVALID_OPERATION);

    GLuint temp_shader = glCreateShader(GL_VERTEX_SHADER);
    check_gl_error("glCreateShader(temp_vertex)");
    glShaderSource(temp_shader, 1, &vsrc, NULL);
    check_gl_error("glShaderSource(temp_vertex)");
    glCompileShader(temp_shader);
    check_gl_error("glCompileShader(temp_vertex)");
    GLuint temp_prog = glCreateProgram();
    check_gl_error("glCreateProgram(temp)");
    glAttachShader(temp_prog, temp_shader);
    check_gl_error("glAttachShader(temp)");
    glDeleteShader(temp_shader);
    check_gl_error("glDeleteShader(attached)");
    GLint temp_shader_delete_status = GL_FALSE;
    glGetShaderiv(temp_shader, GL_DELETE_STATUS, &temp_shader_delete_status);
    check_gl_error("glGetShaderiv(temp, GL_DELETE_STATUS)");
    if (temp_shader_delete_status == GL_TRUE) {
        OSReport("[PASS] glGetShaderiv(temp, GL_DELETE_STATUS) returned deleted.\n");
    } else {
        OSReport("[FAIL] glGetShaderiv(temp, GL_DELETE_STATUS) returned %d\n",
                 temp_shader_delete_status);
    }
    glDetachShader(temp_prog, temp_shader);
    check_gl_error("glDetachShader(temp)");
    glDeleteProgram(temp_prog);
    check_gl_error("glDeleteProgram(temp)");

    OSReport("-> Testing Vertex Arrays (Phase 7)...\n");
    GLuint vao;
    glGenVertexArrays(1, &vao);
    check_gl_error("glGenVertexArrays");
    if (glIsVertexArray(vao) == GL_TRUE) {
        OSReport("[PASS] glIsVertexArray returned true.\n");
    } else {
        OSReport("[FAIL] glIsVertexArray returned false.\n");
    }
    check_gl_error("glIsVertexArray");
    
    // Try invalid count
    glGenVertexArrays(-1, &vao);
    expect_error("glGenVertexArrays(-1)", GL_INVALID_VALUE);

    glBindVertexArray(vao);
    check_gl_error("glBindVertexArray");
    
    glBindBuffer(GL_ARRAY_BUFFER, buffers[0]);
    glEnableVertexAttribArray(0);
    check_gl_error("glEnableVertexAttribArray");
    
    // Try bad attrib
    glEnableVertexAttribArray(999);
    expect_error("glEnableVertexAttribArray(999)", GL_INVALID_VALUE);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
    check_gl_error("glVertexAttribPointer");

    glVertexAttribDivisor(0, 1);
    check_gl_error("glVertexAttribDivisor");
    GLint attrib_enabled = 0;
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &attrib_enabled);
    check_gl_error("glGetVertexAttribiv(GL_VERTEX_ATTRIB_ARRAY_ENABLED)");
    if (attrib_enabled == GL_TRUE) {
        OSReport("[PASS] glGetVertexAttribiv(GL_VERTEX_ATTRIB_ARRAY_ENABLED) returned enabled.\n");
    } else {
        OSReport("[FAIL] glGetVertexAttribiv(GL_VERTEX_ATTRIB_ARRAY_ENABLED) returned %d\n",
                 attrib_enabled);
    }
    GLint attrib_size = 0;
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_SIZE, &attrib_size);
    check_gl_error("glGetVertexAttribiv(GL_VERTEX_ATTRIB_ARRAY_SIZE)");
    if (attrib_size == 2) {
        OSReport("[PASS] glGetVertexAttribiv(GL_VERTEX_ATTRIB_ARRAY_SIZE) returned 2.\n");
    } else {
        OSReport("[FAIL] glGetVertexAttribiv(GL_VERTEX_ATTRIB_ARRAY_SIZE) returned %d\n",
                 attrib_size);
    }
    GLint attrib_buffer_binding = 0;
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &attrib_buffer_binding);
    check_gl_error("glGetVertexAttribiv(GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING)");
    if (attrib_buffer_binding == (GLint)buffers[0]) {
        OSReport("[PASS] glGetVertexAttribiv(GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING) returned buffer id.\n");
    } else {
        OSReport("[FAIL] glGetVertexAttribiv(GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING) returned %d\n",
                 attrib_buffer_binding);
    }
    GLint attrib_divisor = 0;
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_DIVISOR, &attrib_divisor);
    check_gl_error("glGetVertexAttribiv(GL_VERTEX_ATTRIB_ARRAY_DIVISOR)");
    if (attrib_divisor == 1) {
        OSReport("[PASS] glGetVertexAttribiv(GL_VERTEX_ATTRIB_ARRAY_DIVISOR) returned 1.\n");
    } else {
        OSReport("[FAIL] glGetVertexAttribiv(GL_VERTEX_ATTRIB_ARRAY_DIVISOR) returned %d\n",
                 attrib_divisor);
    }
    GLfloat attrib_size_float = 0.0f;
    glGetVertexAttribfv(0, GL_VERTEX_ATTRIB_ARRAY_SIZE, &attrib_size_float);
    check_gl_error("glGetVertexAttribfv(GL_VERTEX_ATTRIB_ARRAY_SIZE)");
    if (attrib_size_float > 1.99f && attrib_size_float < 2.01f) {
        OSReport("[PASS] glGetVertexAttribfv(GL_VERTEX_ATTRIB_ARRAY_SIZE) returned 2.\n");
    } else {
        OSReport("[FAIL] glGetVertexAttribfv(GL_VERTEX_ATTRIB_ARRAY_SIZE) returned %f\n",
                 attrib_size_float);
    }
    glVertexAttrib4f(1, 0.25f, 0.5f, 0.75f, 1.0f);
    check_gl_error("glVertexAttrib4f");
    GLfloat current_attrib[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    glGetVertexAttribfv(1, GL_CURRENT_VERTEX_ATTRIB, current_attrib);
    check_gl_error("glGetVertexAttribfv(GL_CURRENT_VERTEX_ATTRIB)");
    if (current_attrib[0] > 0.249f && current_attrib[0] < 0.251f &&
        current_attrib[1] > 0.499f && current_attrib[1] < 0.501f &&
        current_attrib[2] > 0.749f && current_attrib[2] < 0.751f &&
        current_attrib[3] > 0.999f && current_attrib[3] < 1.001f) {
        OSReport("[PASS] glGetVertexAttribfv(GL_CURRENT_VERTEX_ATTRIB) returned expected values.\n");
    } else {
        OSReport("[FAIL] glGetVertexAttribfv(GL_CURRENT_VERTEX_ATTRIB) returned {%f, %f, %f, %f}\n",
                 current_attrib[0], current_attrib[1], current_attrib[2],
                 current_attrib[3]);
    }
    GLvoid* attrib_pointer = (GLvoid*)1;
    glGetVertexAttribPointerv(0, GL_VERTEX_ATTRIB_ARRAY_POINTER, &attrib_pointer);
    check_gl_error("glGetVertexAttribPointerv(GL_VERTEX_ATTRIB_ARRAY_POINTER)");
    if (attrib_pointer == (GLvoid*)0) {
        OSReport("[PASS] glGetVertexAttribPointerv(GL_VERTEX_ATTRIB_ARRAY_POINTER) returned pointer.\n");
    } else {
        OSReport("[FAIL] glGetVertexAttribPointerv(GL_VERTEX_ATTRIB_ARRAY_POINTER) returned %p\n",
                 attrib_pointer);
    }
    
    // Try bad size
    glVertexAttribPointer(0, 5, GL_FLOAT, GL_FALSE, 0, (void*)0);
    expect_error("glVertexAttribPointer(size=5)", GL_INVALID_VALUE);

    glVertexAttribDivisor(999, 1);
    expect_error("glVertexAttribDivisor(999)", GL_INVALID_VALUE);
    glGetVertexAttribiv(999, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &attrib_enabled);
    expect_error("glGetVertexAttribiv(999)", GL_INVALID_VALUE);
    glGetVertexAttribfv(999, GL_CURRENT_VERTEX_ATTRIB, current_attrib);
    expect_error("glGetVertexAttribfv(999)", GL_INVALID_VALUE);
    glGetVertexAttribfv(0, GL_INVALID_ENUM, current_attrib);
    expect_error("glGetVertexAttribfv(GL_INVALID_ENUM)", GL_INVALID_ENUM);
    glGetVertexAttribPointerv(0, GL_INVALID_ENUM, &attrib_pointer);
    expect_error("glGetVertexAttribPointerv(GL_INVALID_ENUM)", GL_INVALID_ENUM);
    glDisableVertexAttribArray(0);
    check_gl_error("glDisableVertexAttribArray");
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &attrib_enabled);
    check_gl_error("glGetVertexAttribiv(GL_VERTEX_ATTRIB_ARRAY_ENABLED disabled)");
    if (attrib_enabled == GL_FALSE) {
        OSReport("[PASS] glGetVertexAttribiv(GL_VERTEX_ATTRIB_ARRAY_ENABLED disabled) returned disabled.\n");
    } else {
        OSReport("[FAIL] glGetVertexAttribiv(GL_VERTEX_ATTRIB_ARRAY_ENABLED disabled) returned %d\n",
                 attrib_enabled);
    }
    glEnableVertexAttribArray(0);
    check_gl_error("glEnableVertexAttribArray(restore)");
    
    // Framebuffer tests next

    OSReport("-> Testing Framebuffers (Phase 8)...\n");
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    check_gl_error("glGenFramebuffers");
    if (glIsFramebuffer(fbo) == GL_TRUE) {
        OSReport("[PASS] glIsFramebuffer returned true.\n");
    } else {
        OSReport("[FAIL] glIsFramebuffer returned false.\n");
    }
    check_gl_error("glIsFramebuffer");
    
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    check_gl_error("glBindFramebuffer");
    
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex[0], 0);
    check_gl_error("glFramebufferTexture2D");

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, tex[1], 0);
    check_gl_error("glFramebufferTexture2D(color1)");
    GLint color0_object_type = 0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,
                                          &color0_object_type);
    check_gl_error("glGetFramebufferAttachmentParameteriv(color0 type)");
    if (color0_object_type == GL_TEXTURE) {
        OSReport("[PASS] glGetFramebufferAttachmentParameteriv(color0 type) returned GL_TEXTURE.\n");
    } else {
        OSReport("[FAIL] glGetFramebufferAttachmentParameteriv(color0 type) returned 0x%04X\n",
                 color0_object_type);
    }
    GLint color0_object_name = 0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
                                          &color0_object_name);
    check_gl_error("glGetFramebufferAttachmentParameteriv(color0 name)");
    if (color0_object_name == (GLint)tex[0]) {
        OSReport("[PASS] glGetFramebufferAttachmentParameteriv(color0 name) returned texture id.\n");
    } else {
        OSReport("[FAIL] glGetFramebufferAttachmentParameteriv(color0 name) returned %d\n",
                 color0_object_name);
    }

    GLenum mrt_bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, mrt_bufs);
    check_gl_error("glDrawBuffers(MRT)");

    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    check_gl_error("glDrawBuffer(GL_COLOR_ATTACHMENT0)");
    glDrawBuffers(2, mrt_bufs);
    check_gl_error("glDrawBuffers(MRT restore)");

    GLenum remap_bufs[2] = { GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT0 };
    glDrawBuffers(2, remap_bufs);
    expect_error("glDrawBuffers(remap_unsupported)", GL_INVALID_OPERATION);
    
    // Try bad attachment
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_INVALID_ENUM, GL_TEXTURE_2D, tex[0], 0);
    expect_error("glFramebufferTexture2D(GL_INVALID_ENUM attachment)", GL_INVALID_ENUM);
    
    // Try bad target
    glBindFramebuffer(GL_INVALID_ENUM, fbo);
    expect_error("glBindFramebuffer(GL_INVALID_ENUM)", GL_INVALID_ENUM);

    glReadBuffer(GL_COLOR_ATTACHMENT1);
    check_gl_error("glReadBuffer(GL_COLOR_ATTACHMENT1)");
    glClearColor(0.2f, 0.4f, 0.6f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    check_gl_error("glClear(FBO color)");
    GLubyte readback_rgba[4] = {0, 0, 0, 0};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, readback_rgba);
    if (readback_rgba[0] == 51 && readback_rgba[1] == 102 &&
        readback_rgba[2] == 153 && readback_rgba[3] == 255) {
        OSReport("[PASS] glReadPixels(FBO rgba8) returned expected data.\n");
    } else {
        OSReport("[FAIL] glReadPixels(FBO rgba8) returned {%u, %u, %u, %u}\n",
                 readback_rgba[0], readback_rgba[1], readback_rgba[2],
                 readback_rgba[3]);
    }
    check_gl_error("glReadPixels(FBO rgba8)");
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    check_gl_error("glClear(FBO scissor reset)");
    glEnable(GL_SCISSOR_TEST);
    check_gl_error("glEnable(GL_SCISSOR_TEST clip)");
    glScissor(1, 1, 2, 2);
    check_gl_error("glScissor(clear_clip)");
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    check_gl_error("glClear(FBO scissor)");
    GLubyte scissor_readback[4 * 4 * 4] = {0};
    glReadPixels(0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, scissor_readback);
    check_gl_error("glReadPixels(FBO scissor)");
    if (scissor_readback[(0 * 4 + 0) * 4 + 1] == 0 &&
        scissor_readback[(1 * 4 + 1) * 4 + 1] == 255 &&
        scissor_readback[(2 * 4 + 2) * 4 + 1] == 255 &&
        scissor_readback[(3 * 4 + 3) * 4 + 1] == 0) {
        OSReport("[PASS] glScissor(clear_clip) returned expected data.\n");
    } else {
        OSReport("[FAIL] glScissor(clear_clip) returned unexpected green channel values {%u, %u, %u, %u}\n",
                 scissor_readback[(0 * 4 + 0) * 4 + 1],
                 scissor_readback[(1 * 4 + 1) * 4 + 1],
                 scissor_readback[(2 * 4 + 2) * 4 + 1],
                 scissor_readback[(3 * 4 + 3) * 4 + 1]);
    }
    glDisable(GL_SCISSOR_TEST);
    check_gl_error("glDisable(GL_SCISSOR_TEST clip)");
    glScissor(default_scissor_box[0], default_scissor_box[1],
              default_scissor_box[2], default_scissor_box[3]);
    check_gl_error("glScissor(restore_default)");
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, readback_rgba);
    expect_error("glReadPixels(out_of_bounds)", GL_INVALID_VALUE);
    glReadBuffer(GL_BACK);
    expect_error("glReadBuffer(GL_BACK on FBO)", GL_INVALID_OPERATION);
    glReadBuffer(GL_INVALID_ENUM);
    expect_error("glReadBuffer(GL_INVALID_ENUM)", GL_INVALID_ENUM);
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0); // Back to default
    glReadBuffer(GL_BACK);
    check_gl_error("glReadBuffer(default_back)");
    glDrawBuffer(GL_BACK);
    check_gl_error("glDrawBuffer(default_back)");
    GLenum back_buf = GL_BACK;
    glDrawBuffers(1, &back_buf);
    check_gl_error("glDrawBuffers(default_back)");
    GLenum invalid_default_bufs[2] = { GL_BACK, GL_NONE };
    glDrawBuffers(2, invalid_default_bufs);
    expect_error("glDrawBuffers(default_invalid_count)", GL_INVALID_OPERATION);

    GLuint renderbuffers[2] = {0, 0};
    glGenRenderbuffers(2, renderbuffers);
    check_gl_error("glGenRenderbuffers");
    if (glIsRenderbuffer(renderbuffers[0]) == GL_TRUE) {
        OSReport("[PASS] glIsRenderbuffer returned true.\n");
    } else {
        OSReport("[FAIL] glIsRenderbuffer returned false.\n");
    }
    check_gl_error("glIsRenderbuffer");
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffers[0]);
    check_gl_error("glBindRenderbuffer(color)");
    glBindRenderbuffer(GL_INVALID_ENUM, renderbuffers[0]);
    expect_error("glBindRenderbuffer(GL_INVALID_ENUM)", GL_INVALID_ENUM);
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffers[0]);
    check_gl_error("glBindRenderbuffer(color restore)");
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 4, 4);
    check_gl_error("glRenderbufferStorage(color)");
    GLint renderbuffer_width = 0;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH,
                                 &renderbuffer_width);
    check_gl_error("glGetRenderbufferParameteriv(GL_RENDERBUFFER_WIDTH)");
    if (renderbuffer_width == 4) {
        OSReport("[PASS] glGetRenderbufferParameteriv(GL_RENDERBUFFER_WIDTH) returned 4.\n");
    } else {
        OSReport("[FAIL] glGetRenderbufferParameteriv(GL_RENDERBUFFER_WIDTH) returned %d\n",
                 renderbuffer_width);
    }
    glRenderbufferStorage(GL_RENDERBUFFER, GL_INVALID_ENUM, 4, 4);
    expect_error("glRenderbufferStorage(GL_INVALID_ENUM)", GL_INVALID_ENUM);
    glGetRenderbufferParameteriv(GL_INVALID_ENUM, GL_RENDERBUFFER_WIDTH,
                                 &renderbuffer_width);
    expect_error("glGetRenderbufferParameteriv(GL_INVALID_ENUM)", GL_INVALID_ENUM);
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffers[1]);
    check_gl_error("glBindRenderbuffer(depth_stencil)");
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 4, 4);
    check_gl_error("glRenderbufferStorage(depth_stencil)");

    GLuint renderbuffer_fbo;
    glGenFramebuffers(1, &renderbuffer_fbo);
    check_gl_error("glGenFramebuffers(renderbuffer)");
    glBindFramebuffer(GL_FRAMEBUFFER, renderbuffer_fbo);
    check_gl_error("glBindFramebuffer(renderbuffer)");
    GLenum renderbuffer_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (renderbuffer_status == GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT) {
        OSReport("[PASS] glCheckFramebufferStatus(empty_renderbuffer_fbo) returned missing attachment.\n");
    } else {
        OSReport("[FAIL] glCheckFramebufferStatus(empty_renderbuffer_fbo) returned 0x%04X\n",
                 renderbuffer_status);
    }
    check_gl_error("glCheckFramebufferStatus(empty_renderbuffer_fbo)");
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_RENDERBUFFER, renderbuffers[0]);
    check_gl_error("glFramebufferRenderbuffer(color)");
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, renderbuffers[1]);
    check_gl_error("glFramebufferRenderbuffer(depth_stencil)");
    GLint renderbuffer_attachment_type = 0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,
                                          &renderbuffer_attachment_type);
    check_gl_error("glGetFramebufferAttachmentParameteriv(renderbuffer type)");
    GLint renderbuffer_attachment_name = 0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
                                          &renderbuffer_attachment_name);
    check_gl_error("glGetFramebufferAttachmentParameteriv(renderbuffer name)");
    if (renderbuffer_attachment_type == GL_RENDERBUFFER &&
        renderbuffer_attachment_name == (GLint)renderbuffers[0]) {
        OSReport("[PASS] glGetFramebufferAttachmentParameteriv(renderbuffer) returned renderbuffer id.\n");
    } else {
        OSReport("[FAIL] glGetFramebufferAttachmentParameteriv(renderbuffer) returned type=0x%04X name=%d\n",
                 renderbuffer_attachment_type, renderbuffer_attachment_name);
    }
    renderbuffer_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (renderbuffer_status == GL_FRAMEBUFFER_COMPLETE) {
        OSReport("[PASS] glCheckFramebufferStatus(renderbuffer_fbo) returned complete.\n");
    } else {
        OSReport("[FAIL] glCheckFramebufferStatus(renderbuffer_fbo) returned 0x%04X\n",
                 renderbuffer_status);
    }
    check_gl_error("glCheckFramebufferStatus(renderbuffer_fbo)");
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    check_gl_error("glDrawBuffer(renderbuffer_fbo)");
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    check_gl_error("glReadBuffer(renderbuffer_fbo)");
    glClearColor(0.3f, 0.1f, 0.5f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    check_gl_error("glClear(renderbuffer_fbo)");
    GLubyte renderbuffer_readback[4] = {0, 0, 0, 0};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, renderbuffer_readback);
    if (renderbuffer_readback[0] == 77 && renderbuffer_readback[1] == 26 &&
        renderbuffer_readback[2] == 128 && renderbuffer_readback[3] == 255) {
        OSReport("[PASS] glReadPixels(renderbuffer_fbo) returned expected data.\n");
    } else {
        OSReport("[FAIL] glReadPixels(renderbuffer_fbo) returned {%u, %u, %u, %u}\n",
                 renderbuffer_readback[0], renderbuffer_readback[1],
                 renderbuffer_readback[2], renderbuffer_readback[3]);
    }
    check_gl_error("glReadPixels(renderbuffer_fbo)");
    glCheckFramebufferStatus(GL_INVALID_ENUM);
    expect_error("glCheckFramebufferStatus(GL_INVALID_ENUM)", GL_INVALID_ENUM);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    check_gl_error("glBindFramebuffer(renderbuffer restore_default)");

    OSReport("-> Testing Draw Calls & Synchronization (Phase 9/10)...\n");
    
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glBindVertexArray(vao);
    // Draw with no program
    glUseProgram(0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    expect_error("glDrawArrays(unbound_program)", GL_INVALID_OPERATION);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 3, 2);
    expect_error("glDrawArraysInstanced(unbound_program)", GL_INVALID_OPERATION);

    glFlush();
    check_gl_error("glFlush");
    
    glFinish();
    check_gl_error("glFinish");

    OSReport("-> Testing Deletions...\n");
    glDeleteVertexArrays(1, &vao);
    check_gl_error("glDeleteVertexArrays");
    
    glDeleteRenderbuffers(2, renderbuffers);
    check_gl_error("glDeleteRenderbuffers");
    glDeleteSamplers(1, &sampler);
    check_gl_error("glDeleteSamplers");
    glDeleteFramebuffers(1, &pixel_store_fbo);
    check_gl_error("glDeleteFramebuffers(pixel_store)");
    glDeleteFramebuffers(1, &renderbuffer_fbo);
    check_gl_error("glDeleteFramebuffers(renderbuffer)");
    glDeleteFramebuffers(1, &fbo);
    check_gl_error("glDeleteFramebuffers");
    GLuint delete_textures[12] = { tex[0], tex[1], tex3d, pixel_store_tex,
                                   es2_textures[0], es2_textures[1],
                                   es2_textures[2], copy_textures[0],
                                   copy_textures[1], copy_textures[2],
                                   copy_textures[3], copy_textures[4] };
    glDeleteTextures(12, delete_textures);
    check_gl_error("glDeleteTextures");

    OSReport("----- Comprehensive Test Sequence With Failure Coverage Complete! -----\n");

    draw_diagnostic_screen();
    gl_context_destroy(g_gl_context);
    g_gl_context = NULL;
    gl_mem_shutdown();
    
    WHBGfxShutdown();
    WHBProcShutdown();
    return 0;
}
