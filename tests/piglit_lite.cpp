#include "piglit_lite.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gl/gl.h"

namespace {

struct PiglitUniform {
    const char *name;
    enum Type {
        Int,
        Vec4,
    } type;
    GLint int_value;
    GLfloat vec4_value[4];
};

struct PiglitShaderCase {
    const char *name;
    const char *piglit_case;
    const char *fragment_source;
    PiglitUniform uniforms[2];
    int uniform_count;
    GLfloat expected[4];
};

static const char *kFullscreenVertexShader =
    "#version 330 core\n"
    "layout(location = 0) in vec2 aPosition;\n"
    "void main() {\n"
    "    gl_Position = vec4(aPosition, 0.0, 1.0);\n"
    "}\n";

static const PiglitShaderCase kShaderCases[] = {
    {
        "shaders/ssa/fs-lost-copy-problem",
        "tests/shaders/ssa/fs-lost-copy-problem.shader_test",
        "#version 330 core\n"
        "layout(location = 0) out vec4 FragColor;\n"
        "uniform int count;\n"
        "void main() {\n"
        "    int j = 0;\n"
        "    for (int i = 0; i <= count; ++i) {\n"
        "        j = i;\n"
        "    }\n"
        "    if (j == 6)\n"
        "        FragColor = vec4(0.0, 1.0, 0.0, 1.0);\n"
        "    else\n"
        "        FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
        "}\n",
        {{"count", PiglitUniform::Int, 6, {0.0f, 0.0f, 0.0f, 0.0f}}},
        1,
        {0.0f, 1.0f, 0.0f, 1.0f},
    },
    {
        "shaders/ssa/fs-swap-problem",
        "tests/shaders/ssa/fs-swap-problem.shader_test",
        "#version 330 core\n"
        "layout(location = 0) out vec4 FragColor;\n"
        "uniform bool f;\n"
        "void main() {\n"
        "    FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
        "    do {\n"
        "        FragColor.xy = FragColor.yx;\n"
        "    } while (f);\n"
        "}\n",
        {{"f", PiglitUniform::Int, 0, {0.0f, 0.0f, 0.0f, 0.0f}}},
        1,
        {0.0f, 1.0f, 0.0f, 1.0f},
    },
    {
        "shaders/ssa/fs-while-loop-rotate-value",
        "tests/shaders/ssa/fs-while-loop-rotate-value.shader_test",
        "#version 330 core\n"
        "layout(location = 0) out vec4 FragColor;\n"
        "uniform int count;\n"
        "uniform vec4 init_val;\n"
        "void main() {\n"
        "    vec4 out_val = init_val;\n"
        "    int i = 0;\n"
        "    while (i++ < count) {\n"
        "        out_val = out_val.yzwx;\n"
        "    }\n"
        "    FragColor = vec4(out_val.xyz, 1.0);\n"
        "}\n",
        {{"count", PiglitUniform::Int, 3, {0.0f, 0.0f, 0.0f, 0.0f}},
         {"init_val", PiglitUniform::Vec4, 0, {0.25f, 0.5f, 0.75f, 1.0f}}},
        2,
        {1.0f, 0.25f, 0.5f, 1.0f},
    },
};

static void clear_gl_errors(void) {
    while (glGetError() != GL_NO_ERROR) {
    }
}

static bool check_no_error(PiglitReportFunc report, const char *label) {
    GLenum error = glGetError();
    if (error == GL_NO_ERROR) {
        return true;
    }

    report("[FAIL] Piglit %s produced GL error 0x%04X\n", label, error);
    return false;
}

static GLuint compile_shader(PiglitReportFunc report, GLenum type,
                             const char *source, const char *case_name) {
    GLuint shader = glCreateShader(type);
    GLint status = GL_FALSE;
    char info_log[512];
    GLsizei info_length = 0;

    if (shader == 0 || !check_no_error(report, "glCreateShader")) {
        return 0;
    }

    glShaderSource(shader, 1, &source, NULL);
    if (!check_no_error(report, "glShaderSource")) {
        glDeleteShader(shader);
        return 0;
    }

    glCompileShader(shader);
    if (!check_no_error(report, "glCompileShader")) {
        glDeleteShader(shader);
        return 0;
    }

    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!check_no_error(report, "glGetShaderiv(GL_COMPILE_STATUS)")) {
        glDeleteShader(shader);
        return 0;
    }
    if (status == GL_TRUE) {
        return shader;
    }

    memset(info_log, 0, sizeof(info_log));
    glGetShaderInfoLog(shader, sizeof(info_log), &info_length, info_log);
    report("[FAIL] Piglit %s shader compile failed: %s\n", case_name,
           info_log);
    glDeleteShader(shader);
    return 0;
}

static GLuint link_program(PiglitReportFunc report, GLuint vertex_shader,
                           GLuint fragment_shader, const char *case_name) {
    GLuint program = glCreateProgram();
    GLint status = GL_FALSE;
    char info_log[512];
    GLsizei info_length = 0;

    if (program == 0 || !check_no_error(report, "glCreateProgram")) {
        return 0;
    }

    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    if (!check_no_error(report, "glAttachShader")) {
        glDeleteProgram(program);
        return 0;
    }

    glLinkProgram(program);
    if (!check_no_error(report, "glLinkProgram")) {
        glDeleteProgram(program);
        return 0;
    }

    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!check_no_error(report, "glGetProgramiv(GL_LINK_STATUS)")) {
        glDeleteProgram(program);
        return 0;
    }
    if (status == GL_TRUE) {
        return program;
    }

    memset(info_log, 0, sizeof(info_log));
    glGetProgramInfoLog(program, sizeof(info_log), &info_length, info_log);
    report("[FAIL] Piglit %s program link failed: %s\n", case_name,
           info_log);
    glDeleteProgram(program);
    return 0;
}

