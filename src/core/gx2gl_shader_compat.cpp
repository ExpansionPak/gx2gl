#include "gx2gl_shader_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

namespace {

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
      *value = (*value * 10) + (layout[cursor] - '0');
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

  decl->layout = LayoutInfo();
  decl->layout.location = -1;
  decl->layout.binding = -1;
  decl->storage.clear();
  decl->type.clear();
  decl->name.clear();

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
  return true;
}

static bool is_sampler_type(const std::string &type) {
  return type.find("sampler") != std::string::npos;
}

static bool is_builtin_name(const std::string &name) {
  return name.compare(0, 3, "gl_") == 0;
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

static char *copy_source(const char *source) {
  size_t length = strlen(source);
  char *copy = (char *)malloc(length + 1u);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, source, length + 1u);
  return copy;
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

  return copy_source(source);
}
