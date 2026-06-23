#include "piglit_manifest.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "gl/gl.h"

namespace {

static const char *kExternalManifest =
    "/vol/external01/gx2gl/piglit_manifest.tsv";
static const char *kContentManifest =
    "/vol/content/gx2gl/piglit_manifest.tsv";
static const size_t kMaxShaderTestBytes = 512 * 1024;
static const int kTargetWidth = 16;
static const int kTargetHeight = 16;

struct ManifestEntry {
    std::string name;
    std::string kind;
    std::string path;
};

struct ShaderTest {
    std::string requirements;
    std::string vertex_shader;
    std::string fragment_shader;
    std::string geometry_shader;
    std::string tess_control_shader;
    std::string tess_eval_shader;
    std::string compute_shader;
    std::string vertex_data;
    std::string test;
};

struct TestCommand {
    enum Type {
        ClearColor,
        Clear,
        Uniform,
        DrawRect,
        ProbeAllRgba,
        ProbeRgba,
        LinkSuccess,
        LinkError,
    } type;
    std::string text;
    std::string uniform_type;
    std::string uniform_name;
    float values[8];
    int value_count;
};

struct ProgramObjects {
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLuint fbo;
    GLuint texture;
    GLuint vao;
    GLuint vbo;
};

static void add_result(PiglitRunStats *stats, PiglitResult result) {
    ++stats->total;
    if (result == PIGLIT_RESULT_PASS) {
        ++stats->pass;
    } else if (result == PIGLIT_RESULT_FAIL) {
        ++stats->fail;
    } else {
        ++stats->skip;
    }
}

static void publish_result(PiglitRunStats *stats, PiglitResultFunc result_func,
                           const char *name, PiglitResult result,
                           const char *detail, void *user_data) {
    add_result(stats, result);
    if (result_func) {
        result_func(name, result, detail, user_data);
    }
}

static std::string trim(const std::string &value) {
    size_t first = 0;
    size_t last = value.size();
    while (first < last &&
           isspace((unsigned char)value[first])) {
        ++first;
    }
    while (last > first &&
           isspace((unsigned char)value[last - 1])) {
        --last;
    }
    return value.substr(first, last - first);
}

static std::string lower_ascii(std::string value) {
    for (size_t i = 0; i < value.size(); ++i) {
        value[i] = (char)tolower((unsigned char)value[i]);
    }
    return value;
}

static bool starts_with(const std::string &value, const char *prefix) {
    const size_t prefix_len = strlen(prefix);
    return value.size() >= prefix_len &&
           memcmp(value.c_str(), prefix, prefix_len) == 0;
}

static bool contains(const std::string &value, const char *needle) {
    return value.find(needle) != std::string::npos;
}

static std::vector<std::string> split_ws(const std::string &line) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() &&
               isspace((unsigned char)line[i])) {
            ++i;
        }
        const size_t start = i;
        while (i < line.size() &&
               !isspace((unsigned char)line[i])) {
            ++i;
        }
        if (i > start) {
            out.push_back(line.substr(start, i - start));
        }
    }
    return out;
}

static std::vector<std::string> split_tabs(const std::string &line) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= line.size()) {
        const size_t tab = line.find('\t', start);
        if (tab == std::string::npos) {
            out.push_back(line.substr(start));
            break;
        }
        out.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
    return out;
}

static bool parse_float(const std::string &token, float *value) {
    char *end = NULL;
    errno = 0;
    const float parsed = strtof(token.c_str(), &end);
    if (end == token.c_str() || errno == ERANGE) {
        return false;
    }
    while (end && *end) {
        if (!isspace((unsigned char)*end)) {
            return false;
        }
        ++end;
    }
    *value = parsed;
    return true;
}

static std::string strip_comment(const std::string &line) {
    const size_t hash = line.find('#');
    if (hash == std::string::npos) {
        return line;
    }
    return line.substr(0, hash);
}

