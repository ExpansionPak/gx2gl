#include "gl_texture.h"
#include "gl_context.h"
#include "gl_framebuffer.h"
#include "state/gl_state.h"
#include "endian/endian.h"
#include "mem/gl_mem.h"
#include <coreinit/cache.h>
#include <gx2/clear.h>
#include <gx2/enum.h>
#include <gx2/mem.h>
#include <gx2/sampler.h>
#include <gx2/shaders.h>
#include <gx2/state.h>
#include <gx2/surface.h>
#include <gx2/texture.h>
#include <gx2/utils.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_TEXTURES 2048
#define MAX_SAMPLER_OBJECTS 512

typedef struct {
  GX2Texture gx2_texture;
  GX2Sampler gx2_sampler;
  GLenum target;
  GLint internal_format;
  GLsizei width, height, depth;
  GLint base_level, max_level;
  bool in_use;
  bool complete;
  bool pending_delete;
  bool storage_allocated;

  GLenum min_filter;
  GLenum mag_filter;
  GLenum wrap_s;
  GLenum wrap_t;
  GLenum wrap_r;
} GLTexture;

typedef struct {
  GX2SurfaceFormat gx2_format;
  GX2SurfaceUse surface_use;
  uint32_t comp_map;
  uint8_t src_components;
  uint8_t dst_components;
  uint8_t bytes_per_component;
  uint8_t src_bytes_per_texel;
  uint8_t dst_bytes_per_texel;
  bool packed_u32;
  bool mipmap_supported;
} TextureFormatInfo;

typedef struct {
  GX2SurfaceDim dim;
  GLsizei width;
  GLsizei height;
  GLsizei depth;
  uint32_t pitch;
  uint32_t image_size;
  uint32_t slice_size;
} TextureLevelLayout;

static GLTexture g_textures[MAX_TEXTURES];

typedef struct {
  GX2Sampler gx2_sampler;
  bool in_use;
  GLenum min_filter;
  GLenum mag_filter;
  GLenum wrap_s;
  GLenum wrap_t;
  GLenum wrap_r;
} GLSampler;

static GLSampler g_samplers[MAX_SAMPLER_OBJECTS];

static uint32_t min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }

static void free_gx2_texture_storage(GX2Texture *texture) {
  if (!texture) {
    return;
  }
  if (texture->surface.image) {
    gl_mem_free(GL_MEM_TYPE_MEM2, texture->surface.image);
    texture->surface.image = NULL;
  }
  if (texture->surface.mipmaps) {
    gl_mem_free(GL_MEM_TYPE_MEM2, texture->surface.mipmaps);
    texture->surface.mipmaps = NULL;
  }
}

static void free_texture_storage(GLTexture *tex) {
  if (!tex || !tex->storage_allocated) {
    return;
  }
  free_gx2_texture_storage(&tex->gx2_texture);
  memset(&tex->gx2_texture, 0, sizeof(tex->gx2_texture));
  tex->storage_allocated = false;
  tex->complete = false;
  tex->max_level = 0;
}

static bool map_dim(GLenum target, GX2SurfaceDim *dim) {
  switch (target) {
  case GL_TEXTURE_2D:
    *dim = GX2_SURFACE_DIM_TEXTURE_2D;
    return true;
  case GL_TEXTURE_3D:
    *dim = GX2_SURFACE_DIM_TEXTURE_3D;
    return true;
  case GL_TEXTURE_CUBE_MAP:
    *dim = GX2_SURFACE_DIM_TEXTURE_CUBE;
    return true;
  default:
    *dim = GX2_SURFACE_DIM_TEXTURE_2D;
    return false;
  }
}

static bool is_valid_texture_target(GLenum target) {
  return target == GL_TEXTURE_2D || target == GL_TEXTURE_3D ||
         target == GL_TEXTURE_CUBE_MAP;
}

static GX2TexXYFilterMode map_xy_filter(GLenum filter) {
  switch (filter) {
  case GL_NEAREST:
  case GL_NEAREST_MIPMAP_NEAREST:
  case GL_NEAREST_MIPMAP_LINEAR:
    return GX2_TEX_XY_FILTER_MODE_POINT;
  case GL_LINEAR:
  case GL_LINEAR_MIPMAP_NEAREST:
  case GL_LINEAR_MIPMAP_LINEAR:
  default:
    return GX2_TEX_XY_FILTER_MODE_LINEAR;
  }
}

static GX2TexMipFilterMode map_mip_filter(GLenum filter) {
  switch (filter) {
  case GL_NEAREST_MIPMAP_NEAREST:
  case GL_LINEAR_MIPMAP_NEAREST:
    return GX2_TEX_MIP_FILTER_MODE_POINT;
  case GL_NEAREST_MIPMAP_LINEAR:
  case GL_LINEAR_MIPMAP_LINEAR:
    return GX2_TEX_MIP_FILTER_MODE_LINEAR;
  default:
    return GX2_TEX_MIP_FILTER_MODE_NONE;
  }
}

static GX2TexClampMode map_wrap(GLenum wrap) {
  switch (wrap) {
  case GL_CLAMP_TO_EDGE:
    return GX2_TEX_CLAMP_MODE_CLAMP;
  case GL_MIRRORED_REPEAT:
    return GX2_TEX_CLAMP_MODE_MIRROR;
  case GL_REPEAT:
  default:
    return GX2_TEX_CLAMP_MODE_WRAP;
  }
}

static bool is_valid_min_filter(GLint filter) {
  switch (filter) {
  case GL_NEAREST:
  case GL_LINEAR:
  case GL_NEAREST_MIPMAP_NEAREST:
  case GL_LINEAR_MIPMAP_NEAREST:
  case GL_NEAREST_MIPMAP_LINEAR:
  case GL_LINEAR_MIPMAP_LINEAR:
    return true;
  default:
    return false;
  }
}

static bool is_valid_mag_filter(GLint filter) {
  return filter == GL_NEAREST || filter == GL_LINEAR;
}

static bool is_valid_wrap_mode(GLint wrap) {
  return wrap == GL_REPEAT || wrap == GL_CLAMP_TO_EDGE ||
         wrap == GL_MIRRORED_REPEAT;
}

