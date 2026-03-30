#ifndef GL33_GL_H
#define GL33_GL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef void GLvoid;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef float GLfloat;
typedef float GLclampf;
typedef int GLfixed;
typedef double GLdouble;
typedef double GLclampd;
typedef char GLchar;
typedef short GLhalf;
typedef ptrdiff_t GLintptr;
typedef ptrdiff_t GLsizeiptr;

#define GL_INVALID_INDEX 0xFFFFFFFFu

// Common error enums
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NONE 0
#define GL_NO_ERROR 0
#define GL_INVALID_ENUM 0x0500
#define GL_INVALID_VALUE 0x0501
#define GL_INVALID_OPERATION 0x0502
#define GL_OUT_OF_MEMORY 0x0505
#define GL_INVALID_FRAMEBUFFER_OPERATION 0x0506

// String query enums
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_EXTENSIONS 0x1F03
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#define GL_CURRENT_PROGRAM 0x8B8D

#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_MAX_TEXTURE_IMAGE_UNITS 0x8872
#define GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS 0x8B4D
#define GL_MAX_ARRAY_TEXTURE_LAYERS 0x88FF
#define GL_MAX_COLOR_ATTACHMENTS 0x8E23
#define GL_MAX_DRAW_BUFFERS 0x8824
#define GL_MAX_RENDERBUFFER_SIZE 0x84E8
#define GL_MAX_VERTEX_ATTRIBS 0x8869
#define GL_MAX_UNIFORM_BUFFER_BINDINGS 0x8A2F
#define GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT 0x8A34
#define GL_NUM_EXTENSIONS 0x821D

// State toggle enums
#define GL_DEPTH_TEST 0x0B71
#define GL_STENCIL_TEST 0x0B90
#define GL_BLEND 0x0BE2
#define GL_CULL_FACE 0x0B44
#define GL_SCISSOR_TEST 0x0C11
#define GL_POLYGON_OFFSET_POINT 0x2A01
#define GL_POLYGON_OFFSET_LINE 0x2A02
#define GL_POLYGON_OFFSET_FILL 0x8037

// Blend factor enums
#define GL_ZERO 0
#define GL_ONE 1
#define GL_SRC_COLOR 0x0300
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_ALPHA 0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#define GL_DST_COLOR 0x0306
#define GL_ONE_MINUS_DST_COLOR 0x0307
#define GL_SRC_ALPHA_SATURATE 0x0308
#define GL_CONSTANT_COLOR 0x8001
#define GL_ONE_MINUS_CONSTANT_COLOR 0x8002
#define GL_CONSTANT_ALPHA 0x8003
#define GL_ONE_MINUS_CONSTANT_ALPHA 0x8004

#define GL_FUNC_ADD 0x8006
#define GL_BLEND_COLOR 0x8005
#define GL_BLEND_EQUATION 0x8009
#define GL_FUNC_SUBTRACT 0x800A
#define GL_FUNC_REVERSE_SUBTRACT 0x800B
#define GL_MIN 0x8007
#define GL_MAX 0x8008

#define GL_NEVER 0x0200
#define GL_LESS 0x0201
#define GL_EQUAL 0x0202
#define GL_LEQUAL 0x0203
#define GL_GREATER 0x0204
#define GL_NOTEQUAL 0x0205
#define GL_GEQUAL 0x0206
#define GL_ALWAYS 0x0207

#define GL_KEEP 0x1E00
#define GL_REPLACE 0x1E01
#define GL_INCR 0x1E02
#define GL_DECR 0x1E03
#define GL_INVERT 0x150A
#define GL_INCR_WRAP 0x8507
#define GL_DECR_WRAP 0x8508

#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_FRONT_AND_BACK 0x0408

#define GL_CW 0x0900
#define GL_CCW 0x0901

#define GL_POINT 0x1B00
#define GL_LINE 0x1B01
#define GL_FILL 0x1B02
#define GL_DONT_CARE 0x1100
#define GL_FASTEST 0x1101
#define GL_NICEST 0x1102

