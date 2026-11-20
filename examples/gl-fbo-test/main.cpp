/* gx2gl framebuffer object orientation test. Four pairs of squares; in each
 * pair the left one is drawn straight to the screen and the right one is the
 * same thing drawn into a 64x64 texture through a framebuffer object, then
 * shown with texture coordinates (0,0) at the bottom left. Both squares of a
 * pair must look the same.
 *
 *   F1 orientation: the top half red, the bottom half green.
 *   F2 face culling (GL_BACK, counter-clockwise front): a yellow triangle
 *      pointing up; a blue clockwise one drawn over it must be culled.
 *   F3 gl_FragCoord: blue where gl_FragCoord.y is in the lower half, white
 *      above: blue at the bottom.
 *   F4 scissor: grey, with a magenta square in the bottom left quarter
 *      (glClear inside a scissor box at y = 0).
 *
 * glReadPixels of the framebuffer object and of the screen are logged: both
 * must read green at the bottom and red at the top of F1.
 *
 * Log: sd:/gx2gl-fbo-test.log
 */
#include "../common/example_util.h"

#include <gx2gl/present.h>
#include <whb/log_cafe.h>
#include <whb/log_udp.h>
#include <whb/proc.h>

#ifndef GX2GL_BUILD_ID
#define GX2GL_BUILD_ID "local"
#endif

using example::Log;

static const char kVS[] = R"(
#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;
out vec2 passTexCoord;
void main() { passTexCoord = aTexCoord; gl_Position = vec4(aPosition, 0.0, 1.0); }
)";
static const char kColourFS[] = R"(
#version 330 core
uniform vec4 uColour;
out vec4 fragColor;
void main() { fragColor = uColour; }
)";
static const char kFragCoordFS[] = R"(
#version 330 core
uniform vec2 uOriginHalf; /* the square's bottom row, half its height */
out vec4 fragColor;
void main() {
   fragColor = gl_FragCoord.y - uOriginHalf.x < uOriginHalf.y
                  ? vec4(0.0, 0.3, 1.0, 1.0) : vec4(1.0);
}
)";
static const char kTextureFS[] = R"(
#version 330 core
in vec2 passTexCoord;
uniform sampler2D tex;
out vec4 fragColor;
void main() { fragColor = texture(tex, passTexCoord); }
)";

static const int kFboSize = 64;