static bool read_text_file(const char *path, size_t max_bytes,
                           std::string *out) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    const long size = ftell(file);
    if (size < 0 || (size_t)size > max_bytes) {
        fclose(file);
        return false;
    }
    rewind(file);

    out->assign((size_t)size, '\0');
    if (size > 0) {
        const size_t read_count =
            fread(&(*out)[0], 1, (size_t)size, file);
        if (read_count != (size_t)size) {
            fclose(file);
            return false;
        }
    }
    fclose(file);
    return true;
}

static void clear_gl_errors(void) {
    while (glGetError() != GL_NO_ERROR) {
    }
}

static bool expect_no_error(PiglitReportFunc report, const char *label) {
    const GLenum error = glGetError();
    if (error == GL_NO_ERROR) {
        return true;
    }
    report("[FAIL] Piglit manifest %s produced GL error 0x%04X\n",
           label, error);
    return false;
}

static std::string make_source_330(GLenum stage, const std::string &source) {
    std::string out;
    const std::string trimmed = trim(source);
    if (starts_with(trimmed, "#version")) {
        return source;
    }

    out = "#version 330 core\n";
    if (stage == GL_FRAGMENT_SHADER) {
        out += "in vec4 gx2gl_FrontColor;\n";
        out += "layout(location = 0) out vec4 gx2gl_FragColor;\n";
        out += "#define gl_FragColor gx2gl_FragColor\n";
        out += "#define gl_Color gx2gl_FrontColor\n";
        out += "#define texture2D texture\n";
        out += "#define textureCube texture\n";
    } else if (stage == GL_VERTEX_SHADER) {
        out += "layout(location = 0) in vec4 gx2gl_Vertex;\n";
        out += "out vec4 gx2gl_FrontColor;\n";
        out += "#define attribute in\n";
        out += "#define varying out\n";
        out += "#define gl_Vertex gx2gl_Vertex\n";
        out += "#define gl_FrontColor gx2gl_FrontColor\n";
        out += "#define gl_ModelViewProjectionMatrix mat4(1.0)\n";
        out += "#define ftransform() (gl_ModelViewProjectionMatrix * gl_Vertex)\n";
    }
    out += source;
    return out;
}

static GLuint compile_shader(PiglitReportFunc report, GLenum type,
                             const std::string &source,
                             const char *case_name) {
    const std::string upgraded = make_source_330(type, source);
    const char *src = upgraded.c_str();
    GLuint shader = glCreateShader(type);
    GLint status = GL_FALSE;
    char log[1024];
    GLsizei log_len = 0;

    if (!shader || !expect_no_error(report, "glCreateShader")) {
        return 0;
    }
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    if (!expect_no_error(report, "glCompileShader")) {
        glDeleteShader(shader);
        return 0;
    }
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!expect_no_error(report, "glGetShaderiv")) {
        glDeleteShader(shader);
        return 0;
    }
    if (status == GL_TRUE) {
        return shader;
    }

    memset(log, 0, sizeof(log));
    glGetShaderInfoLog(shader, sizeof(log), &log_len, log);
    report("[FAIL] Piglit manifest %s shader compile failed: %s\n",
           case_name, log);
    glDeleteShader(shader);
    return 0;
}

static GLuint link_program(PiglitReportFunc report, GLuint vs, GLuint fs,
                           const char *case_name, bool *linked) {
    GLuint program = glCreateProgram();
    GLint status = GL_FALSE;
    char log[1024];
    GLsizei log_len = 0;

    *linked = false;
    if (!program || !expect_no_error(report, "glCreateProgram")) {
        return 0;
    }
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    if (!expect_no_error(report, "glLinkProgram")) {
        glDeleteProgram(program);
        return 0;
    }
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!expect_no_error(report, "glGetProgramiv(GL_LINK_STATUS)")) {
        glDeleteProgram(program);
        return 0;
    }
    if (status == GL_TRUE) {
        *linked = true;
        return program;
    }

    memset(log, 0, sizeof(log));
    glGetProgramInfoLog(program, sizeof(log), &log_len, log);
    report("[INFO] Piglit manifest %s link failed: %s\n", case_name, log);
    return program;
}