// Texture target enums
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE 0x1702
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_3D 0x806F
#define GL_TEXTURE_CUBE_MAP 0x8513
#define GL_TEXTURE_2D_ARRAY 0x8C1A

#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TEXTURE_WRAP_R 0x8072

#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_MIRRORED_REPEAT 0x8370

#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_LINEAR_MIPMAP_NEAREST 0x2701
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#define GL_LINEAR_MIPMAP_LINEAR 0x2703

// Pixel format enums
#define GL_ALPHA 0x1906
#define GL_LUMINANCE 0x1909
#define GL_LUMINANCE_ALPHA 0x190A
#define GL_RED 0x1903
#define GL_RG 0x8227
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_DEPTH_COMPONENT 0x1902
#define GL_DEPTH_STENCIL 0x84F9

#define GL_R8 0x8229
#define GL_RG8 0x822B
#define GL_RGB8 0x8051
#define GL_RGBA8 0x8058
#define GL_RGBA16F 0x881A
#define GL_RGBA32F 0x8814
#define GL_DEPTH_COMPONENT32F 0x8CAC
#define GL_DEPTH24_STENCIL8 0x88F0

#define GL_UNSIGNED_BYTE 0x1401
#define GL_INT 0x1404
#define GL_FLOAT 0x1406
#define GL_HALF_FLOAT 0x140B
#define GL_UNSIGNED_INT_24_8 0x84FA

// Shader stage enums
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_GEOMETRY_SHADER 0x8DD9

// Binding target enums
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_UNIFORM_BUFFER 0x8A11
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_READ_FRAMEBUFFER 0x8CA8

// Primitive mode enums
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN 0x0006
#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_LINE_LOOP 0x0002
#define GL_LINE_STRIP 0x0003

#define GL_UNSIGNED_SHORT 0x1403
#define GL_UNSIGNED_INT 0x1405
#define GL_FIXED 0x140C

#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_STREAM_DRAW 0x88E0

#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_COLOR_ATTACHMENT1 0x8CE1
#define GL_COLOR_ATTACHMENT2 0x8CE2
#define GL_COLOR_ATTACHMENT3 0x8CE3
#define GL_COLOR_ATTACHMENT4 0x8CE4
#define GL_COLOR_ATTACHMENT5 0x8CE5
#define GL_COLOR_ATTACHMENT6 0x8CE6
#define GL_COLOR_ATTACHMENT7 0x8CE7
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT 0x8CD6
#define GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT 0x8CD7
#define GL_FRAMEBUFFER_UNSUPPORTED 0x8CDD
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_STENCIL_ATTACHMENT 0x8D20
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A

#define GL_READ_ONLY 0x88B8
#define GL_WRITE_ONLY 0x88B9
#define GL_READ_WRITE 0x88BA
#define GL_BUFFER_ACCESS 0x88BB
#define GL_BUFFER_MAPPED 0x88BC
#define GL_BUFFER_MAP_POINTER 0x88BD
#define GL_BUFFER_SIZE 0x8764
#define GL_BUFFER_USAGE 0x8765

#define GL_RENDERBUFFER_WIDTH 0x8D42
#define GL_RENDERBUFFER_HEIGHT 0x8D43
#define GL_RENDERBUFFER_INTERNAL_FORMAT 0x8D44

#define GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE 0x8CD0
#define GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME 0x8CD1
#define GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL 0x8CD2

