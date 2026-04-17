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
#include <gx2/sampler.h>
#include <gx2/texture.h>
#include <stdio.h>
#include <string.h>
#include <whb/gfx.h>
#include <stdlib.h>

#define MAX_PROGRAMS 512
#define MAX_SHADERS 512
#define MAX_PROGRAM_SAMPLERS 32

#define GL_LOCATION_STAGE_PIXEL 0x80000000u
#define GL_LOCATION_KIND_SAMPLER 0x40000000u
#define GL_LOCATION_BLOCK_MASK 0x3FFFu

typedef struct {
  bool in_use;
  bool delete_pending;
  GLenum type;
  char *source;
  char *info_log;
  uint32_t source_length;
  bool source_present;
  bool compile_requested;
  bool compile_succeeded;
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
  bool validated;
  bool delete_pending;
  GLuint attached_vertex_shader;
  GLuint attached_pixel_shader;
  GLuint attached_geometry_shader;
  char *info_log;
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
  char *attrib_binding_names[GL33_MAX_VERTEX_ATTRIBS];
} GLProgram;

static GLShader g_shaders[MAX_SHADERS];
static GLProgram g_programs[MAX_PROGRAMS];

static bool is_valid_shader(GLuint s) { return s > 0 && s < MAX_SHADERS && g_shaders[s].in_use; }
static bool is_valid_program(GLuint p) { return p > 0 && p < MAX_PROGRAMS && g_programs[p].in_use; }

static bool location_is_sampler(GLint l) { return (((uint32_t)l & GL_LOCATION_KIND_SAMPLER) != 0u); }
static bool location_is_pixel(GLint l) { return (((uint32_t)l & GL_LOCATION_STAGE_PIXEL) != 0u); }

static bool update_uniform_words(GLint location, const uint32_t *data, GLsizei count_words, uint32_t extra_offset_bytes) {
  if (location == -1 || count_words <= 0 || !data) return true;
  GLuint prog_id = g_gl_context->bound_program;
  if (!is_valid_program(prog_id)) return false;
  GLProgram *prog = &g_programs[prog_id];
  bool is_pixel = location_is_pixel(location);
  uint32_t block_idx = ((uint32_t)location >> 16) & GL_LOCATION_BLOCK_MASK;
  uint32_t offset = ((uint32_t)location & 0xFFFFu) + extra_offset_bytes;
  UniformBlockShadow *block = is_pixel ? (block_idx < prog->ps_block_count ? &prog->ps_blocks[block_idx] : NULL) : (block_idx < prog->vs_block_count ? &prog->vs_blocks[block_idx] : NULL);
  if (!block || !block->buffer || offset + count_words * 4 > block->size) return false;
  uint32_t *dst = (uint32_t *)((uint8_t *)block->buffer + offset);
  for (GLsizei i = 0; i < count_words; i++) dst[i] = CPU_TO_GPU_32(data[i]);
  DCFlushRange(dst, (uint32_t)count_words * 4);
  return true;
}