static std::string section_name(const std::string &line) {
    const std::string trimmed = trim(line);
    if (trimmed.size() < 3 || trimmed[0] != '[' ||
        trimmed[trimmed.size() - 1] != ']') {
        return std::string();
    }
    return lower_ascii(trim(trimmed.substr(1, trimmed.size() - 2)));
}

static void append_section(ShaderTest *test, const std::string &section,
                           const std::string &line) {
    std::string *target = NULL;
    if (section == "require") {
        target = &test->requirements;
    } else if (section == "vertex shader") {
        target = &test->vertex_shader;
    } else if (section == "fragment shader") {
        target = &test->fragment_shader;
    } else if (section == "geometry shader") {
        target = &test->geometry_shader;
    } else if (section == "tessellation control shader") {
        target = &test->tess_control_shader;
    } else if (section == "tessellation evaluation shader") {
        target = &test->tess_eval_shader;
    } else if (section == "compute shader") {
        target = &test->compute_shader;
    } else if (section == "vertex data") {
        target = &test->vertex_data;
    } else if (section == "test") {
        target = &test->test;
    }

    if (target) {
        target->append(line);
        target->push_back('\n');
    }
}

static ShaderTest parse_shader_test(const std::string &contents) {
    ShaderTest test;
    std::string section;
    size_t pos = 0;

    while (pos <= contents.size()) {
        size_t next = contents.find('\n', pos);
        if (next == std::string::npos) {
            next = contents.size();
        }
        std::string line = contents.substr(pos, next - pos);
        if (!line.empty() && line[line.size() - 1] == '\r') {
            line.resize(line.size() - 1);
        }

        const std::string new_section = section_name(line);
        if (!new_section.empty()) {
            section = new_section;
        } else {
            append_section(&test, section, line);
        }

        if (next == contents.size()) {
            break;
        }
        pos = next + 1;
    }
    return test;
}

static bool parse_gl_version(const std::string &token, int *scaled) {
    float value = 0.0f;
    if (!parse_float(token, &value)) {
        return false;
    }
    *scaled = (int)(value * 100.0f + 0.5f);
    return true;
}

static bool requirements_supported(const ShaderTest &test,
                                   std::string *skip_reason) {
    size_t pos = 0;
    while (pos <= test.requirements.size()) {
        size_t next = test.requirements.find('\n', pos);
        if (next == std::string::npos) {
            next = test.requirements.size();
        }
        const std::string line = trim(strip_comment(
            test.requirements.substr(pos, next - pos)));
        if (!line.empty()) {
            const std::vector<std::string> tokens = split_ws(line);
            if (tokens.size() >= 3 && tokens[0] == "GL" &&
                tokens[1] == ">=") {
                int version = 0;
                if (!parse_gl_version(tokens[2], &version) ||
                    version > 330) {
                    *skip_reason = "requires GL newer than 3.3";
                    return false;
                }
            } else if (tokens.size() >= 3 && tokens[0] == "GLSL" &&
                       tokens[1] == ">=") {
                int version = 0;
                if (!parse_gl_version(tokens[2], &version) ||
                    version > 330) {
                    *skip_reason = "requires GLSL newer than 3.30";
                    return false;
                }
            } else if (tokens.size() >= 1 &&
                       starts_with(tokens[0], "GL_")) {
                *skip_reason = "extension-gated shader_test";
                return false;
            } else if (tokens.size() >= 1 &&
                       (tokens[0] == "GLSL" || tokens[0] == "GL")) {
                *skip_reason = "unsupported version requirement";
                return false;
            }
        }

        if (next == test.requirements.size()) {
            break;
        }
        pos = next + 1;
    }
    return true;
}