#define GL_DEPTH_RANGE 0x0B70
#define GL_VIEWPORT 0x0BA2
#define GL_DEPTH_WRITEMASK 0x0B72
#define GL_LINE_WIDTH 0x0B21
#define GL_STENCIL_WRITEMASK 0x0B98
#define GL_SCISSOR_BOX 0x0C10
#define GL_COLOR_WRITEMASK 0x0C23
#define GL_COLOR_CLEAR_VALUE 0x0C22
#define GL_DEPTH_CLEAR_VALUE 0x0B73
#define GL_STENCIL_CLEAR_VALUE 0x0B91
#define GL_GENERATE_MIPMAP_HINT 0x8192
#define GL_SAMPLE_COVERAGE 0x80A0
#define GL_SAMPLE_COVERAGE_VALUE 0x80AA
#define GL_SAMPLE_COVERAGE_INVERT 0x80AB
#define GL_NUM_COMPRESSED_TEXTURE_FORMATS 0x86A2
#define GL_COMPRESSED_TEXTURE_FORMATS 0x86A3
#define GL_VERTEX_ATTRIB_ARRAY_ENABLED 0x8622
#define GL_VERTEX_ATTRIB_ARRAY_SIZE 0x8623
#define GL_VERTEX_ATTRIB_ARRAY_STRIDE 0x8624
#define GL_VERTEX_ATTRIB_ARRAY_TYPE 0x8625
#define GL_CURRENT_VERTEX_ATTRIB 0x8626
#define GL_VERTEX_ATTRIB_ARRAY_POINTER 0x8645
#define GL_VERTEX_ATTRIB_ARRAY_NORMALIZED 0x886A
#define GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING 0x889F
#define GL_VERTEX_ATTRIB_ARRAY_INTEGER 0x88FD
#define GL_VERTEX_ATTRIB_ARRAY_DIVISOR 0x88FE
#define GL_PACK_ROW_LENGTH 0x0D02
#define GL_PACK_SKIP_ROWS 0x0D03
#define GL_PACK_SKIP_PIXELS 0x0D04
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#define GL_UNPACK_SKIP_ROWS 0x0CF3
#define GL_UNPACK_SKIP_PIXELS 0x0CF4
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_SKIP_IMAGES 0x806B
#define GL_PACK_IMAGE_HEIGHT 0x806C
#define GL_UNPACK_SKIP_IMAGES 0x806D
#define GL_UNPACK_IMAGE_HEIGHT 0x806E

#define GL_DELETE_STATUS 0x8B80
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_VALIDATE_STATUS 0x8B83
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_ATTACHED_SHADERS 0x8B85
#define GL_ACTIVE_UNIFORMS 0x8B86
#define GL_ACTIVE_UNIFORM_MAX_LENGTH 0x8B87
#define GL_SHADER_SOURCE_LENGTH 0x8B88
#define GL_ACTIVE_ATTRIBUTES 0x8B89
#define GL_ACTIVE_ATTRIBUTE_MAX_LENGTH 0x8B8A
#define GL_SHADER_TYPE 0x8B4F
#define GL_SHADER_BINARY_FORMATS 0x8DF8
#define GL_NUM_SHADER_BINARY_FORMATS 0x8DF9
#define GL_SHADER_COMPILER 0x8DFA
#define GL_LOW_FLOAT 0x8DF0
#define GL_MEDIUM_FLOAT 0x8DF1
#define GL_HIGH_FLOAT 0x8DF2
#define GL_LOW_INT 0x8DF3
#define GL_MEDIUM_INT 0x8DF4
#define GL_HIGH_INT 0x8DF5
#define GL_IMPLEMENTATION_COLOR_READ_TYPE 0x8B9A
#define GL_IMPLEMENTATION_COLOR_READ_FORMAT 0x8B9B
#define GL_FLOAT_VEC2 0x8B50
#define GL_FLOAT_VEC3 0x8B51
#define GL_FLOAT_VEC4 0x8B52
#define GL_INT_VEC2 0x8B53
#define GL_INT_VEC3 0x8B54
#define GL_INT_VEC4 0x8B55
#define GL_BOOL 0x8B56
#define GL_BOOL_VEC2 0x8B57
#define GL_BOOL_VEC3 0x8B58
#define GL_BOOL_VEC4 0x8B59
#define GL_FLOAT_MAT2 0x8B5A
#define GL_FLOAT_MAT3 0x8B5B
#define GL_FLOAT_MAT4 0x8B5C
#define GL_SAMPLER_1D 0x8B5D
#define GL_SAMPLER_2D 0x8B5E
#define GL_SAMPLER_3D 0x8B5F
#define GL_SAMPLER_CUBE 0x8B60

