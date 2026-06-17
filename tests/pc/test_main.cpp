#ifdef GX2GL_COMPARE_WIIU
#include <whb/gfx.h>
#include <whb/proc.h>

#include "core/gl_context.h"
#include "mem/gl_mem.h"
#else
#include "desktop_gl_bindings.h"
#include "desktop_wgl_platform.h"
#endif

#ifdef GX2GL_COMPARE_WIIU
static void platform_prepare_default_readback(void) {
    WHBGfxFinishRenderTV();
    WHBGfxFinishRender();
}

static void platform_resume_after_default_readback(void) {
    WHBGfxBeginRender();
    WHBGfxBeginRenderTV();
}
#else
static void platform_prepare_default_readback(void) {}
static void platform_resume_after_default_readback(void) {}
#endif

#include "gl/gl.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef GL_POINT_SIZE
#define GL_POINT_SIZE 0x0B11
#endif
#ifndef GL_POLYGON_OFFSET_FACTOR
#define GL_POLYGON_OFFSET_FACTOR 0x8038
#endif
#ifndef GL_POLYGON_OFFSET_UNITS
#define GL_POLYGON_OFFSET_UNITS 0x2A00
#endif
#ifndef GL_UNIFORM_TYPE
#define GL_UNIFORM_TYPE 0x8A37
#endif
#ifndef GL_SYNC_STATUS
#define GL_SYNC_STATUS 0x9114
#endif

static int g_pass_count = 0;
static int g_expected_error_pass_count = 0;
static int g_fail_count = 0;
static char g_failure_lines[64][256];
static unsigned int g_failure_line_count = 0;

#ifdef GX2GL_COMPARE_WIIU
static void write_wiiu_results_file(const char *status) {
    FILE *fp = std::fopen("/vol/external01/gx2gl_results.txt", "wb");
    if (!fp) {
        return;
    }

    std::fprintf(fp, "status=%s\n", status ? status : "unknown");
    std::fprintf(fp, "pass=%d\n", g_pass_count);
    std::fprintf(fp, "expected_error_pass=%d\n", g_expected_error_pass_count);
    std::fprintf(fp, "negative_pass=%d\n", g_expected_error_pass_count);
    std::fprintf(fp, "fail=%d\n", g_fail_count);
    for (unsigned int i = 0; i < g_failure_line_count; ++i) {
        std::fprintf(fp, "failure[%u]=%s\n", i, g_failure_lines[i]);
    }
    std::fclose(fp);
}

static void write_wiiu_done_flag(void) {
    FILE *fp = std::fopen("/vol/external01/gx2gl_done.flag", "wb");
    if (!fp) {
        return;
    }

    std::fwrite("done\n", 1, 5, fp);
    std::fclose(fp);
}
#endif

static void report(const char *tag, const char *message) {
    std::printf("%s %s\n", tag, message);
    if (tag && message && std::strcmp(tag, "[FAIL]") == 0 &&
        g_failure_line_count < (sizeof(g_failure_lines) / sizeof(g_failure_lines[0]))) {
        std::snprintf(g_failure_lines[g_failure_line_count],
                      sizeof(g_failure_lines[g_failure_line_count]),
                      "%s", message);
        ++g_failure_line_count;
    }
}

static void pass(const char *message) {
    ++g_pass_count;
    report("[PASS]", message);
}

static void fail(const char *message) {
    ++g_fail_count;
    report("[FAIL]", message);
}

static void check_gl_error(const char *label) {
    GLenum err = glGetError();
    if (err == GL_NO_ERROR) {
        std::string message = std::string(label) + " completed successfully.";
        pass(message.c_str());
    } else {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), "%s produced error 0x%04X.", label, (unsigned int)err);
        fail(buffer);
    }
}

static void expect_error(const char *label, GLenum expected) {
    GLenum actual = glGetError();
    if (actual == expected) {
        ++g_expected_error_pass_count;
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), "%s produced expected 0x%04X.", label, (unsigned int)expected);
        report("[PASS-EXPECTED-ERROR]", buffer);
    } else {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), "%s expected 0x%04X but got 0x%04X.",
                      label, (unsigned int)expected, (unsigned int)actual);
        fail(buffer);
    }
}

static bool rgba_equals(const GLubyte *rgba, GLubyte r, GLubyte g, GLubyte b, GLubyte a) {
    return rgba[0] == r && rgba[1] == g && rgba[2] == b && rgba[3] == a;
}

static bool approx_equalf(GLfloat a, GLfloat b, GLfloat epsilon) {
    GLfloat delta = a - b;
    if (delta < 0.0f) {
        delta = -delta;
    }
    return delta <= epsilon;
}

static bool approx_equald(GLdouble a, GLdouble b, GLdouble epsilon) {
    GLdouble delta = a - b;
    if (delta < 0.0) {
        delta = -delta;
    }
    return delta <= epsilon;
}

static bool write_ppm(const char *path, int width, int height, const GLubyte *rgba) {
    FILE *fp = std::fopen(path, "wb");
    if (!fp) {
        return false;
    }

    std::fprintf(fp, "P6\n%d %d\n255\n", width, height);
    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            const GLubyte *src = rgba + ((y * width) + x) * 4;
            std::fwrite(src, 1, 3, fp);
        }
    }

    std::fclose(fp);
    return true;
}

static void build_complex_scene_pixels(int width, int height, std::vector<GLubyte> *pixels) {
    pixels->resize((size_t)width * (size_t)height * 4u);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int cx = x - width / 2;
            int cy = y - height / 2;
            int ring = (cx * cx * 5 + cy * cy * 13) / 97;
            int checker = ((x / 8) ^ (y / 8)) & 1;
            int diagonal = ((x * 3 + y * 5) / 17) & 7;
            int fine = ((x * 29) ^ (y * 43) ^ ((x * y) >> 2)) & 255;
            GLubyte *dst = pixels->data() + ((size_t)y * (size_t)width + (size_t)x) * 4u;

            int r = (x * 5 + y * 2 + fine + checker * 53) & 255;
            int g = (x * 2 + y * 7 + ring * 11 + diagonal * 13) & 255;
            int b = (x * 11 + y * 3 + (fine >> 1) + checker * 91) & 255;

            if ((x % 32) < 2 || (y % 24) < 2) {
                r = (r + 90) & 255;
                g = (g + 120) & 255;
                b = (b + 150) & 255;
            }
            if ((ring & 31) < 3) {
                r = 255 - r;
                g = (g + 64) & 255;
                b = 255 - b;
            }

            dst[0] = (GLubyte)r;
            dst[1] = (GLubyte)g;
            dst[2] = (GLubyte)b;
            dst[3] = 255;
        }
    }
}

