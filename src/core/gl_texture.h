#ifndef GL33_TEXTURE_H
#define GL33_TEXTURE_H

#include "core/gl_context.h"
#include <gx2/texture.h>
#include <gx2/sampler.h>

#ifdef __cplusplus
extern "C" {
#endif

void gl_texture_init(void);

void _gl_GenTextures(GLsizei n, GLuint *textures);
void _gl_DeleteTextures(GLsizei n, const GLuint *textures);
void _gl_BindTexture(GLenum target, GLuint texture);
void _gl_ActiveTexture(GLenum texture);
void _gl_TexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void _gl_TexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void _gl_TexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                       GLsizei width, GLsizei height, GLenum format, GLenum type,
                       const GLvoid *pixels);
void _gl_TexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                       GLint zoffset, GLsizei width, GLsizei height,
                       GLsizei depth, GLenum format, GLenum type,
                       const GLvoid *pixels);
void _gl_TexParameteri(GLenum target, GLenum pname, GLint param);
void _gl_GenerateMipmap(GLenum target);

/* Connects to gl_flush_state to apply bindings before drawing */
void gl_bind_textures(void);

// Used by FBO
struct GX2Texture;
struct GX2Texture* gl_get_gx2_texture(GLuint texture);
struct GX2Sampler;
struct GX2Sampler* gl_get_gx2_sampler(GLuint texture);

#ifdef __cplusplus
}
#endif

#endif /* GL33_TEXTURE_H */
