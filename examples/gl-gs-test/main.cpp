/* gx2gl geometry shader test, written against the OpenGL API only.
 *
 * Eight panels on the TV (mirrored to the GamePad), each adding one feature:
 *
 *   top row:     A   no geometry shader (control)
 *                B1  pass-through GS, no uniforms anywhere
 *                B2  pass-through GS scaled by a uniform only the GS declares
 *   middle row:  B3  pass-through GS, vertex shader reads a uniform
 *                C1  points expanded to squares, no uniforms
 *                C2  squares sized by a uniform buffer, tinted by a uniform the
 *                    vertex shader also declares
 *   bottom row:  C3  as C2 without the buffer (tint shared by VS and GS only)
 *                C4  as C2 without the tint (buffer only, plain vertex shader)
 *
 * The geometry panels start one second apart from frame 120. Press A on the
 * GamePad to toggle "serialized" mode, which waits for the GPU after every draw;
 * a white square in the bottom-right corner shows it is on. Every step goes to
 * sd:/gx2gl-gs-test.log, reopened per line so it survives a GPU hang.
 */
#include <gl/gl.h>
#include <gx2gl/present.h>

#include <vpad/input.h>
#include <whb/log.h>
#include <whb/log_cafe.h>
#include <whb/log_udp.h>
#include <whb/proc.h>
#include <whb/sdcard.h>

#include <cstdarg>
#include <cstdio>
#include <initializer_list>
#include <string>

#ifndef GL_UNIFORM_BLOCK_REFERENCED_BY_GEOMETRY_SHADER
#define GL_UNIFORM_BLOCK_REFERENCED_BY_GEOMETRY_SHADER 0x8A45
#endif

static std::string g_log_path;

static void Log(const char *format, ...)
{
   char line[2048];
   va_list args;
   va_start(args, format);
   vsnprintf(line, sizeof(line), format, args);
   va_end(args);

   WHBLogPrintf("%s", line);
   if (g_log_path.empty())
      return;
   if (FILE *file = fopen(g_log_path.c_str(), "a")) {
      fprintf(file, "%s\n", line);
      fclose(file);
   }
}

static void LogGLError(const char *where)
{
   for (GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError())
      Log("  GL error 0x%04x after %s", error, where);
}

static const char kPlainVertexSource[] = R"(
#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec4 aColor;
out vec4 vColor;
void main() {
   vColor = aColor;
   gl_Position = vec4(aPosition, 0.0, 1.0);
}
)";

static const char kTintVertexSource[] = R"(
#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec4 aColor;
out vec4 vColor;
uniform vec4 uTint;
void main() {
   vColor = aColor;
   // uTint.w is 1.0: the position only survives if the uniform arrives.
   gl_Position = vec4(aPosition * uTint.w, 0.0, 1.0);
}
)";

static const char kFragmentSource[] = R"(
#version 330 core
in vec4 vColor;
out vec4 fragColor;
void main() { fragColor = vColor; }
)";

static const char kGeometryFragmentSource[] = R"(
#version 330 core
in vec4 gColor;
out vec4 fragColor;
void main() { fragColor = gColor; }
)";

static const char kPassThroughGeometrySource[] = R"(
#version 330 core
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;
in vec4 vColor[];
out vec4 gColor;
void main() {
   for (int i = 0; i < 3; ++i) {
      gColor = vColor[i];
      gl_Position = gl_in[i].gl_Position;
      EmitVertex();
   }
   EndPrimitive();
}
)";

static const char kScaledGeometrySource[] = R"(
#version 330 core
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;
in vec4 vColor[];
out vec4 gColor;
uniform float uGeometryScale;
void main() {
   vec4 center = (gl_in[0].gl_Position + gl_in[1].gl_Position + gl_in[2].gl_Position) / 3.0;
   for (int i = 0; i < 3; ++i) {
      gColor = vColor[i];
      gl_Position = center + (gl_in[i].gl_Position - center) * uGeometryScale;
      EmitVertex();
   }
   EndPrimitive();
}
)";

