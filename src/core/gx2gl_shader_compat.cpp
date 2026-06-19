#include "gx2gl_shader_compat.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

namespace {

static const int kMaxShaderInterfaceLocations = 16;

struct LayoutInfo {
  bool present;
  bool has_location;
  bool has_binding;
  int location;
  int binding;
};

struct UniformDecl {
  bool is_block;
  LayoutInfo layout;
  std::string type;
  std::string name;
};

struct InterfaceDecl {
  LayoutInfo layout;
  std::string storage;
  std::string type;
  std::string name;
  int array_size;
};

struct CollectedInterfaceDecl {
  InterfaceDecl decl;
  uint32_t line_number;
  int location_span;
};

static void write_info_log(char *info_log_out, int info_log_max_length,
                           const char *message) {
  if (info_log_out && info_log_max_length > 0) {
    snprintf(info_log_out, (size_t)info_log_max_length, "%s",
             message ? message : "");
  }
}

static bool is_identifier_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}

static size_t skip_whitespace(const std::string &line, size_t index) {
  while (index < line.size() &&
         (line[index] == ' ' || line[index] == '\t' ||
          line[index] == '\r')) {
    ++index;
  }
  return index;
}

static bool starts_with_keyword(const std::string &line, size_t index,
                                const char *keyword) {
  size_t length = strlen(keyword);
  size_t end = index + length;

  if (end > line.size() || line.compare(index, length, keyword) != 0) {
    return false;
  }
  if (index > 0 && is_identifier_char(line[index - 1])) {
    return false;
  }
  if (end < line.size() && is_identifier_char(line[end])) {
    return false;
  }
  return true;
}

static bool consume_keyword(const std::string &line, size_t *index,
                            const char *keyword) {
  size_t cursor = skip_whitespace(line, *index);
  if (!starts_with_keyword(line, cursor, keyword)) {
    return false;
  }
  *index = cursor + strlen(keyword);
  return true;
}

static bool consume_identifier(const std::string &line, size_t *index,
                               std::string *identifier) {
  size_t cursor = skip_whitespace(line, *index);
  size_t start = cursor;

  if (cursor >= line.size() ||
      !((line[cursor] >= 'a' && line[cursor] <= 'z') ||
        (line[cursor] >= 'A' && line[cursor] <= 'Z') ||
        line[cursor] == '_')) {
    return false;
  }

  ++cursor;
  while (cursor < line.size() && is_identifier_char(line[cursor])) {
    ++cursor;
  }

  if (identifier) {
    *identifier = line.substr(start, cursor - start);
  }
  *index = cursor;
  return true;
}

static bool layout_key_matches(const std::string &layout, size_t index,
                               const char *key) {
  size_t length = strlen(key);
  size_t end = index + length;

  if (end > layout.size() || layout.compare(index, length, key) != 0) {
    return false;
  }
  if (index > 0 && is_identifier_char(layout[index - 1])) {
    return false;
  }
  if (end < layout.size() && is_identifier_char(layout[end])) {
    return false;
  }
  return true;
}

static bool read_layout_integer(const std::string &layout, const char *key,
                                int *value) {
  for (size_t cursor = 0; cursor < layout.size(); ++cursor) {
    if (!layout_key_matches(layout, cursor, key)) {
      continue;
    }

    cursor += strlen(key);
    cursor = skip_whitespace(layout, cursor);
    if (cursor >= layout.size() || layout[cursor] != '=') {
      return false;
    }
    cursor = skip_whitespace(layout, cursor + 1u);
    if (cursor >= layout.size() || layout[cursor] < '0' ||
        layout[cursor] > '9') {
      return false;
    }

    *value = 0;
    while (cursor < layout.size() && layout[cursor] >= '0' &&
           layout[cursor] <= '9') {
      int digit = layout[cursor] - '0';
      if (*value > (INT_MAX - digit) / 10) {
        return false;
      }
      *value = (*value * 10) + digit;
      ++cursor;
    }
    return true;
  }

  return false;
}