static GLuint compile_shader(GLenum type, const char *source, const char *label) {
    GLuint shader = glCreateShader(type);
    GLint compiled = GL_FALSE;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        GLchar info_log[1024];
        GLsizei info_length = 0;
        glGetShaderInfoLog(shader, (GLsizei)sizeof(info_log), &info_length, info_log);
        char buffer[1280];
        std::snprintf(buffer, sizeof(buffer), "%s failed to compile: %.*s",
                      label, (int)info_length, info_log);
        fail(buffer);
    } else {
        std::string message = std::string(label) + " compiled.";
        pass(message.c_str());
    }

    return shader;
}

static GLuint build_scene_program(void) {
    static const char *kVertexSource =
        "#version 330 core\n"
        "layout(location = 0) in vec2 aPosition;\n"
        "layout(location = 1) in vec2 aTexCoord;\n"
        "layout(location = 0) out vec2 vTexCoord;\n"
        "void main() {\n"
        "  vTexCoord = aTexCoord;\n"
        "  gl_Position = vec4(aPosition, 0.0, 1.0);\n"
        "}\n";
    static const char *kFragmentSource =
        "#version 330 core\n"
        "layout(location = 0) in vec2 vTexCoord;\n"
        "layout(binding = 0) uniform sampler2D uTexture;\n"
        "layout(location = 0) out vec4 FragColor;\n"
        "void main() {\n"
        "  FragColor = texture(uTexture, vTexCoord);\n"
        "}\n";

    GLuint vs = compile_shader(GL_VERTEX_SHADER, kVertexSource, "scene vertex shader");
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, kFragmentSource, "scene fragment shader");
    GLuint program = glCreateProgram();
    GLint linked = GL_FALSE;

    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "aPosition");
    glBindAttribLocation(program, 1, "aTexCoord");
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLchar info_log[1024];
        GLsizei info_length = 0;
        glGetProgramInfoLog(program, (GLsizei)sizeof(info_log), &info_length, info_log);
        char buffer[1280];
        std::snprintf(buffer, sizeof(buffer), "scene program failed to link: %.*s",
                      (int)info_length, info_log);
        fail(buffer);
    } else {
        pass("scene program linked.");
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

static bool render_complex_scene_to_window_and_file(const char *reference_ppm_path) {
    static const int kSceneWidth = 256;
    static const int kSceneHeight = 144;
    static const GLfloat kQuadVertices[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
    };

    std::vector<GLubyte> source_pixels;
    std::vector<GLubyte> rendered_pixels((size_t)kSceneWidth * (size_t)kSceneHeight * 4u);
    GLuint scene_program = 0;
    GLuint scene_texture = 0;
    GLuint target_texture = 0;
    GLuint scene_fbo = 0;
    GLuint scene_vao = 0;
    GLuint scene_vbo = 0;
    GLint position_location = -1;
    GLint texcoord_location = -1;
    GLint texture_location = -1;
    bool ok = true;

    build_complex_scene_pixels(kSceneWidth, kSceneHeight, &source_pixels);
    scene_program = build_scene_program();

    glGenTextures(1, &scene_texture);
    glBindTexture(GL_TEXTURE_2D, scene_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSceneWidth, kSceneHeight, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, source_pixels.data());
    check_gl_error("complex scene source texture upload");

    glGenTextures(1, &target_texture);
    glBindTexture(GL_TEXTURE_2D, target_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSceneWidth, kSceneHeight, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glGenFramebuffers(1, &scene_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target_texture, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    check_gl_error("complex scene framebuffer setup");
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        pass("complex scene framebuffer status is complete.");
    } else {
        fail("complex scene framebuffer status is not complete.");
        ok = false;
    }
    check_gl_error("complex scene glCheckFramebufferStatus");

    glGenVertexArrays(1, &scene_vao);
    glBindVertexArray(scene_vao);
    glGenBuffers(1, &scene_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, scene_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices, GL_STATIC_DRAW);
    position_location = glGetAttribLocation(scene_program, "aPosition");
    texcoord_location = glGetAttribLocation(scene_program, "aTexCoord");
    if (position_location < 0 || texcoord_location < 0) {
        fail("complex scene program did not expose expected attribute locations.");
        ok = false;
    } else {
        glVertexAttribPointer((GLuint)position_location, 2, GL_FLOAT, GL_FALSE,
                              sizeof(GLfloat) * 4, (const void *)0);
        glEnableVertexAttribArray((GLuint)position_location);
        glVertexAttribPointer((GLuint)texcoord_location, 2, GL_FLOAT, GL_FALSE,
                              sizeof(GLfloat) * 4, (const void *)(sizeof(GLfloat) * 2));
        glEnableVertexAttribArray((GLuint)texcoord_location);
    }
    check_gl_error("complex scene VAO setup");

    glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo);
    glViewport(0, 0, kSceneWidth, kSceneHeight);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(scene_program);
    texture_location = glGetUniformLocation(scene_program, "uTexture");
    glUniform1i(texture_location, 0);
    glBindTexture(GL_TEXTURE_2D, scene_texture);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glFinish();
    check_gl_error("complex scene draw");

    glReadPixels(0, 0, kSceneWidth, kSceneHeight, GL_RGBA, GL_UNSIGNED_BYTE, rendered_pixels.data());
    check_gl_error("complex scene readback");
    {
        bool has_nonzero_pixel = false;
        for (size_t i = 0; i < rendered_pixels.size(); i += 4u) {
            if (rendered_pixels[i] || rendered_pixels[i + 1u] || rendered_pixels[i + 2u]) {
                has_nonzero_pixel = true;
                break;
            }
        }
        if (has_nonzero_pixel) {
            pass("complex scene GPU output was non-empty.");
        } else {
            fail("complex scene GPU output was blank.");
            ok = false;
        }
    }

    if (write_ppm(reference_ppm_path, kSceneWidth, kSceneHeight, rendered_pixels.data())) {
        std::string message = std::string("wrote complex window output ") + reference_ppm_path + ".";
        pass(message.c_str());
    } else {
        std::string message = std::string("failed to write complex window output ") + reference_ppm_path + ".";
        fail(message.c_str());
        ok = false;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, scene_fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK);
    glViewport(0, 0, kSceneWidth, kSceneHeight);
    glBlitFramebuffer(0, 0, kSceneWidth, kSceneHeight,
                       0, 0, kSceneWidth, kSceneHeight,
                       GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glFinish();
    check_gl_error("complex scene window blit");

    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDeleteBuffers(1, &scene_vbo);
    glDeleteVertexArrays(1, &scene_vao);
    glDeleteFramebuffers(1, &scene_fbo);
    glDeleteTextures(1, &target_texture);
    glDeleteTextures(1, &scene_texture);
    glDeleteProgram(scene_program);
    check_gl_error("complex scene cleanup");

    return ok;
}

static GLuint build_reflect_program(void) {
    static const char *kVertexSource =
        "#version 330 core\n"
        "layout(location = 0) in vec3 aPosition;\n"
        "layout(location = 1) in vec2 aTexCoord;\n"
        "uniform mat4 uModelView;\n"
        "uniform ivec2 uViewportSize;\n"
        "layout(location = 0) out vec2 vTexCoord;\n"
        "void main() {\n"
        "  vTexCoord = aTexCoord;\n"
        "  vec2 viewportScale = vec2(max(uViewportSize.x, 1), max(uViewportSize.y, 1));\n"
        "  gl_Position = uModelView * vec4(aPosition.xy / viewportScale, aPosition.z, 1.0);\n"
        "}\n";
    static const char *kFragmentSource =
        "#version 330 core\n"
        "layout(location = 0) in vec2 vTexCoord;\n"
        "uniform vec4 uTint;\n"
        "uniform ivec3 uLightMask;\n"
        "uniform ivec4 uMask;\n"
        "layout(binding = 0) uniform sampler2D uTexture;\n"
        "layout(location = 0) out vec4 FragColor;\n"
        "void main() {\n"
        "  vec4 texel = texture(uTexture, vTexCoord);\n"
        "  vec4 uniformProbe = vec4(float(uMask.x + uMask.y) / 4096.0,\n"
        "                           float(uLightMask.x + uLightMask.y + uLightMask.z) / 4096.0,\n"
        "                           0.0,\n"
        "                           0.0);\n"
        "  FragColor = texel + uTint + uniformProbe;\n"
        "}\n";

    GLuint vs = compile_shader(GL_VERTEX_SHADER, kVertexSource, "reflect vertex shader");
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, kFragmentSource, "reflect fragment shader");
    GLuint program = glCreateProgram();
    GLint linked = GL_FALSE;

    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "aPosition");
    glBindAttribLocation(program, 1, "aTexCoord");
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLchar info_log[1024];
        GLsizei info_length = 0;
        glGetProgramInfoLog(program, (GLsizei)sizeof(info_log), &info_length, info_log);
        char buffer[1280];
        std::snprintf(buffer, sizeof(buffer), "reflect program failed to link: %.*s",
                      (int)info_length, info_log);
        fail(buffer);
    } else {
        pass("reflect program linked.");
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

static int run_compare_suite(const char *reference_ppm_path) {
    g_pass_count = 0;
    g_expected_error_pass_count = 0;
    g_fail_count = 0;
    g_failure_line_count = 0;
    if (!reference_ppm_path) {
        reference_ppm_path = "gx2gl_reference.ppm";
    }

    const GLubyte *vendor = glGetString(GL_VENDOR);
    const GLubyte *renderer = glGetString(GL_RENDERER);
    const GLubyte *version = glGetString(GL_VERSION);
    if (vendor && renderer && version) {
        std::printf("[INFO] Vendor: %s\n", vendor);
        std::printf("[INFO] Renderer: %s\n", renderer);
        std::printf("[INFO] Version: %s\n", version);
        pass("glGetString returned non-null identity strings.");
    } else {
        fail("glGetString returned a null identity string.");
    }

    GLint gl_major = 0;
    GLint gl_minor = 0;
    GLint gl_profile = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &gl_major);
    check_gl_error("glGetIntegerv(GL_MAJOR_VERSION)");
    glGetIntegerv(GL_MINOR_VERSION, &gl_minor);
    check_gl_error("glGetIntegerv(GL_MINOR_VERSION)");
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &gl_profile);
    check_gl_error("glGetIntegerv(GL_CONTEXT_PROFILE_MASK)");
