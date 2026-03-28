#include <coreinit/debug.h>
#include <whb/proc.h>
#include <whb/gfx.h>
#include <coreinit/memheap.h>
#include <coreinit/memexpheap.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif
#include <gx2/enum.h>
#include <gx2/draw.h>
#include <gx2/registers.h>
#ifdef __cplusplus
}
#endif

#include "gl/gl.h"
#include "core/gl_context.h"
#include "mem/gl_mem.h"

// Helper to check errors
static void check_gl_error(const char* func_name) {
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        OSReport("[FAIL] %s resulted in error code 0x%04X\n", func_name, err);
    } else {
        OSReport("[PASS] %s completed successfully.\n", func_name);
    }
}

// Verification helper for negative tests
static void expect_error(const char* test_name, GLenum expected_error) {
    GLenum actual = glGetError();
    if (actual == expected_error) {
        OSReport("   [PASS-NEGATIVE] %s correctly produced 0x%04X\n", test_name, expected_error);
    } else {
        OSReport("   [FAIL-NEGATIVE] %s expected 0x%04X but got 0x%04X\n", test_name, expected_error, actual);
    }
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
    GLfloat legacy_depth_range[2] = {0.0f, 0.0f};
    glGetFloatv(GL_DEPTH_RANGE, legacy_depth_range);
    check_gl_error("glGetFloatv(GL_DEPTH_RANGE)");
    GLdouble legacy_depth_range_double[2] = {0.0, 0.0};
    glGetDoublev(GL_DEPTH_RANGE, legacy_depth_range_double);
    check_gl_error("glGetDoublev(GL_DEPTH_RANGE)");
    if (legacy_depth_range_double[0] == 0.2 && legacy_depth_range_double[1] == 0.8) {
        OSReport("[PASS] glGetDoublev(GL_DEPTH_RANGE) returned expected values.\n");
    } else {
        OSReport("[FAIL] glGetDoublev(GL_DEPTH_RANGE) returned {%f, %f}\n",
                 legacy_depth_range_double[0], legacy_depth_range_double[1]);
    }
    GLfloat legacy_blend_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    glGetFloatv(GL_BLEND_COLOR, legacy_blend_color);
    check_gl_error("glGetFloatv(GL_BLEND_COLOR)");
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
    
    // Negative Array Size
    GLuint bad_buffers[1] = {0};
    glGenBuffers(-1, bad_buffers);
    expect_error("glGenBuffers(-1)", GL_INVALID_VALUE);

    glBindBuffer(GL_ARRAY_BUFFER, buffers[0]);
    check_gl_error("glBindBuffer(GL_ARRAY_BUFFER)");
    
    // Invalid buffer target
    glBindBuffer(GL_INVALID_ENUM, buffers[0]);
    expect_error("glBindBuffer(GL_INVALID_ENUM)", GL_INVALID_ENUM);

    float vertices[] = { 0.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f };
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    check_gl_error("glBufferData(GL_ARRAY_BUFFER)");

    // Buffer data on unsupported/invalid target
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
    glUnmapBuffer(GL_UNIFORM_BUFFER);
    check_gl_error("glUnmapBuffer(GL_UNIFORM_BUFFER)");

    OSReport("-> Testing Texture Objects (Phase 5)...\n");
    GLuint tex[2];
    glGenTextures(2, tex);
    check_gl_error("glGenTextures");
    
    // Negative tex count
    glGenTextures(-5, tex);
    expect_error("glGenTextures(-5)", GL_INVALID_VALUE);
    
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    check_gl_error("glBindTexture");
    
    // Bind to invalid target
    glBindTexture(GL_INVALID_ENUM, tex[0]);
    expect_error("glBindTexture(GL_INVALID_ENUM)", GL_INVALID_ENUM);

    unsigned char pixels[4 * 4 * 4]; // 4x4 RGBA
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
    
    // Negative dimensions in glTexImage2D
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, -4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    expect_error("glTexImage2D(negative_width)", GL_INVALID_VALUE);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    check_gl_error("glTexParameteri");
    
    // Invalid filter/param
    glTexParameteri(GL_INVALID_ENUM, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    expect_error("glTexParameteri(GL_INVALID_ENUM)", GL_INVALID_ENUM);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    expect_error("glTexParameteri(invalid_mag_filter)", GL_INVALID_ENUM);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_INVALID_ENUM);
    expect_error("glTexParameteri(invalid_wrap)", GL_INVALID_ENUM);
    
    glGenerateMipmap(GL_TEXTURE_2D);
    check_gl_error("glGenerateMipmap");

    OSReport("-> Testing Shaders & Programs (Phase 6)...\n");
    GLuint vshader = glCreateShader(GL_VERTEX_SHADER);
    check_gl_error("glCreateShader(GL_VERTEX_SHADER)");
    
    // Invalid shader type
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

    glCompileShader(pshader);
    check_gl_error("glCompileShader(fragment)");
    
    GLuint prog = glCreateProgram();
    check_gl_error("glCreateProgram");
    
    glAttachShader(prog, vshader);
    check_gl_error("glAttachShader");

    glAttachShader(prog, pshader);
    check_gl_error("glAttachShader(fragment)");
    GLint attached_shader_count = 0;
    glGetProgramiv(prog, GL_ATTACHED_SHADERS, &attached_shader_count);
    check_gl_error("glGetProgramiv(GL_ATTACHED_SHADERS)");
    if (attached_shader_count == 2) {
        OSReport("[PASS] glGetProgramiv(GL_ATTACHED_SHADERS) returned 2.\n");
    } else {
        OSReport("[FAIL] glGetProgramiv(GL_ATTACHED_SHADERS) returned %d\n",
                 attached_shader_count);
    }
    
    // Attach to 0 (invalid program ID)
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

    glUseProgram(prog);
    expect_error("glUseProgram(source_only)", GL_INVALID_OPERATION);
    
    // Invalid UseProgram
    glUseProgram(999);
    expect_error("glUseProgram(999)", GL_INVALID_VALUE);
    
    // Unbind program
    glUseProgram(0);
    check_gl_error("glUseProgram(0)");
    
    // Test uniforms without program bound
    glUniform1f(0, 1.0f);
    expect_error("glUniform1f(unbound_prog)", GL_INVALID_OPERATION);
    glUniform4f(0, 1.0f, 0.5f, 0.25f, 1.0f);
    expect_error("glUniform4f(unbound_prog)", GL_INVALID_OPERATION);
    glUniform1i(0, 0);
    expect_error("glUniform1i(unbound_prog)", GL_INVALID_OPERATION);

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
    
    // Negative VAO gen
    glGenVertexArrays(-1, &vao);
    expect_error("glGenVertexArrays(-1)", GL_INVALID_VALUE);

    glBindVertexArray(vao);
    check_gl_error("glBindVertexArray");
    
    glBindBuffer(GL_ARRAY_BUFFER, buffers[0]);
    glEnableVertexAttribArray(0);
    check_gl_error("glEnableVertexAttribArray");
    
    // Enable out of bounds attrib
    glEnableVertexAttribArray(999);
    expect_error("glEnableVertexAttribArray(999)", GL_INVALID_VALUE);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
    check_gl_error("glVertexAttribPointer");

    glVertexAttribDivisor(0, 1);
    check_gl_error("glVertexAttribDivisor");
    
    // Invalid size for attrib pointer (must be 1, 2, 3, 4)
    glVertexAttribPointer(0, 5, GL_FLOAT, GL_FALSE, 0, (void*)0);
    expect_error("glVertexAttribPointer(size=5)", GL_INVALID_VALUE);

    glVertexAttribDivisor(999, 1);
    expect_error("glVertexAttribDivisor(999)", GL_INVALID_VALUE);
    
    // Invalid type for attrib pointer
    // We didn't explicitly implement catching invalid type in gl_vao.cpp yet, but it falls back to FLOAT.

    OSReport("-> Testing Framebuffers (Phase 8)...\n");
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    check_gl_error("glGenFramebuffers");
    
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    check_gl_error("glBindFramebuffer");
    
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex[0], 0);
    check_gl_error("glFramebufferTexture2D");

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, tex[1], 0);
    check_gl_error("glFramebufferTexture2D(color1)");

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
    
    // Invalid attachment enum
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_INVALID_ENUM, GL_TEXTURE_2D, tex[0], 0);
    expect_error("glFramebufferTexture2D(GL_INVALID_ENUM attachment)", GL_INVALID_ENUM);
    
    // Binding FB on unbound context or invalid target
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
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, readback_rgba);
    expect_error("glReadPixels(out_of_bounds)", GL_INVALID_VALUE);
    glReadBuffer(GL_BACK);
    expect_error("glReadBuffer(GL_BACK on FBO)", GL_INVALID_OPERATION);
    glReadBuffer(GL_INVALID_ENUM);
    expect_error("glReadBuffer(GL_INVALID_ENUM)", GL_INVALID_ENUM);
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0); // Restore default
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

    OSReport("-> Testing Draw Calls & Synchronization (Phase 9/10)...\n");
    
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glBindVertexArray(vao);
    // A valid precompiled CafeGLSL shader group is required for successful draws.
    
    // Negative: draw with no active program (we'll unbind first)
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
    
    glDeleteFramebuffers(1, &fbo);
    check_gl_error("glDeleteFramebuffers");

    OSReport("----- Comprehensive Test Sequence With Failure Coverage Complete! -----\n");

    gl_context_destroy(g_gl_context);
    gl_mem_shutdown();
    
    WHBGfxShutdown();
    WHBProcShutdown();
    return 0;
}