static bool consume_layout(const std::string &line, size_t *index,
                           LayoutInfo *layout) {
  size_t cursor = skip_whitespace(line, *index);
  size_t paren_start;
  size_t paren_end;
  std::string layout_body;

  memset(layout, 0, sizeof(*layout));
  layout->location = -1;
  layout->binding = -1;

  if (!starts_with_keyword(line, cursor, "layout")) {
    *index = cursor;
    return true;
  }

  cursor += strlen("layout");
  cursor = skip_whitespace(line, cursor);
  if (cursor >= line.size() || line[cursor] != '(') {
    return false;
  }

  paren_start = cursor;
  paren_end = line.find(')', paren_start + 1u);
  if (paren_end == std::string::npos) {
    return false;
  }

  layout_body = line.substr(paren_start + 1u, paren_end - paren_start - 1u);
  layout->present = true;
  layout->has_location = read_layout_integer(layout_body, "location",
                                             &layout->location);
  layout->has_binding = read_layout_integer(layout_body, "binding",
                                            &layout->binding);
  *index = paren_end + 1u;
  return true;
}

static void consume_interpolation_qualifiers(const std::string &line,
                                             size_t *index) {
  bool consumed = true;
  while (consumed) {
    consumed = consume_keyword(line, index, "flat") ||
               consume_keyword(line, index, "smooth") ||
               consume_keyword(line, index, "noperspective") ||
               consume_keyword(line, index, "centroid") ||
               consume_keyword(line, index, "sample") ||
               consume_keyword(line, index, "invariant");
  }
}

static void consume_precision_qualifier(const std::string &line,
                                        size_t *index) {
  if (consume_keyword(line, index, "lowp") ||
      consume_keyword(line, index, "mediump") ||
      consume_keyword(line, index, "highp")) {
    return;
  }
}

static std::string strip_line_comment(const std::string &line) {
  size_t comment = line.find("//");
  if (comment == std::string::npos) {
    return line;
  }
  return line.substr(0, comment);
}

static std::string leading_whitespace(const std::string &line) {
  size_t count = 0;
  while (count < line.size() &&
         (line[count] == ' ' || line[count] == '\t')) {
    ++count;
  }
  return line.substr(0, count);
}

static int vector_component_count(const std::string &type) {
  if (type == "vec2" || type == "ivec2" || type == "uvec2" ||
      type == "bvec2") {
    return 2;
  }
  if (type == "vec3" || type == "ivec3" || type == "uvec3" ||
      type == "bvec3") {
    return 3;
  }
  if (type == "vec4" || type == "ivec4" || type == "uvec4" ||
      type == "bvec4") {
    return 4;
  }
  return 1;
}

static bool parse_uniform_declaration(const std::string &line,
                                      UniformDecl *decl) {
  size_t cursor = 0;
  std::string first;
  std::string second;

  decl->is_block = false;
  decl->layout = LayoutInfo();
  decl->layout.location = -1;
  decl->layout.binding = -1;
  decl->type.clear();
  decl->name.clear();

  if (!consume_layout(line, &cursor, &decl->layout)) {
    return false;
  }
  if (!consume_keyword(line, &cursor, "uniform")) {
    return false;
  }
  if (!consume_identifier(line, &cursor, &first)) {
    return false;
  }

  cursor = skip_whitespace(line, cursor);
  if (cursor < line.size() && line[cursor] == '{') {
    decl->is_block = true;
    decl->name = first;
    return true;
  }

  if (!consume_identifier(line, &cursor, &second)) {
    return false;
  }

  decl->type = first;
  decl->name = second;
  return true;
}

static bool parse_interface_declaration(const std::string &line,
                                        InterfaceDecl *decl) {
  size_t cursor = 0;
  size_t name_end;

  decl->layout = LayoutInfo();
  decl->layout.location = -1;
  decl->layout.binding = -1;
  decl->storage.clear();
  decl->type.clear();
  decl->name.clear();
  decl->array_size = 1;

  if (!consume_layout(line, &cursor, &decl->layout)) {
    return false;
  }
  consume_interpolation_qualifiers(line, &cursor);

  if (consume_keyword(line, &cursor, "in")) {
    decl->storage = "in";
  } else if (consume_keyword(line, &cursor, "out")) {
    decl->storage = "out";
  } else {
    return false;
  }

  consume_precision_qualifier(line, &cursor);
  if (!consume_identifier(line, &cursor, &decl->type) ||
      !consume_identifier(line, &cursor, &decl->name)) {
    return false;
  }
  name_end = skip_whitespace(line, cursor);
  if (name_end < line.size() && line[name_end] == '[') {
    size_t close = line.find(']', name_end + 1u);
    int size = 0;
    if (close == std::string::npos) {
      return false;
    }
    for (size_t i = name_end + 1u; i < close; ++i) {
      if (line[i] == ' ' || line[i] == '\t' || line[i] == '\r') {
        continue;
      }
      if (line[i] < '0' || line[i] > '9') {
        return false;
      }
      int digit = line[i] - '0';
      if (size > (INT_MAX - digit) / 10) {
        return false;
      }
      size = (size * 10) + digit;
    }
    if (size <= 0) {
      return false;
    }
    decl->array_size = size;
  }
  return true;
}