#ifdef GX2GL_COMPARE_WIIU
    if (gl_major == 0 && gl_minor == 1 && gl_profile == 0) {
        pass("version/profile getters reported gx2gl WIP surface.");
    } else {
        fail("gx2gl version/profile getters advertised a Khronos GL profile.");
    }
#else
    if (gl_major == 3 && gl_minor == 3 &&
        (gl_profile & GL_CONTEXT_CORE_PROFILE_BIT)) {
        pass("version/profile getters reported 3.3 core.");
    } else {
        fail("version/profile getters reported unexpected values.");
    }
#endif

    GLint num_extensions = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &num_extensions);
    check_gl_error("glGetIntegerv(GL_NUM_EXTENSIONS)");
    if (num_extensions > 0 && glGetStringi(GL_EXTENSIONS, 0)) {
        pass("glGetStringi(GL_EXTENSIONS, 0) returned a string.");
    } else {
        fail("glGetStringi(GL_EXTENSIONS, 0) returned null.");
    }

    glGetString(GL_EXTENSIONS);
    expect_error("glGetString(GL_EXTENSIONS)", GL_INVALID_ENUM);

#ifdef GX2GL_COMPARE_WIIU
    {
        GLuint gshader = glCreateShader(GL_GEOMETRY_SHADER);
        if (gshader == 0) {
            pass("glCreateShader(GL_GEOMETRY_SHADER) rejected unsupported CafeGLSL stage.");
        } else {
            glDeleteShader(gshader);
            fail("glCreateShader(GL_GEOMETRY_SHADER) returned an object.");
        }
        expect_error("glCreateShader(GL_GEOMETRY_SHADER)", GL_INVALID_ENUM);
    }