#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_STENCIL_BUFFER_BIT 0x00000400
#define GL_COLOR_BUFFER_BIT 0x00004000

// Public API entrypoints
void glGenBuffers(GLsizei n, GLuint *buffers);
void glDeleteBuffers(GLsizei n, const GLuint *buffers);
GLboolean glIsBuffer(GLuint buffer);
void glBindBuffer(GLenum target, GLuint buffer);
void glBindBufferBase(GLenum target, GLuint index, GLuint buffer);
void glBindBufferRange(GLenum target, GLuint index, GLuint buffer,
                       GLintptr offset, GLsizeiptr size);
void glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data,
                  GLenum usage);
void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size,
                     const GLvoid *data);
void glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params);
void glGetBufferPointerv(GLenum target, GLenum pname, GLvoid **params);
void *glMapBuffer(GLenum target, GLenum access);
void *glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length,
                       GLbitfield access);
GLboolean glUnmapBuffer(GLenum target);

void glEnable(GLenum cap);
void glDisable(GLenum cap);
GLboolean glIsEnabled(GLenum cap);
void glBlendFunc(GLenum sfactor, GLenum dfactor);
void glBlendEquation(GLenum mode);
void glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha);
void glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB,
                         GLenum sfactorAlpha, GLenum dfactorAlpha);
void glBlendColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
void glDepthFunc(GLenum func);
void glDepthMask(GLboolean flag);
void glDepthRange(GLclampd nearVal, GLclampd farVal);
void glDepthRangef(GLclampf nearVal, GLclampf farVal);
void glStencilFunc(GLenum func, GLint ref, GLuint mask);
void glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask);
void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass);
void glStencilOpSeparate(GLenum face, GLenum fail, GLenum zfail, GLenum zpass);
void glStencilMask(GLuint mask);
void glStencilMaskSeparate(GLenum face, GLuint mask);
void glCullFace(GLenum mode);
void glFrontFace(GLenum mode);
void glPolygonMode(GLenum face, GLenum mode);
void glPolygonOffset(GLfloat factor, GLfloat units);
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
void glColorMask(GLboolean red, GLboolean green, GLboolean blue,
                 GLboolean alpha);
void glLineWidth(GLfloat width);
void glHint(GLenum target, GLenum mode);
void glSampleCoverage(GLclampf value, GLboolean invert);
void glPixelStorei(GLenum pname, GLint param);
void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
void glClearDepth(GLclampd depth);
void glClearDepthf(GLclampf depth);
void glClearStencil(GLint s);
void glClear(GLbitfield mask);

void glGenTextures(GLsizei n, GLuint *textures);
void glDeleteTextures(GLsizei n, const GLuint *textures);
GLboolean glIsTexture(GLuint texture);
void glGenSamplers(GLsizei n, GLuint *samplers);
void glDeleteSamplers(GLsizei n, const GLuint *samplers);
GLboolean glIsSampler(GLuint sampler);
void glBindTexture(GLenum target, GLuint texture);
void glBindSampler(GLuint unit, GLuint sampler);
void glActiveTexture(GLenum texture);
void glTexImage2D(GLenum target, GLint level, GLint internalformat,
                  GLsizei width, GLsizei height, GLint border, GLenum format,
                  GLenum type, const GLvoid *pixels);
void glTexImage3D(GLenum target, GLint level, GLint internalformat,
                  GLsizei width, GLsizei height, GLsizei depth, GLint border,
                  GLenum format, GLenum type, const GLvoid *pixels);
void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                     GLsizei width, GLsizei height, GLenum format, GLenum type,
                     const GLvoid *pixels);
void glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                     GLint zoffset, GLsizei width, GLsizei height,
                     GLsizei depth, GLenum format, GLenum type,
                     const GLvoid *pixels);
void glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat,
                      GLint x, GLint y, GLsizei width, GLsizei height,
                      GLint border);
void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                         GLint yoffset, GLint x, GLint y, GLsizei width,
                         GLsizei height);
void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat,
                            GLsizei width, GLsizei height, GLint border,
                            GLsizei imageSize, const GLvoid *data);
void glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                               GLint yoffset, GLsizei width, GLsizei height,
                               GLenum format, GLsizei imageSize,
                               const GLvoid *data);
void glTexParameteri(GLenum target, GLenum pname, GLint param);
void glTexParameterf(GLenum target, GLenum pname, GLfloat param);
void glTexParameteriv(GLenum target, GLenum pname, const GLint *params);
void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params);
void glGetTexParameteriv(GLenum target, GLenum pname, GLint *params);
void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params);
void glSamplerParameteriv(GLuint sampler, GLenum pname, const GLint *param);
void glSamplerParameterfv(GLuint sampler, GLenum pname, const GLfloat *param);
void glSamplerParameteri(GLuint sampler, GLenum pname, GLint param);
void glSamplerParameterf(GLuint sampler, GLenum pname, GLfloat param);
void glGetSamplerParameteriv(GLuint sampler, GLenum pname, GLint *params);
void glGetSamplerParameterfv(GLuint sampler, GLenum pname, GLfloat *params);
void glGenerateMipmap(GLenum target);

GLuint glCreateShader(GLenum type);
void glDeleteShader(GLuint shader);
GLboolean glIsShader(GLuint shader);
void glShaderSource(GLuint shader, GLsizei count, const GLchar *const *string,
                    const GLint *length);
void glGetShaderSource(GLuint shader, GLsizei bufSize, GLsizei *length,
                       GLchar *source);
void glCompileShader(GLuint shader);
GLuint glCreateProgram(void);
void glDeleteProgram(GLuint program);
GLboolean glIsProgram(GLuint program);
void glAttachShader(GLuint program, GLuint shader);
void glDetachShader(GLuint program, GLuint shader);
void glBindAttribLocation(GLuint program, GLuint index, const GLchar *name);
void glGetAttachedShaders(GLuint program, GLsizei maxCount, GLsizei *count,
                          GLuint *shaders);
void glGetActiveAttrib(GLuint program, GLuint index, GLsizei bufSize,
                       GLsizei *length, GLint *size, GLenum *type,
                       GLchar *name);
void glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize,
                        GLsizei *length, GLint *size, GLenum *type,
                        GLchar *name);
void glLinkProgram(GLuint program);
void glValidateProgram(GLuint program);
void glUseProgram(GLuint program);
void glGetShaderiv(GLuint shader, GLenum pname, GLint *params);
void glGetProgramiv(GLuint program, GLenum pname, GLint *params);
void glGetShaderInfoLog(GLuint shader, GLsizei maxLength, GLsizei *length,
                        GLchar *infoLog);
void glGetProgramInfoLog(GLuint program, GLsizei maxLength, GLsizei *length,
                         GLchar *infoLog);
void glGetUniformfv(GLuint program, GLint location, GLfloat *params);
void glGetUniformiv(GLuint program, GLint location, GLint *params);
void glUniform1f(GLint location, GLfloat v0);
void glUniform1fv(GLint location, GLsizei count, const GLfloat *value);
void glUniform1i(GLint location, GLint v0);
void glUniform1iv(GLint location, GLsizei count, const GLint *value);
void glUniform2f(GLint location, GLfloat v0, GLfloat v1);
void glUniform2fv(GLint location, GLsizei count, const GLfloat *value);
void glUniform2i(GLint location, GLint v0, GLint v1);
void glUniform2iv(GLint location, GLsizei count, const GLint *value);
void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
void glUniform3fv(GLint location, GLsizei count, const GLfloat *value);
void glUniform3i(GLint location, GLint v0, GLint v1, GLint v2);
void glUniform3iv(GLint location, GLsizei count, const GLint *value);
void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
void glUniform4fv(GLint location, GLsizei count, const GLfloat *value);
void glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3);
void glUniform4iv(GLint location, GLsizei count, const GLint *value);
void glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose,
                        const GLfloat *value);
void glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose,
                        const GLfloat *value);
void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose,
                        const GLfloat *value);