int main(int, char **)
{
   WHBProcInit();
   WHBLogCafeInit();
   WHBLogUdpInit();
   example::OpenLog("gx2gl-fbo-test.log");
   Log("gx2gl FBO test, build %s", GX2GL_BUILD_ID);

   GX2GL_Context context = GX2GL_CreateContext();
   if (!context) {
      Log("GX2GL_CreateContext FAILED");
      WHBProcShutdown();
      return 0;
   }

   using example::CompileShader;
   using example::LinkProgram;
   const GLuint vs = CompileShader("vs", GL_VERTEX_SHADER, kVS);
   const GLuint colour = LinkProgram("colour", vs, 0, CompileShader("colour-fs", GL_FRAGMENT_SHADER, kColourFS), false);
   const GLuint frag_coord = LinkProgram("fragcoord", vs, 0, CompileShader("fragcoord-fs", GL_FRAGMENT_SHADER, kFragCoordFS), false);
   const GLuint textured = LinkProgram("texture", vs, 0, CompileShader("texture-fs", GL_FRAGMENT_SHADER, kTextureFS), false);
   glUseProgram(textured);
   glUniform1i(glGetUniformLocation(textured, "tex"), 0);

   /* Geometry, in NDC of whatever viewport is set. */
   const float vertices[] = {
      /* 0: top half */
      -1, 1, 0, 0,  1, 1, 0, 0,  1, 0, 0, 0,  -1, 1, 0, 0,  1, 0, 0, 0,  -1, 0, 0, 0,
      /* 6: bottom half */
      -1, 0, 0, 0,  1, 0, 0, 0,  1, -1, 0, 0,  -1, 0, 0, 0,  1, -1, 0, 0,  -1, -1, 0, 0,
      /* 12: counter-clockwise triangle pointing up */
      -0.8f, -0.8f, 0, 0,  0.8f, -0.8f, 0, 0,  0, 0.8f, 0, 0,
      /* 15: clockwise triangle pointing down */
      -0.8f, 0.8f, 0, 0,  0.8f, 0.8f, 0, 0,  0, -0.8f, 0, 0,
      /* 18: the whole viewport, texture coordinates (0,0) at the bottom left */
      -1, -1, 0, 0,  1, -1, 1, 0,  1, 1, 1, 1,  -1, -1, 0, 0,  1, 1, 1, 1,  -1, 1, 0, 1,
   };
   GLuint vao, vbo;
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (const void *)0);
   glEnableVertexAttribArray(0);
   glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (const void *)(2 * sizeof(float)));
   glEnableVertexAttribArray(1);

   /* One texture and framebuffer object per test. */
   GLuint textures[4], fbos[4];
   glGenTextures(4, textures);
   glGenFramebuffers(4, fbos);
   for (int i = 0; i < 4; ++i) {
      glBindTexture(GL_TEXTURE_2D, textures[i]);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kFboSize, kFboSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glBindFramebuffer(GL_FRAMEBUFFER, fbos[i]);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[i], 0);
      Log("FBO %d status 0x%04x", i + 1, glCheckFramebufferStatus(GL_FRAMEBUFFER));
   }
   glBindFramebuffer(GL_FRAMEBUFFER, 0);

   GLint screen[4];
   glGetIntegerv(GL_VIEWPORT, screen);
   Log("screen %dx%d", screen[2], screen[3]);
   const int square = screen[2] / 11;
   const int gap = square / 8;
   const int pair_width = 2 * square + gap;
   const int left = (screen[2] - 4 * pair_width - 3 * 2 * gap) / 2;
   const int bottom = (screen[3] - square) / 2;
   example::LogGLError("setup");

   /* Draws test i into the bound framebuffer, in the square (x, y, size). */
   auto draw_test = [&](int i, int x, int y, int size) {
      glViewport(x, y, size, size);
      glDisable(GL_CULL_FACE);
      glDisable(GL_SCISSOR_TEST);
      switch (i) {
      case 0:
         glUseProgram(colour);
         glUniform4f(glGetUniformLocation(colour, "uColour"), 1, 0, 0, 1);
         glDrawArrays(GL_TRIANGLES, 0, 6);
         glUniform4f(glGetUniformLocation(colour, "uColour"), 0, 1, 0, 1);
         glDrawArrays(GL_TRIANGLES, 6, 6);
         break;
      case 1:
         glUseProgram(colour);
         glUniform4f(glGetUniformLocation(colour, "uColour"), 0.2f, 0.2f, 0.2f, 1);
         glDrawArrays(GL_TRIANGLES, 0, 12);
         glEnable(GL_CULL_FACE);
         glCullFace(GL_BACK);
         glFrontFace(GL_CCW);
         glUniform4f(glGetUniformLocation(colour, "uColour"), 1, 1, 0, 1);
         glDrawArrays(GL_TRIANGLES, 12, 3);
         glUniform4f(glGetUniformLocation(colour, "uColour"), 0, 0, 1, 1);
         glDrawArrays(GL_TRIANGLES, 15, 3);
         glDisable(GL_CULL_FACE);
         break;
      case 2:
         glUseProgram(frag_coord);
         glUniform2f(glGetUniformLocation(frag_coord, "uOriginHalf"), (float)y, (float)size / 2.0f);
         glDrawArrays(GL_TRIANGLES, 0, 12);
         break;
      case 3:
         glEnable(GL_SCISSOR_TEST);
         glScissor(x, y, size, size);
         glClearColor(0.5f, 0.5f, 0.5f, 1);
         glClear(GL_COLOR_BUFFER_BIT);
         glScissor(x, y, size / 2, size / 2);
         glClearColor(1, 0, 1, 1);
         glClear(GL_COLOR_BUFFER_BIT);
         glDisable(GL_SCISSOR_TEST);
         break;
      }
   };

   uint32_t frame = 0;
   while (WHBProcIsRunning()) {
      example::PollGamePad();

      for (int i = 0; i < 4; ++i) {
         glBindFramebuffer(GL_FRAMEBUFFER, fbos[i]);
         draw_test(i, 0, 0, kFboSize);
      }
      if (frame == 0) {
         unsigned char low[4] = {}, high[4] = {};
         glBindFramebuffer(GL_FRAMEBUFFER, fbos[0]);
         glReadPixels(kFboSize / 2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, low);
         glReadPixels(kFboSize / 2, kFboSize - 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, high);
         Log("F1 FBO read: bottom %u,%u,%u top %u,%u,%u (expected 0,255,0 and 255,0,0)",
             low[0], low[1], low[2], high[0], high[1], high[2]);
      }

      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glViewport(0, 0, screen[2], screen[3]);
      glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      for (int i = 0; i < 4; ++i) {
         const int x = left + i * (pair_width + 2 * gap);
         draw_test(i, x, bottom, square);
         glViewport(x + square + gap, bottom, square, square);
         glUseProgram(textured);
         glBindTexture(GL_TEXTURE_2D, textures[i]);
         glDrawArrays(GL_TRIANGLES, 18, 6);
      }

      if (frame == 0) {
         unsigned char low[4] = {}, high[4] = {};
         glReadPixels(left + square / 2, bottom + 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, low);
         glReadPixels(left + square / 2, bottom + square - 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, high);
         Log("F1 screen read: bottom %u,%u,%u top %u,%u,%u (expected 0,255,0 and 255,0,0)",
             low[0], low[1], low[2], high[0], high[1], high[2]);
         glFinish();
         example::LogGLError("frame 0");
         Log("frame 0 drawn");
      }
      GX2GL_Present();
      ++frame;
   }
   Log("exiting after %u frames", frame);
   GX2GL_DeleteContext(context);
   WHBUnmountSdCard();
   WHBProcShutdown();
   return 0;
}