static const char kPlainExpandGeometrySource[] = R"(
#version 330 core
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;
in vec4 vColor[];
out vec4 gColor;
void main() {
   const vec2 halfSize = vec2(0.05, 0.08);
   for (int i = 0; i < 4; ++i) {
      vec2 corner = vec2(float(i & 1), float(i >> 1)) * 2.0 - 1.0;
      gColor = vColor[0];
      gl_Position = gl_in[0].gl_Position + vec4(corner * halfSize, 0.0, 0.0);
      EmitVertex();
   }
   EndPrimitive();
}
)";

static const char kFullExpandGeometrySource[] = R"(
#version 330 core
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;
in vec4 vColor[];
out vec4 gColor;
uniform vec4 uTint;
layout(std140) uniform SpriteData {
   vec4 halfSize;
};
void main() {
   for (int i = 0; i < 4; ++i) {
      vec2 corner = vec2(float(i & 1), float(i >> 1)) * 2.0 - 1.0;
      gColor = vColor[0] * vec4(uTint.rgb, 1.0);
      gl_Position = gl_in[0].gl_Position + vec4(corner * halfSize.xy, 0.0, 0.0);
      EmitVertex();
   }
   EndPrimitive();
}
)";

static const char kTintExpandGeometrySource[] = R"(
#version 330 core
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;
in vec4 vColor[];
out vec4 gColor;
uniform vec4 uTint;
void main() {
   const vec2 halfSize = vec2(0.05, 0.08);
   for (int i = 0; i < 4; ++i) {
      vec2 corner = vec2(float(i & 1), float(i >> 1)) * 2.0 - 1.0;
      gColor = vColor[0] * vec4(uTint.rgb, 1.0);
      gl_Position = gl_in[0].gl_Position + vec4(corner * halfSize, 0.0, 0.0);
      EmitVertex();
   }
   EndPrimitive();
}
)";

static const char kBufferExpandGeometrySource[] = R"(
#version 330 core
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;
in vec4 vColor[];
out vec4 gColor;
layout(std140) uniform SpriteData {
   vec4 halfSize;
};
void main() {
   for (int i = 0; i < 4; ++i) {
      vec2 corner = vec2(float(i & 1), float(i >> 1)) * 2.0 - 1.0;
      gColor = vColor[0];
      gl_Position = gl_in[0].gl_Position + vec4(corner * halfSize.xy, 0.0, 0.0);
      EmitVertex();
   }
   EndPrimitive();
}
)";

static GLuint CompileShader(const char *name, GLenum type, const char *source)
{
   GLuint shader = glCreateShader(type);
   glShaderSource(shader, 1, &source, nullptr);
   glCompileShader(shader);
   GLint ok = GL_FALSE;
   glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
   char log[2048] = {};
   glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
   Log("%s: compile %s%s%s", name, ok ? "ok" : "FAILED", log[0] ? ": " : "", log);
   LogGLError(name);
   return ok ? shader : 0;
}

static GLuint LinkProgram(const char *name, GLuint vs, GLuint gs, GLuint fs, bool needs_gs)
{
   if (!vs || !fs || (needs_gs && !gs))
      return 0;
   GLuint program = glCreateProgram();
   glAttachShader(program, vs);
   if (gs)
      glAttachShader(program, gs);
   glAttachShader(program, fs);
   glLinkProgram(program);
   GLint ok = GL_FALSE;
   glGetProgramiv(program, GL_LINK_STATUS, &ok);
   char log[2048] = {};
   glGetProgramInfoLog(program, sizeof(log), nullptr, log);
   Log("%s: link %s%s%s", name, ok ? "ok" : "FAILED", log[0] ? ": " : "", log);
   LogGLError(name);
   return ok ? program : 0;
}

struct Mesh {
   GLuint vao = 0;
   GLuint vbo = 0;
   GLsizei count = 0;
};

static Mesh CreateMesh(const float *vertices, GLsizei count)
{
   Mesh mesh;
   mesh.count = count;
   glGenVertexArrays(1, &mesh.vao);
   glBindVertexArray(mesh.vao);
   glGenBuffers(1, &mesh.vbo);
   glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
   glBufferData(GL_ARRAY_BUFFER, count * 6 * sizeof(float), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (const void *)0);
   glEnableVertexAttribArray(0);
   glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                         (const void *)(2 * sizeof(float)));
   glEnableVertexAttribArray(1);
   glBindVertexArray(0);
   return mesh;
}

