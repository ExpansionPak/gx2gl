#ifndef GX2GL_SHADER_COMPAT_H
#define GX2GL_SHADER_COMPAT_H

#include "gl_context.h"

#ifdef __cplusplus
extern "C" {
#endif

char *gx2gl_prepare_shader_source_for_cafeglsl(const char *source, GLenum shader_type);
char *gx2gl_prepare_shader_source_for_cafeglsl_ex(const char *source,
                                                  GLenum shader_type,
                                                  char *info_log_out,
                                                  int info_log_max_length);

#ifdef __cplusplus
}
#endif

#endif