static bool probe_all_rgba(PiglitReportFunc report,
                           const PiglitShaderCase &test_case,
                           int width, int height) {
    GLubyte pixels[4 * 4 * 4];
    const int tolerance = 3;

    memset(pixels, 0, sizeof(pixels));
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    if (!check_no_error(report, "glReadPixels")) {
        return false;
    }

    for (int i = 0; i < width * height; ++i) {
        const GLubyte *p = pixels + i * 4;
        for (int c = 0; c < 4; ++c) {
            int expected = (int)(test_case.expected[c] * 255.0f + 0.5f);
            int diff = (int)p[c] - expected;
            if (diff < 0) {
                diff = -diff;
            }
            if (diff > tolerance) {
                report("[FAIL] Piglit %s probe pixel %d returned {%u,%u,%u,%u}, expected {%d,%d,%d,%d}\n",
                       test_case.name, i, p[0], p[1], p[2], p[3],
                       (int)(test_case.expected[0] * 255.0f + 0.5f),
                       (int)(test_case.expected[1] * 255.0f + 0.5f),
                       (int)(test_case.expected[2] * 255.0f + 0.5f),
                       (int)(test_case.expected[3] * 255.0f + 0.5f));
                return false;
            }
        }
    }

    report("[PASS] Piglit %s matched probe all rgba (%s).\n",
           test_case.name, test_case.piglit_case);
    return true;
}

static bool run_shader_case(PiglitReportFunc report,
                            const PiglitShaderCase &test_case) {
    static const GLfloat vertices[] = {
        -1.0f, -1.0f,
         3.0f, -1.0f,
        -1.0f,  3.0f,
    };
    GLuint vertex_shader = 0;
    GLuint fragment_shader = 0;
    GLuint program = 0;
    GLuint fbo = 0;
    GLuint texture = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    bool ok = false;

    clear_gl_errors();

    vertex_shader = compile_shader(report, GL_VERTEX_SHADER,
                                   kFullscreenVertexShader, test_case.name);
    fragment_shader = compile_shader(report, GL_FRAGMENT_SHADER,
                                     test_case.fragment_source,
                                     test_case.name);
    if (vertex_shader == 0 || fragment_shader == 0) {
        goto cleanup;
    }

    program = link_program(report, vertex_shader, fragment_shader,
                           test_case.name);
    if (program == 0) {
        goto cleanup;
    }

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           texture, 0);
    if (!check_no_error(report, "FBO setup")) {
        goto cleanup;
    }
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        report("[FAIL] Piglit %s framebuffer was incomplete.\n",
               test_case.name);
        goto cleanup;
    }

    glViewport(0, 0, 4, 4);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(program);
    for (int i = 0; i < test_case.uniform_count; ++i) {
        const PiglitUniform &uniform = test_case.uniforms[i];
        GLint location = glGetUniformLocation(program, uniform.name);
        if (location < 0) {
            report("[FAIL] Piglit %s missing uniform '%s'.\n",
                   test_case.name, uniform.name);
            goto cleanup;
        }
        if (uniform.type == PiglitUniform::Int) {
            glUniform1i(location, uniform.int_value);
        } else {
            glUniform4fv(location, 1, uniform.vec4_value);
        }
    }
    if (!check_no_error(report, "uniform setup")) {
        goto cleanup;
    }

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), 0);
    glEnableVertexAttribArray(0);
    if (!check_no_error(report, "vertex setup")) {
        goto cleanup;
    }

    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (!check_no_error(report, "glDrawArrays")) {
        goto cleanup;
    }
    glFinish();
    ok = probe_all_rgba(report, test_case, 4, 4);

cleanup:
    glUseProgram(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (vbo != 0) {
        glDeleteBuffers(1, &vbo);
    }
    if (vao != 0) {
        glDeleteVertexArrays(1, &vao);
    }
    if (fbo != 0) {
        glDeleteFramebuffers(1, &fbo);
    }
    if (texture != 0) {
        glDeleteTextures(1, &texture);
    }
    if (program != 0) {
        glDeleteProgram(program);
    }
    if (vertex_shader != 0) {
        glDeleteShader(vertex_shader);
    }
    if (fragment_shader != 0) {
        glDeleteShader(fragment_shader);
    }

    return ok;
}

} // namespace

PiglitRunStats run_piglit_tests(PiglitReportFunc report,
                                PiglitResultFunc result_func,
                                void *user_data) {
    PiglitRunStats stats = {};

    if (!report) {
        return stats;
    }

    for (size_t i = 0; i < sizeof(kShaderCases) / sizeof(kShaderCases[0]); ++i) {
        const bool passed = run_shader_case(report, kShaderCases[i]);
        const PiglitResult result = passed ? PIGLIT_RESULT_PASS
                                           : PIGLIT_RESULT_FAIL;

        ++stats.total;
        if (passed) {
            ++stats.pass;
        } else {
            ++stats.fail;
        }
        if (result_func) {
            result_func(kShaderCases[i].name, result,
                        passed ? "probe matched" : "test failed",
                        user_data);
        }
    }

    return stats;
}