#endif

    GLfloat max_lod_bias = 0.0f;
    GLint max_lod_bias_i = 0;
    glGetFloatv(GL_MAX_TEXTURE_LOD_BIAS, &max_lod_bias);
    check_gl_error("glGetFloatv(GL_MAX_TEXTURE_LOD_BIAS)");
    glGetIntegerv(GL_MAX_TEXTURE_LOD_BIAS, &max_lod_bias_i);
    check_gl_error("glGetIntegerv(GL_MAX_TEXTURE_LOD_BIAS)");
    if (approx_equalf(max_lod_bias, 15.0f, 1e-6f) && max_lod_bias_i == 15) {
        pass("GL_MAX_TEXTURE_LOD_BIAS converted across getter types.");
    } else {
        fail("GL_MAX_TEXTURE_LOD_BIAS returned unexpected values.");
    }

    GLint max_combined_textures = 0;
    GLint max_combined_uniform_blocks = 0;
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &max_combined_textures);
    check_gl_error("glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS)");
    glGetIntegerv(GL_MAX_COMBINED_UNIFORM_BLOCKS, &max_combined_uniform_blocks);
    check_gl_error("glGetIntegerv(GL_MAX_COMBINED_UNIFORM_BLOCKS)");
    if (max_combined_textures >= 48 && max_combined_uniform_blocks >= 36) {
        pass("GL 3.3 combined limit queries meet core minimums.");
    } else {
        fail("combined limit queries were below GL 3.3 core minimums.");
    }

    glActiveTexture(GL_TEXTURE0 + 47);
    check_gl_error("glActiveTexture(GL_TEXTURE0 + 47)");
    GLint active_texture_query = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture_query);
    check_gl_error("glGetIntegerv(GL_ACTIVE_TEXTURE high unit)");
    if (active_texture_query == GL_TEXTURE0 + 47) {
        pass("GL_ACTIVE_TEXTURE returned the active texture enum.");
    } else {
        fail("GL_ACTIVE_TEXTURE returned an unexpected value.");
    }
    glActiveTexture(GL_TEXTURE0);
    check_gl_error("glActiveTexture(GL_TEXTURE0 restore)");

    glViewport(10, 20, 320, 180);
    glScissor(4, 8, 64, 32);
    check_gl_error("glViewport/glScissor");

    GLint viewport[4] = {0, 0, 0, 0};
    GLfloat viewport_float[4] = {0, 0, 0, 0};
    GLint scissor_box[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    check_gl_error("glGetIntegerv(GL_VIEWPORT)");
    glGetFloatv(GL_VIEWPORT, viewport_float);
    check_gl_error("glGetFloatv(GL_VIEWPORT)");
    glGetIntegerv(GL_SCISSOR_BOX, scissor_box);
    check_gl_error("glGetIntegerv(GL_SCISSOR_BOX)");

    if (viewport[0] == 10 && viewport[1] == 20 && viewport[2] == 320 && viewport[3] == 180 &&
        viewport_float[0] == 10.0f && viewport_float[1] == 20.0f &&
        viewport_float[2] == 320.0f && viewport_float[3] == 180.0f &&
        scissor_box[0] == 4 && scissor_box[1] == 8 && scissor_box[2] == 64 && scissor_box[3] == 32) {
        pass("viewport and scissor getters returned expected values.");
    } else {
        fail("viewport or scissor getters returned unexpected values.");
    }

    glClearDepthf(0.75f);
    glDepthRangef(0.25f, 0.9f);
    glBlendColor(0.1f, 0.2f, 0.3f, 0.4f);
    glPointSize(3.0f);
    glPolygonOffset(2.0f, 5.0f);
    check_gl_error("state scalar setters");

    GLdouble depth_range[4] = {0.0, 0.0, 0.0, 0.0};
    GLfloat blend_color[4] = {0, 0, 0, 0};
    GLfloat point_size = 0.0f;
    GLfloat polygon_offset_factor = 0.0f;
    GLfloat polygon_offset_units = 0.0f;
    glGetDoublev(GL_DEPTH_RANGE, depth_range);
    check_gl_error("glGetDoublev(GL_DEPTH_RANGE)");
    glGetFloatv(GL_BLEND_COLOR, blend_color);
    check_gl_error("glGetFloatv(GL_BLEND_COLOR)");
    glGetFloatv(GL_POINT_SIZE, &point_size);
    check_gl_error("glGetFloatv(GL_POINT_SIZE)");
    glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &polygon_offset_factor);
    check_gl_error("glGetFloatv(GL_POLYGON_OFFSET_FACTOR)");
    glGetFloatv(GL_POLYGON_OFFSET_UNITS, &polygon_offset_units);
    check_gl_error("glGetFloatv(GL_POLYGON_OFFSET_UNITS)");

    if (approx_equald(depth_range[0], 0.25, 1e-6) &&
        approx_equald(depth_range[1], 0.9, 1e-6) &&
        approx_equalf(blend_color[0], 0.1f, 1e-6f) &&
        approx_equalf(blend_color[1], 0.2f, 1e-6f) &&
        approx_equalf(blend_color[2], 0.3f, 1e-6f) &&
        approx_equalf(blend_color[3], 0.4f, 1e-6f) &&
        approx_equalf(point_size, 3.0f, 1e-6f) &&
        approx_equalf(polygon_offset_factor, 2.0f, 1e-6f) &&
        approx_equalf(polygon_offset_units, 5.0f, 1e-6f)) {
        pass("float/double getters returned tracked scalar state.");
    } else {
        fail("float/double getters returned unexpected scalar state.");
    }
    GLint clamp_read_color = 0;
    glGetIntegerv(GL_CLAMP_READ_COLOR, &clamp_read_color);
    check_gl_error("glGetIntegerv(GL_CLAMP_READ_COLOR default)");
    if (clamp_read_color == GL_FIXED_ONLY) {
        pass("GL_CLAMP_READ_COLOR defaulted to GL_FIXED_ONLY.");
    } else {
        fail("GL_CLAMP_READ_COLOR default was unexpected.");
    }
    glClampColor(GL_CLAMP_READ_COLOR, GL_FALSE);
    check_gl_error("glClampColor(GL_CLAMP_READ_COLOR, GL_FALSE)");
    glGetIntegerv(GL_CLAMP_READ_COLOR, &clamp_read_color);
    check_gl_error("glGetIntegerv(GL_CLAMP_READ_COLOR false)");
    if (clamp_read_color == GL_FALSE) {
        pass("glClampColor stored GL_FALSE for read clamp state.");
    } else {
        fail("glClampColor stored unexpected read clamp state.");
    }
    glClampColor(GL_CLAMP_READ_COLOR, GL_FIXED_ONLY);
    check_gl_error("glClampColor(GL_CLAMP_READ_COLOR, GL_FIXED_ONLY)");
    glClampColor(GL_CLAMP_VERTEX_COLOR, GL_TRUE);
    expect_error("glClampColor(GL_CLAMP_VERTEX_COLOR)", GL_INVALID_ENUM);
    glClampColor(GL_CLAMP_READ_COLOR, GL_INVALID_ENUM);
    expect_error("glClampColor(invalid clamp)", GL_INVALID_ENUM);
    GLint provoking_vertex = 0;
    glGetIntegerv(GL_PROVOKING_VERTEX, &provoking_vertex);
    check_gl_error("glGetIntegerv(GL_PROVOKING_VERTEX default)");
    if (provoking_vertex == GL_LAST_VERTEX_CONVENTION) {
        pass("GL_PROVOKING_VERTEX defaulted to GL_LAST_VERTEX_CONVENTION.");
    } else {
        fail("GL_PROVOKING_VERTEX default was unexpected.");
    }
    glProvokingVertex(GL_FIRST_VERTEX_CONVENTION);
    check_gl_error("glProvokingVertex(GL_FIRST_VERTEX_CONVENTION)");
    glGetIntegerv(GL_PROVOKING_VERTEX, &provoking_vertex);
    check_gl_error("glGetIntegerv(GL_PROVOKING_VERTEX first)");
    if (provoking_vertex == GL_FIRST_VERTEX_CONVENTION) {
        pass("glProvokingVertex stored GL_FIRST_VERTEX_CONVENTION.");
    } else {
        fail("glProvokingVertex did not store GL_FIRST_VERTEX_CONVENTION.");
    }
    glProvokingVertex(GL_LAST_VERTEX_CONVENTION);
    check_gl_error("glProvokingVertex(GL_LAST_VERTEX_CONVENTION)");
    glProvokingVertex(GL_INVALID_ENUM);
    expect_error("glProvokingVertex(GL_INVALID_ENUM)", GL_INVALID_ENUM);
    glEnable(GL_RASTERIZER_DISCARD);
    check_gl_error("glEnable(GL_RASTERIZER_DISCARD)");
    if (glIsEnabled(GL_RASTERIZER_DISCARD) == GL_TRUE) {
        pass("glEnable(GL_RASTERIZER_DISCARD) updated rasterizer discard state.");
    } else {
        fail("glEnable(GL_RASTERIZER_DISCARD) did not enable the state.");
    }
    check_gl_error("glIsEnabled(GL_RASTERIZER_DISCARD)");
    glDisable(GL_RASTERIZER_DISCARD);
    check_gl_error("glDisable(GL_RASTERIZER_DISCARD)");
    GLint rasterizer_discard = GL_TRUE;
    glGetIntegerv(GL_RASTERIZER_DISCARD, &rasterizer_discard);
    check_gl_error("glGetIntegerv(GL_RASTERIZER_DISCARD)");
    if (rasterizer_discard == GL_FALSE) {
        pass("glDisable(GL_RASTERIZER_DISCARD) restored rasterization.");
    } else {
        fail("glDisable(GL_RASTERIZER_DISCARD) left the state enabled.");
    }

    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, 64, 64);
    glClearColor(51.0f / 255.0f, 102.0f / 255.0f, 153.0f / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    check_gl_error("default framebuffer clear");

    GLubyte default_readback[4] = {0, 0, 0, 0};
    platform_prepare_default_readback();
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, default_readback);
    check_gl_error("glReadPixels(default)");
    platform_resume_after_default_readback();

    GLuint buffers[2] = {0, 0};
    glGenBuffers(2, buffers);
    check_gl_error("glGenBuffers");
    glBindBuffer(GL_ARRAY_BUFFER, buffers[0]);
    check_gl_error("glBindBuffer(GL_ARRAY_BUFFER)");
    GLfloat vertex_data[4] = {0.0f, 1.0f, 2.0f, 3.0f};
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_data), vertex_data, GL_STATIC_DRAW);
    check_gl_error("glBufferData(GL_ARRAY_BUFFER)");
    GLint buffer_size = 0;
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &buffer_size);
    check_gl_error("glGetBufferParameteriv(GL_BUFFER_SIZE)");
    if (buffer_size == (GLint)sizeof(vertex_data)) {
        pass("buffer size query returned expected value.");
    } else {
        fail("buffer size query returned unexpected value.");
    }
    GLfloat vertex_copy[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertex_copy), vertex_copy);
    check_gl_error("glGetBufferSubData(GL_ARRAY_BUFFER)");
    if (std::memcmp(vertex_copy, vertex_data, sizeof(vertex_data)) == 0) {
        pass("glGetBufferSubData returned expected data.");
    } else {
        fail("glGetBufferSubData returned unexpected data.");
    }

    glBindBuffer(GL_UNIFORM_BUFFER, buffers[1]);
    check_gl_error("glBindBuffer(GL_UNIFORM_BUFFER)");
    GLint uniform_data[4] = {11, 12, 13, 14};
    glBufferData(GL_UNIFORM_BUFFER, sizeof(uniform_data), uniform_data, GL_DYNAMIC_DRAW);
    check_gl_error("glBufferData(GL_UNIFORM_BUFFER)");
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, buffers[1]);
    check_gl_error("glBindBufferBase(GL_UNIFORM_BUFFER)");
    GLint indexed_ubo_binding = 0;
    glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 1, &indexed_ubo_binding);
    check_gl_error("glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING)");
    if (indexed_ubo_binding == (GLint)buffers[1]) {
        pass("indexed uniform buffer binding returned expected buffer.");
    } else {
        fail("indexed uniform buffer binding returned unexpected buffer.");
    }
    void *mapped = glMapBufferRange(GL_UNIFORM_BUFFER, 0, sizeof(uniform_data), GL_MAP_READ_BIT);
    check_gl_error("glMapBufferRange(GL_UNIFORM_BUFFER)");
    if (mapped) {
        pass("glMapBufferRange returned a non-null pointer.");
        glUnmapBuffer(GL_UNIFORM_BUFFER);
        check_gl_error("glUnmapBuffer(GL_UNIFORM_BUFFER)");
    } else {
        fail("glMapBufferRange returned NULL.");
    }

    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    check_gl_error("glGenVertexArrays");
    glBindVertexArray(vao);
    check_gl_error("glBindVertexArray");
    glBindBuffer(GL_ARRAY_BUFFER, buffers[0]);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 2, (const void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribDivisor(0, 1);
    check_gl_error("vertex attrib setup");

    GLint attrib_enabled = 0;
    GLint attrib_divisor = 0;
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &attrib_enabled);
    check_gl_error("glGetVertexAttribiv(GL_VERTEX_ATTRIB_ARRAY_ENABLED)");
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_DIVISOR, &attrib_divisor);
    check_gl_error("glGetVertexAttribiv(GL_VERTEX_ATTRIB_ARRAY_DIVISOR)");
    if (attrib_enabled == GL_TRUE && attrib_divisor == 1) {
        pass("vertex attrib queries returned expected values.");
    } else {
        fail("vertex attrib queries returned unexpected values.");
    }

    GLbyte normalized_bytes[4] = {
        (GLbyte)-128, (GLbyte)-1, (GLbyte)0, (GLbyte)127
    };
    GLfloat current_attrib[4] = {0, 0, 0, 0};
    glVertexAttrib4Nbv(1, normalized_bytes);
    check_gl_error("glVertexAttrib4Nbv(edge values)");
    glGetVertexAttribfv(1, GL_CURRENT_VERTEX_ATTRIB, current_attrib);
    check_gl_error("glGetVertexAttribfv(GL_CURRENT_VERTEX_ATTRIB normalized byte)");
    if (approx_equalf(current_attrib[0], -1.0f, 0.0001f) &&
        approx_equalf(current_attrib[1], -1.0f / 255.0f, 0.0001f) &&
        approx_equalf(current_attrib[2], 1.0f / 255.0f, 0.0001f) &&
        approx_equalf(current_attrib[3], 1.0f, 0.0001f)) {
        pass("glVertexAttrib4Nbv followed signed normalized conversion.");
    } else {
        fail("glVertexAttrib4Nbv returned unexpected values.");
    }
    GLuint packed_snorm = (0x200u << 0u) | (0x000u << 10u) |
                          (0x1FFu << 20u) | (0x1u << 30u);
    glVertexAttribP4ui(1, GL_INT_2_10_10_10_REV, GL_TRUE, packed_snorm);
    check_gl_error("glVertexAttribP4ui(GL_INT_2_10_10_10_REV)");
    glGetVertexAttribfv(1, GL_CURRENT_VERTEX_ATTRIB, current_attrib);
    check_gl_error("glGetVertexAttribfv(GL_CURRENT_VERTEX_ATTRIB packed)");
    if (approx_equalf(current_attrib[0], -1.0f, 0.0001f) &&
        approx_equalf(current_attrib[1], 1.0f / 1023.0f, 0.0001f) &&
        approx_equalf(current_attrib[2], 1.0f, 0.0001f) &&
        approx_equalf(current_attrib[3], 1.0f, 0.0001f)) {
        pass("glVertexAttribP4ui followed packed signed normalized conversion.");
    } else {
        fail("glVertexAttribP4ui returned unexpected values.");
    }

    glEnablei(GL_SCISSOR_TEST, 0);
    check_gl_error("glEnablei(GL_SCISSOR_TEST, 0)");
    GLboolean indexed_scissor = GL_FALSE;
    glGetBooleani_v(GL_SCISSOR_TEST, 0, &indexed_scissor);
    check_gl_error("glGetBooleani_v(GL_SCISSOR_TEST, 0)");
    if (indexed_scissor == GL_TRUE && glIsEnabledi(GL_SCISSOR_TEST, 0) == GL_TRUE) {
        pass("indexed scissor state returned expected values.");
    } else {
        fail("indexed scissor state returned unexpected values.");
    }
    check_gl_error("glIsEnabledi(GL_SCISSOR_TEST, 0)");
    glDisablei(GL_SCISSOR_TEST, 0);
    check_gl_error("glDisablei(GL_SCISSOR_TEST, 0)");
    glEnablei(GL_BLEND, 0);
    check_gl_error("glEnablei(GL_BLEND, 0)");
    GLboolean indexed_blend = GL_FALSE;
    glGetBooleani_v(GL_BLEND, 0, &indexed_blend);
    check_gl_error("glGetBooleani_v(GL_BLEND, 0)");
    glColorMaski(0, GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
    check_gl_error("glColorMaski(0)");
    GLboolean indexed_color_mask[4] = {GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE};
    glGetBooleani_v(GL_COLOR_WRITEMASK, 0, indexed_color_mask);
    check_gl_error("glGetBooleani_v(GL_COLOR_WRITEMASK, 0)");
    if (indexed_blend == GL_TRUE &&
        indexed_color_mask[0] == GL_TRUE &&
        indexed_color_mask[1] == GL_FALSE &&
        indexed_color_mask[2] == GL_TRUE &&
        indexed_color_mask[3] == GL_FALSE) {
        pass("indexed blend/color mask state returned expected values.");
    } else {
        fail("indexed blend/color mask state returned unexpected values.");
    }
    glDisablei(GL_BLEND, 0);
    check_gl_error("glDisablei(GL_BLEND, 0)");
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    check_gl_error("glColorMask(restore)");

    GLuint textures[2] = {0, 0};
    GLuint sampler = 0;
    GLuint fbo = 0;
    glGenTextures(2, textures);
    glBindTexture(GL_TEXTURE_2D, textures[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, textures[1]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    check_gl_error("texture allocation");

    glGenSamplers(1, &sampler);
    glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glSamplerParameterf(sampler, GL_TEXTURE_WRAP_S, (GLfloat)GL_CLAMP_TO_EDGE);
    check_gl_error("sampler setup");

    GLint sampler_filter = 0;
    GLfloat sampler_wrap = 0.0f;
    glGetSamplerParameteriv(sampler, GL_TEXTURE_MIN_FILTER, &sampler_filter);
    check_gl_error("glGetSamplerParameteriv");
    glGetSamplerParameterfv(sampler, GL_TEXTURE_WRAP_S, &sampler_wrap);
    check_gl_error("glGetSamplerParameterfv");
    if (sampler_filter == GL_NEAREST && sampler_wrap == (GLfloat)GL_CLAMP_TO_EDGE) {
        pass("sampler queries returned expected values.");
    } else {
        fail("sampler queries returned unexpected values.");
    }

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[0], 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, textures[1], 0);
    GLenum mrt_buffers[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, mrt_buffers);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    check_gl_error("framebuffer setup");
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        pass("framebuffer status is complete.");
    } else {
        fail("framebuffer status is not complete.");
    }
    check_gl_error("glCheckFramebufferStatus");
    GLenum remap_buffers[2] = {GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT0};
    glDrawBuffers(2, remap_buffers);
    check_gl_error("glDrawBuffers(remap_supported)");
    glDrawBuffers(2, mrt_buffers);
    check_gl_error("glDrawBuffers(restore)");

    glViewport(0, 0, 4, 4);
    glClearColor(77.0f / 255.0f, 26.0f / 255.0f, 128.0f / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    check_gl_error("FBO clear");

    GLubyte fbo_readback[4 * 4 * 4] = {0};
    glReadPixels(0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, fbo_readback);
    check_gl_error("glReadPixels(FBO)");
    if (rgba_equals(fbo_readback, 77, 26, 128, 255)) {
        pass("FBO readback matched expected clear color.");
    } else {
        fail("FBO readback did not match expected clear color.");
    }
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    check_gl_error("FBO scissor clear reset");
    glEnable(GL_SCISSOR_TEST);
    check_gl_error("glEnable(GL_SCISSOR_TEST)");
    glScissor(1, 1, 2, 2);
    check_gl_error("glScissor");
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    check_gl_error("FBO scissor clear");
    GLubyte scissor_readback[4 * 4 * 4] = {0};
    glReadPixels(0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, scissor_readback);
    check_gl_error("glReadPixels(FBO scissor)");
    if (scissor_readback[(0 * 4 + 0) * 4 + 1] == 0 &&
        scissor_readback[(1 * 4 + 1) * 4 + 1] == 255 &&
        scissor_readback[(2 * 4 + 2) * 4 + 1] == 255 &&
        scissor_readback[(3 * 4 + 3) * 4 + 1] == 0) {
        pass("FBO scissor clear returned expected data.");
    } else {
        fail("FBO scissor clear returned unexpected data.");
    }
    glDisable(GL_SCISSOR_TEST);
    check_gl_error("glDisable(GL_SCISSOR_TEST)");
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, default_readback);
    check_gl_error("glReadPixels(out_of_bounds)");
    glReadBuffer(GL_COLOR_ATTACHMENT1);
    check_gl_error("glReadBuffer(GL_COLOR_ATTACHMENT1)");
    glClearColor(51.0f / 255.0f, 102.0f / 255.0f, 153.0f / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    check_gl_error("FBO color1 clear");
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, default_readback);
    check_gl_error("glReadPixels(FBO color1)");
    if (rgba_equals(default_readback, 51, 102, 153, 255)) {
        pass("FBO readback from color attachment 1 matched expected clear color.");
    } else {
        fail("FBO readback from color attachment 1 did not match expected clear color.");
    }
    GLubyte color1_dump[4 * 4 * 4] = {0};
    glReadPixels(0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, color1_dump);
    check_gl_error("glReadPixels(FBO color1 dump)");
    glReadBuffer(GL_COLOR_ATTACHMENT1);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glBlitFramebuffer(0, 0, 4, 4, 0, 0, 4, 4, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    check_gl_error("glBlitFramebuffer");
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, default_readback);
    check_gl_error("glReadPixels(FBO blit)");
    if (rgba_equals(default_readback, 51, 102, 153, 255)) {
        pass("FBO blit copied expected color data.");
    } else {
        fail("FBO blit did not copy expected color data.");
    }
    glBindTexture(GL_TEXTURE_2D, textures[0]);
    GLubyte tex_image[4 * 4 * 4] = {0};
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex_image);
    check_gl_error("glGetTexImage(GL_TEXTURE_2D)");
    if (rgba_equals(tex_image, 51, 102, 153, 255)) {
        pass("glGetTexImage returned expected texture data.");
    } else {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer),
                      "glGetTexImage returned unexpected texture data (got {%u, %u, %u, %u}).",
                      tex_image[0], tex_image[1], tex_image[2], tex_image[3]);
        fail(buffer);
    }

    GLuint layered_texture = 0;
    GLuint layered_fbo = 0;
    GLubyte layered_seed[4 * 4 * 2 * 4] = {0};
    glGenTextures(1, &layered_texture);
    glBindTexture(GL_TEXTURE_3D, layered_texture);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, 4, 4, 2, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, layered_seed);
    glGenFramebuffers(1, &layered_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, layered_fbo);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              layered_texture, 0, 1);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    check_gl_error("layered framebuffer setup");
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        pass("glFramebufferTextureLayer framebuffer is complete.");
    } else {
        fail("glFramebufferTextureLayer framebuffer is not complete.");
    }
    GLint reported_layer = -1;
    GLint reported_layered = -1;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER,
                                          &reported_layer);
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_LAYERED,
                                          &reported_layered);
    check_gl_error("layered framebuffer attachment queries");
    if (reported_layer == 1 && reported_layered == GL_FALSE) {
        pass("layered framebuffer attachment queries matched expected values.");
    } else {
        fail("layered framebuffer attachment queries returned unexpected values.");
    }
    glClearColor(13.0f / 255.0f, 140.0f / 255.0f, 217.0f / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    check_gl_error("layered framebuffer clear");
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, default_readback);
    check_gl_error("glReadPixels(layered framebuffer)");
    if (rgba_equals(default_readback, 13, 140, 217, 255)) {
        pass("layered framebuffer readback matched attached layer.");
    } else {
        fail("layered framebuffer readback did not match attached layer.");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_3D, layered_texture);
    GLubyte layered_image[4 * 4 * 2 * 4] = {0};
    glGetTexImage(GL_TEXTURE_3D, 0, GL_RGBA, GL_UNSIGNED_BYTE, layered_image);
    check_gl_error("glGetTexImage(layered texture)");
    if (rgba_equals(layered_image, 0, 0, 0, 0) &&
        rgba_equals(layered_image + 4 * 4 * 4, 13, 140, 217, 255)) {
        pass("glFramebufferTextureLayer updated only the selected 3D layer.");
    } else {
        fail("glFramebufferTextureLayer updated the wrong texture layer.");
    }
    glDeleteFramebuffers(1, &layered_fbo);
    glDeleteTextures(1, &layered_texture);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    check_gl_error("restore framebuffer after layered test");

    render_complex_scene_to_window_and_file(reference_ppm_path);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    check_gl_error("glBindFramebuffer(restore texture FBO after complex scene)");

    GLuint renderbuffer = 0;
    GLuint renderbuffer_fbo = 0;
    glGenRenderbuffers(1, &renderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 4, 4);
    check_gl_error("renderbuffer allocation");
    glGenFramebuffers(1, &renderbuffer_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, renderbuffer_fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, renderbuffer);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    check_gl_error("renderbuffer framebuffer setup");
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        pass("renderbuffer framebuffer status is complete.");
    } else {
        fail("renderbuffer framebuffer status is not complete.");
    }
    glClearColor(179.0f / 255.0f, 51.0f / 255.0f, 102.0f / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    check_gl_error("renderbuffer clear");
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, default_readback);
    check_gl_error("glReadPixels(renderbuffer FBO)");
    if (rgba_equals(default_readback, 179, 51, 102, 255)) {
        pass("renderbuffer FBO readback matched expected clear color.");
    } else {
        fail("renderbuffer FBO readback did not match expected clear color.");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    check_gl_error("glBindFramebuffer(restore texture FBO)");

    GLuint reflect_program = build_reflect_program();
    GLint active_attributes = 0;
    GLint active_uniforms = 0;
    glGetProgramiv(reflect_program, GL_ACTIVE_ATTRIBUTES, &active_attributes);
    check_gl_error("glGetProgramiv(GL_ACTIVE_ATTRIBUTES)");
    glGetProgramiv(reflect_program, GL_ACTIVE_UNIFORMS, &active_uniforms);
    check_gl_error("glGetProgramiv(GL_ACTIVE_UNIFORMS)");
    if (active_attributes == 2 && active_uniforms == 6) {
        pass("reflect program active counts matched expected values.");
    } else {
        fail("reflect program active counts were unexpected.");
    }

    GLchar active_name[64] = {0};
    GLsizei active_name_length = 0;
    GLint active_size = 0;
    GLenum active_type = 0;
    glGetActiveAttrib(reflect_program, 0, (GLsizei)sizeof(active_name), &active_name_length, &active_size, &active_type, active_name);
    check_gl_error("glGetActiveAttrib");
    if (std::strcmp(active_name, "aPosition") == 0) {
        pass("glGetActiveAttrib returned expected attribute name.");
    } else {
        fail("glGetActiveAttrib returned unexpected attribute name.");
    }
    const GLchar *uniform_names[2] = {"uTint", "uTexture"};
    GLuint uniform_indices[2] = {GL_INVALID_INDEX, GL_INVALID_INDEX};
    GLint uniform_types[2] = {0, 0};
    glGetUniformIndices(reflect_program, 2, uniform_names, uniform_indices);
    check_gl_error("glGetUniformIndices");
    glGetActiveUniformsiv(reflect_program, 2, uniform_indices, GL_UNIFORM_TYPE, uniform_types);
    check_gl_error("glGetActiveUniformsiv(GL_UNIFORM_TYPE)");
    if (uniform_indices[0] != GL_INVALID_INDEX &&
        uniform_indices[1] != GL_INVALID_INDEX &&
        uniform_types[0] == GL_FLOAT_VEC4 &&
        uniform_types[1] == GL_SAMPLER_2D) {
        pass("uniform reflection queries returned expected values.");
    } else {
        fail("uniform reflection queries returned unexpected values.");
    }

    GLint tint_location = glGetUniformLocation(reflect_program, "uTint");
    GLint sampler_location = glGetUniformLocation(reflect_program, "uTexture");
    GLint viewport_location = glGetUniformLocation(reflect_program, "uViewportSize");
    GLint light_mask_location = glGetUniformLocation(reflect_program, "uLightMask");
    GLint mask_location = glGetUniformLocation(reflect_program, "uMask");
    check_gl_error("glGetUniformLocation");
    glUseProgram(reflect_program);
    glUniform4f(tint_location, 0.25f, 0.5f, 0.75f, 1.0f);
    glUniform1i(sampler_location, 0);
    glUniform2i(viewport_location, 320, 180);
    glUniform3i(light_mask_location, 21, 22, 23);
    glUniform4i(mask_location, 11, 12, 13, 14);
    check_gl_error("uniform setters");

    GLfloat tint_query[4] = {0, 0, 0, 0};
    GLint sampler_query = -1;
    GLint mask_query[4] = {0, 0, 0, 0};
    glGetUniformfv(reflect_program, tint_location, tint_query);
    check_gl_error("glGetUniformfv");
    glGetUniformiv(reflect_program, sampler_location, &sampler_query);
    check_gl_error("glGetUniformiv(sampler)");
    glGetUniformiv(reflect_program, mask_location, mask_query);
    check_gl_error("glGetUniformiv(mask)");
    if (tint_query[0] > 0.249f && tint_query[0] < 0.251f &&
        tint_query[1] > 0.499f && tint_query[1] < 0.501f &&
        sampler_query == 0 &&
        mask_query[0] == 11 && mask_query[3] == 14) {
        pass("uniform queries returned expected values.");
    } else {
        fail("uniform queries returned unexpected values.");
    }
    GLsync sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    check_gl_error("glFenceSync");
    if (sync && glIsSync(sync) == GL_TRUE) {
        pass("glFenceSync returned a valid sync object.");
    } else {
        fail("glFenceSync returned an invalid sync object.");
    }
    check_gl_error("glIsSync");
    GLenum wait_result = glClientWaitSync(sync, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ULL);
    check_gl_error("glClientWaitSync");
    if (wait_result == GL_ALREADY_SIGNALED || wait_result == GL_CONDITION_SATISFIED) {
        pass("glClientWaitSync returned a signaled status.");
    } else {
        fail("glClientWaitSync returned an unexpected status.");
    }
    GLint sync_status = 0;
    GLsizei sync_length = 0;
    glGetSynciv(sync, GL_SYNC_STATUS, 1, &sync_length, &sync_status);
    check_gl_error("glGetSynciv(GL_SYNC_STATUS)");
    if (sync_length == 1 && sync_status == GL_SIGNALED) {
        pass("glGetSynciv returned expected sync status.");
    } else {
        fail("glGetSynciv returned unexpected sync status.");
    }
    glWaitSync(sync, 0, GL_TIMEOUT_IGNORED);
    check_gl_error("glWaitSync");
    glDeleteSync(sync);
    check_gl_error("glDeleteSync");
    glUseProgram(0);
    check_gl_error("glUseProgram(0)");

    glDeleteProgram(reflect_program);
    glDeleteFramebuffers(1, &renderbuffer_fbo);
    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &renderbuffer);
    glDeleteSamplers(1, &sampler);
    glDeleteTextures(2, textures);
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(2, buffers);
    check_gl_error("resource deletion");

    return g_fail_count == 0 ? 0 : 1;
}