static bool parse_command_line(const std::string &raw_line,
                               TestCommand *command,
                               std::string *skip_reason) {
    command->type = TestCommand::Clear;
    command->text.clear();
    command->uniform_type.clear();
    command->uniform_name.clear();
    command->value_count = 0;
    for (int i = 0; i < 8; ++i) {
        command->values[i] = 0.0f;
    }

    const std::string line = trim(strip_comment(raw_line));
    if (line.empty()) {
        return true;
    }

    const std::vector<std::string> tokens = split_ws(line);
    if (tokens.empty()) {
        return true;
    }

    command->text = line;

    if (tokens.size() == 6 && tokens[0] == "clear" &&
        tokens[1] == "color") {
        command->type = TestCommand::ClearColor;
        for (int i = 0; i < 4; ++i) {
            if (!parse_float(tokens[2 + i], &command->values[i])) {
                *skip_reason = "unsupported clear color";
                return false;
            }
        }
        command->value_count = 4;
        return true;
    }
    if (tokens.size() == 1 && tokens[0] == "clear") {
        command->type = TestCommand::Clear;
        return true;
    }
    if (tokens.size() >= 4 && tokens[0] == "uniform") {
        command->type = TestCommand::Uniform;
        command->uniform_type = tokens[1];
        command->uniform_name = tokens[2];
        command->value_count = (int)tokens.size() - 3;
        if (command->value_count > 8) {
            *skip_reason = "uniform has too many values";
            return false;
        }
        for (int i = 0; i < command->value_count; ++i) {
            if (!parse_float(tokens[3 + i], &command->values[i])) {
                *skip_reason = "unsupported uniform value";
                return false;
            }
        }
        return true;
    }
    if (tokens.size() == 6 && tokens[0] == "draw" &&
        tokens[1] == "rect") {
        command->type = TestCommand::DrawRect;
        for (int i = 0; i < 4; ++i) {
            if (!parse_float(tokens[2 + i], &command->values[i])) {
                *skip_reason = "unsupported draw rect";
                return false;
            }
        }
        command->value_count = 4;
        return true;
    }
    if (tokens.size() == 7 && tokens[0] == "probe" &&
        tokens[1] == "all" && tokens[2] == "rgba") {
        command->type = TestCommand::ProbeAllRgba;
        for (int i = 0; i < 4; ++i) {
            if (!parse_float(tokens[3 + i], &command->values[i])) {
                *skip_reason = "unsupported probe all rgba";
                return false;
            }
        }
        command->value_count = 4;
        return true;
    }
    if (tokens.size() == 8 && tokens[0] == "probe" &&
        tokens[1] == "rgba") {
        command->type = TestCommand::ProbeRgba;
        for (int i = 0; i < 6; ++i) {
            if (!parse_float(tokens[2 + i], &command->values[i])) {
                *skip_reason = "unsupported probe rgba";
                return false;
            }
        }
        command->value_count = 6;
        return true;
    }
    if (tokens.size() == 2 && tokens[0] == "link" &&
        tokens[1] == "success") {
        command->type = TestCommand::LinkSuccess;
        return true;
    }
    if (tokens.size() == 2 && tokens[0] == "link" &&
        tokens[1] == "error") {
        command->type = TestCommand::LinkError;
        return true;
    }

    *skip_reason = std::string("unsupported shader_test command: ") +
                   tokens[0];
    return false;
}

static bool parse_commands(const ShaderTest &test,
                           std::vector<TestCommand> *commands,
                           std::string *skip_reason) {
    size_t pos = 0;
    while (pos <= test.test.size()) {
        size_t next = test.test.find('\n', pos);
        if (next == std::string::npos) {
            next = test.test.size();
        }
        TestCommand command;
        if (!parse_command_line(test.test.substr(pos, next - pos), &command,
                                skip_reason)) {
            return false;
        }
        if (!command.text.empty()) {
            commands->push_back(command);
        }
        if (next == test.test.size()) {
            break;
        }
        pos = next + 1;
    }
    return true;
}

static bool command_list_has_draw_or_link(
    const std::vector<TestCommand> &commands) {
    for (size_t i = 0; i < commands.size(); ++i) {
        if (commands[i].type == TestCommand::DrawRect ||
            commands[i].type == TestCommand::LinkSuccess ||
            commands[i].type == TestCommand::LinkError) {
            return true;
        }
    }
    return false;
}