void gl_shader_init(void) { memset(g_shaders, 0, sizeof(g_shaders)); memset(g_programs, 0, sizeof(g_programs)); }
GLuint _gl_CreateShader(GLenum type) {
    for (int i = 1; i < MAX_SHADERS; i++) if (!g_shaders[i].in_use) { g_shaders[i].in_use = true; g_shaders[i].type = type; return i; }
    return 0;
}
void _gl_DeleteShader(GLuint s) { if (is_valid_shader(s)) g_shaders[s].in_use = false; }
GLboolean _gl_IsShader(GLuint s) { return is_valid_shader(s) ? GL_TRUE : GL_FALSE; }
void _gl_ShaderSource(GLuint s, GLsizei c, const GLchar* const* str, const GLint* l) { (void)s;(void)c;(void)str;(void)l; }
void _gl_CompileShader(GLuint s) { if (is_valid_shader(s)) g_shaders[s].compile_requested = true; }
GLuint _gl_CreateProgram(void) {
    for (int i = 1; i < MAX_PROGRAMS; i++) if (!g_programs[i].in_use) { g_programs[i].in_use = true; return i; }
    return 0;
}
void _gl_DeleteProgram(GLuint p) { if (is_valid_program(p)) g_programs[p].in_use = false; }
GLboolean _gl_IsProgram(GLuint p) { return is_valid_program(p) ? GL_TRUE : GL_FALSE; }
void _gl_AttachShader(GLuint p, GLuint s) { (void)p;(void)s; }
void _gl_DetachShader(GLuint p, GLuint s) { (void)p;(void)s; }
void _gl_LinkProgram(GLuint p) { if (is_valid_program(p)) g_programs[p].linked = true; }
void _gl_UseProgram(GLuint p) {
  if (!g_gl_context) return;
  g_gl_context->bound_program = p;
  g_gl_context->dirty_flags |= GL_DIRTY_PROGRAM | GL_DIRTY_TEXTURE_BINDINGS | GL_DIRTY_UNIFORM_BINDINGS;
}
void _gl_ValidateProgram(GLuint p) { (void)p; }
void _gl_BindAttribLocation(GLuint p, GLuint i, const GLchar* n) { (void)p;(void)i;(void)n; }
void _gl_GetAttachedShaders(GLuint p, GLsizei m, GLsizei* c, GLuint* s) { (void)p;(void)m;(void)c;(void)s; }
void _gl_GetActiveAttrib(GLuint p, GLuint i, GLsizei b, GLsizei* l, GLint* s, GLenum* t, GLchar* n) { (void)p;(void)i;(void)b;(void)l;(void)s;(void)t;(void)n; }
void _gl_GetActiveUniform(GLuint p, GLuint i, GLsizei b, GLsizei* l, GLint* s, GLenum* t, GLchar* n) { (void)p;(void)i;(void)b;(void)l;(void)s;(void)t;(void)n; }
void _gl_GetShaderiv(GLuint s, GLenum p, GLint *v) {
  if (!is_valid_shader(s) || !v) return;
  switch (p) {
  case GL_COMPILE_STATUS:  *v = g_shaders[s].compile_succeeded ? GL_TRUE : (g_shaders[s].compile_requested ? GL_TRUE : GL_FALSE); break;
  case GL_SHADER_TYPE:     *v = (GLint)g_shaders[s].type; break;
  case GL_DELETE_STATUS:   *v = g_shaders[s].delete_pending ? GL_TRUE : GL_FALSE; break;
  case GL_INFO_LOG_LENGTH: *v = 0; break;
  case GL_SHADER_SOURCE_LENGTH: *v = g_shaders[s].source_length; break;
  default: _gl_set_error(GL_INVALID_ENUM); break;
  }
}
void _gl_GetProgramiv(GLuint p, GLenum n, GLint *v) {
  if (!is_valid_program(p) || !v) return;
  GLProgram *prog = &g_programs[p];
  switch (n) {
  case GL_LINK_STATUS:          *v = prog->linked ? GL_TRUE : GL_FALSE; break;
  case GL_VALIDATE_STATUS:      *v = prog->validated ? GL_TRUE : GL_FALSE; break;
  case GL_DELETE_STATUS:        *v = prog->delete_pending ? GL_TRUE : GL_FALSE; break;
  case GL_INFO_LOG_LENGTH:      *v = 0; break;
  case GL_ATTACHED_SHADERS:     *v = (prog->attached_vertex_shader ? 1 : 0) + (prog->attached_pixel_shader ? 1 : 0); break;
  case GL_ACTIVE_UNIFORMS:      *v = prog->group && prog->group->vertexShader ? (GLint)prog->group->vertexShader->uniformVarCount : 0; break;
  case GL_ACTIVE_ATTRIBUTES:    *v = prog->group && prog->group->vertexShader ? (GLint)prog->group->vertexShader->attribVarCount : 0; break;
  case GL_ACTIVE_UNIFORM_MAX_LENGTH: *v = 64; break;
  case GL_ACTIVE_ATTRIBUTE_MAX_LENGTH: *v = 64; break;
  default: _gl_set_error(GL_INVALID_ENUM); break;
  }
}
void _gl_GetShaderInfoLog(GLuint s, GLsizei m, GLsizei *l, GLchar *i) { (void)s;(void)m;(void)l;(void)i; }
void _gl_GetProgramInfoLog(GLuint p, GLsizei m, GLsizei *l, GLchar *i) { (void)p;(void)m;(void)l;(void)i; }
void _gl_GetShaderSource(GLuint s, GLsizei b, GLsizei *l, GLchar *src) { (void)s;(void)b;(void)l;(void)src; }
void _gl_GetUniformfv(GLuint p, GLint l, GLfloat *v) { (void)p;(void)l;(void)v; }
void _gl_GetUniformiv(GLuint p, GLint l, GLint *v) { (void)p;(void)l;(void)v; }
void _gl_GetUniformuiv(GLuint program, GLint location, GLuint *params) { (void)program;(void)location;(void)params; _gl_set_error(GL_INVALID_OPERATION); }
GLuint _gl_GetUniformBlockIndex(GLuint p, const GLchar *n) { (void)p;(void)n; return GL_INVALID_INDEX; }
void _gl_UniformBlockBinding(GLuint p, GLuint i, GLuint b) { (void)p;(void)i;(void)b; }
void _gl_WiiULoadShaderGroup(GLuint p, const void *g) {
  if (!is_valid_program(p) || !g) return;
  GLProgram *prog = &g_programs[p];
  const WHBGfxShaderGroup *group = (const WHBGfxShaderGroup *)g;

  if (prog->vs_blocks) {
    for (uint32_t i = 0; i < prog->vs_block_count; i++)
      if (prog->vs_blocks[i].buffer) gl_mem_free(GL_MEM_TYPE_MEM2, prog->vs_blocks[i].buffer);
    free(prog->vs_blocks); prog->vs_blocks = NULL;
  }
  if (prog->ps_blocks) {
    for (uint32_t i = 0; i < prog->ps_block_count; i++)
      if (prog->ps_blocks[i].buffer) gl_mem_free(GL_MEM_TYPE_MEM2, prog->ps_blocks[i].buffer);
    free(prog->ps_blocks); prog->ps_blocks = NULL;
  }
  prog->group = group;
  prog->owns_group = false;
  GX2VertexShader *vs = group->vertexShader;
  GX2PixelShader  *ps = group->pixelShader;
  prog->vs_block_count = vs ? vs->uniformBlockCount : 0;
  prog->ps_block_count = ps ? ps->uniformBlockCount : 0;
  if (prog->vs_block_count) {
    prog->vs_blocks = (UniformBlockShadow*)calloc(prog->vs_block_count, sizeof(UniformBlockShadow));
    for (uint32_t i = 0; i < prog->vs_block_count; i++) {
      prog->vs_blocks[i].size   = vs->uniformBlocks[i].size;
      prog->vs_blocks[i].buffer = gl_mem_alloc(GL_MEM_TYPE_MEM2, vs->uniformBlocks[i].size, 256);
      if (prog->vs_blocks[i].buffer) memset(prog->vs_blocks[i].buffer, 0, vs->uniformBlocks[i].size);
    }
  }
  if (prog->ps_block_count) {
    prog->ps_blocks = (UniformBlockShadow*)calloc(prog->ps_block_count, sizeof(UniformBlockShadow));
    for (uint32_t i = 0; i < prog->ps_block_count; i++) {
      prog->ps_blocks[i].size   = ps->uniformBlocks[i].size;
      prog->ps_blocks[i].buffer = gl_mem_alloc(GL_MEM_TYPE_MEM2, ps->uniformBlocks[i].size, 256);
      if (prog->ps_blocks[i].buffer) memset(prog->ps_blocks[i].buffer, 0, ps->uniformBlocks[i].size);
    }
  }
  memset(prog->vertex_sampler_units, -1, sizeof(prog->vertex_sampler_units));
  memset(prog->pixel_sampler_units,  -1, sizeof(prog->pixel_sampler_units));
  if (vs) for (uint32_t i = 0; i < vs->samplerVarCount && i < MAX_PROGRAM_SAMPLERS; i++)
    prog->vertex_sampler_units[vs->samplerVars[i].location] = (GLint)i;
  if (ps) for (uint32_t i = 0; i < ps->samplerVarCount && i < MAX_PROGRAM_SAMPLERS; i++)
    prog->pixel_sampler_units[ps->samplerVars[i].location]  = (GLint)i;
  prog->linked = true;
}
void _gl_WiiULoadShaderGroupGFD(GLuint p, GLuint idx, const void *d) { (void)idx; (void)d; (void)p; }