#ifdef GX2GL_COMPARE_WIIU
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    std::remove("/vol/external01/gx2gl_results.txt");
    std::remove("/vol/external01/gx2gl_done.flag");
    std::remove("/vol/external01/gx2gl_diag.ppm");

    WHBProcInit();
    WHBGfxInit();
    gl_mem_init();
    g_gl_context = gl_context_create();
    if (!g_gl_context) {
        std::fprintf(stderr, "[FAIL] gl_context_create failed.\n");
        g_fail_count = 1;
        write_wiiu_results_file("failed");
        write_wiiu_done_flag();
        gl_mem_shutdown();
        WHBGfxShutdown();
        WHBProcShutdown();
        return 1;
    }

    WHBGfxBeginRender();
    WHBGfxBeginRenderTV();
    int exit_code = run_compare_suite("/vol/external01/gx2gl_diag.ppm");
    WHBGfxFinishRenderTV();
    WHBGfxFinishRender();
    std::printf("[SUMMARY] pass=%d expected_error_pass=%d fail=%d\n",
                g_pass_count, g_expected_error_pass_count, g_fail_count);
    write_wiiu_results_file(exit_code == 0 ? "complete" : "failed");
    write_wiiu_done_flag();

    gl_context_destroy(g_gl_context);
    g_gl_context = NULL;
    gl_mem_shutdown();
    WHBGfxShutdown();
    WHBProcShutdown();
    return exit_code;
}
#else
int main() {
    if (!DesktopGLPlatformInitialize("gx2gl-pc", 640, 360)) {
        std::fprintf(stderr, "[FAIL] DesktopGLPlatformInitialize failed.\n");
        return 1;
    }

    if (!DesktopGLLoadTestEntryPoints()) {
        const char *missing = DesktopGLGetLastMissingProc();
        std::fprintf(stderr, "[FAIL] DesktopGLLoadTestEntryPoints missing %s.\n",
                     missing ? missing : "<unknown>");
        DesktopGLPlatformShutdown();
        return 1;
    }

    int exit_code = run_compare_suite("gx2gl_pc_reference.ppm");
    std::printf("[SUMMARY] pass=%d expected_error_pass=%d fail=%d\n",
                g_pass_count, g_expected_error_pass_count, g_fail_count);
    DesktopGLPlatformShutdown();
    return exit_code;
}
#endif