static bool unsupported_shader_sections(const ShaderTest &test,
                                        std::string *skip_reason) {
    if (!test.geometry_shader.empty() ||
        !test.tess_control_shader.empty() ||
        !test.tess_eval_shader.empty() ||
        !test.compute_shader.empty()) {
        *skip_reason = "shader_test uses unsupported shader stage";
        return true;
    }
    if (!test.vertex_data.empty()) {
        *skip_reason = "shader_test uses vertex data";
        return true;
    }
    if (test.fragment_shader.empty()) {
        *skip_reason = "shader_test has no fragment shader";
        return true;
    }
    if (contains(test.vertex_shader, "switch") ||
        contains(test.fragment_shader, "switch")) {
        *skip_reason = "shader_test uses switch (CafeGLSL fatal guard)";
        return true;
    }
    return false;
}

static const char *fallback_vertex_shader(void) {
    return "#version 330 core\n"
           "layout(location = 0) in vec2 aPosition;\n"
           "out vec4 gx2gl_FrontColor;\n"
           "void main() {\n"
           "    gx2gl_FrontColor = vec4(1.0);\n"
           "    gl_Position = vec4(aPosition, 0.0, 1.0);\n"
           "}\n";
}

static bool setup_render_target(PiglitReportFunc report, ProgramObjects *obj) {
    glGenFramebuffers(1, &obj->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, obj->fbo);
    glGenTextures(1, &obj->texture);
    glBindTexture(GL_TEXTURE_2D, obj->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kTargetWidth, kTargetHeight,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, obj->texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        report("[FAIL] Piglit manifest framebuffer incomplete.\n");
        return false;
    }
    return expect_no_error(report, "manifest FBO setup");
}

static bool setup_rect_vertices(PiglitReportFunc report, ProgramObjects *obj,
                                const TestCommand &command) {
    const float x = command.values[0];
    const float y = command.values[1];
    const float w = command.values[2];
    const float h = command.values[3];
    const GLfloat vertices[] = {
        x,     y,
        x + w, y,
        x + w, y + h,
        x,     y,
        x + w, y + h,
        x,     y + h,
    };

    if (!obj->vao) {
        glGenVertexArrays(1, &obj->vao);
    }
    if (!obj->vbo) {
        glGenBuffers(1, &obj->vbo);
    }
    glBindVertexArray(obj->vao);
    glBindBuffer(GL_ARRAY_BUFFER, obj->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices,
                 GL_STREAM_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat),
                          (const GLvoid *)0);
    glEnableVertexAttribArray(0);
    return expect_no_error(report, "manifest vertex setup");
}

static bool apply_uniform(PiglitReportFunc report, GLuint program,
                          const TestCommand &command) {
    const GLint location =
        glGetUniformLocation(program, command.uniform_name.c_str());
    const std::string type = command.uniform_type;
    if (location < 0) {
        return expect_no_error(report, "manifest inactive uniform");
    }
    if (type == "int" || type == "bool" || type == "uint") {
        glUniform1i(location, (GLint)command.values[0]);
    } else if (type == "float") {
        glUniform1f(location, command.values[0]);
    } else if (type == "vec2") {
        glUniform2f(location, command.values[0], command.values[1]);
    } else if (type == "vec3") {
        glUniform3f(location, command.values[0], command.values[1],
                    command.values[2]);
    } else if (type == "vec4") {
        glUniform4f(location, command.values[0], command.values[1],
                    command.values[2], command.values[3]);
    } else if (type == "ivec2" || type == "bvec2" || type == "uvec2") {
        glUniform2i(location, (GLint)command.values[0],
                    (GLint)command.values[1]);
    } else if (type == "ivec3" || type == "bvec3" || type == "uvec3") {
        glUniform3i(location, (GLint)command.values[0],
                    (GLint)command.values[1],
                    (GLint)command.values[2]);
    } else if (type == "ivec4" || type == "bvec4" || type == "uvec4") {
        glUniform4i(location, (GLint)command.values[0],
                    (GLint)command.values[1],
                    (GLint)command.values[2],
                    (GLint)command.values[3]);
    } else {
        report("[INFO] Piglit manifest unsupported uniform type %s.\n",
               type.c_str());
        return false;
    }
    return expect_no_error(report, "manifest uniform");
}

