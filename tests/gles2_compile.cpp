#include <GLES2/gl2.h>

void gles2_compile_smoke(void) {
  GLfloat fvec1[1] = {0.0f};
  GLfloat fvec2[2] = {0.0f, 1.0f};
  GLfloat fvec3[3] = {0.0f, 1.0f, 2.0f};
  GLfloat fvec4[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  GLint range[2] = {0, 0};
  GLint precision = 0;
  GLint value = 0;
  GLint ivec4[4] = {0, 1, 2, 3};

  glClearDepthf(1.0f);
  glDepthRangef(0.0f, 1.0f);
  glHint(GL_GENERATE_MIPMAP_HINT, GL_NICEST);
  glSampleCoverage(0.5f, GL_TRUE);
  glReleaseShaderCompiler();
  glViewport(0, 0, 1, 1);
  glScissor(0, 0, 1, 1);
  glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT, range,
                             &precision);
  glGetIntegerv(GL_SHADER_COMPILER, &value);
  glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &value);
  glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT, &value);
  glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE, &value);
  glGetIntegerv(GL_NUM_SHADER_BINARY_FORMATS, &value);
  glGetIntegerv(GL_VIEWPORT, ivec4);
  glGetIntegerv(GL_SCISSOR_BOX, ivec4);
  glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 1, 1, 0);
  glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, 1, 1);
  glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, 0, 0);
  glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, 0, 0);
  glBindAttribLocation(0, 0, "aPosition");
  glGetUniformfv(0, 0, fvec4);
  glGetUniformiv(0, 0, &value);
  glGetVertexAttribfv(0, GL_CURRENT_VERTEX_ATTRIB, fvec4);
  glVertexAttrib1f(0, 0.5f);
  glVertexAttrib1fv(0, fvec1);
  glVertexAttrib2f(0, 0.5f, 1.0f);
  glVertexAttrib2fv(0, fvec2);
  glVertexAttrib3f(0, 0.5f, 1.0f, 1.5f);
  glVertexAttrib3fv(0, fvec3);
  glVertexAttrib4f(0, 0.5f, 1.0f, 1.5f, 2.0f);
  glVertexAttrib4fv(0, fvec4);
  glUniform1iv(0, 1, &value);
  glUniform2i(0, 0, 1);
  glUniform3iv(0, 1, ivec4);
  glUniform4iv(0, 1, ivec4);
}