static bool get_texture_format_info(GLint internalformat, GLenum format,
                                    GLenum type, bool validate_upload,
                                    TextureFormatInfo *info) {
  memset(info, 0, sizeof(*info));

  switch (internalformat) {
  case GL_ALPHA:
    info->gx2_format = GX2_SURFACE_FORMAT_UNORM_R8;
    info->surface_use =
        (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
    info->comp_map =
        GX2_COMP_MAP(GX2_SQ_SEL_0, GX2_SQ_SEL_0, GX2_SQ_SEL_0, GX2_SQ_SEL_R);
    info->src_components = 1;
    info->dst_components = 1;
    info->bytes_per_component = 1;
    info->src_bytes_per_texel = 1;
    info->dst_bytes_per_texel = 1;
    info->mipmap_supported = true;
    if (validate_upload &&
        (format != GL_ALPHA || type != GL_UNSIGNED_BYTE)) {
      return false;
    }
    return true;
  case GL_LUMINANCE:
    info->gx2_format = GX2_SURFACE_FORMAT_UNORM_R8;
    info->surface_use =
        (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
    info->comp_map =
        GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_R, GX2_SQ_SEL_R, GX2_SQ_SEL_1);
    info->src_components = 1;
    info->dst_components = 1;
    info->bytes_per_component = 1;
    info->src_bytes_per_texel = 1;
    info->dst_bytes_per_texel = 1;
    info->mipmap_supported = true;
    if (validate_upload &&
        (format != GL_LUMINANCE || type != GL_UNSIGNED_BYTE)) {
      return false;
    }
    return true;
  case GL_LUMINANCE_ALPHA:
    info->gx2_format = GX2_SURFACE_FORMAT_UNORM_R8_G8;
    info->surface_use =
        (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
    info->comp_map =
        GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_R, GX2_SQ_SEL_R, GX2_SQ_SEL_G);
    info->src_components = 2;
    info->dst_components = 2;
    info->bytes_per_component = 1;
    info->src_bytes_per_texel = 2;
    info->dst_bytes_per_texel = 2;
    info->mipmap_supported = true;
    if (validate_upload &&
        (format != GL_LUMINANCE_ALPHA || type != GL_UNSIGNED_BYTE)) {
      return false;
    }
    return true;
  case 1:
  case GL_RED:
  case GL_R8:
    info->gx2_format = GX2_SURFACE_FORMAT_UNORM_R8;
    info->surface_use =
        (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
    info->comp_map =
        GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_0, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
    info->src_components = 1;
    info->dst_components = 1;
    info->bytes_per_component = 1;
    info->src_bytes_per_texel = 1;
    info->dst_bytes_per_texel = 1;
    info->mipmap_supported = true;
    if (validate_upload &&
        (format != GL_RED || type != GL_UNSIGNED_BYTE)) {
      return false;
    }
    return true;
  case 2:
  case GL_RG:
  case GL_RG8:
    info->gx2_format = GX2_SURFACE_FORMAT_UNORM_R8_G8;
    info->surface_use =
        (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
    info->comp_map =
        GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
    info->src_components = 2;
    info->dst_components = 2;
    info->bytes_per_component = 1;
    info->src_bytes_per_texel = 2;
    info->dst_bytes_per_texel = 2;
    info->mipmap_supported = true;
    if (validate_upload &&
        (format != GL_RG || type != GL_UNSIGNED_BYTE)) {
      return false;
    }
    return true;
  case 3:
  case GL_RGB:
  case GL_RGB8:
    info->gx2_format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    info->surface_use =
        (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
    info->comp_map =
        GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
    info->src_components = 3;
    info->dst_components = 4;
    info->bytes_per_component = 1;
    info->src_bytes_per_texel = 3;
    info->dst_bytes_per_texel = 4;
    info->mipmap_supported = true;
    if (validate_upload &&
        (format != GL_RGB || type != GL_UNSIGNED_BYTE)) {
      return false;
    }
    return true;
  case 4:
  case GL_RGBA:
  case GL_RGBA8:
    info->gx2_format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    info->surface_use =
        (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
    info->comp_map =
        GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
    info->src_components = 4;
    info->dst_components = 4;
    info->bytes_per_component = 1;
    info->src_bytes_per_texel = 4;
    info->dst_bytes_per_texel = 4;
    info->mipmap_supported = true;
    if (validate_upload &&
        (format != GL_RGBA || type != GL_UNSIGNED_BYTE)) {
      return false;
    }
    return true;
  case GL_RGBA16F:
    info->gx2_format = GX2_SURFACE_FORMAT_FLOAT_R16_G16_B16_A16;
    info->surface_use =
        (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
    info->comp_map =
        GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
    info->src_components = 4;
    info->dst_components = 4;
    info->bytes_per_component = 2;
    info->src_bytes_per_texel = 8;
    info->dst_bytes_per_texel = 8;
    info->mipmap_supported = true;
    if (validate_upload &&
        (format != GL_RGBA || type != GL_HALF_FLOAT)) {
      return false;
    }
    return true;
  case GL_RGBA32F:
    info->gx2_format = GX2_SURFACE_FORMAT_FLOAT_R32_G32_B32_A32;
    info->surface_use =
        (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
    info->comp_map =
        GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
    info->src_components = 4;
    info->dst_components = 4;
    info->bytes_per_component = 4;
    info->src_bytes_per_texel = 16;
    info->dst_bytes_per_texel = 16;
    info->mipmap_supported = true;
    if (validate_upload &&
        (format != GL_RGBA || type != GL_FLOAT)) {
      return false;
    }
    return true;
  case GL_DEPTH_COMPONENT:
  case GL_DEPTH_COMPONENT32F:
    info->gx2_format = GX2_SURFACE_FORMAT_FLOAT_R32;
    info->surface_use =
        (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_DEPTH_BUFFER);
    info->comp_map =
        GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_0, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
    info->src_components = 1;
    info->dst_components = 1;
    info->bytes_per_component = 4;
    info->src_bytes_per_texel = 4;
    info->dst_bytes_per_texel = 4;
    info->mipmap_supported = false;
    if (validate_upload &&
        (format != GL_DEPTH_COMPONENT || type != GL_FLOAT)) {
      return false;
    }
    return true;
  case GL_DEPTH_STENCIL:
  case GL_DEPTH24_STENCIL8:
    info->gx2_format = GX2_SURFACE_FORMAT_UNORM_R24_X8;
    info->surface_use =
        (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_DEPTH_BUFFER);
    info->comp_map =
        GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_0, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
    info->src_components = 1;
    info->dst_components = 1;
    info->bytes_per_component = 4;
    info->src_bytes_per_texel = 4;
    info->dst_bytes_per_texel = 4;
    info->packed_u32 = true;
    info->mipmap_supported = false;
    if (validate_upload &&
        (format != GL_DEPTH_STENCIL || type != GL_UNSIGNED_INT_24_8)) {
      return false;
    }
    return true;
  default:
    return false;
  }
}

static bool calc_level_layout(GLenum target, GX2SurfaceFormat format,
                              GLsizei base_width, GLsizei base_height,
                              GLsizei base_depth, uint32_t level,
                              TextureLevelLayout *layout) {
  GX2Surface surface;
  GX2SurfaceDim dim;

  memset(&surface, 0, sizeof(surface));
  if (!map_dim(target, &dim)) {
    return false;
  }

  surface.dim = dim;
  surface.width = (uint32_t)((base_width >> level) > 0 ? (base_width >> level) : 1);
  surface.height =
      (uint32_t)((base_height >> level) > 0 ? (base_height >> level) : 1);
  if (target == GL_TEXTURE_3D) {
    surface.depth =
        (uint32_t)((base_depth >> level) > 0 ? (base_depth >> level) : 1);
  } else {
    surface.depth = (uint32_t)(base_depth > 0 ? base_depth : 1);
  }
  surface.mipLevels = 1;
  surface.format = format;
  surface.use = GX2_SURFACE_USE_TEXTURE;
  surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;

  GX2CalcSurfaceSizeAndAlignment(&surface);

  layout->dim = dim;
  layout->width = (GLsizei)surface.width;
  layout->height = (GLsizei)surface.height;
  layout->depth = (GLsizei)surface.depth;
  layout->pitch = surface.pitch;
  layout->image_size = surface.imageSize;
  layout->slice_size =
      surface.depth > 0 ? surface.imageSize / surface.depth : surface.imageSize;
  return true;
}

static uint8_t *get_texture_level_ptr_from_gx2(const GX2Texture *texture,
                                               uint32_t level) {
  if (!texture || level >= texture->surface.mipLevels) {
    return NULL;
  }
  if (level == 0) {
    return (uint8_t *)texture->surface.image;
  }
  if (!texture->surface.mipmaps) {
    return NULL;
  }
  return (uint8_t *)texture->surface.mipmaps +
         texture->surface.mipLevelOffset[level - 1];
}

static uint8_t *get_texture_level_ptr(const GLTexture *tex, uint32_t level) {
  return get_texture_level_ptr_from_gx2(&tex->gx2_texture, level);
}

static void init_sampler_state(GX2Sampler *sampler, GLenum min_filter,
                               GLenum mag_filter, GLenum wrap_s,
                               GLenum wrap_t, GLenum wrap_r) {
  GX2InitSamplerXYFilter(sampler, map_xy_filter(mag_filter),
                         map_xy_filter(min_filter),
                         GX2_TEX_ANISO_RATIO_NONE);
  GX2InitSamplerZMFilter(sampler, GX2_TEX_Z_FILTER_MODE_NONE,
                         map_mip_filter(min_filter));
  GX2InitSamplerClamping(sampler, map_wrap(wrap_s), map_wrap(wrap_t),
                         map_wrap(wrap_r));
}

static void init_texture_sampler(GLTexture *tex) {
  init_sampler_state(&tex->gx2_sampler, tex->min_filter, tex->mag_filter,
                     tex->wrap_s, tex->wrap_t, tex->wrap_r);
}

static void init_sampler_object(GLSampler *sampler) {
  init_sampler_state(&sampler->gx2_sampler, sampler->min_filter,
                     sampler->mag_filter, sampler->wrap_s, sampler->wrap_t,
                     sampler->wrap_r);
}

static bool rebuild_texture_storage(GLTexture *tex, GLsizei width,
                                    GLsizei height, GLsizei depth,
                                    GLint internalformat,
                                    uint32_t mip_levels,
                                    bool preserve_existing) {
  TextureFormatInfo info;
  GX2Texture new_texture;
  GX2Texture old_texture;
  bool same_layout;
  uint32_t preserve_levels = 0;
  GX2SurfaceDim dim;

  if (!get_texture_format_info(internalformat, GL_RGBA, GL_UNSIGNED_BYTE, false,
                               &info) ||
      !map_dim(tex->target, &dim)) {
    return false;
  }

  memset(&new_texture, 0, sizeof(new_texture));
  new_texture.surface.dim = dim;
  new_texture.surface.width = (uint32_t)width;
  new_texture.surface.height = (uint32_t)height;
  new_texture.surface.depth = (uint32_t)depth;
  new_texture.surface.mipLevels = mip_levels;
  new_texture.surface.format = info.gx2_format;
  new_texture.surface.use = info.surface_use;
  new_texture.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
  new_texture.viewFirstMip = 0;
  new_texture.viewNumMips = mip_levels;
  new_texture.viewFirstSlice = 0;
  new_texture.viewNumSlices = (dim == GX2_SURFACE_DIM_TEXTURE_3D)
                                  ? (uint32_t)depth
                                  : 1;
  new_texture.compMap = info.comp_map;

  GX2CalcSurfaceSizeAndAlignment(&new_texture.surface);

  if (new_texture.surface.imageSize > 0) {
    new_texture.surface.image =
        gl_mem_alloc(GL_MEM_TYPE_MEM2, new_texture.surface.imageSize,
                     new_texture.surface.alignment);
    if (!new_texture.surface.image) {
      return false;
    }
    memset(new_texture.surface.image, 0, new_texture.surface.imageSize);
  }

  if (new_texture.surface.mipmapSize > 0) {
    new_texture.surface.mipmaps =
        gl_mem_alloc(GL_MEM_TYPE_MEM2, new_texture.surface.mipmapSize,
                     new_texture.surface.alignment);
    if (!new_texture.surface.mipmaps) {
      free_gx2_texture_storage(&new_texture);
      return false;
    }
    memset(new_texture.surface.mipmaps, 0, new_texture.surface.mipmapSize);
  }

  old_texture = tex->gx2_texture;
  same_layout = tex->storage_allocated && tex->internal_format == internalformat &&
                tex->width == width && tex->height == height &&
                tex->depth == depth;

  if (preserve_existing && same_layout) {
    preserve_levels = min_u32(old_texture.surface.mipLevels,
                              new_texture.surface.mipLevels);
    for (uint32_t level = 0; level < preserve_levels; ++level) {
      TextureLevelLayout layout;
      uint8_t *dst = get_texture_level_ptr_from_gx2(&new_texture, level);
      uint8_t *src = get_texture_level_ptr_from_gx2(&old_texture, level);
      if (!dst || !src ||
          !calc_level_layout(tex->target, info.gx2_format, width, height, depth,
                             level, &layout)) {
        continue;
      }
      memcpy(dst, src, layout.image_size);
    }
  }

  free_texture_storage(tex);

  tex->gx2_texture = new_texture;
  tex->storage_allocated = true;
  tex->complete = true;
  tex->internal_format = internalformat;
  tex->width = width;
  tex->height = height;
  tex->depth = depth;
  tex->base_level = 0;
  tex->max_level = (GLint)(mip_levels - 1);

  GX2InitTextureRegs(&tex->gx2_texture);
  gl_framebuffer_mark_texture_dirty((GLuint)(tex - g_textures));
  return true;
}

static void copy_texture_row(uint8_t *dst, const uint8_t *src,
                             uint32_t texel_count,
                             const TextureFormatInfo *info) {
  if (info->packed_u32) {
    const uint32_t *src32 = (const uint32_t *)src;
    uint32_t *dst32 = (uint32_t *)dst;
    for (uint32_t i = 0; i < texel_count; ++i) {
      dst32[i] = CPU_TO_GPU_32(src32[i]);
    }
    return;
  }

  if (info->bytes_per_component == 1) {
    if (info->src_components == info->dst_components) {
      memcpy(dst, src, texel_count * info->dst_bytes_per_texel);
      return;
    }

    for (uint32_t i = 0; i < texel_count; ++i) {
      const uint8_t *src_texel = src + i * info->src_bytes_per_texel;
      uint8_t *dst_texel = dst + i * info->dst_bytes_per_texel;
      dst_texel[0] = src_texel[0];
      dst_texel[1] = src_texel[1];
      dst_texel[2] = src_texel[2];
      dst_texel[3] = 0xFF;
    }
    return;
  }

  if (info->bytes_per_component == 2) {
    const uint16_t *src16 = (const uint16_t *)src;
    uint16_t *dst16 = (uint16_t *)dst;
    uint32_t count = texel_count * info->dst_components;
    for (uint32_t i = 0; i < count; ++i) {
      dst16[i] = CPU_TO_GPU_16(src16[i]);
    }
    return;
  }

  if (info->bytes_per_component == 4) {
    const uint32_t *src32 = (const uint32_t *)src;
    uint32_t *dst32 = (uint32_t *)dst;
    uint32_t count = texel_count * info->dst_components;
    for (uint32_t i = 0; i < count; ++i) {
      dst32[i] = CPU_TO_GPU_32(src32[i]);
    }
    return;
  }
}

static uint32_t align_transfer_row_bytes(uint32_t row_bytes, GLint alignment) {
  uint32_t align = (uint32_t)alignment;
  if (align <= 1u) {
    return row_bytes;
  }
  return (row_bytes + align - 1u) & ~(align - 1u);
}

static bool get_unpack_layout(const TextureFormatInfo *info, GLsizei width,
                              GLsizei height, uint32_t *row_bytes,
                              uint32_t *image_bytes, size_t *base_offset) {
  GLint row_length;
  GLint image_height;

  if (!g_gl_context || !info || !row_bytes || !image_bytes || !base_offset) {
    return false;
  }

  row_length = g_gl_context->unpack_row_length > 0 ? g_gl_context->unpack_row_length
                                                   : width;
  image_height = g_gl_context->unpack_image_height > 0
                     ? g_gl_context->unpack_image_height
                     : height;

  *row_bytes = align_transfer_row_bytes(
      (uint32_t)row_length * info->src_bytes_per_texel,
      g_gl_context->unpack_alignment);
  *image_bytes = *row_bytes * (uint32_t)image_height;
  *base_offset =
      (size_t)g_gl_context->unpack_skip_images * (*image_bytes) +
      (size_t)g_gl_context->unpack_skip_rows * (*row_bytes) +
      (size_t)g_gl_context->unpack_skip_pixels * info->src_bytes_per_texel;
  return true;
}

static bool upload_texture_level(GLTexture *tex, GLint level, GLsizei width,
                                 GLsizei height, GLsizei depth,
                                 const TextureFormatInfo *info,
                                 const GLvoid *pixels) {
  TextureLevelLayout layout;
  uint8_t *dst;
  const uint8_t *src;
  uint32_t src_row_bytes;
  uint32_t src_image_bytes;
  uint32_t dst_row_bytes;
  size_t src_base_offset;

  if (!pixels) {
    return true;
  }
  if (!calc_level_layout(tex->target, tex->gx2_texture.surface.format,
                         tex->width, tex->height, tex->depth, (uint32_t)level,
                         &layout)) {
    return false;
  }

  dst = get_texture_level_ptr(tex, (uint32_t)level);
  if (!dst) {
    return false;
  }

  memset(dst, 0, layout.image_size);

  if (!get_unpack_layout(info, width, height, &src_row_bytes, &src_image_bytes,
                         &src_base_offset)) {
    return false;
  }
  src = (const uint8_t *)pixels + src_base_offset;
  dst_row_bytes = layout.pitch * info->dst_bytes_per_texel;

  for (GLsizei z = 0; z < depth; ++z) {
    uint8_t *dst_slice = dst + (uint32_t)z * layout.slice_size;
    const uint8_t *src_slice = src + (size_t)z * src_image_bytes;
    for (GLsizei y = 0; y < height; ++y) {
      copy_texture_row(dst_slice + (uint32_t)y * dst_row_bytes,
                       src_slice + (size_t)y * src_row_bytes, (uint32_t)width,
                       info);
    }
  }

  DCFlushRange(dst, layout.image_size);
  GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, dst, layout.image_size);
  return true;
}

static bool upload_texture_sub_region(GLTexture *tex, GLint level,
                                      GLint xoffset, GLint yoffset,
                                      GLint zoffset, GLsizei width,
                                      GLsizei height, GLsizei depth,
                                      const TextureFormatInfo *info,
                                      const GLvoid *pixels) {
  TextureLevelLayout layout;
  uint8_t *dst;
  const uint8_t *src;
  uint32_t src_row_bytes;
  uint32_t src_image_bytes;
  uint32_t dst_row_bytes;
  size_t src_base_offset;

  if (!pixels) {
    return false;
  }
  if (!calc_level_layout(tex->target, tex->gx2_texture.surface.format,
                         tex->width, tex->height, tex->depth, (uint32_t)level,
                         &layout)) {
    return false;
  }
  if (xoffset < 0 || yoffset < 0 || zoffset < 0 || width < 0 || height < 0 ||
      depth < 0 || xoffset + width > layout.width ||
      yoffset + height > layout.height || zoffset + depth > layout.depth) {
    return false;
  }

  dst = get_texture_level_ptr(tex, (uint32_t)level);
  if (!dst) {
    return false;
  }

  if (!get_unpack_layout(info, width, height, &src_row_bytes, &src_image_bytes,
                         &src_base_offset)) {
    return false;
  }
  src = (const uint8_t *)pixels + src_base_offset;
  dst_row_bytes = layout.pitch * info->dst_bytes_per_texel;

  for (GLsizei z = 0; z < depth; ++z) {
    uint8_t *dst_slice = dst + (uint32_t)(zoffset + z) * layout.slice_size;
    const uint8_t *src_slice = src + (size_t)z * src_image_bytes;
    for (GLsizei y = 0; y < height; ++y) {
      copy_texture_row(
          dst_slice + (uint32_t)(yoffset + y) * dst_row_bytes +
              (uint32_t)xoffset * info->dst_bytes_per_texel,
          src_slice + (size_t)y * src_row_bytes, (uint32_t)width, info);
    }
  }

  DCFlushRange(dst, layout.image_size);
  GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, dst, layout.image_size);
  return true;
}

static float decode_gpu_float(const uint8_t *ptr) {
  uint32_t word;
  float value;

  memcpy(&word, ptr, sizeof(word));
  word = GPU_TO_CPU_32(word);
  memcpy(&value, &word, sizeof(value));
  return value;
}

static void encode_gpu_float(uint8_t *ptr, float value) {
  uint32_t word;

  memcpy(&word, &value, sizeof(word));
  word = CPU_TO_GPU_32(word);
  memcpy(ptr, &word, sizeof(word));
}

static bool generate_mipmap_level(GLTexture *tex, uint32_t dst_level,
                                  const TextureFormatInfo *info) {
  TextureLevelLayout src_layout;
  TextureLevelLayout dst_layout;
  uint8_t *src_base;
  uint8_t *dst_base;
  uint32_t src_row_bytes;
  uint32_t dst_row_bytes;

  if (dst_level == 0) {
    return false;
  }
  if (!calc_level_layout(tex->target, tex->gx2_texture.surface.format,
                         tex->width, tex->height, tex->depth, dst_level - 1,
                         &src_layout) ||
      !calc_level_layout(tex->target, tex->gx2_texture.surface.format,
                         tex->width, tex->height, tex->depth, dst_level,
                         &dst_layout)) {
    return false;
  }

  src_base = get_texture_level_ptr(tex, dst_level - 1);
  dst_base = get_texture_level_ptr(tex, dst_level);
  if (!src_base || !dst_base) {
    return false;
  }

  memset(dst_base, 0, dst_layout.image_size);

  src_row_bytes = src_layout.pitch * info->dst_bytes_per_texel;
  dst_row_bytes = dst_layout.pitch * info->dst_bytes_per_texel;

  for (GLsizei z = 0; z < dst_layout.depth; ++z) {
    uint8_t *dst_slice = dst_base + (uint32_t)z * dst_layout.slice_size;
    uint32_t src_z0 = (uint32_t)z * 2u;
    uint32_t src_z_count = src_layout.depth > 1 ? 2u : 1u;

    if (src_z0 + src_z_count > (uint32_t)src_layout.depth) {
      src_z_count = (uint32_t)src_layout.depth - src_z0;
    }

    for (GLsizei y = 0; y < dst_layout.height; ++y) {
      uint8_t *dst_row = dst_slice + (uint32_t)y * dst_row_bytes;
      uint32_t src_y0 = (uint32_t)y * 2u;
      uint32_t src_y_count = src_y0 + 1u < (uint32_t)src_layout.height ? 2u : 1u;

      for (GLsizei x = 0; x < dst_layout.width; ++x) {
        uint32_t src_x0 = (uint32_t)x * 2u;
        uint32_t src_x_count =
            src_x0 + 1u < (uint32_t)src_layout.width ? 2u : 1u;
        uint8_t *dst_texel = dst_row + (uint32_t)x * info->dst_bytes_per_texel;
        uint32_t sample_count = src_x_count * src_y_count * src_z_count;

        if (info->bytes_per_component == 1) {
          uint32_t sums[4] = {0, 0, 0, 0};
          for (uint32_t dz = 0; dz < src_z_count; ++dz) {
            const uint8_t *src_slice =
                src_base + (src_z0 + dz) * src_layout.slice_size;
            for (uint32_t dy = 0; dy < src_y_count; ++dy) {
              const uint8_t *src_row =
                  src_slice + (src_y0 + dy) * src_row_bytes;
              for (uint32_t dx = 0; dx < src_x_count; ++dx) {
                const uint8_t *src_texel =
                    src_row + (src_x0 + dx) * info->dst_bytes_per_texel;
                for (uint32_t c = 0; c < info->dst_components; ++c) {
                  sums[c] += src_texel[c];
                }
              }
            }
          }

          for (uint32_t c = 0; c < info->dst_components; ++c) {
            dst_texel[c] =
                (uint8_t)((sums[c] + sample_count / 2u) / sample_count);
          }
        } else if (info->bytes_per_component == 4 && !info->packed_u32) {
          float sums[4] = {0.0f, 0.0f, 0.0f, 0.0f};
          for (uint32_t dz = 0; dz < src_z_count; ++dz) {
            const uint8_t *src_slice =
                src_base + (src_z0 + dz) * src_layout.slice_size;
            for (uint32_t dy = 0; dy < src_y_count; ++dy) {
              const uint8_t *src_row =
                  src_slice + (src_y0 + dy) * src_row_bytes;
              for (uint32_t dx = 0; dx < src_x_count; ++dx) {
                const uint8_t *src_texel =
                    src_row + (src_x0 + dx) * info->dst_bytes_per_texel;
                for (uint32_t c = 0; c < info->dst_components; ++c) {
                  sums[c] += decode_gpu_float(src_texel + c * 4u);
                }
              }
            }
          }

          for (uint32_t c = 0; c < info->dst_components; ++c) {
            encode_gpu_float(dst_texel + c * 4u, sums[c] / sample_count);
          }
        } else {
          return false;
        }
      }
    }
  }

  DCFlushRange(dst_base, dst_layout.image_size);
  GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, dst_base, dst_layout.image_size);
  return true;
}

static uint32_t calc_full_mip_count(GLsizei width, GLsizei height,
                                    GLsizei depth) {
  uint32_t levels = 1;
  GLsizei max_dim = width;
  if (height > max_dim) {
    max_dim = height;
  }
  if (depth > max_dim) {
    max_dim = depth;
  }

  while (max_dim > 1) {
    max_dim >>= 1;
    ++levels;
  }
  return levels;
}

static GLuint get_bound_tex(GLenum target) {
  if (!g_gl_context) {
    return 0;
  }
  GLuint unit = g_gl_context->active_texture;
  switch (target) {
  case GL_TEXTURE_2D:
    return g_gl_context->bound_texture_2d[unit];
  case GL_TEXTURE_3D:
    return g_gl_context->bound_texture_3d[unit];
  case GL_TEXTURE_CUBE_MAP:
    return g_gl_context->bound_texture_cube[unit];
  default:
    return 0;
  }
}

static bool is_copy_color_format_supported(GLint internalformat,
                                           const TextureFormatInfo *info) {
  if (!info || info->bytes_per_component != 1 || info->packed_u32) {
    return false;
  }

  switch (internalformat) {
  case GL_ALPHA:
  case GL_LUMINANCE:
  case GL_LUMINANCE_ALPHA:
  case 1:
  case GL_RED:
  case GL_R8:
  case 2:
  case GL_RG:
  case GL_RG8:
  case 3:
  case GL_RGB:
  case GL_RGB8:
  case 4:
  case GL_RGBA:
  case GL_RGBA8:
    return true;
  default:
    return false;
  }
}

static bool convert_copy_texels(const uint8_t *src_rgba, GLint internalformat,
                                const TextureFormatInfo *info,
                                GLsizei width, GLsizei height,
                                uint8_t *dst_pixels) {
  uint32_t texel_count;

  if (!src_rgba || !info || !dst_pixels || width < 0 || height < 0) {
    return false;
  }

  texel_count = (uint32_t)width * (uint32_t)height;
  for (uint32_t i = 0; i < texel_count; ++i) {
    const uint8_t *src_texel = src_rgba + i * 4u;
    uint8_t *dst_texel = dst_pixels + i * info->src_bytes_per_texel;

    switch (internalformat) {
    case GL_ALPHA:
      dst_texel[0] = src_texel[3];
      break;
    case GL_LUMINANCE:
    case 1:
    case GL_RED:
    case GL_R8:
      dst_texel[0] = src_texel[0];
      break;
    case GL_LUMINANCE_ALPHA:
      dst_texel[0] = src_texel[0];
      dst_texel[1] = src_texel[3];
      break;
    case 2:
    case GL_RG:
    case GL_RG8:
      dst_texel[0] = src_texel[0];
      dst_texel[1] = src_texel[1];
      break;
    case 3:
    case GL_RGB:
    case GL_RGB8:
      dst_texel[0] = src_texel[0];
      dst_texel[1] = src_texel[1];
      dst_texel[2] = src_texel[2];
      break;
    case 4:
    case GL_RGBA:
    case GL_RGBA8:
      memcpy(dst_texel, src_texel, 4u);
      break;
    default:
      return false;
    }
  }

  return true;
}

static uint8_t *build_copy_upload_pixels(GLint x, GLint y, GLsizei width,
                                         GLsizei height, GLint internalformat,
                                         const TextureFormatInfo *info) {
  size_t texel_count;
  size_t rgba_size;
  size_t upload_size;
  uint8_t *rgba_pixels;
  uint8_t *upload_pixels;

  if (width == 0 || height == 0) {
    return NULL;
  }

  texel_count = (size_t)width * (size_t)height;
  rgba_size = texel_count * 4u;
  upload_size = texel_count * info->src_bytes_per_texel;
  rgba_pixels = (uint8_t *)gl_mem_alloc(GL_MEM_TYPE_MEM2, rgba_size, 64);
  if (!rgba_pixels) {
    _gl_set_error(GL_OUT_OF_MEMORY);
    return NULL;
  }
  upload_pixels = (uint8_t *)gl_mem_alloc(GL_MEM_TYPE_MEM2, upload_size, 64);
  if (!upload_pixels) {
    gl_mem_free(GL_MEM_TYPE_MEM2, rgba_pixels);
    _gl_set_error(GL_OUT_OF_MEMORY);
    return NULL;
  }

  if (!gl_read_color_pixels_rgba8(x, y, width, height, rgba_pixels) ||
      !convert_copy_texels(rgba_pixels, internalformat, info, width, height,
                           upload_pixels)) {
    gl_mem_free(GL_MEM_TYPE_MEM2, rgba_pixels);
    gl_mem_free(GL_MEM_TYPE_MEM2, upload_pixels);
    return NULL;
  }

  gl_mem_free(GL_MEM_TYPE_MEM2, rgba_pixels);
  return upload_pixels;
}

void gl_texture_init(void) {
  memset(g_textures, 0, sizeof(g_textures));
  memset(g_samplers, 0, sizeof(g_samplers));
}

void _gl_GenTextures(GLsizei n, GLuint *textures) {
  if (!g_gl_context || n < 0) {
    if (n < 0) {
      _gl_set_error(GL_INVALID_VALUE);
    }
    return;
  }
  int generated = 0;
  for (int i = 1; i < MAX_TEXTURES && generated < n; i++) {
    if (!g_textures[i].in_use) {
      memset(&g_textures[i], 0, sizeof(GLTexture));
      g_textures[i].in_use = true;
      g_textures[i].target = GL_TEXTURE_2D;
      g_textures[i].min_filter = GL_NEAREST_MIPMAP_LINEAR;
      g_textures[i].mag_filter = GL_LINEAR;
      g_textures[i].wrap_s = GL_REPEAT;
      g_textures[i].wrap_t = GL_REPEAT;
      g_textures[i].wrap_r = GL_REPEAT;
      init_texture_sampler(&g_textures[i]);
      textures[generated++] = i;
    }
  }
  if (generated < n) {
    _gl_set_error(GL_OUT_OF_MEMORY);
  }
}

void _gl_DeleteTextures(GLsizei n, const GLuint *textures) {
  if (!g_gl_context || n < 0) {
    if (n < 0) {
      _gl_set_error(GL_INVALID_VALUE);
    }
    return;
  }
  for (int i = 0; i < n; i++) {
    GLuint id = textures[i];
    if (id > 0 && id < MAX_TEXTURES && g_textures[id].in_use) {
      gl_framebuffer_mark_texture_dirty(id);
      for (int u = 0; u < 32; u++) {
        if (g_gl_context->bound_texture_2d[u] == id) {
          g_gl_context->bound_texture_2d[u] = 0;
        }
        if (g_gl_context->bound_texture_3d[u] == id) {
          g_gl_context->bound_texture_3d[u] = 0;
        }
        if (g_gl_context->bound_texture_cube[u] == id) {
          g_gl_context->bound_texture_cube[u] = 0;
        }
      }
      free_texture_storage(&g_textures[id]);
      g_textures[id].in_use = false;
    }
  }
}

GLboolean _gl_IsTexture(GLuint texture) {
  return (texture > 0 && texture < MAX_TEXTURES && g_textures[texture].in_use)
             ? GL_TRUE
             : GL_FALSE;
}

void _gl_GenSamplers(GLsizei n, GLuint *samplers) {
  int generated = 0;

  if (!g_gl_context || n < 0) {
    if (n < 0) {
      _gl_set_error(GL_INVALID_VALUE);
    }
    return;
  }

  for (int i = 1; i < MAX_SAMPLER_OBJECTS && generated < n; ++i) {
    if (!g_samplers[i].in_use) {
      memset(&g_samplers[i], 0, sizeof(GLSampler));
      g_samplers[i].in_use = true;
      g_samplers[i].min_filter = GL_NEAREST_MIPMAP_LINEAR;
      g_samplers[i].mag_filter = GL_LINEAR;
      g_samplers[i].wrap_s = GL_REPEAT;
      g_samplers[i].wrap_t = GL_REPEAT;
      g_samplers[i].wrap_r = GL_REPEAT;
      init_sampler_object(&g_samplers[i]);
      samplers[generated++] = i;
    }
  }

  if (generated < n) {
    _gl_set_error(GL_OUT_OF_MEMORY);
  }
}

void _gl_DeleteSamplers(GLsizei n, const GLuint *samplers) {
  if (!g_gl_context || n < 0) {
    if (n < 0) {
      _gl_set_error(GL_INVALID_VALUE);
    }
    return;
  }

  for (int i = 0; i < n; ++i) {
    GLuint id = samplers[i];
    if (id == 0 || id >= MAX_SAMPLER_OBJECTS || !g_samplers[id].in_use) {
      continue;
    }

    for (int unit = 0; unit < 32; ++unit) {
      if (g_gl_context->bound_sampler[unit] == id) {
        g_gl_context->bound_sampler[unit] = 0;
      }
    }

    memset(&g_samplers[id], 0, sizeof(GLSampler));
  }
}

GLboolean _gl_IsSampler(GLuint sampler) {
  if (sampler == 0) {
    return GL_FALSE;
  }
  return (sampler < MAX_SAMPLER_OBJECTS && g_samplers[sampler].in_use)
             ? GL_TRUE
             : GL_FALSE;
}

void _gl_BindTexture(GLenum target, GLuint texture) {
  if (!g_gl_context) {
    return;
  }
  if (texture >= MAX_TEXTURES || (texture > 0 && !g_textures[texture].in_use)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  GLuint unit = g_gl_context->active_texture;
  switch (target) {
  case GL_TEXTURE_2D:
    g_gl_context->bound_texture_2d[unit] = texture;
    break;
  case GL_TEXTURE_3D:
    g_gl_context->bound_texture_3d[unit] = texture;
    break;
  case GL_TEXTURE_CUBE_MAP:
    g_gl_context->bound_texture_cube[unit] = texture;
    break;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  if (texture > 0) {
    g_textures[texture].target = target;
  }
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_BindSampler(GLuint unit, GLuint sampler) {
  if (!g_gl_context) {
    return;
  }
  if (unit >= 32) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (sampler >= MAX_SAMPLER_OBJECTS ||
      (sampler > 0 && !g_samplers[sampler].in_use)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  g_gl_context->bound_sampler[unit] = sampler;
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_ActiveTexture(GLenum texture) {
  if (!g_gl_context) {
    return;
  }
  if (texture < GL_TEXTURE0 || texture > GL_TEXTURE0 + 31) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  g_gl_context->active_texture = texture - GL_TEXTURE0;
}

void _gl_TexImage2D(GLenum target, GLint level, GLint internalformat,
                    GLsizei width, GLsizei height, GLint border, GLenum format,
                    GLenum type, const GLvoid *pixels) {
  GLuint id;
  GLTexture *tex;
  TextureFormatInfo info;
  GLsizei expected_width;
  GLsizei expected_height;

  if (!g_gl_context) {
    return;
  }
  if (target != GL_TEXTURE_2D) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  id = get_bound_tex(target);
  if (!id) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (level < 0 || width < 0 || height < 0 || border != 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!get_texture_format_info(internalformat, format, type, pixels != NULL,
                               &info)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  tex = &g_textures[id];
  if (level == 0) {
    if (!rebuild_texture_storage(tex, width, height, 1, internalformat, 1,
                                 false)) {
      _gl_set_error(GL_OUT_OF_MEMORY);
      return;
    }
  } else {
    if (!tex->storage_allocated || tex->internal_format != internalformat) {
      _gl_set_error(GL_INVALID_OPERATION);
      return;
    }

    expected_width = tex->width >> level;
    expected_height = tex->height >> level;
    if (expected_width < 1) {
      expected_width = 1;
    }
    if (expected_height < 1) {
      expected_height = 1;
    }
    if (width != expected_width || height != expected_height) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }

    if ((uint32_t)(level + 1) > tex->gx2_texture.surface.mipLevels &&
        !rebuild_texture_storage(tex, tex->width, tex->height, tex->depth,
                                 tex->internal_format, (uint32_t)(level + 1),
                                 true)) {
      _gl_set_error(GL_OUT_OF_MEMORY);
      return;
    }
  }

  if (!upload_texture_level(tex, level, width, height, 1, &info, pixels)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  tex->complete = true;
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_TexImage3D(GLenum target, GLint level, GLint internalformat,
                    GLsizei width, GLsizei height, GLsizei depth, GLint border,
                    GLenum format, GLenum type, const GLvoid *pixels) {
  GLuint id;
  GLTexture *tex;
  TextureFormatInfo info;
  GLsizei expected_width;
  GLsizei expected_height;
  GLsizei expected_depth;

  if (!g_gl_context) {
    return;
  }
  if (target != GL_TEXTURE_3D) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  id = get_bound_tex(target);
  if (!id) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (level < 0 || width < 0 || height < 0 || depth < 0 || border != 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!get_texture_format_info(internalformat, format, type, pixels != NULL,
                               &info)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  tex = &g_textures[id];
  if (level == 0) {
    if (!rebuild_texture_storage(tex, width, height, depth, internalformat, 1,
                                 false)) {
      _gl_set_error(GL_OUT_OF_MEMORY);
      return;
    }
  } else {
    if (!tex->storage_allocated || tex->internal_format != internalformat) {
      _gl_set_error(GL_INVALID_OPERATION);
      return;
    }

    expected_width = tex->width >> level;
    expected_height = tex->height >> level;
    expected_depth = tex->depth >> level;
    if (expected_width < 1) {
      expected_width = 1;
    }
    if (expected_height < 1) {
      expected_height = 1;
    }
    if (expected_depth < 1) {
      expected_depth = 1;
    }
    if (width != expected_width || height != expected_height ||
        depth != expected_depth) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }

    if ((uint32_t)(level + 1) > tex->gx2_texture.surface.mipLevels &&
        !rebuild_texture_storage(tex, tex->width, tex->height, tex->depth,
                                 tex->internal_format, (uint32_t)(level + 1),
                                 true)) {
      _gl_set_error(GL_OUT_OF_MEMORY);
      return;
    }
  }

  if (!upload_texture_level(tex, level, width, height, depth, &info, pixels)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  tex->complete = true;
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_TexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                       GLsizei width, GLsizei height, GLenum format,
                       GLenum type, const GLvoid *pixels) {
  GLuint id;
  GLTexture *tex;
  TextureFormatInfo info;

  if (!g_gl_context) {
    return;
  }
  if (target != GL_TEXTURE_2D) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  id = get_bound_tex(target);
  if (!id) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (level < 0 || xoffset < 0 || yoffset < 0 || width < 0 || height < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  tex = &g_textures[id];
  if (!tex->storage_allocated || !tex->complete) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if ((uint32_t)level >= tex->gx2_texture.surface.mipLevels) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!get_texture_format_info(tex->internal_format, format, type,
                               pixels != NULL, &info)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (!upload_texture_sub_region(tex, level, xoffset, yoffset, 0, width,
                                 height, 1, &info, pixels)) {
    _gl_set_error(pixels ? GL_INVALID_VALUE : GL_INVALID_OPERATION);
    return;
  }

  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_TexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                       GLint zoffset, GLsizei width, GLsizei height,
                       GLsizei depth, GLenum format, GLenum type,
                       const GLvoid *pixels) {
  GLuint id;
  GLTexture *tex;
  TextureFormatInfo info;

  if (!g_gl_context) {
    return;
  }
  if (target != GL_TEXTURE_3D) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  id = get_bound_tex(target);
  if (!id) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (level < 0 || xoffset < 0 || yoffset < 0 || zoffset < 0 || width < 0 ||
      height < 0 || depth < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  tex = &g_textures[id];
  if (!tex->storage_allocated || !tex->complete) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if ((uint32_t)level >= tex->gx2_texture.surface.mipLevels) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!get_texture_format_info(tex->internal_format, format, type,
                               pixels != NULL, &info)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (!upload_texture_sub_region(tex, level, xoffset, yoffset, zoffset, width,
                                 height, depth, &info, pixels)) {
    _gl_set_error(pixels ? GL_INVALID_VALUE : GL_INVALID_OPERATION);
    return;
  }

  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_CopyTexImage2D(GLenum target, GLint level, GLenum internalformat,
                        GLint x, GLint y, GLsizei width, GLsizei height,
                        GLint border) {
  GLuint id;
  GLTexture *tex;
  TextureFormatInfo info;
  GLsizei expected_width;
  GLsizei expected_height;
  uint8_t *upload_pixels = NULL;

  if (!g_gl_context) {
    return;
  }
  if (target != GL_TEXTURE_2D) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  id = get_bound_tex(target);
  if (!id) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (level < 0 || x < 0 || y < 0 || width < 0 || height < 0 || border != 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!get_texture_format_info(internalformat, GL_RGBA, GL_UNSIGNED_BYTE, false,
                               &info) ||
      !is_copy_color_format_supported(internalformat, &info)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  tex = &g_textures[id];
  if (level > 0) {
    if (!tex->storage_allocated || tex->internal_format != internalformat) {
      _gl_set_error(GL_INVALID_OPERATION);
      return;
    }

    expected_width = tex->width >> level;
    expected_height = tex->height >> level;
    if (expected_width < 1) {
      expected_width = 1;
    }
    if (expected_height < 1) {
      expected_height = 1;
    }
    if (width != expected_width || height != expected_height) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
  }

  upload_pixels =
      build_copy_upload_pixels(x, y, width, height, internalformat, &info);
  if ((width > 0 && height > 0) && !upload_pixels) {
    return;
  }

  if (level == 0) {
    if (!rebuild_texture_storage(tex, width, height, 1, internalformat, 1,
                                 false)) {
      if (upload_pixels) {
        gl_mem_free(GL_MEM_TYPE_MEM2, upload_pixels);
      }
      _gl_set_error(GL_OUT_OF_MEMORY);
      return;
    }
  } else if ((uint32_t)(level + 1) > tex->gx2_texture.surface.mipLevels &&
             !rebuild_texture_storage(tex, tex->width, tex->height, tex->depth,
                                      tex->internal_format,
                                      (uint32_t)(level + 1), true)) {
    if (upload_pixels) {
      gl_mem_free(GL_MEM_TYPE_MEM2, upload_pixels);
    }
    _gl_set_error(GL_OUT_OF_MEMORY);
    return;
  }

  if (upload_pixels &&
      !upload_texture_level(tex, level, width, height, 1, &info,
                            upload_pixels)) {
    gl_mem_free(GL_MEM_TYPE_MEM2, upload_pixels);
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  if (upload_pixels) {
    gl_mem_free(GL_MEM_TYPE_MEM2, upload_pixels);
  }
  tex->complete = true;
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_CopyTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                           GLint yoffset, GLint x, GLint y, GLsizei width,
                           GLsizei height) {
  GLuint id;
  GLTexture *tex;
  TextureFormatInfo info;
  uint8_t *upload_pixels = NULL;

  if (!g_gl_context) {
    return;
  }
  if (target != GL_TEXTURE_2D) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  id = get_bound_tex(target);
  if (!id) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (level < 0 || xoffset < 0 || yoffset < 0 || x < 0 || y < 0 ||
      width < 0 || height < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  tex = &g_textures[id];
  if (!tex->storage_allocated || !tex->complete) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if ((uint32_t)level >= tex->gx2_texture.surface.mipLevels) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!get_texture_format_info(tex->internal_format, GL_RGBA,
                               GL_UNSIGNED_BYTE, false, &info) ||
      !is_copy_color_format_supported(tex->internal_format, &info)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (width == 0 || height == 0) {
    return;
  }

  upload_pixels =
      build_copy_upload_pixels(x, y, width, height, tex->internal_format,
                               &info);
  if (!upload_pixels) {
    return;
  }

  if (!upload_texture_sub_region(tex, level, xoffset, yoffset, 0, width,
                                 height, 1, &info, upload_pixels)) {
    gl_mem_free(GL_MEM_TYPE_MEM2, upload_pixels);
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  gl_mem_free(GL_MEM_TYPE_MEM2, upload_pixels);
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_CompressedTexImage2D(GLenum target, GLint level, GLenum internalformat,
                              GLsizei width, GLsizei height, GLint border,
                              GLsizei imageSize, const GLvoid *data) {
  (void)internalformat;
  (void)data;

  if (!g_gl_context) {
    return;
  }
  if (target != GL_TEXTURE_2D) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (level < 0 || width < 0 || height < 0 || border != 0 || imageSize < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!get_bound_tex(target)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  _gl_set_error(GL_INVALID_ENUM);
}

void _gl_CompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                                 GLint yoffset, GLsizei width, GLsizei height,
                                 GLenum format, GLsizei imageSize,
                                 const GLvoid *data) {
  (void)format;
  (void)data;

  if (!g_gl_context) {
    return;
  }
  if (target != GL_TEXTURE_2D) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (level < 0 || xoffset < 0 || yoffset < 0 || width < 0 || height < 0 ||
      imageSize < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!get_bound_tex(target)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  _gl_set_error(GL_INVALID_ENUM);
}

static bool apply_sampler_parameter(GLenum pname, GLint param,
                                    GLenum *min_filter, GLenum *mag_filter,
                                    GLenum *wrap_s, GLenum *wrap_t,
                                    GLenum *wrap_r) {
  switch (pname) {
  case GL_TEXTURE_MIN_FILTER:
    if (!is_valid_min_filter(param)) {
      return false;
    }
    *min_filter = param;
    return true;
  case GL_TEXTURE_MAG_FILTER:
    if (!is_valid_mag_filter(param)) {
      return false;
    }
    *mag_filter = param;
    return true;
  case GL_TEXTURE_WRAP_S:
    if (!is_valid_wrap_mode(param)) {
      return false;
    }
    *wrap_s = param;
    return true;
  case GL_TEXTURE_WRAP_T:
    if (!is_valid_wrap_mode(param)) {
      return false;
    }
    *wrap_t = param;
    return true;
  case GL_TEXTURE_WRAP_R:
    if (!is_valid_wrap_mode(param)) {
      return false;
    }
    *wrap_r = param;
    return true;
  default:
    return false;
  }
}

void _gl_TexParameteri(GLenum target, GLenum pname, GLint param) {
  if (!g_gl_context) {
    return;
  }
  if (!is_valid_texture_target(target)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  GLuint id = get_bound_tex(target);
  if (!id) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  GLTexture *tex = &g_textures[id];

  if (!apply_sampler_parameter(pname, param, &tex->min_filter, &tex->mag_filter,
                               &tex->wrap_s, &tex->wrap_t, &tex->wrap_r)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  init_texture_sampler(tex);
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_TexParameterf(GLenum target, GLenum pname, GLfloat param) {
  GLint value = (GLint)param;

  if ((GLfloat)value != param) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  _gl_TexParameteri(target, pname, value);
}

void _gl_TexParameteriv(GLenum target, GLenum pname, const GLint *params) {
  if (!params) {
    return;
  }

  _gl_TexParameteri(target, pname, params[0]);
}

void _gl_TexParameterfv(GLenum target, GLenum pname, const GLfloat *params) {
  if (!params) {
    return;
  }

  _gl_TexParameterf(target, pname, params[0]);
}

void _gl_GetTexParameteriv(GLenum target, GLenum pname, GLint *params) {
  GLuint id;
  GLTexture *tex;

  if (!g_gl_context || !params) {
    return;
  }
  if (!is_valid_texture_target(target)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  id = get_bound_tex(target);
  if (!id) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  tex = &g_textures[id];
  switch (pname) {
  case GL_TEXTURE_MIN_FILTER:
    *params = (GLint)tex->min_filter;
    break;
  case GL_TEXTURE_MAG_FILTER:
    *params = (GLint)tex->mag_filter;
    break;
  case GL_TEXTURE_WRAP_S:
    *params = (GLint)tex->wrap_s;
    break;
  case GL_TEXTURE_WRAP_T:
    *params = (GLint)tex->wrap_t;
    break;
  case GL_TEXTURE_WRAP_R:
    *params = (GLint)tex->wrap_r;
    break;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    break;
  }
}

void _gl_TexParameterIiv(GLenum target, GLenum pname, const GLint *params) {
  if (params) _gl_TexParameteri(target, pname, params[0]);
}
void _gl_TexParameterIuiv(GLenum target, GLenum pname, const GLuint *params) {
  if (params) _gl_TexParameteri(target, pname, (GLint)params[0]);
}
void _gl_GetTexParameterIiv(GLenum target, GLenum pname, GLint *params) {
  _gl_GetTexParameteriv(target, pname, params);
}
void _gl_GetTexParameterIuiv(GLenum target, GLenum pname, GLuint *params) {
  GLint v = 0; _gl_GetTexParameteriv(target, pname, &v); if (params) *params = (GLuint)v;
}

void _gl_GetTexParameterfv(GLenum target, GLenum pname, GLfloat *params) {
  GLuint id;
  GLTexture *tex;

  if (!g_gl_context || !params) {
    return;
  }
  if (!is_valid_texture_target(target)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  id = get_bound_tex(target);
  if (!id) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  tex = &g_textures[id];
  switch (pname) {
  case GL_TEXTURE_MIN_FILTER:
    *params = (GLfloat)tex->min_filter;
    break;
  case GL_TEXTURE_MAG_FILTER:
    *params = (GLfloat)tex->mag_filter;
    break;
  case GL_TEXTURE_WRAP_S:
    *params = (GLfloat)tex->wrap_s;
    break;
  case GL_TEXTURE_WRAP_T:
    *params = (GLfloat)tex->wrap_t;
    break;
  case GL_TEXTURE_WRAP_R:
    *params = (GLfloat)tex->wrap_r;
    break;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    break;
  }
}

void _gl_SamplerParameteriv(GLuint sampler, GLenum pname, const GLint *param) {
  if (!param) return;
  _gl_SamplerParameteri(sampler, pname, param[0]);
}

void _gl_SamplerParameterIiv(GLuint sampler, GLenum pname, const GLint *param) {
  if (param) _gl_SamplerParameteri(sampler, pname, param[0]);
}
void _gl_SamplerParameterIuiv(GLuint sampler, GLenum pname, const GLuint *param) {
  if (param) _gl_SamplerParameteri(sampler, pname, (GLint)param[0]);
}

void _gl_SamplerParameterfv(GLuint sampler, GLenum pname,
                            const GLfloat *param) {
  if (!param) {
    return;
  }

  _gl_SamplerParameterf(sampler, pname, param[0]);
}

void _gl_SamplerParameteri(GLuint sampler, GLenum pname, GLint param) {
  if (!g_gl_context) {
    return;
  }
  if (sampler == 0 || sampler >= MAX_SAMPLER_OBJECTS ||
      !g_samplers[sampler].in_use) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!apply_sampler_parameter(pname, param, &g_samplers[sampler].min_filter,
                               &g_samplers[sampler].mag_filter,
                               &g_samplers[sampler].wrap_s,
                               &g_samplers[sampler].wrap_t,
                               &g_samplers[sampler].wrap_r)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  init_sampler_object(&g_samplers[sampler]);
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_SamplerParameterf(GLuint sampler, GLenum pname, GLfloat param) {
  GLint value = (GLint)param;

  if ((GLfloat)value != param) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  _gl_SamplerParameteri(sampler, pname, value);
}

void _gl_GetSamplerParameteriv(GLuint sampler, GLenum pname, GLint *params) {
  if (!g_gl_context || !params) {
    return;
  }
  if (sampler == 0 || sampler >= MAX_SAMPLER_OBJECTS ||
      !g_samplers[sampler].in_use) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  switch (pname) {
  case GL_TEXTURE_MIN_FILTER:
    *params = (GLint)g_samplers[sampler].min_filter;
    break;
  case GL_TEXTURE_MAG_FILTER:
    *params = (GLint)g_samplers[sampler].mag_filter;
    break;
  case GL_TEXTURE_WRAP_S:
    *params = (GLint)g_samplers[sampler].wrap_s;
    break;
  case GL_TEXTURE_WRAP_T:
    *params = (GLint)g_samplers[sampler].wrap_t;
    break;
  case GL_TEXTURE_WRAP_R:
    *params = (GLint)g_samplers[sampler].wrap_r;
    break;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    break;
  }
}

void _gl_GetSamplerParameterIiv(GLuint sampler, GLenum pname, GLint *params) {
  _gl_GetSamplerParameteriv(sampler, pname, params);
}
void _gl_GetSamplerParameterIuiv(GLuint sampler, GLenum pname, GLuint *params) {
  GLint v = 0; _gl_GetSamplerParameteriv(sampler, pname, &v); if (params) *params = (GLuint)v;
}

void _gl_GetSamplerParameterfv(GLuint sampler, GLenum pname, GLfloat *params) {
  if (!g_gl_context || !params) {
    return;
  }
  if (sampler == 0 || sampler >= MAX_SAMPLER_OBJECTS ||
      !g_samplers[sampler].in_use) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  switch (pname) {
  case GL_TEXTURE_MIN_FILTER:
    *params = (GLfloat)g_samplers[sampler].min_filter;
    break;
  case GL_TEXTURE_MAG_FILTER:
    *params = (GLfloat)g_samplers[sampler].mag_filter;
    break;
  case GL_TEXTURE_WRAP_S:
    *params = (GLfloat)g_samplers[sampler].wrap_s;
    break;
  case GL_TEXTURE_WRAP_T:
    *params = (GLfloat)g_samplers[sampler].wrap_t;
    break;
  case GL_TEXTURE_WRAP_R:
    *params = (GLfloat)g_samplers[sampler].wrap_r;
    break;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    break;
  }
}

void _gl_TexImage1D(GLenum target, GLint level, GLint internalformat,
                   GLsizei width, GLint border, GLenum format,
                   GLenum type, const GLvoid *pixels) {
    _gl_TexImage2D(GL_TEXTURE_2D, level, internalformat, width, 1, border, format, type, pixels);
}

void _gl_TexSubImage1D(GLenum target, GLint level, GLint xoffset,
                       GLsizei width, GLenum format, GLenum type,
                       const GLvoid *pixels) {
    _gl_TexSubImage2D(GL_TEXTURE_2D, level, xoffset, 0, width, 1, format, type, pixels);
}

void _gl_CopyTexImage1D(GLenum target, GLint level, GLenum internalformat,
                        GLint x, GLint y, GLsizei width, GLint border) {
    _gl_CopyTexImage2D(GL_TEXTURE_2D, level, internalformat, x, y, width, 1, border);
}

void _gl_CopyTexSubImage1D(GLenum target, GLint level, GLint xoffset,
                           GLint x, GLint y, GLsizei width) {
    _gl_CopyTexSubImage2D(GL_TEXTURE_2D, level, xoffset, 0, x, y, width, 1);
}

void _gl_CopyTexSubImage3D(GLenum target, GLint level, GLint xoffset,
                           GLint yoffset, GLint zoffset, GLint x, GLint y,
                           GLsizei width, GLsizei height) {
    _gl_set_error(GL_INVALID_OPERATION);
}

void _gl_CompressedTexImage1D(GLenum target, GLint level, GLenum internalformat,
                              GLsizei width, GLint border, GLsizei imageSize,
                              const GLvoid *data) {
    _gl_CompressedTexImage2D(GL_TEXTURE_2D, level, internalformat, width, 1, border, imageSize, data);
}

void _gl_CompressedTexImage3D(GLenum target, GLint level, GLenum internalformat,
                              GLsizei width, GLsizei height, GLsizei depth,
                              GLint border, GLsizei imageSize, const GLvoid *data) {
    _gl_set_error(GL_INVALID_OPERATION);
}

void _gl_CompressedTexSubImage1D(GLenum target, GLint level, GLint xoffset,
                                 GLsizei width, GLenum format, GLsizei imageSize,
                                 const GLvoid *data) {
    _gl_CompressedTexSubImage2D(GL_TEXTURE_2D, level, xoffset, 0, width, 1, format, imageSize, data);
}

void _gl_CompressedTexSubImage3D(GLenum target, GLint level, GLint xoffset,
                                 GLint yoffset, GLint zoffset, GLsizei width,
                                 GLsizei height, GLsizei depth, GLenum format,
                                 GLsizei imageSize, const GLvoid *data) {
    _gl_set_error(GL_INVALID_OPERATION);
}

void _gl_GetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params) {
    if (!g_gl_context || !params) return;
    switch (pname) {
        case GL_TEXTURE_WIDTH: *params = 0; break;
        case GL_TEXTURE_HEIGHT: *params = 0; break;
        case GL_TEXTURE_INTERNAL_FORMAT: *params = GL_RGBA8; break;
        default: _gl_set_error(GL_INVALID_ENUM); break;
    }
}

void _gl_GetTexLevelParameterfv(GLenum target, GLint level, GLenum pname, GLfloat *params) {
    GLint iparams;
    _gl_GetTexLevelParameteriv(target, level, pname, &iparams);
    if (params) *params = (GLfloat)iparams;
}

void _gl_GenerateMipmap(GLenum target) {
  GLuint id;
  GLTexture *tex;
  TextureFormatInfo info;
  uint32_t mip_count;

  if (!g_gl_context) {
    return;
  }
  if (target != GL_TEXTURE_2D && target != GL_TEXTURE_3D) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  id = get_bound_tex(target);
  if (!id) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  tex = &g_textures[id];
  if (!tex->storage_allocated || !tex->complete ||
      !get_texture_format_info(tex->internal_format, GL_RGBA,
                               GL_UNSIGNED_BYTE, false, &info) ||
      !info.mipmap_supported) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  mip_count = calc_full_mip_count(tex->width, tex->height,
                                  target == GL_TEXTURE_3D ? tex->depth : 1);
  if (mip_count <= 1) {
    return;
  }

  if (tex->gx2_texture.surface.mipLevels != mip_count &&
      !rebuild_texture_storage(tex, tex->width, tex->height, tex->depth,
                               tex->internal_format, mip_count, true)) {
    _gl_set_error(GL_OUT_OF_MEMORY);
    return;
  }

  for (uint32_t level = 1; level < mip_count; ++level) {
    if (!generate_mipmap_level(tex, level, &info)) {
      _gl_set_error(GL_INVALID_OPERATION);
      return;
    }
  }

  tex->max_level = (GLint)(mip_count - 1);
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void gl_bind_textures(void) {

}

GX2Texture *gl_get_gx2_texture(GLuint id) {
  if (id > 0 && id < MAX_TEXTURES && g_textures[id].in_use &&
      g_textures[id].complete) {
    return &g_textures[id].gx2_texture;
  }
  return NULL;
}

GLint gl_get_texture_internal_format(GLuint id) {
  if (id > 0 && id < MAX_TEXTURES && g_textures[id].in_use &&
      g_textures[id].complete) {
    return g_textures[id].internal_format;
  }
  return 0;
}


GX2Sampler *gl_get_gx2_sampler(GLuint id, bool use_sampler_obj) {
  if (use_sampler_obj) {
    if (id > 0 && id < MAX_SAMPLER_OBJECTS && g_samplers[id].in_use)
      return &g_samplers[id].gx2_sampler;
    return NULL;
  }
  if (id > 0 && id < MAX_TEXTURES && g_textures[id].in_use && g_textures[id].complete)
    return &g_textures[id].gx2_sampler;
  return NULL;
}

GX2Sampler *gl_get_effective_gx2_sampler(GLuint unit, GLuint texture) {
  GLuint sampler_id;

  if (!g_gl_context || unit >= 32) {
    return NULL;
  }

  sampler_id = g_gl_context->bound_sampler[unit];
  if (sampler_id > 0 && sampler_id < MAX_SAMPLER_OBJECTS &&
      g_samplers[sampler_id].in_use) {
    return &g_samplers[sampler_id].gx2_sampler;
  }

  return gl_get_gx2_sampler(texture, false);
}

#ifdef __cplusplus
}
#endif