GLint _gl_GetUniformLocation(GLuint p, const GLchar *name) {
  if (!is_valid_program(p) || !name) return -1;
  GLProgram *prog = &g_programs[p];
  if (!prog->group) return -1;
  GX2VertexShader *vs = prog->group->vertexShader;
  GX2PixelShader  *ps = prog->group->pixelShader;
  if (vs) for (uint32_t i = 0; i < vs->uniformVarCount; i++) {
    if (vs->uniformVars[i].name && strcmp(vs->uniformVars[i].name, name) == 0) {
      uint32_t blk = (vs->uniformVars[i].block >= 0) ? (uint32_t)vs->uniformVars[i].block : 0;
      return (GLint)((blk << 16) | (vs->uniformVars[i].offset & 0xFFFFu));
    }
  }
  if (ps) for (uint32_t i = 0; i < ps->uniformVarCount; i++) {
    if (ps->uniformVars[i].name && strcmp(ps->uniformVars[i].name, name) == 0) {
      uint32_t blk = (ps->uniformVars[i].block >= 0) ? (uint32_t)ps->uniformVars[i].block : 0;
      return (GLint)(GL_LOCATION_STAGE_PIXEL | (blk << 16) | (ps->uniformVars[i].offset & 0xFFFFu));
    }
  }
  return -1;
}
GLint _gl_GetAttribLocation(GLuint p, const GLchar *name) {
  if (!is_valid_program(p) || !name) return -1;
  GLProgram *prog = &g_programs[p];
  if (!prog->group || !prog->group->vertexShader) return -1;
  GX2VertexShader *vs = prog->group->vertexShader;
  for (uint32_t i = 0; i < vs->attribVarCount; i++)
    if (vs->attribVars[i].name && strcmp(vs->attribVars[i].name, name) == 0)
      return (GLint)vs->attribVars[i].location;
  return -1;
}
void _gl_ReleaseShaderCompiler(void) {}
void _gl_ShaderBinary(GLsizei c, const GLuint* s, GLenum f, const GLvoid* b, GLsizei l) { (void)c;(void)s;(void)f;(void)b;(void)l; }
void _gl_GetShaderPrecisionFormat(GLenum shaderType, GLenum precisionType, GLint *range, GLint *precision) {
    (void)shaderType;
    if (range) { range[0] = 127; range[1] = 127; }
    if (precision) {
        switch (precisionType) {
        case 0x8DF0: case 0x8DF1: *precision = 8; break;
        case 0x8DF2: case 0x8DF3: *precision = 16; break;
        default: *precision = 23; break;
        }
    }
}
void _gl_Uniform1f(GLint l, GLfloat v) { update_uniform_words(l, (uint32_t*)&v, 1, 0); }
void _gl_Uniform1fv(GLint l, GLsizei c, const GLfloat *v) { update_uniform_words(l, (uint32_t*)v, c, 0); }
void _gl_Uniform1i(GLint l, GLint v) { update_uniform_words(l, (uint32_t*)&v, 1, 0); }
void _gl_Uniform1iv(GLint l, GLsizei c, const GLint *v) { update_uniform_words(l, (uint32_t*)v, c, 0); }
void _gl_Uniform2f(GLint l, GLfloat v0, GLfloat v1) { GLfloat d[2]={v0,v1}; update_uniform_words(l, (uint32_t*)d, 2, 0); }
void _gl_Uniform2fv(GLint l, GLsizei c, const GLfloat *v) { update_uniform_words(l, (uint32_t*)v, c*2, 0); }
void _gl_Uniform2i(GLint l, GLint v0, GLint v1) { GLint d[2]={v0,v1}; update_uniform_words(l, (uint32_t*)d, 2, 0); }
void _gl_Uniform2iv(GLint l, GLsizei c, const GLint *v) { update_uniform_words(l, (uint32_t*)v, c*2, 0); }
void _gl_Uniform3f(GLint l, GLfloat v0, GLfloat v1, GLfloat v2) { GLfloat d[3]={v0,v1,v2}; update_uniform_words(l, (uint32_t*)d, 3, 0); }
void _gl_Uniform3fv(GLint l, GLsizei c, const GLfloat *v) { update_uniform_words(l, (uint32_t*)v, c*3, 0); }
void _gl_Uniform3i(GLint l, GLint v0, GLint v1, GLint v2) { GLint d[3]={v0,v1,v2}; update_uniform_words(l, (uint32_t*)d, 3, 0); }
void _gl_Uniform3iv(GLint l, GLsizei c, const GLint *v) { update_uniform_words(l, (uint32_t*)v, c*3, 0); }
void _gl_Uniform4f(GLint l, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) { GLfloat d[4]={v0,v1,v2,v3}; update_uniform_words(l, (uint32_t*)d, 4, 0); }
void _gl_Uniform4fv(GLint l, GLsizei c, const GLfloat *v) { update_uniform_words(l, (uint32_t*)v, c*4, 0); }
void _gl_Uniform4i(GLint l, GLint v0, GLint v1, GLint v2, GLint v3) { GLint d[4]={v0,v1,v2,v3}; update_uniform_words(l, (uint32_t*)d, 4, 0); }
void _gl_Uniform4iv(GLint l, GLsizei c, const GLint *v) { update_uniform_words(l, (uint32_t*)v, c*4, 0); }
void _gl_Uniform1ui(GLint l, GLuint v) { update_uniform_words(l, &v, 1, 0); }
void _gl_Uniform2ui(GLint l, GLuint v0, GLuint v1) { GLuint d[2]={v0,v1}; update_uniform_words(l, d, 2, 0); }
void _gl_Uniform3ui(GLint l, GLuint v0, GLuint v1, GLuint v2) { GLuint d[3]={v0,v1,v2}; update_uniform_words(l, d, 3, 0); }
void _gl_Uniform4ui(GLint l, GLuint v0, GLuint v1, GLuint v2, GLuint v3) { GLuint d[4]={v0,v1,v2,v3}; update_uniform_words(l, d, 4, 0); }
void _gl_Uniform1uiv(GLint l, GLsizei c, const GLuint *v) { update_uniform_words(l, v, c, 0); }
void _gl_Uniform2uiv(GLint l, GLsizei c, const GLuint *v) { update_uniform_words(l, v, c*2, 0); }
void _gl_Uniform3uiv(GLint l, GLsizei c, const GLuint *v) { update_uniform_words(l, v, c*3, 0); }
void _gl_Uniform4uiv(GLint l, GLsizei c, const GLuint *v) {
    if (l == -1 || c <= 0 || !v) return;
    uint32_t *swapped = (uint32_t*)malloc(c * 4 * 4);
    if (!swapped) return;
    for(int i=0; i<c*4; i++) swapped[i] = v[i];
    update_uniform_words(l, swapped, c * 4, 0);
    free(swapped);
}