static bool is_sampler_type(const std::string &type) {
  return type.find("sampler") != std::string::npos;
}

static bool is_builtin_name(const std::string &name) {
  return name.compare(0, 3, "gl_") == 0;
}

static bool shader_output_type_info(const std::string &type, GLenum *gl_type,
                                    uint32_t *component_count) {
  struct TypeInfo {
    const char *name;
    GLenum gl_type;
    uint32_t components;
  };
  static const TypeInfo kTypes[] = {
      {"float", GL_FLOAT, 1},
      {"vec2", GL_FLOAT_VEC2, 2},
      {"vec3", GL_FLOAT_VEC3, 3},
      {"vec4", GL_FLOAT_VEC4, 4},
      {"int", GL_INT, 1},
      {"ivec2", GL_INT_VEC2, 2},
      {"ivec3", GL_INT_VEC3, 3},
      {"ivec4", GL_INT_VEC4, 4},
      {"uint", GL_UNSIGNED_INT, 1},
      {"uvec2", GL_UNSIGNED_INT_VEC2, 2},
      {"uvec3", GL_UNSIGNED_INT_VEC3, 3},
      {"uvec4", GL_UNSIGNED_INT_VEC4, 4},
      {"bool", GL_BOOL, 1},
      {"bvec2", GL_BOOL_VEC2, 2},
      {"bvec3", GL_BOOL_VEC3, 3},
      {"bvec4", GL_BOOL_VEC4, 4},
      {"mat2", GL_FLOAT_MAT2, 4},
      {"mat3", GL_FLOAT_MAT3, 9},
      {"mat4", GL_FLOAT_MAT4, 16},
      {"mat2x3", GL_FLOAT_MAT2x3, 6},
      {"mat2x4", GL_FLOAT_MAT2x4, 8},
      {"mat3x2", GL_FLOAT_MAT3x2, 6},
      {"mat3x4", GL_FLOAT_MAT3x4, 12},
      {"mat4x2", GL_FLOAT_MAT4x2, 8},
      {"mat4x3", GL_FLOAT_MAT4x3, 12},
  };

  for (size_t i = 0; i < sizeof(kTypes) / sizeof(kTypes[0]); ++i) {
    if (type == kTypes[i].name) {
      if (gl_type) *gl_type = kTypes[i].gl_type;
      if (component_count) *component_count = kTypes[i].components;
      return true;
    }
  }

  return false;
}

static int shader_type_location_span(const std::string &type) {
  struct TypeSpan {
    const char *name;
    int span;
  };
  static const TypeSpan kSpans[] = {
      {"float", 1}, {"vec2", 1}, {"vec3", 1}, {"vec4", 1},
      {"int", 1},   {"ivec2", 1}, {"ivec3", 1}, {"ivec4", 1},
      {"uint", 1},  {"uvec2", 1}, {"uvec3", 1}, {"uvec4", 1},
      {"bool", 1},  {"bvec2", 1}, {"bvec3", 1}, {"bvec4", 1},
      {"mat2", 2},  {"mat3", 3},  {"mat4", 4},
      {"mat2x3", 2}, {"mat2x4", 2},
      {"mat3x2", 3}, {"mat3x4", 3},
      {"mat4x2", 4}, {"mat4x3", 4},
  };

  for (size_t i = 0; i < sizeof(kSpans) / sizeof(kSpans[0]); ++i) {
    if (type == kSpans[i].name) {
      return kSpans[i].span;
    }
  }

  return 0;
}

static const char *shader_stage_name(GLenum shader_type) {
  switch (shader_type) {
  case GL_VERTEX_SHADER:
    return "vertex";
  case GL_FRAGMENT_SHADER:
    return "fragment";
  default:
    return "shader";
  }
}