static int expected_u8(float value) {
    int converted = (int)(value * 255.0f + 0.5f);
    if (converted < 0) {
        converted = 0;
    } else if (converted > 255) {
        converted = 255;
    }
    return converted;
}

static bool probe_pixel(const GLubyte *pixel, const float *expected) {
    const int tolerance = 3;
    for (int c = 0; c < 4; ++c) {
        int diff = (int)pixel[c] - expected_u8(expected[c]);
        if (diff < 0) {
            diff = -diff;
        }
        if (diff > tolerance) {
            return false;
        }
    }
    return true;
}

static bool run_probe_all(PiglitReportFunc report, const char *case_name,
                          const TestCommand &command) {
    std::vector<GLubyte> pixels(kTargetWidth * kTargetHeight * 4);
    glReadPixels(0, 0, kTargetWidth, kTargetHeight, GL_RGBA,
                 GL_UNSIGNED_BYTE, &pixels[0]);
    if (!expect_no_error(report, "manifest probe all rgba")) {
        return false;
    }
    for (int i = 0; i < kTargetWidth * kTargetHeight; ++i) {
        if (!probe_pixel(&pixels[(size_t)i * 4], command.values)) {
            const GLubyte *p = &pixels[(size_t)i * 4];
            report("[FAIL] Piglit manifest %s pixel %d returned "
                   "{%u,%u,%u,%u}, expected {%d,%d,%d,%d}.\n",
                   case_name, i, p[0], p[1], p[2], p[3],
                   expected_u8(command.values[0]),
                   expected_u8(command.values[1]),
                   expected_u8(command.values[2]),
                   expected_u8(command.values[3]));
            return false;
        }
    }
    return true;
}

static bool run_probe(PiglitReportFunc report, const char *case_name,
                      const TestCommand &command) {
    GLubyte pixel[4] = {0, 0, 0, 0};
    const int x = (int)command.values[0];
    const int y = (int)command.values[1];
    if (x < 0 || y < 0 || x >= kTargetWidth || y >= kTargetHeight) {
        report("[FAIL] Piglit manifest %s probe outside target.\n",
               case_name);
        return false;
    }
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (!expect_no_error(report, "manifest probe rgba")) {
        return false;
    }
    if (probe_pixel(pixel, &command.values[2])) {
        return true;
    }
    report("[FAIL] Piglit manifest %s probe returned {%u,%u,%u,%u}, "
           "expected {%d,%d,%d,%d}.\n",
           case_name, pixel[0], pixel[1], pixel[2], pixel[3],
           expected_u8(command.values[2]), expected_u8(command.values[3]),
           expected_u8(command.values[4]), expected_u8(command.values[5]));
    return false;
}