void _gl_UniformMatrix2fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { (void)t; update_uniform_words(l, (uint32_t*)v, c*4, 0); }
void _gl_UniformMatrix3fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { (void)t; update_uniform_words(l, (uint32_t*)v, c*9, 0); }
void _gl_UniformMatrix4fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { (void)t; update_uniform_words(l, (uint32_t*)v, c*16, 0); }
void _gl_UniformMatrix2x3fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { (void)t; update_uniform_words(l, (uint32_t*)v, c*6, 0); }
void _gl_UniformMatrix3x2fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { (void)t; update_uniform_words(l, (uint32_t*)v, c*6, 0); }
void _gl_UniformMatrix2x4fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { (void)t; update_uniform_words(l, (uint32_t*)v, c*8, 0); }
void _gl_UniformMatrix4x2fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { (void)t; update_uniform_words(l, (uint32_t*)v, c*8, 0); }
void _gl_UniformMatrix3x4fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { (void)t; update_uniform_words(l, (uint32_t*)v, c*12, 0); }
void _gl_UniformMatrix4x3fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { (void)t; update_uniform_words(l, (uint32_t*)v, c*12, 0); }

void _gl_GetActiveUniformBlockiv(GLuint p, GLuint i, GLenum n, GLint *v) { (void)p;(void)i;(void)n; if(v) *v = 0; }
void _gl_GetActiveUniformBlockName(GLuint p, GLuint i, GLsizei s, GLsizei *l, GLchar *n) { (void)p;(void)i;(void)s; if(l) *l = 0; if(n) n[0] = '\0'; }
void _gl_GetActiveUniformsiv(GLuint p, GLsizei c, const GLuint *i, GLenum n, GLint *v) { (void)p;(void)c;(void)i;(void)n; if(v) for(int j=0; j<c; j++) v[j] = 0; }
void _gl_GetActiveUniformName(GLuint p, GLuint i, GLsizei s, GLsizei *l, GLchar *n) { (void)p;(void)i;(void)s; if(l) *l = 0; if(n) n[0] = '\0'; }