/* Panel centres: three columns, three rows. */
static void PanelCenter(int panel, float &x, float &y)
{
   x = -0.66f + 0.66f * (float)(panel % 3);
   y = 0.66f - 0.66f * (float)(panel / 3);
}

static Mesh CreateTriangle(int panel)
{
   float x, y;
   PanelCenter(panel, x, y);
   const float vertices[] = {
      x - 0.22f, y - 0.26f, 1, 0, 0, 1,
      x + 0.22f, y - 0.26f, 0, 1, 0, 1,
      x, y + 0.26f, 0, 0, 1, 1,
   };
   return CreateMesh(vertices, 3);
}

static Mesh CreatePoints(int panel)
{
   float x, y;
   PanelCenter(panel, x, y);
   const float vertices[] = {
      x - 0.12f, y - 0.15f, 1, 1, 0, 1,
      x + 0.12f, y - 0.15f, 0, 1, 1, 1,
      x - 0.12f, y + 0.15f, 1, 0, 1, 1,
      x + 0.12f, y + 0.15f, 1, 1, 1, 1,
   };
   return CreateMesh(vertices, 4);
}

struct Panel {
   const char *name;
   GLuint program;
   Mesh mesh;
   GLenum primitive;
   uint32_t first_frame;
};

int main(int argc, char **argv)
{
   WHBProcInit();
   WHBLogCafeInit();
   WHBLogUdpInit();
   if (WHBMountSdCard()) {
      g_log_path = std::string(WHBGetSdCardMountPath()) + "/gx2gl-gs-test.log";
      if (FILE *file = fopen(g_log_path.c_str(), "w"))
         fclose(file);
   }
   Log("gx2gl geometry shader test started");

   GX2GL_Context context = GX2GL_CreateContext();
   if (!context) {
      Log("GX2GL_CreateContext FAILED");
      WHBProcShutdown();
      return 0;
   }
   Log("GL_VERSION %s", (const char *)glGetString(GL_VERSION));

   GLuint plain_vs = CompileShader("plain-vs", GL_VERTEX_SHADER, kPlainVertexSource);
   GLuint tint_vs = CompileShader("tint-vs", GL_VERTEX_SHADER, kTintVertexSource);
   GLuint fs = CompileShader("fs", GL_FRAGMENT_SHADER, kFragmentSource);
   GLuint gfs = CompileShader("gs-fs", GL_FRAGMENT_SHADER, kGeometryFragmentSource);
   GLuint pass_gs = CompileShader("pass-gs", GL_GEOMETRY_SHADER, kPassThroughGeometrySource);
   GLuint scaled_gs = CompileShader("scaled-gs", GL_GEOMETRY_SHADER, kScaledGeometrySource);
   GLuint plain_expand_gs =
      CompileShader("plain-expand-gs", GL_GEOMETRY_SHADER, kPlainExpandGeometrySource);
   GLuint full_expand_gs =
      CompileShader("full-expand-gs", GL_GEOMETRY_SHADER, kFullExpandGeometrySource);
   GLuint tint_expand_gs =
      CompileShader("tint-expand-gs", GL_GEOMETRY_SHADER, kTintExpandGeometrySource);
   GLuint buffer_expand_gs =
      CompileShader("buffer-expand-gs", GL_GEOMETRY_SHADER, kBufferExpandGeometrySource);

   Panel panels[] = {
      {"A", LinkProgram("A", plain_vs, 0, fs, false), CreateTriangle(0), GL_TRIANGLES, 0},
      {"B1", LinkProgram("B1", plain_vs, pass_gs, gfs, true), CreateTriangle(1), GL_TRIANGLES, 120},
      {"B2", LinkProgram("B2", plain_vs, scaled_gs, gfs, true), CreateTriangle(2), GL_TRIANGLES, 180},
      {"B3", LinkProgram("B3", tint_vs, pass_gs, gfs, true), CreateTriangle(3), GL_TRIANGLES, 240},
      {"C1", LinkProgram("C1", plain_vs, plain_expand_gs, gfs, true), CreatePoints(4), GL_POINTS, 300},
      {"C2", LinkProgram("C2", tint_vs, full_expand_gs, gfs, true), CreatePoints(5), GL_POINTS, 360},
      {"C3", LinkProgram("C3", tint_vs, tint_expand_gs, gfs, true), CreatePoints(6), GL_POINTS, 420},
      {"C4", LinkProgram("C4", plain_vs, buffer_expand_gs, gfs, true), CreatePoints(7), GL_POINTS, 480},
   };
   /* The serialized-mode indicator: a small square in the last cell. */
   const float indicator_vertices[] = {
      0.80f, -0.80f, 1, 1, 1, 1,  0.92f, -0.80f, 1, 1, 1, 1,  0.80f, -0.92f, 1, 1, 1, 1,
      0.92f, -0.80f, 1, 1, 1, 1,  0.92f, -0.92f, 1, 1, 1, 1,  0.80f, -0.92f, 1, 1, 1, 1,
   };
   const Mesh indicator = CreateMesh(indicator_vertices, 6);
   LogGLError("mesh setup");

   for (Panel &panel : panels) {
      if (!panel.program)
         continue;
      glUseProgram(panel.program);
      const GLint tint = glGetUniformLocation(panel.program, "uTint");
      const GLint scale = glGetUniformLocation(panel.program, "uGeometryScale");
      if (tint >= 0)
         glUniform4f(tint, 1.0f, 1.0f, 1.0f, 1.0f);
      if (scale >= 0)
         glUniform1f(scale, 1.0f);
      Log("%s: uTint location %d, uGeometryScale location %d", panel.name, tint, scale);
   }

   GLuint sprite_ubo = 0;
   const float half_size[4] = {0.05f, 0.08f, 0.0f, 0.0f};
   glGenBuffers(1, &sprite_ubo);
   glBindBuffer(GL_UNIFORM_BUFFER, sprite_ubo);
   glBufferData(GL_UNIFORM_BUFFER, sizeof(half_size), half_size, GL_STATIC_DRAW);
   glBindBufferBase(GL_UNIFORM_BUFFER, 2, sprite_ubo);
   for (const Panel &panel : panels) {
      if (!panel.program)
         continue;
      GLuint block = glGetUniformBlockIndex(panel.program, "SpriteData");
      if (block == GL_INVALID_INDEX)
         continue;
      GLint referenced = -1;
      glGetActiveUniformBlockiv(panel.program, block,
                                GL_UNIFORM_BLOCK_REFERENCED_BY_GEOMETRY_SHADER, &referenced);
      Log("%s: SpriteData block index %u, referenced by GS %d", panel.name, block, referenced);
      glUniformBlockBinding(panel.program, block, 2);
   }
   LogGLError("uniform setup");
   Log("setup done: geometry panels start every 60 frames from frame 120");

   bool serialized = false;
   uint32_t frame = 0;
   while (WHBProcIsRunning()) {
      /* Aroma's button combos (screenshots and the like) only see input the
       * app reads, so poll the GamePad even though the test ignores it.
       */
      VPADStatus vpad;
      VPADReadError vpad_error;
      if (VPADRead(VPAD_CHAN_0, &vpad, 1, &vpad_error) > 0 &&
          vpad_error == VPAD_READ_SUCCESS && (vpad.trigger & VPAD_BUTTON_A)) {
         serialized = !serialized;
         Log("frame %u: serialized mode %s", frame, serialized ? "on" : "off");
      }

      bool first_draw = false;
      glClearColor(0.12f, 0.12f, 0.16f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      for (const Panel &panel : panels) {
         if (!panel.program || frame < panel.first_frame)
            continue;
         if (frame == panel.first_frame) {
            Log("frame %u: first %s draw", frame, panel.name);
            first_draw = true;
         }
         glUseProgram(panel.program);
         glBindVertexArray(panel.mesh.vao);
         glDrawArrays(panel.primitive, 0, panel.mesh.count);
         if (serialized)
            glFinish();
      }
      if (serialized && panels[0].program) {
         glUseProgram(panels[0].program);
         glBindVertexArray(indicator.vao);
         glDrawArrays(GL_TRIANGLES, 0, indicator.count);
      }
      glBindVertexArray(0);
      if (first_draw) {
         glFinish();
         LogGLError("draw");
         Log("frame %u: finished on the GPU", frame);
      }
      GX2GL_Present();
      if (frame == 600)
         Log("frame 600: still running");
      ++frame;
   }

   Log("exiting after %u frames", frame);
   GX2GL_DeleteContext(context);
   Log("context deleted");
   WHBUnmountSdCard();
   WHBProcShutdown();
   return 0;
}