GLint glGetUniformLocation(GLuint program, const GLchar *name);
GLint glGetAttribLocation(GLuint program, const GLchar *name);
GLuint glGetUniformBlockIndex(GLuint program, const GLchar *uniformBlockName);
void glUniformBlockBinding(GLuint program, GLuint uniformBlockIndex,
                           GLuint uniformBlockBinding);

// Wii U helper hooks
void glWiiULoadShaderGroup(GLuint program, const void *shaderGroup);
void glWiiULoadShaderGroupGFD(GLuint program, GLuint index,
                              const void *gfdData);

void glGenVertexArrays(GLsizei n, GLuint *arrays);
void glDeleteVertexArrays(GLsizei n, const GLuint *arrays);
GLboolean glIsVertexArray(GLuint array);
void glBindVertexArray(GLuint array);
void glEnableVertexAttribArray(GLuint index);
void glDisableVertexAttribArray(GLuint index);
void glGetVertexAttribfv(GLuint index, GLenum pname, GLfloat *params);
void glGetVertexAttribiv(GLuint index, GLenum pname, GLint *params);
void glGetVertexAttribPointerv(GLuint index, GLenum pname, GLvoid **pointer);
void glVertexAttrib1f(GLuint index, GLfloat x);
void glVertexAttrib1fv(GLuint index, const GLfloat *v);
void glVertexAttrib2f(GLuint index, GLfloat x, GLfloat y);
void glVertexAttrib2fv(GLuint index, const GLfloat *v);
void glVertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z);
void glVertexAttrib3fv(GLuint index, const GLfloat *v);
void glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z,
                      GLfloat w);
void glVertexAttrib4fv(GLuint index, const GLfloat *v);
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid *pointer);
void glVertexAttribDivisor(GLuint index, GLuint divisor);

void glGenFramebuffers(GLsizei n, GLuint *ids);
void glDeleteFramebuffers(GLsizei n, const GLuint *ids);
GLboolean glIsFramebuffer(GLuint framebuffer);
void glGenRenderbuffers(GLsizei n, GLuint *ids);
void glDeleteRenderbuffers(GLsizei n, const GLuint *ids);
GLboolean glIsRenderbuffer(GLuint renderbuffer);
void glBindFramebuffer(GLenum target, GLuint framebuffer);
void glBindRenderbuffer(GLenum target, GLuint renderbuffer);
GLenum glCheckFramebufferStatus(GLenum target);
void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
void glFramebufferRenderbuffer(GLenum target, GLenum attachment,
                               GLenum renderbuffertarget,
                               GLuint renderbuffer);
void glRenderbufferStorage(GLenum target, GLenum internalformat,
                           GLsizei width, GLsizei height);
void glGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params);
void glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment,
                                           GLenum pname, GLint *params);
void glDrawBuffer(GLenum buf);
void glDrawBuffers(GLsizei n, const GLenum *bufs);
void glReadBuffer(GLenum src);
void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type, GLvoid *pixels);

void glDrawArrays(GLenum mode, GLint first, GLsizei count);
void glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count,
                           GLsizei instancecount);
void glDrawElements(GLenum mode, GLsizei count, GLenum type,
                    const GLvoid *indices);
void glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type,
                             const GLvoid *indices, GLsizei instancecount);

const GLubyte *glGetString(GLenum name);
const GLubyte *glGetStringi(GLenum name, GLuint index);
void glGetBooleanv(GLenum pname, GLboolean *data);
void glGetDoublev(GLenum pname, GLdouble *data);
void glGetIntegerv(GLenum pname, GLint *data);
void glGetFloatv(GLenum pname, GLfloat *data);
void glReleaseShaderCompiler(void);
void glShaderBinary(GLsizei count, const GLuint *shaders, GLenum binaryFormat,
                    const GLvoid *binary, GLsizei length);
void glGetShaderPrecisionFormat(GLenum shadertype, GLenum precisiontype,
                                GLint *range, GLint *precision);
GLenum glGetError(void);
void glFlush(void);
void glFinish(void);

#ifdef __cplusplus
}
#endif

#endif // GL header guard