static void destroy_objects(ProgramObjects *obj) {
    glUseProgram(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (obj->vbo) glDeleteBuffers(1, &obj->vbo);
    if (obj->vao) glDeleteVertexArrays(1, &obj->vao);
    if (obj->texture) glDeleteTextures(1, &obj->texture);
    if (obj->fbo) glDeleteFramebuffers(1, &obj->fbo);
    if (obj->program) glDeleteProgram(obj->program);
    if (obj->vertex_shader) glDeleteShader(obj->vertex_shader);
    if (obj->fragment_shader) glDeleteShader(obj->fragment_shader);
    memset(obj, 0, sizeof(*obj));
}

static PiglitResult run_shader_test(PiglitReportFunc report,
                                    const char *case_name,
                                    const std::string &contents,
                                    std::string *detail) {
    const ShaderTest test = parse_shader_test(contents);
    std::vector<TestCommand> commands;
    ProgramObjects obj = {};
    bool linked = false;
    bool link_error_expected = false;

    if (unsupported_shader_sections(test, detail)) {
        return PIGLIT_RESULT_SKIP;
    }
    if (!requirements_supported(test, detail)) {
        return PIGLIT_RESULT_SKIP;
    }
    if (!parse_commands(test, &commands, detail)) {
        return PIGLIT_RESULT_SKIP;
    }
    if (!command_list_has_draw_or_link(commands)) {
        *detail = "shader_test has no supported draw/link command";
        return PIGLIT_RESULT_SKIP;
    }

    for (size_t i = 0; i < commands.size(); ++i) {
        if (commands[i].type == TestCommand::LinkError) {
            link_error_expected = true;
        }
    }

    clear_gl_errors();
    obj.vertex_shader = compile_shader(
        report, GL_VERTEX_SHADER,
        test.vertex_shader.empty() ? std::string(fallback_vertex_shader())
                                   : test.vertex_shader,
        case_name);
    obj.fragment_shader = compile_shader(report, GL_FRAGMENT_SHADER,
                                         test.fragment_shader, case_name);
    if (!obj.vertex_shader || !obj.fragment_shader) {
        *detail = "shader compile failed";
        destroy_objects(&obj);
        return link_error_expected ? PIGLIT_RESULT_PASS : PIGLIT_RESULT_FAIL;
    }

    obj.program = link_program(report, obj.vertex_shader, obj.fragment_shader,
                               case_name, &linked);
    if (!obj.program) {
        *detail = "program allocation failed";
        destroy_objects(&obj);
        return PIGLIT_RESULT_FAIL;
    }
    if (!linked) {
        *detail = link_error_expected ? "link failed as expected"
                                      : "program link failed";
        destroy_objects(&obj);
        return link_error_expected ? PIGLIT_RESULT_PASS : PIGLIT_RESULT_FAIL;
    }
    if (link_error_expected) {
        *detail = "link succeeded but Piglit expected an error";
        destroy_objects(&obj);
        return PIGLIT_RESULT_FAIL;
    }

    if (!setup_render_target(report, &obj)) {
        *detail = "render target setup failed";
        destroy_objects(&obj);
        return PIGLIT_RESULT_FAIL;
    }

    glViewport(0, 0, kTargetWidth, kTargetHeight);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(obj.program);

    for (size_t i = 0; i < commands.size(); ++i) {
        const TestCommand &command = commands[i];
        switch (command.type) {
            case TestCommand::ClearColor:
                glClearColor(command.values[0], command.values[1],
                             command.values[2], command.values[3]);
                if (!expect_no_error(report, "manifest clear color")) {
                    *detail = "clear color failed";
                    destroy_objects(&obj);
                    return PIGLIT_RESULT_FAIL;
                }
                break;
            case TestCommand::Clear:
                glClear(GL_COLOR_BUFFER_BIT);
                if (!expect_no_error(report, "manifest clear")) {
                    *detail = "clear failed";
                    destroy_objects(&obj);
                    return PIGLIT_RESULT_FAIL;
                }
                break;
            case TestCommand::Uniform:
                if (!apply_uniform(report, obj.program, command)) {
                    *detail = "uniform setup failed";
                    destroy_objects(&obj);
                    return PIGLIT_RESULT_FAIL;
                }
                break;
            case TestCommand::DrawRect:
                if (!setup_rect_vertices(report, &obj, command)) {
                    *detail = "draw rect vertex setup failed";
                    destroy_objects(&obj);
                    return PIGLIT_RESULT_FAIL;
                }
                glDrawArrays(GL_TRIANGLES, 0, 6);
                if (!expect_no_error(report, "manifest draw rect")) {
                    *detail = "draw rect failed";
                    destroy_objects(&obj);
                    return PIGLIT_RESULT_FAIL;
                }
                break;
            case TestCommand::ProbeAllRgba:
                glFinish();
                if (!run_probe_all(report, case_name, command)) {
                    *detail = "probe all rgba failed";
                    destroy_objects(&obj);
                    return PIGLIT_RESULT_FAIL;
                }
                break;
            case TestCommand::ProbeRgba:
                glFinish();
                if (!run_probe(report, case_name, command)) {
                    *detail = "probe rgba failed";
                    destroy_objects(&obj);
                    return PIGLIT_RESULT_FAIL;
                }
                break;
            case TestCommand::LinkSuccess:
            case TestCommand::LinkError:
                break;
        }
    }

    *detail = "executed shader_test";
    destroy_objects(&obj);
    return PIGLIT_RESULT_PASS;
}

static bool load_manifest(const char *path,
                          std::vector<ManifestEntry> *entries) {
    std::string contents;
    if (!read_text_file(path, 4 * 1024 * 1024, &contents)) {
        return false;
    }

    size_t pos = 0;
    while (pos <= contents.size()) {
        size_t next = contents.find('\n', pos);
        if (next == std::string::npos) {
            next = contents.size();
        }
        std::string line = trim(contents.substr(pos, next - pos));
        if (!line.empty() && line[0] != '#') {
            const std::vector<std::string> fields = split_tabs(line);
            if (fields.size() >= 3 && fields[0] != "name") {
                ManifestEntry entry;
                entry.name = fields[0];
                entry.kind = fields[1];
                entry.path = fields[2];
                entries->push_back(entry);
            }
        }
        if (next == contents.size()) {
            break;
        }
        pos = next + 1;
    }
    return true;
}

static std::string source_path_for(const char *manifest_path,
                                   const ManifestEntry &entry) {
    std::string base = manifest_path;
    const size_t slash = base.find_last_of('/');
    if (slash != std::string::npos) {
        base.resize(slash + 1);
    } else {
        base.clear();
    }
    return base + "piglit/" + entry.path;
}

static PiglitResult run_manifest_entry(PiglitReportFunc report,
                                       const char *manifest_path,
                                       const ManifestEntry &entry,
                                       std::string *detail) {
    if (entry.kind != "shader_test") {
        *detail = "non-shader Piglit case not ported to RPX runner yet";
        return PIGLIT_RESULT_SKIP;
    }

    const std::string source_path = source_path_for(manifest_path, entry);
    std::string contents;
    if (!read_text_file(source_path.c_str(), kMaxShaderTestBytes,
                        &contents)) {
        *detail = "shader_test source missing or too large";
        return PIGLIT_RESULT_FAIL;
    }
    return run_shader_test(report, entry.name.c_str(), contents, detail);
}

} // namespace