static bool validate_cafeglsl_contract(const char *source, GLenum shader_type,
                                       char *info_log_out,
                                       int info_log_max_length) {
  const char *line_start = source;
  uint32_t line_number = 1;

  if (shader_type != GL_VERTEX_SHADER && shader_type != GL_FRAGMENT_SHADER) {
    return true;
  }

  while (*line_start) {
    const char *line_end = strchr(line_start, '\n');
    size_t line_length = line_end ? (size_t)(line_end - line_start)
                                  : strlen(line_start);
    std::string line = strip_line_comment(std::string(line_start, line_length));
    size_t first = skip_whitespace(line, 0);
    UniformDecl uniform_decl;
    InterfaceDecl interface_decl;

    if (first < line.size() && line[first] != '#') {
      if (parse_uniform_declaration(line, &uniform_decl)) {
        if (uniform_decl.is_block && !uniform_decl.layout.has_binding) {
          char message[256];
          snprintf(message, sizeof(message),
                   "CafeGLSL requires layout(binding = N) on uniform block '%s' "
                   "at line %u.",
                   uniform_decl.name.c_str(), line_number);
          write_info_log(info_log_out, info_log_max_length, message);
          return false;
        }
        if (!uniform_decl.is_block && is_sampler_type(uniform_decl.type) &&
            !uniform_decl.layout.has_binding) {
          char message[256];
          snprintf(message, sizeof(message),
                   "CafeGLSL requires layout(binding = N) on sampler '%s' "
                   "at line %u.",
                   uniform_decl.name.c_str(), line_number);
          write_info_log(info_log_out, info_log_max_length, message);
          return false;
        }
      } else if (parse_interface_declaration(line, &interface_decl) &&
                 !is_builtin_name(interface_decl.name) &&
                 !interface_decl.layout.has_location) {
        char message[256];
        snprintf(message, sizeof(message),
                 "CafeGLSL requires layout(location = N) on %s shader %s "
                 "'%s' at line %u.",
                 shader_stage_name(shader_type), interface_decl.storage.c_str(),
                 interface_decl.name.c_str(), line_number);
        write_info_log(info_log_out, info_log_max_length, message);
        return false;
      }
    }

    if (!line_end) {
      break;
    }
    line_start = line_end + 1;
    ++line_number;
  }

  return true;
}

static bool collect_interface_declarations(
    const char *source, GLenum shader_type, const char *storage,
    std::vector<CollectedInterfaceDecl> *decls, char *info_log_out,
    int info_log_max_length) {
  const char *line_start = source;
  uint32_t line_number = 1;

  if (!source || !storage || !decls) {
    write_info_log(info_log_out, info_log_max_length,
                   "Missing shader source for interface validation.");
    return false;
  }

  while (*line_start) {
    const char *line_end = strchr(line_start, '\n');
    size_t line_length = line_end ? (size_t)(line_end - line_start)
                                  : strlen(line_start);
    std::string line = strip_line_comment(std::string(line_start, line_length));
    size_t first = skip_whitespace(line, 0);
    InterfaceDecl decl;

    if (first < line.size() && line[first] != '#' &&
        parse_interface_declaration(line, &decl) &&
        decl.storage == storage && !is_builtin_name(decl.name)) {
      int span = shader_type_location_span(decl.type);
      CollectedInterfaceDecl collected;

      if (!decl.layout.has_location) {
        char message[256];
        snprintf(message, sizeof(message),
                 "CafeGLSL requires layout(location = N) on %s shader %s "
                 "'%s' at line %u.",
                 shader_stage_name(shader_type), decl.storage.c_str(),
                 decl.name.c_str(), line_number);
        write_info_log(info_log_out, info_log_max_length, message);
        return false;
      }
      if (span <= 0) {
        char message[256];
        snprintf(message, sizeof(message),
                 "Unsupported %s shader interface type '%s' for '%s' at line %u.",
                 shader_stage_name(shader_type), decl.type.c_str(),
                 decl.name.c_str(), line_number);
        write_info_log(info_log_out, info_log_max_length, message);
        return false;
      }
      if (decl.layout.location < 0 ||
          decl.array_size > kMaxShaderInterfaceLocations / span ||
          decl.layout.location >
              kMaxShaderInterfaceLocations - (span * decl.array_size)) {
        char message[256];
        snprintf(message, sizeof(message),
                 "%s shader %s '%s' exceeds the supported interface location "
                 "range at line %u.",
                 shader_stage_name(shader_type), decl.storage.c_str(),
                 decl.name.c_str(), line_number);
        write_info_log(info_log_out, info_log_max_length, message);
        return false;
      }

      collected.decl = decl;
      collected.line_number = line_number;
      collected.location_span = span * decl.array_size;
      decls->push_back(collected);
    }

    if (!line_end) {
      break;
    }
    line_start = line_end + 1;
    ++line_number;
  }

  return true;
}

