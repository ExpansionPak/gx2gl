#ifndef GL33_CONTEXT_H
#define GL33_CONTEXT_H

#include "gl/gl.h"
#include <coreinit/mutex.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*glGenBuffers)(GLsizei, GLuint*);
    void (*glDeleteBuffers)(GLsizei, const GLuint*);
    void (*glBindBuffer)(GLenum, GLuint);
    void (*glBindBufferBase)(GLenum, GLuint, GLuint);
    void (*glBindBufferRange)(GLenum, GLuint, GLuint, GLintptr, GLsizeiptr);
    void (*glBufferData)(GLenum, GLsizeiptr, const GLvoid*, GLenum);
    void (*glBufferSubData)(GLenum, GLintptr, GLsizeiptr, const GLvoid*);
    void (*glEnable)(GLenum);
    void (*glDisable)(GLenum);
    GLboolean (*glIsEnabled)(GLenum);
    void (*glClearColor)(GLclampf, GLclampf, GLclampf, GLclampf);
    void (*glClearDepth)(GLclampd);
    void (*glClearStencil)(GLint);
    void (*glClear)(GLbitfield);
    void (*glDrawArrays)(GLenum, GLint, GLsizei);
    void (*glDrawArraysInstanced)(GLenum, GLint, GLsizei, GLsizei);
    void (*glDrawElements)(GLenum, GLsizei, GLenum, const GLvoid*);
    void (*glDrawElementsInstanced)(GLenum, GLsizei, GLenum, const GLvoid*,
                                    GLsizei);
    void (*glGetIntegerv)(GLenum, GLint*);
    void (*glGetFloatv)(GLenum, GLfloat*);
    const GLubyte* (*glGetString)(GLenum);
    
    void* (*glMapBuffer)(GLenum, GLenum);
    void* (*glMapBufferRange)(GLenum, GLintptr, GLsizeiptr, GLbitfield);
    GLboolean (*glUnmapBuffer)(GLenum);
    
    void (*glGenTextures)(GLsizei, GLuint*);
    void (*glDeleteTextures)(GLsizei, const GLuint*);
    void (*glBindTexture)(GLenum, GLuint);
    void (*glActiveTexture)(GLenum);
    void (*glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid*);
    void (*glTexImage3D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid*);
    void (*glTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid*);
    void (*glTexSubImage3D)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const GLvoid*);
    void (*glTexParameteri)(GLenum, GLenum, GLint);
    void (*glGenerateMipmap)(GLenum);
    
    GLuint (*glCreateShader)(GLenum);
    void (*glDeleteShader)(GLuint);
    void (*glShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
    void (*glCompileShader)(GLuint);
    GLuint (*glCreateProgram)(void);
    void (*glDeleteProgram)(GLuint);
    void (*glAttachShader)(GLuint, GLuint);
    void (*glDetachShader)(GLuint, GLuint);
    void (*glLinkProgram)(GLuint);
    void (*glUseProgram)(GLuint);
    void (*glGetShaderiv)(GLuint, GLenum, GLint *);
    void (*glGetProgramiv)(GLuint, GLenum, GLint *);
    void (*glGetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
    void (*glGetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
    void (*glUniform1f)(GLint, GLfloat);
    void (*glUniform1fv)(GLint, GLsizei, const GLfloat*);
    void (*glUniform1i)(GLint, GLint);
    void (*glUniform2f)(GLint, GLfloat, GLfloat);
    void (*glUniform2fv)(GLint, GLsizei, const GLfloat*);
    void (*glUniform3f)(GLint, GLfloat, GLfloat, GLfloat);
    void (*glUniform3fv)(GLint, GLsizei, const GLfloat*);
    void (*glUniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
    void (*glUniform4fv)(GLint, GLsizei, const GLfloat*);
    void (*glUniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*);
    GLint (*glGetUniformLocation)(GLuint, const GLchar*);
    GLint (*glGetAttribLocation)(GLuint, const GLchar*);
    GLuint (*glGetUniformBlockIndex)(GLuint, const GLchar*);
    void (*glUniformBlockBinding)(GLuint, GLuint, GLuint);
    void (*glWiiULoadShaderGroup)(GLuint, const void*);
    void (*glWiiULoadShaderGroupGFD)(GLuint, GLuint, const void*);
    
    void (*glGenVertexArrays)(GLsizei, GLuint*);
    void (*glDeleteVertexArrays)(GLsizei, const GLuint*);
    void (*glBindVertexArray)(GLuint);
    void (*glEnableVertexAttribArray)(GLuint);
    void (*glDisableVertexAttribArray)(GLuint);
    void (*glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const GLvoid*);
    void (*glVertexAttribDivisor)(GLuint, GLuint);
    
    void (*glGenFramebuffers)(GLsizei, GLuint*);
    void (*glDeleteFramebuffers)(GLsizei, const GLuint*);
    void (*glBindFramebuffer)(GLenum, GLuint);
    void (*glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
    void (*glDrawBuffer)(GLenum);
    void (*glDrawBuffers)(GLsizei, const GLenum*);
    void (*glReadBuffer)(GLenum);
    void (*glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
                         GLvoid*);
    
    void (*glBlendFunc)(GLenum, GLenum);
    void (*glBlendEquation)(GLenum);
    void (*glBlendEquationSeparate)(GLenum, GLenum);
    void (*glBlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum);
    void (*glBlendColor)(GLclampf, GLclampf, GLclampf, GLclampf);
    void (*glDepthFunc)(GLenum);
    void (*glDepthMask)(GLboolean);
    void (*glDepthRange)(GLclampd, GLclampd);
    void (*glStencilFunc)(GLenum, GLint, GLuint);
    void (*glStencilFuncSeparate)(GLenum, GLenum, GLint, GLuint);
    void (*glStencilOp)(GLenum, GLenum, GLenum);
    void (*glStencilOpSeparate)(GLenum, GLenum, GLenum, GLenum);
    void (*glStencilMask)(GLuint);
    void (*glStencilMaskSeparate)(GLenum, GLuint);
    void (*glCullFace)(GLenum);
    void (*glFrontFace)(GLenum);
    void (*glPolygonMode)(GLenum, GLenum);
    void (*glPolygonOffset)(GLfloat, GLfloat);
    void (*glViewport)(GLint, GLint, GLsizei, GLsizei);
    void (*glScissor)(GLint, GLint, GLsizei, GLsizei);
    void (*glColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
    void (*glLineWidth)(GLfloat);
    void (*glGetBooleanv)(GLenum, GLboolean *);
    void (*glGetDoublev)(GLenum, GLdouble *);
} gl_dispatch_t;

#define GL_DIRTY_BLEND         (1 << 0)
#define GL_DIRTY_DEPTH_STENCIL (1 << 1)
#define GL_DIRTY_CULL          (1 << 2)
#define GL_DIRTY_SCISSOR       (1 << 3)
#define GL_DIRTY_VIEWPORT      (1 << 4)
#define GL_DIRTY_COLOR_MASK    (1 << 5)
#define GL_DIRTY_LINE_WIDTH    (1 << 6)
#define GL_DIRTY_FRONT_FACE    (1 << 7)
#define GL_DIRTY_POLYGON_MODE  (1 << 8)
#define GL_DIRTY_VAO           (1 << 9)
#define GL_DIRTY_PROGRAM       (1 << 10)
#define GL_DIRTY_TEXTURE_BINDINGS (1 << 11)
#define GL_DIRTY_FRAMEBUFFER      (1 << 12)
#define GL_DIRTY_UNIFORM_BINDINGS (1 << 13)

#define GL_ERROR_QUEUE_SIZE 8
#define GL33_MAX_UNIFORM_BUFFER_BINDINGS 36

typedef struct {
    GLuint buffer;
    GLintptr offset;
    GLsizeiptr size;
    GLboolean whole_buffer;
} gl_uniform_buffer_binding_t;

typedef struct {
    GLuint bound_array_buffer;
    GLuint bound_element_array_buffer;
    GLuint bound_uniform_buffer;
    gl_uniform_buffer_binding_t uniform_buffer_bindings[GL33_MAX_UNIFORM_BUFFER_BINDINGS];
    
    GLuint active_texture;
    GLuint bound_texture_2d[32];
    GLuint bound_texture_3d[32];
    GLuint bound_texture_cube[32];
    
    GLuint bound_framebuffer;
    GLuint bound_read_framebuffer;
    GLuint bound_program;
    GLuint bound_vao;

    struct {
        GLint x, y;
        GLsizei width, height;
        GLfloat near_z, far_z;
    } viewport;

    struct {
        GLint x, y;
        GLsizei width, height;
    } scissor;

    GLenum blend_src_rgb, blend_dst_rgb, blend_src_alpha, blend_dst_alpha;
    GLenum blend_eq_rgb, blend_eq_alpha;
    GLfloat blend_color[4];
    GLenum depth_func;
    GLboolean depth_mask;
    GLuint stencil_compare_mask[2];
    GLuint stencil_write_mask[2];
    GLenum stencil_func[2], stencil_fail[2], stencil_zfail[2], stencil_zpass[2];
    GLint stencil_ref[2];
    GLenum cull_face_mode;
    GLenum front_face;
    GLenum polygon_mode;
    GLfloat polygon_offset_factor;
    GLfloat polygon_offset_units;
    GLboolean color_mask[4];
    GLfloat line_width;
    GLfloat clear_color[4];
    GLfloat clear_depth;
    GLint clear_stencil;

    GLboolean depth_test_enabled;
    GLboolean stencil_test_enabled;
    GLboolean blend_enabled;
    GLboolean cull_face_enabled;
    GLboolean scissor_test_enabled;
    GLboolean polygon_offset_point_enabled;
    GLboolean polygon_offset_line_enabled;
    GLboolean polygon_offset_fill_enabled;
    GLenum error;

    GLenum error_queue[GL_ERROR_QUEUE_SIZE];
    uint32_t error_head;
    uint32_t error_tail;
    OSMutex error_mutex;

    uint32_t dirty_flags;
    gl_dispatch_t dispatch;
} gl_context_t;

extern gl_context_t *g_gl_context;

gl_context_t* gl_context_create(void);
void gl_context_destroy(gl_context_t *ctx);

void _gl_set_error(GLenum error);
const GLubyte* _gl_GetString(GLenum name);
void _gl_GetBooleanv(GLenum pname, GLboolean *params);
void _gl_GetDoublev(GLenum pname, GLdouble *params);
void _gl_GetIntegerv(GLenum pname, GLint *params);
void _gl_GetFloatv(GLenum pname, GLfloat *params);

#ifdef __cplusplus
}
#endif

#endif /* GL33_CONTEXT_H */