PiglitRunStats run_piglit_manifest_tests(PiglitReportFunc report,
                                         PiglitResultFunc result_func,
                                         PiglitContinueFunc continue_func,
                                         void *user_data) {
    PiglitRunStats stats = {};
    std::vector<ManifestEntry> entries;
    const char *manifest_path = NULL;

    if (!report) {
        return stats;
    }
    if (load_manifest(kExternalManifest, &entries)) {
        manifest_path = kExternalManifest;
    } else if (load_manifest(kContentManifest, &entries)) {
        manifest_path = kContentManifest;
    } else {
        publish_result(&stats, result_func, "piglit/external-manifest",
                       PIGLIT_RESULT_SKIP,
                       "copy gx2gl/piglit_manifest.tsv to SD or content",
                       user_data);
        return stats;
    }

    report("[INFO] Piglit manifest loaded %u cases from %s\n",
           (unsigned int)entries.size(), manifest_path);

    for (size_t i = 0; i < entries.size(); ++i) {
        if (continue_func && !continue_func(user_data)) {
            break;
        }
        std::string detail;
        const PiglitResult result =
            run_manifest_entry(report, manifest_path, entries[i], &detail);
        publish_result(&stats, result_func, entries[i].name.c_str(), result,
                       detail.c_str(), user_data);
        if ((i & 63u) == 63u) {
            glReleaseShaderCompiler();
        }
    }
    glReleaseShaderCompiler();

    return stats;
}