static bool validate_interface_location_ranges(
    const std::vector<CollectedInterfaceDecl> &decls, GLenum shader_type,
    const char *storage, char *info_log_out, int info_log_max_length) {
  for (size_t i = 0; i < decls.size(); ++i) {
    int a_start = decls[i].decl.layout.location;
    int a_end = a_start + decls[i].location_span;

    for (size_t j = i + 1; j < decls.size(); ++j) {
      int b_start = decls[j].decl.layout.location;
      int b_end = b_start + decls[j].location_span;
      if (a_start < b_end && b_start < a_end) {
        char message[256];
        snprintf(message, sizeof(message),
                 "%s shader %s variables '%s' and '%s' overlap location %d.",
                 shader_stage_name(shader_type), storage,
                 decls[i].decl.name.c_str(), decls[j].decl.name.c_str(),
                 b_start > a_start ? b_start : a_start);
        write_info_log(info_log_out, info_log_max_length, message);
        return false;
      }
    }
  }

  return true;
}

static char *copy_source(const char *source) {
  size_t length = strlen(source);
  char *copy = (char *)malloc(length + 1u);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, source, length + 1u);
  return copy;
}

static void collect_fragment_vec4_outputs(
    const std::string &source, std::vector<std::string> *outputs) {
  size_t line_start = 0;

  if (!outputs) return;
  while (line_start < source.size()) {
    size_t line_end = source.find('\n', line_start);
    size_t line_length =
        line_end == std::string::npos ? source.size() - line_start
                                     : line_end - line_start;
    InterfaceDecl decl;
    std::string line =
        strip_line_comment(source.substr(line_start, line_length));

    if (parse_interface_declaration(line, &decl) && decl.storage == "out" &&
        vector_component_count(decl.type) == 4) {
      outputs->push_back(decl.name);
    }

    if (line_end == std::string::npos) break;
    line_start = line_end + 1u;
  }
}

static size_t find_main_closing_brace(const std::string &source) {
  size_t main_pos = source.find("void main");
  size_t open_brace;
  int depth = 0;
  bool line_comment = false;
  bool block_comment = false;

  if (main_pos == std::string::npos) return std::string::npos;
  open_brace = source.find('{', main_pos);
  if (open_brace == std::string::npos) return std::string::npos;

  for (size_t i = open_brace; i < source.size(); ++i) {
    char c = source[i];
    char next = i + 1u < source.size() ? source[i + 1u] : '\0';

    if (line_comment) {
      if (c == '\n') line_comment = false;
      continue;
    }
    if (block_comment) {
      if (c == '*' && next == '/') {
        block_comment = false;
        ++i;
      }
      continue;
    }
    if (c == '/' && next == '/') {
      line_comment = true;
      ++i;
      continue;
    }
    if (c == '/' && next == '*') {
      block_comment = true;
      ++i;
      continue;
    }
    if (c == '{') {
      ++depth;
    } else if (c == '}') {
      --depth;
      if (depth == 0) return i;
    }
  }

  return std::string::npos;
}

static std::string normalize_fragment_output_channels(
    const std::string &source) {
  std::vector<std::string> outputs;
  size_t closing_brace;
  size_t line_start;
  std::string indent;
  std::string assignments;

  collect_fragment_vec4_outputs(source, &outputs);
  if (outputs.empty()) return source;

  closing_brace = find_main_closing_brace(source);
  if (closing_brace == std::string::npos) return source;

  line_start = source.rfind('\n', closing_brace);
  line_start = line_start == std::string::npos ? 0 : line_start + 1u;
  indent = leading_whitespace(source.substr(line_start,
                                             closing_brace - line_start));

  // CafeGLSL emits runtime pixel-export lanes in wzyx order.
  for (size_t i = 0; i < outputs.size(); ++i) {
    assignments += indent + "    " + outputs[i] + " = " + outputs[i] +
                   ".wzyx;\n";
  }

  return source.substr(0, closing_brace) + assignments +
         source.substr(closing_brace);
}

static char *lower_source_for_cafeglsl(const char *source,
                                       GLenum shader_type) {
  std::string lowered_source(source);

  if (shader_type == GL_FRAGMENT_SHADER) {
    lowered_source = normalize_fragment_output_channels(lowered_source);
  }
  return copy_source(lowered_source.c_str());
}

} // namespace

char *gx2gl_prepare_shader_source_for_cafeglsl(const char *source,
                                               GLenum shader_type) {
  return gx2gl_prepare_shader_source_for_cafeglsl_ex(source, shader_type, NULL, 0);
}

