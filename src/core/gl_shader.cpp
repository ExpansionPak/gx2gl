#include "gl_shader.h"
#include "gl_buffer.h"
#include "endian/endian.h"
#include "mem/gl_mem.h"
#include "gl_texture.h"

#ifdef __cplusplus
extern "C" {
#endif
#include <coreinit/cache.h>
#include <gfd.h>
#include <gx2/mem.h>
#include <gx2/shaders.h>
#include <gx2/state.h>
#include <string.h>
#include <whb/gfx.h>

#define MAX_PROGRAMS 512
#define MAX_SHADERS 512
#define MAX_PROGRAM_SAMPLERS 32

#define GL_LOCATION_STAGE_PIXEL 0x80000000u
#define GL_LOCATION_KIND_SAMPLER 0x40000000u
#define GL_LOCATION_BLOCK_MASK 0x3FFFu

typedef struct {
  bool in_use;
  GLenum type;
  char *source;
  uint32_t source_length;
  bool source_present;
  bool compile_requested;
} GLShader;

typedef struct {
  uint32_t size;
  void *buffer;
} UniformBlockShadow;

typedef struct {
  char *name;
  int32_t vertex_block_index;
  int32_t pixel_block_index;
  uint32_t vertex_location;
  uint32_t pixel_location;
  uint32_t vertex_size;
  uint32_t pixel_size;
  GLuint binding_point;
} ProgramUniformBlock;

typedef struct {
  bool in_use;
  bool linked;

  GLuint attached_vertex_shader;
  GLuint attached_pixel_shader;
  GLuint attached_geometry_shader;

  const WHBGfxShaderGroup *group;
  bool owns_group;
  WHBGfxShaderGroup owned_group;

  UniformBlockShadow *vs_blocks;
  uint32_t vs_block_count;

  UniformBlockShadow *ps_blocks;
  uint32_t ps_block_count;

  ProgramUniformBlock *uniform_blocks;
  uint32_t uniform_block_count;

  GLint vertex_sampler_units[MAX_PROGRAM_SAMPLERS];
  GLint pixel_sampler_units[MAX_PROGRAM_SAMPLERS];
} GLProgram;

static GLShader g_shaders[MAX_SHADERS];
static GLProgram g_programs[MAX_PROGRAMS];

static bool is_valid_shader(GLuint shader) {
  return shader > 0 && shader < MAX_SHADERS && g_shaders[shader].in_use;
}

static bool is_valid_program(GLuint program) {
  return program > 0 && program < MAX_PROGRAMS && g_programs[program].in_use;
}

static bool location_is_sampler(GLint location) {
  return (((uint32_t)location & GL_LOCATION_KIND_SAMPLER) != 0u);
}

static bool location_is_pixel(GLint location) {
  return (((uint32_t)location & GL_LOCATION_STAGE_PIXEL) != 0u);
}

static void free_shader_source(GLShader *shader) {
  if (!shader || !shader->source) {
    return;
  }
  gl_mem_free(GL_MEM_TYPE_MEM2, shader->source);
  shader->source = NULL;
  shader->source_length = 0;
  shader->source_present = false;
  shader->compile_requested = false;
}

static void free_shadow_blocks(UniformBlockShadow *blocks, uint32_t count) {
  if (!blocks) {
    return;
  }
  for (uint32_t i = 0; i < count; i++) {
    if (blocks[i].buffer) {
      gl_mem_free(GL_MEM_TYPE_MEM2, blocks[i].buffer);
    }
  }
  gl_mem_free(GL_MEM_TYPE_MEM2, blocks);
}

static void free_program_uniform_blocks(ProgramUniformBlock *blocks,
                                        uint32_t count) {
  if (!blocks) {
    return;
  }
  for (uint32_t i = 0; i < count; ++i) {
    if (blocks[i].name) {
      gl_mem_free(GL_MEM_TYPE_MEM2, blocks[i].name);
    }
  }
  gl_mem_free(GL_MEM_TYPE_MEM2, blocks);
}

static char *copy_program_string(const char *src) {
  size_t length;
  char *copy;

  if (!src) {
    return NULL;
  }

  length = strlen(src);
  copy = (char *)gl_mem_alloc(GL_MEM_TYPE_MEM2, (uint32_t)length + 1u, 4);
  if (!copy) {
    return NULL;
  }

  memcpy(copy, src, length + 1u);
  return copy;
}

static void clear_program_group(GLProgram *prog) {
  if (!prog) {
    return;
  }

  free_shadow_blocks(prog->vs_blocks, prog->vs_block_count);
  free_shadow_blocks(prog->ps_blocks, prog->ps_block_count);
  prog->vs_blocks = NULL;
  prog->ps_blocks = NULL;
  prog->vs_block_count = 0;
  prog->ps_block_count = 0;
  free_program_uniform_blocks(prog->uniform_blocks, prog->uniform_block_count);
  prog->uniform_blocks = NULL;
  prog->uniform_block_count = 0;

  if (prog->owns_group) {
    WHBGfxFreeShaderGroup(&prog->owned_group);
    memset(&prog->owned_group, 0, sizeof(prog->owned_group));
  }

  prog->group = NULL;
  prog->owns_group = false;
  prog->linked = false;
  memset(prog->vertex_sampler_units, 0, sizeof(prog->vertex_sampler_units));
  memset(prog->pixel_sampler_units, 0, sizeof(prog->pixel_sampler_units));
}

static int32_t find_program_uniform_block_index(const GLProgram *prog,
                                                const char *name) {
  if (!prog || !name) {
    return -1;
  }

  for (uint32_t i = 0; i < prog->uniform_block_count; ++i) {
    if (prog->uniform_blocks[i].name &&
        strcmp(prog->uniform_blocks[i].name, name) == 0) {
      return (int32_t)i;
    }
  }

  return -1;
}

static ProgramUniformBlock *find_program_uniform_block_for_stage(
    GLProgram *prog, uint32_t block_index, bool pixel_stage) {
  if (!prog) {
    return NULL;
  }

  for (uint32_t i = 0; i < prog->uniform_block_count; ++i) {
    if ((!pixel_stage && prog->uniform_blocks[i].vertex_block_index == (int32_t)block_index) ||
        (pixel_stage && prog->uniform_blocks[i].pixel_block_index == (int32_t)block_index)) {
      return &prog->uniform_blocks[i];
    }
  }

  return NULL;
}

static int32_t find_program_uniform_block_index_in_array(
    const ProgramUniformBlock *blocks, uint32_t count, const char *name) {
  if (!blocks || !name) {
    return -1;
  }

  for (uint32_t i = 0; i < count; ++i) {
    if (blocks[i].name && strcmp(blocks[i].name, name) == 0) {
      return (int32_t)i;
    }
  }

  return -1;
}

static bool build_program_uniform_blocks(GLProgram *prog,
                                         const GX2VertexShader *vertex_shader,
                                         const GX2PixelShader *pixel_shader) {
  uint32_t max_blocks;
  ProgramUniformBlock *blocks;
  uint32_t count = 0;

  max_blocks = (vertex_shader ? vertex_shader->uniformBlockCount : 0u) +
               (pixel_shader ? pixel_shader->uniformBlockCount : 0u);
  if (max_blocks == 0) {
    prog->uniform_blocks = NULL;
    prog->uniform_block_count = 0;
    return true;
  }

  blocks = (ProgramUniformBlock *)gl_mem_alloc(
      GL_MEM_TYPE_MEM2, sizeof(ProgramUniformBlock) * max_blocks, 4);
  if (!blocks) {
    return false;
  }
  memset(blocks, 0, sizeof(ProgramUniformBlock) * max_blocks);

  if (vertex_shader) {
    for (uint32_t i = 0; i < vertex_shader->uniformBlockCount; ++i) {
      const GX2UniformBlock *block = &vertex_shader->uniformBlocks[i];
      int32_t existing =
          find_program_uniform_block_index_in_array(blocks, count, block->name);
      ProgramUniformBlock *dst;

      if (existing >= 0) {
        dst = &blocks[existing];
      } else {
        dst = &blocks[count++];
        memset(dst, 0, sizeof(*dst));
        dst->name = copy_program_string(block->name);
        if (!dst->name) {
          free_program_uniform_blocks(blocks, max_blocks);
          return false;
        }
        dst->vertex_block_index = -1;
        dst->pixel_block_index = -1;
      }

      dst->vertex_block_index = (int32_t)i;
      dst->vertex_location = block->offset;
      dst->vertex_size = block->size;
    }
  }

  prog->uniform_blocks = blocks;
  prog->uniform_block_count = count;

  if (pixel_shader) {
    for (uint32_t i = 0; i < pixel_shader->uniformBlockCount; ++i) {
      const GX2UniformBlock *block = &pixel_shader->uniformBlocks[i];
      int32_t existing =
          find_program_uniform_block_index_in_array(blocks, count, block->name);
      ProgramUniformBlock *dst;

      if (existing >= 0) {
        dst = &blocks[existing];
      } else {
        if (count >= max_blocks) {
          free_program_uniform_blocks(blocks, max_blocks);
          prog->uniform_blocks = NULL;
          prog->uniform_block_count = 0;
          return false;
        }
        dst = &blocks[count++];
        memset(dst, 0, sizeof(*dst));
        dst->name = copy_program_string(block->name);
        if (!dst->name) {
          free_program_uniform_blocks(blocks, max_blocks);
          prog->uniform_blocks = NULL;
          prog->uniform_block_count = 0;
          return false;
        }
        dst->vertex_block_index = -1;
        dst->pixel_block_index = -1;
      }

      dst->pixel_block_index = (int32_t)i;
      dst->pixel_location = block->offset;
      dst->pixel_size = block->size;
    }
  }

  prog->uniform_block_count = count;
  return true;
}

static bool alloc_shadow_block_set(UniformBlockShadow **out_blocks,
                                   uint32_t *out_count,
                                   const GX2UniformBlock *uniform_blocks,
                                   uint32_t count) {
  UniformBlockShadow *blocks;

  *out_blocks = NULL;
  *out_count = 0;
  if (!uniform_blocks || count == 0) {
    return true;
  }

  blocks = (UniformBlockShadow *)gl_mem_alloc(
      GL_MEM_TYPE_MEM2, sizeof(UniformBlockShadow) * count, 4);
  if (!blocks) {
    return false;
  }
  memset(blocks, 0, sizeof(UniformBlockShadow) * count);

  for (uint32_t i = 0; i < count; ++i) {
    blocks[i].size = uniform_blocks[i].size;
    if (blocks[i].size == 0) {
      continue;
    }

    blocks[i].buffer = gl_mem_alloc(GL_MEM_TYPE_MEM2, blocks[i].size, 256);
    if (!blocks[i].buffer) {
      free_shadow_blocks(blocks, count);
      return false;
    }
    memset(blocks[i].buffer, 0, blocks[i].size);
  }

  *out_blocks = blocks;
  *out_count = count;
  return true;
}

static bool attach_program_group(GLProgram *prog, const WHBGfxShaderGroup *group,
                                 bool owns_group) {
  clear_program_group(prog);

  prog->group = group;
  prog->owns_group = owns_group;
  prog->linked = group != NULL;

  if (!group) {
    return true;
  }

  memset(prog->vertex_sampler_units, 0, sizeof(prog->vertex_sampler_units));
  memset(prog->pixel_sampler_units, 0, sizeof(prog->pixel_sampler_units));

  if (group->vertexShader &&
      !alloc_shadow_block_set(&prog->vs_blocks, &prog->vs_block_count,
                              group->vertexShader->uniformBlocks,
                              group->vertexShader->uniformBlockCount)) {
    clear_program_group(prog);
    return false;
  }

  if (group->pixelShader &&
      !alloc_shadow_block_set(&prog->ps_blocks, &prog->ps_block_count,
                              group->pixelShader->uniformBlocks,
                              group->pixelShader->uniformBlockCount)) {
    clear_program_group(prog);
    return false;
  }

  if (!build_program_uniform_blocks(prog, group->vertexShader,
                                    group->pixelShader)) {
    clear_program_group(prog);
    return false;
  }

  return true;
}

static GLProgram *get_bound_linked_program(bool set_error) {
  GLuint program;

  if (!g_gl_context) {
    return NULL;
  }

  program = g_gl_context->bound_program;
  if (!is_valid_program(program) || !g_programs[program].linked ||
      !g_programs[program].group) {
    if (set_error) {
      _gl_set_error(GL_INVALID_OPERATION);
    }
    return NULL;
  }

  return &g_programs[program];
}

static bool update_uniform_words(GLint location, const uint32_t *data,
                                 GLsizei count_words,
                                 uint32_t extra_offset_bytes) {
  GLProgram *prog;
  uint32_t is_pixel;
  uint32_t block_idx;
  uint32_t offset;
  UniformBlockShadow *block = NULL;
  uint32_t *dst;

  if (location == -1) {
    return true;
  }
  if (count_words < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return false;
  }
  if (count_words == 0) {
    return true;
  }
  if (!data) {
    _gl_set_error(GL_INVALID_VALUE);
    return false;
  }
  if (location_is_sampler(location)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return false;
  }

  prog = get_bound_linked_program(true);
  if (!prog) {
    return false;
  }

  is_pixel = location_is_pixel(location) ? 1u : 0u;
  block_idx = ((uint32_t)location >> 16) & GL_LOCATION_BLOCK_MASK;
  offset = ((uint32_t)location & 0xFFFFu) + extra_offset_bytes;

  if (!is_pixel && block_idx < prog->vs_block_count) {
    block = &prog->vs_blocks[block_idx];
  } else if (is_pixel && block_idx < prog->ps_block_count) {
    block = &prog->ps_blocks[block_idx];
  }

  if (!block || !block->buffer ||
      offset + (uint32_t)count_words * 4u > block->size) {
    _gl_set_error(GL_INVALID_OPERATION);
    return false;
  }

  dst = (uint32_t *)((uint8_t *)block->buffer + offset);

  for (GLsizei i = 0; i < count_words; i++) {
    dst[i] = CPU_TO_GPU_32(data[i]);
  }

  DCFlushRange(dst, (uint32_t)count_words * 4u);
  return true;
}

static bool set_sampler_unit(GLint location, GLint unit) {
  GLProgram *prog = get_bound_linked_program(true);
  uint32_t sampler_location;

  if (location == -1) {
    return true;
  }
  if (!prog) {
    return false;
  }
  if (!location_is_sampler(location)) {
    uint32_t raw = (uint32_t)unit;
    return update_uniform_words(location, &raw, 1, 0);
  }
  if (unit < 0 || unit >= 32) {
    _gl_set_error(GL_INVALID_VALUE);
    return false;
  }

  sampler_location = (uint32_t)location & 0xFFFFu;
  if (sampler_location >= MAX_PROGRAM_SAMPLERS) {
    _gl_set_error(GL_INVALID_OPERATION);
    return false;
  }

  if (location_is_pixel(location)) {
    prog->pixel_sampler_units[sampler_location] = unit;
  } else {
    prog->vertex_sampler_units[sampler_location] = unit;
  }

  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
  return true;
}

void gl_shader_init(void) {
  memset(g_shaders, 0, sizeof(g_shaders));
  memset(g_programs, 0, sizeof(g_programs));
}

GLuint _gl_CreateShader(GLenum type) {
  if (!g_gl_context) {
    return 0;
  }
  if (type != GL_VERTEX_SHADER && type != GL_FRAGMENT_SHADER &&
      type != GL_GEOMETRY_SHADER) {
    _gl_set_error(GL_INVALID_ENUM);
    return 0;
  }

  for (int i = 1; i < MAX_SHADERS; i++) {
    if (!g_shaders[i].in_use) {
      memset(&g_shaders[i], 0, sizeof(g_shaders[i]));
      g_shaders[i].in_use = true;
      g_shaders[i].type = type;
      return i;
    }
  }

  _gl_set_error(GL_OUT_OF_MEMORY);
  return 0;
}

void _gl_ShaderSource(GLuint shader, GLsizei count, const GLchar *const *string,
                      const GLint *length) {
  size_t total_length = 0;
  char *source;
  char *dst;

  if (!g_gl_context) {
    return;
  }
  if (!is_valid_shader(shader)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (count < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (count > 0 && !string) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  for (GLsizei i = 0; i < count; ++i) {
    size_t part_length;
    if (!string[i]) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
    if (length && length[i] >= 0) {
      part_length = (size_t)length[i];
    } else {
      part_length = strlen(string[i]);
    }
    total_length += part_length;
  }

  source = (char *)gl_mem_alloc(GL_MEM_TYPE_MEM2, total_length + 1u, 4);
  if (!source) {
    _gl_set_error(GL_OUT_OF_MEMORY);
    return;
  }

  dst = source;
  for (GLsizei i = 0; i < count; ++i) {
    size_t part_length =
        (length && length[i] >= 0) ? (size_t)length[i] : strlen(string[i]);
    memcpy(dst, string[i], part_length);
    dst += part_length;
  }
  *dst = '\0';

  free_shader_source(&g_shaders[shader]);
  g_shaders[shader].source = source;
  g_shaders[shader].source_length = (uint32_t)total_length;
  g_shaders[shader].source_present = true;
  g_shaders[shader].compile_requested = false;
}

void _gl_CompileShader(GLuint shader) {
  if (!g_gl_context) {
    return;
  }
  if (!is_valid_shader(shader)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!g_shaders[shader].source_present) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  /* CafeGLSL compilation happens off-device. Runtime compile only marks the
   * source as ready for an offline pass that produces a GFD/GSH blob. */
  g_shaders[shader].compile_requested = true;
}

GLuint _gl_CreateProgram(void) {
  if (!g_gl_context) {
    return 0;
  }

  for (int i = 1; i < MAX_PROGRAMS; i++) {
    if (!g_programs[i].in_use) {
      memset(&g_programs[i], 0, sizeof(g_programs[i]));
      g_programs[i].in_use = true;
      return i;
    }
  }

  _gl_set_error(GL_OUT_OF_MEMORY);
  return 0;
}

void _gl_AttachShader(GLuint program, GLuint shader) {
  GLProgram *prog;
  GLShader *src_shader;

  if (!g_gl_context) {
    return;
  }
  if (!is_valid_program(program) || !is_valid_shader(shader)) {
    _gl_set_error(program == 0 ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
    return;
  }

  prog = &g_programs[program];
  src_shader = &g_shaders[shader];

  switch (src_shader->type) {
  case GL_VERTEX_SHADER:
    prog->attached_vertex_shader = shader;
    break;
  case GL_FRAGMENT_SHADER:
    prog->attached_pixel_shader = shader;
    break;
  case GL_GEOMETRY_SHADER:
    prog->attached_geometry_shader = shader;
    break;
  default:
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
}

void _gl_LinkProgram(GLuint program) {
  GLProgram *prog;

  if (!g_gl_context) {
    return;
  }
  if (!is_valid_program(program)) {
    _gl_set_error(program == 0 ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
    return;
  }

  prog = &g_programs[program];
  if (prog->group) {
    prog->linked = true;
    return;
  }

  if (!prog->attached_vertex_shader || !prog->attached_pixel_shader ||
      !g_shaders[prog->attached_vertex_shader].compile_requested ||
      !g_shaders[prog->attached_pixel_shader].compile_requested) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  /* Runtime GLSL-to-GX2 compilation is intentionally unsupported. Applications
   * must compile with CafeGLSL on the host and load the resulting GFD/GSH blob
   * through glWiiULoadShaderGroupGFD or glWiiULoadShaderGroup. */
  _gl_set_error(GL_INVALID_OPERATION);
}

void _gl_UseProgram(GLuint program) {
  if (!g_gl_context) {
    return;
  }
  if (program >= MAX_PROGRAMS || (program > 0 && !g_programs[program].in_use)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (program > 0 && (!g_programs[program].linked || !g_programs[program].group)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  g_gl_context->bound_program = program;
  g_gl_context->dirty_flags |= GL_DIRTY_PROGRAM;
}

void _gl_WiiULoadShaderGroup(GLuint program, const void *shaderGroup) {
  GLProgram *prog;

  if (!g_gl_context || !is_valid_program(program)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  prog = &g_programs[program];
  if (!attach_program_group(prog, (const WHBGfxShaderGroup *)shaderGroup, false)) {
    _gl_set_error(GL_OUT_OF_MEMORY);
    return;
  }

  g_gl_context->dirty_flags |= GL_DIRTY_PROGRAM;
}

void _gl_WiiULoadShaderGroupGFD(GLuint program, GLuint index,
                                const void *gfdData) {
  GLProgram *prog;

  if (!g_gl_context || !is_valid_program(program) || !gfdData) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  prog = &g_programs[program];
  clear_program_group(prog);
  memset(&prog->owned_group, 0, sizeof(prog->owned_group));

  if (!WHBGfxLoadGFDShaderGroup(&prog->owned_group, index, gfdData)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  if (!attach_program_group(prog, &prog->owned_group, true)) {
    _gl_set_error(GL_OUT_OF_MEMORY);
    return;
  }

  g_gl_context->dirty_flags |= GL_DIRTY_PROGRAM;
}

/* Encoding: bit 31 = is_pixel, bits 16-30 = block_index, bits 0-15 = offset */
GLint _gl_GetUniformLocation(GLuint program, const GLchar *name) {
  GLProgram *prog;

  if (!g_gl_context || !is_valid_program(program) || !name) {
    return -1;
  }

  prog = &g_programs[program];
  if (!prog->group) {
    return -1;
  }

  if (prog->group->vertexShader) {
    const GX2UniformVar *var =
        GX2GetVertexUniformVar(prog->group->vertexShader, name);
    if (var) {
      return (GLint)(((uint32_t)(var->block & GL_LOCATION_BLOCK_MASK) << 16) |
                     ((uint32_t)var->offset & 0xFFFF));
    }
    for (uint32_t i = 0; i < prog->group->vertexShader->samplerVarCount; ++i) {
      const GX2SamplerVar *sampler = &prog->group->vertexShader->samplerVars[i];
      if (strcmp(name, sampler->name) == 0) {
        return (GLint)(GL_LOCATION_KIND_SAMPLER |
                       (sampler->location & 0xFFFFu));
      }
    }
  }

  if (prog->group->pixelShader) {
    const GX2UniformVar *var =
        GX2GetPixelUniformVar(prog->group->pixelShader, name);
    if (var) {
      return (GLint)(GL_LOCATION_STAGE_PIXEL |
                     ((uint32_t)(var->block & GL_LOCATION_BLOCK_MASK) << 16) |
                     ((uint32_t)var->offset & 0xFFFF));
    }
    for (uint32_t i = 0; i < prog->group->pixelShader->samplerVarCount; ++i) {
      const GX2SamplerVar *sampler = &prog->group->pixelShader->samplerVars[i];
      if (strcmp(name, sampler->name) == 0) {
        return (GLint)(GL_LOCATION_STAGE_PIXEL | GL_LOCATION_KIND_SAMPLER |
                       (sampler->location & 0xFFFFu));
      }
    }
  }

  return -1;
}

GLuint _gl_GetUniformBlockIndex(GLuint program, const GLchar *uniformBlockName) {
  GLProgram *prog;
  int32_t block_index;

  if (!g_gl_context || !uniformBlockName) {
    if (g_gl_context && !uniformBlockName) {
      _gl_set_error(GL_INVALID_VALUE);
    }
    return GL_INVALID_INDEX;
  }
  if (!is_valid_program(program)) {
    _gl_set_error(program == 0 ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
    return GL_INVALID_INDEX;
  }

  prog = &g_programs[program];
  if (!prog->linked || !prog->group) {
    _gl_set_error(GL_INVALID_OPERATION);
    return GL_INVALID_INDEX;
  }

  block_index = find_program_uniform_block_index(prog, uniformBlockName);
  if (block_index < 0) {
    return GL_INVALID_INDEX;
  }

  return (GLuint)block_index;
}

void _gl_UniformBlockBinding(GLuint program, GLuint uniformBlockIndex,
                             GLuint uniformBlockBinding) {
  GLProgram *prog;

  if (!g_gl_context) {
    return;
  }
  if (!is_valid_program(program)) {
    _gl_set_error(program == 0 ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
    return;
  }
  if (uniformBlockBinding >= GL33_MAX_UNIFORM_BUFFER_BINDINGS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  prog = &g_programs[program];
  if (!prog->linked || !prog->group) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (uniformBlockIndex >= prog->uniform_block_count) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  prog->uniform_blocks[uniformBlockIndex].binding_point = uniformBlockBinding;
  if (g_gl_context->bound_program == program) {
    g_gl_context->dirty_flags |= GL_DIRTY_UNIFORM_BINDINGS;
  }
}

void _gl_Uniform1f(GLint location, GLfloat v0) {
  (void)update_uniform_words(location, (const uint32_t *)&v0, 1, 0);
}

void _gl_Uniform1fv(GLint location, GLsizei count, const GLfloat *value) {
  (void)update_uniform_words(location, (const uint32_t *)value, count, 0);
}

void _gl_Uniform1i(GLint location, GLint v0) {
  (void)set_sampler_unit(location, v0);
}

void _gl_Uniform2f(GLint location, GLfloat v0, GLfloat v1) {
  GLfloat data[2] = {v0, v1};
  (void)update_uniform_words(location, (const uint32_t *)data, 2, 0);
}

void _gl_Uniform2fv(GLint location, GLsizei count, const GLfloat *value) {
  if (count < 0 || (count > 0 && !value)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  (void)update_uniform_words(location, (const uint32_t *)value, count * 2, 0);
}

void _gl_Uniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) {
  GLfloat data[3] = {v0, v1, v2};
  (void)update_uniform_words(location, (const uint32_t *)data, 3, 0);
}

void _gl_Uniform3fv(GLint location, GLsizei count, const GLfloat *value) {
  if (count < 0 || (count > 0 && !value)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  (void)update_uniform_words(location, (const uint32_t *)value, count * 3, 0);
}

void _gl_Uniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2,
                   GLfloat v3) {
  GLfloat data[4] = {v0, v1, v2, v3};
  (void)update_uniform_words(location, (const uint32_t *)data, 4, 0);
}

void _gl_Uniform4fv(GLint location, GLsizei count, const GLfloat *value) {
  if (count < 0 || (count > 0 && !value)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  (void)update_uniform_words(location, (const uint32_t *)value, count * 4, 0);
}

void _gl_UniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose,
                          const GLfloat *value) {
  if (location == -1) {
    return;
  }
  if (count < 0 || !value) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  for (GLsizei c = 0; c < count; ++c) {
    const GLfloat *src = value + c * 16;
    uint32_t offset = (uint32_t)c * 16u * 4u;

    if (transpose) {
      GLfloat transposed[16];
      for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
          transposed[row * 4 + col] = src[col * 4 + row];
        }
      }
      if (!update_uniform_words(location, (const uint32_t *)transposed, 16,
                                offset)) {
        return;
      }
    } else if (!update_uniform_words(location, (const uint32_t *)src, 16,
                                     offset)) {
      return;
    }
  }
}

static void bind_shader_sampler_vars(const GX2SamplerVar *samplers,
                                     uint32_t sampler_count,
                                     const GLint *sampler_units,
                                     bool pixel_stage) {
  for (uint32_t i = 0; i < sampler_count; ++i) {
    const GX2SamplerVar *sampler = &samplers[i];
    GLuint gl_unit;
    GLuint texture_id = 0;
    GX2Texture *texture;
    GX2Sampler *gx2_sampler;

    if (sampler->location >= MAX_PROGRAM_SAMPLERS) {
      continue;
    }

    gl_unit = (GLuint)sampler_units[sampler->location];
    if (gl_unit >= 32) {
      continue;
    }

    switch (sampler->type) {
    case GX2_SAMPLER_VAR_TYPE_SAMPLER_1D:
    case GX2_SAMPLER_VAR_TYPE_SAMPLER_2D:
      texture_id = g_gl_context->bound_texture_2d[gl_unit];
      break;
    case GX2_SAMPLER_VAR_TYPE_SAMPLER_3D:
      texture_id = g_gl_context->bound_texture_3d[gl_unit];
      break;
    case GX2_SAMPLER_VAR_TYPE_SAMPLER_CUBE:
      texture_id = g_gl_context->bound_texture_cube[gl_unit];
      break;
    default:
      continue;
    }

    texture = gl_get_gx2_texture(texture_id);
    gx2_sampler = gl_get_gx2_sampler(texture_id);
    if (!texture || !gx2_sampler) {
      continue;
    }

    if (pixel_stage) {
      GX2SetPixelTexture(texture, sampler->location);
      GX2SetPixelSampler(gx2_sampler, sampler->location);
    } else {
      GX2SetVertexTexture(texture, sampler->location);
      GX2SetVertexSampler(gx2_sampler, sampler->location);
    }
  }
}

static bool bind_program_uniform_block(GLProgram *prog, ProgramUniformBlock *block,
                                       bool pixel_stage) {
  const gl_uniform_buffer_binding_t *binding;
  GX2RBuffer *buffer;
  GLsizeiptr buffer_size;
  GLsizeiptr available_size;
  uint32_t required_size;
  uint32_t location;

  if (!g_gl_context || !prog || !block) {
    return false;
  }
  if (block->binding_point >= GL33_MAX_UNIFORM_BUFFER_BINDINGS) {
    return false;
  }

  binding = &g_gl_context->uniform_buffer_bindings[block->binding_point];
  if (binding->buffer == 0 || binding->offset < 0 ||
      (binding->offset % GX2_UNIFORM_BLOCK_ALIGNMENT) != 0) {
    return false;
  }

  buffer = gl_buffer_get_gx2r_buffer(binding->buffer);
  buffer_size = gl_buffer_get_size(binding->buffer);
  if (!buffer || binding->offset > buffer_size) {
    return false;
  }
  if (!binding->whole_buffer &&
      binding->offset + binding->size > buffer_size) {
    return false;
  }

  available_size = binding->whole_buffer ? (buffer_size - binding->offset)
                                         : binding->size;
  required_size = pixel_stage ? block->pixel_size : block->vertex_size;
  location = pixel_stage ? block->pixel_location : block->vertex_location;
  if (required_size == 0 || available_size < required_size) {
    return false;
  }

  if (pixel_stage) {
    GX2RSetPixelUniformBlock(buffer, location, (uint32_t)binding->offset);
  } else {
    GX2RSetVertexUniformBlock(buffer, location, (uint32_t)binding->offset);
  }

  return true;
}

void gl_bind_shaders(void) {
  GLProgram *prog = get_bound_linked_program(false);

  if (!prog) {
    return;
  }

  if (g_gl_context->dirty_flags & GL_DIRTY_PROGRAM) {
    if (prog->group->vertexShader) {
      GX2SetVertexShader(prog->group->vertexShader);
    }
    if (prog->group->pixelShader) {
      GX2SetPixelShader(prog->group->pixelShader);
    }
  }

  if (prog->group->vertexShader && prog->vs_block_count > 0) {
    for (uint32_t i = 0; i < prog->vs_block_count; i++) {
      ProgramUniformBlock *program_block =
          find_program_uniform_block_for_stage(prog, i, false);
      if (!bind_program_uniform_block(prog, program_block, false)) {
        GX2SetVertexUniformBlock(
            prog->group->vertexShader->uniformBlocks[i].offset,
            prog->vs_blocks[i].size, prog->vs_blocks[i].buffer);
      }
    }
  }

  if (prog->group->pixelShader && prog->ps_block_count > 0) {
    for (uint32_t i = 0; i < prog->ps_block_count; i++) {
      ProgramUniformBlock *program_block =
          find_program_uniform_block_for_stage(prog, i, true);
      if (!bind_program_uniform_block(prog, program_block, true)) {
        GX2SetPixelUniformBlock(
            prog->group->pixelShader->uniformBlocks[i].offset,
            prog->ps_blocks[i].size, prog->ps_blocks[i].buffer);
      }
    }
  }

  if (prog->group->vertexShader && prog->group->vertexShader->samplerVarCount > 0) {
    bind_shader_sampler_vars(prog->group->vertexShader->samplerVars,
                             prog->group->vertexShader->samplerVarCount,
                             prog->vertex_sampler_units, false);
  }

  if (prog->group->pixelShader && prog->group->pixelShader->samplerVarCount > 0) {
    bind_shader_sampler_vars(prog->group->pixelShader->samplerVars,
                             prog->group->pixelShader->samplerVarCount,
                             prog->pixel_sampler_units, true);
  }
}

#ifdef __cplusplus
}
#endif