void gl_bind_shaders(void) {
  if (!g_gl_context) return;
  GLuint prog_id = g_gl_context->bound_program;
  if (!is_valid_program(prog_id)) return;
  GLProgram *prog = &g_programs[prog_id];
  if (!prog->group || !prog->linked) return;
  GX2SetFetchShader((GX2FetchShader*)&prog->group->fetchShader);
  if (prog->group->vertexShader) GX2SetVertexShader(prog->group->vertexShader);
  if (prog->group->pixelShader)  GX2SetPixelShader(prog->group->pixelShader);

  if (prog->group->vertexShader) {
    for (uint32_t i = 0; i < prog->vs_block_count; i++) {
      if (!prog->vs_blocks || !prog->vs_blocks[i].buffer) continue;
      DCFlushRange(prog->vs_blocks[i].buffer, prog->vs_blocks[i].size);
      GX2SetVertexUniformBlock(prog->group->vertexShader->uniformBlocks[i].offset,
                               prog->vs_blocks[i].size, prog->vs_blocks[i].buffer);
    }
  }
  if (prog->group->pixelShader) {
    for (uint32_t i = 0; i < prog->ps_block_count; i++) {
      if (!prog->ps_blocks || !prog->ps_blocks[i].buffer) continue;
      DCFlushRange(prog->ps_blocks[i].buffer, prog->ps_blocks[i].size);
      GX2SetPixelUniformBlock(prog->group->pixelShader->uniformBlocks[i].offset,
                              prog->ps_blocks[i].size, prog->ps_blocks[i].buffer);
    }
  }

  for (uint32_t unit = 0; unit < 16; ++unit) {

    if (prog->pixel_sampler_units[unit] >= 0) {
      GLuint tex_id = g_gl_context->bound_texture_2d[unit];
      if (tex_id == 0) tex_id = g_gl_context->bound_texture_cube[unit];
      if (tex_id == 0) tex_id = g_gl_context->bound_texture_3d[unit];
      GX2Texture *tex = gl_get_gx2_texture(tex_id);
      if (tex) {
        GX2SetPixelTexture(tex, unit);

        GLuint samp_id = g_gl_context->bound_sampler[unit];
        GX2Sampler *sampler = gl_get_gx2_sampler(samp_id ? samp_id : tex_id, samp_id != 0);
        if (sampler) GX2SetPixelSampler(sampler, unit);
      }
    }

    if (prog->vertex_sampler_units[unit] >= 0) {
      GLuint tex_id = g_gl_context->bound_texture_2d[unit];
      GX2Texture *tex = gl_get_gx2_texture(tex_id);
      if (tex) {
        GX2SetVertexTexture(tex, unit);
        GLuint samp_id = g_gl_context->bound_sampler[unit];
        GX2Sampler *sampler = gl_get_gx2_sampler(samp_id ? samp_id : tex_id, samp_id != 0);
        if (sampler) GX2SetVertexSampler(sampler, unit);
      }
    }
  }
}

#ifdef __cplusplus
}
#endif