char *gx2gl_prepare_shader_source_for_cafeglsl_ex(const char *source,
                                                  GLenum shader_type,
                                                  char *info_log_out,
                                                  int info_log_max_length) {
  if (!source) {
    write_info_log(info_log_out, info_log_max_length,
                   "No shader source was provided.");
    return NULL;
  }

  if (!validate_cafeglsl_contract(source, shader_type, info_log_out,
                                  info_log_max_length)) {
    return NULL;
  }

  return lower_source_for_cafeglsl(source, shader_type);
}

bool gx2gl_validate_program_shader_interfaces(const char *vertex_source,
                                              const char *fragment_source,
                                              char *info_log_out,
                                              int info_log_max_length) {
  std::vector<CollectedInterfaceDecl> vertex_outputs;
  std::vector<CollectedInterfaceDecl> fragment_inputs;

  if (!collect_interface_declarations(vertex_source, GL_VERTEX_SHADER, "out",
                                      &vertex_outputs, info_log_out,
                                      info_log_max_length) ||
      !collect_interface_declarations(fragment_source, GL_FRAGMENT_SHADER, "in",
                                      &fragment_inputs, info_log_out,
                                      info_log_max_length)) {
    return false;
  }

  if (!validate_interface_location_ranges(vertex_outputs, GL_VERTEX_SHADER,
                                          "out", info_log_out,
                                          info_log_max_length) ||
      !validate_interface_location_ranges(fragment_inputs, GL_FRAGMENT_SHADER,
                                          "in", info_log_out,
                                          info_log_max_length)) {
    return false;
  }

  for (size_t i = 0; i < fragment_inputs.size(); ++i) {
    const CollectedInterfaceDecl *match = NULL;

    for (size_t j = 0; j < vertex_outputs.size(); ++j) {
      if (vertex_outputs[j].decl.layout.location ==
          fragment_inputs[i].decl.layout.location) {
        match = &vertex_outputs[j];
        break;
      }
    }

    if (!match) {
      char message[256];
      snprintf(message, sizeof(message),
               "Fragment shader input '%s' at location %d has no matching "
               "vertex shader output.",
               fragment_inputs[i].decl.name.c_str(),
               fragment_inputs[i].decl.layout.location);
      write_info_log(info_log_out, info_log_max_length, message);
      return false;
    }

    if (match->decl.type != fragment_inputs[i].decl.type ||
        match->decl.array_size != fragment_inputs[i].decl.array_size ||
        match->location_span != fragment_inputs[i].location_span) {
      char message[256];
      snprintf(message, sizeof(message),
               "Shader interface location %d type mismatch: vertex output "
               "'%s' is %s[%d], fragment input '%s' is %s[%d].",
               fragment_inputs[i].decl.layout.location,
               match->decl.name.c_str(), match->decl.type.c_str(),
               match->decl.array_size, fragment_inputs[i].decl.name.c_str(),
               fragment_inputs[i].decl.type.c_str(),
               fragment_inputs[i].decl.array_size);
      write_info_log(info_log_out, info_log_max_length, message);
      return false;
    }
  }

  write_info_log(info_log_out, info_log_max_length, "");
  return true;
}

bool gx2gl_find_shader_output_info(const char *source, const char *name,
                                   GLenum *type_out, GLint *size_out,
                                   uint32_t *component_count_out) {
  const char *line_start;

  if (!source || !name) {
    return false;
  }

  if (strcmp(name, "gl_Position") == 0) {
    if (type_out) *type_out = GL_FLOAT_VEC4;
    if (size_out) *size_out = 1;
    if (component_count_out) *component_count_out = 4;
    return true;
  }

  line_start = source;
  while (*line_start) {
    const char *line_end = strchr(line_start, '\n');
    size_t line_length = line_end ? (size_t)(line_end - line_start)
                                  : strlen(line_start);
    std::string line = strip_line_comment(std::string(line_start, line_length));
    size_t first = skip_whitespace(line, 0);
    InterfaceDecl decl;

    if (first < line.size() && line[first] != '#' &&
        parse_interface_declaration(line, &decl) && decl.storage == "out" &&
        decl.name == name) {
      GLenum type = 0;
      uint32_t components = 0;
      if (!shader_output_type_info(decl.type, &type, &components)) {
        return false;
      }
      if (type_out) *type_out = type;
      if (size_out) *size_out = decl.array_size;
      if (component_count_out) {
        *component_count_out = components * (uint32_t)decl.array_size;
      }
      return true;
    }

    if (!line_end) {
      break;
    }
    line_start = line_end + 1;
  }

  return false;
}
