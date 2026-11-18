/* gx2gl feature test: what the geometry shader test did not cover.
 *
 * Nine panels, left to right and top to bottom. Each one names what it expects:
 *
 *   A   control triangle, no uniforms or geometry shader
 *   D1  vertex shader with a uniform buffer AND a loose uniform: the triangle
 *       only lands in its cell if both are read from the right slots
 *   D2  fragment shader with two uniform buffers and a loose uniform: PURPLE
 *       (red mixed with blue); red, blue or black means slots got swapped
 *   E0  texture read in the fragment shader (reference): squares red (bottom
 *       left), green (bottom right), blue (top left), white (top right)
 *   E1  the same texture read in the GEOMETRY shader: must look like E0
 *   E2  lines expanded to thick quads by a geometry shader (two bars)
 *   E3  triangles_adjacency: a triangle coloured yellow / cyan / magenta from its
 *       adjacency vertices; black means the adjacency vertices were lost
 *   F1  interface blocks between VS, GS and FS (out VS_OUT { ... })
 *   F2  mat4 uniforms: one shared by VS and GS, one only the GS declares; the
 *       triangle must point DOWN
 *
 * Log: sd:/gx2gl-features-test.log
 */
#include "../common/example_util.h"

#include <gx2gl/present.h>
#include <whb/log_cafe.h>
#include <whb/log_udp.h>
#include <whb/proc.h>

#include <initializer_list>
#include <vector>

using example::Log;

#ifndef GL_UNIFORM_BLOCK_REFERENCED_BY_GEOMETRY_SHADER
#define GL_UNIFORM_BLOCK_REFERENCED_BY_GEOMETRY_SHADER 0x8A45
#endif

/* ---- shaders ------------------------------------------------------------ */

static const char kPlainVS[] = R"(
#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec4 aColor;
out vec4 vColor;
void main() {
   vColor = aColor;
   gl_Position = vec4(aPosition, 0.0, 1.0);
}
)";

static const char kPlainFS[] = R"(
#version 330 core
in vec4 vColor;
out vec4 fragColor;
void main() { fragColor = vColor; }
)";

static const char kGeometryFS[] = R"(
#version 330 core
in vec4 gColor;
out vec4 fragColor;
void main() { fragColor = gColor; }
)";

/* D1: a uniform buffer and a loose uniform in one vertex shader. */
static const char kBufferVS[] = R"(
#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec4 aColor;
out vec4 vColor;
uniform vec2 uOffset;
layout(std140) uniform Transform {
   vec4 scale;
};
void main() {
   vColor = aColor;
   gl_Position = vec4(aPosition * scale.xy + uOffset, 0.0, 1.0);
}
)";

/* D2: two uniform buffers and a loose uniform in one fragment shader. */
static const char kTwoBufferFS[] = R"(
#version 330 core
in vec4 vColor;
out vec4 fragColor;
uniform float uMix;
layout(std140) uniform Alpha {
   vec4 colorA;
};
layout(std140) uniform Beta {
   vec4 colorB;
};
void main() { fragColor = vec4(mix(colorA.rgb, colorB.rgb, uMix), vColor.a); }
)";

/* E0/E1: points expanded to squares; aColor.rg carries a texture coordinate. */
static const char kTexturePassGS[] = R"(
#version 330 core
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;
in vec4 vColor[];
out vec2 gUv;
void main() {
   const vec2 halfSize = vec2(0.05, 0.08);
   for (int i = 0; i < 4; ++i) {
      vec2 corner = vec2(float(i & 1), float(i >> 1)) * 2.0 - 1.0;
      gUv = vColor[0].rg;
      gl_Position = gl_in[0].gl_Position + vec4(corner * halfSize, 0.0, 0.0);
      EmitVertex();
   }
   EndPrimitive();
}
)";

static const char kTextureFS[] = R"(
#version 330 core
in vec2 gUv;
out vec4 fragColor;
uniform sampler2D uTexture;
void main() { fragColor = texture(uTexture, gUv); }
)";

static const char kTextureSampleGS[] = R"(
#version 330 core
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;
in vec4 vColor[];
out vec4 gColor;
uniform sampler2D uGeometryTexture;
void main() {
   const vec2 halfSize = vec2(0.05, 0.08);
   vec4 color = textureLod(uGeometryTexture, vColor[0].rg, 0.0);
   for (int i = 0; i < 4; ++i) {
      vec2 corner = vec2(float(i & 1), float(i >> 1)) * 2.0 - 1.0;
      gColor = color;
      gl_Position = gl_in[0].gl_Position + vec4(corner * halfSize, 0.0, 0.0);
      EmitVertex();
   }
   EndPrimitive();
}
)";

/* E2: each line becomes a quad. */
static const char kLinesGS[] = R"(
#version 330 core
layout(lines) in;
layout(triangle_strip, max_vertices = 4) out;
in vec4 vColor[];
out vec4 gColor;
void main() {
   vec2 direction = normalize(gl_in[1].gl_Position.xy - gl_in[0].gl_Position.xy);
   vec4 side = vec4(-direction.y, direction.x, 0.0, 0.0) * 0.025;
   for (int i = 0; i < 2; ++i) {
      gColor = vColor[i];
      gl_Position = gl_in[i].gl_Position - side;
      EmitVertex();
      gl_Position = gl_in[i].gl_Position + side;
      EmitVertex();
   }
   EndPrimitive();
}
)";

/* E3: the triangle takes its colours from the adjacency vertices. */
static const char kAdjacencyGS[] = R"(
#version 330 core
layout(triangles_adjacency) in;
layout(triangle_strip, max_vertices = 3) out;
in vec4 vColor[];
out vec4 gColor;
void main() {
   for (int i = 0; i < 3; ++i) {
      gColor = vColor[i * 2 + 1];
      gl_Position = gl_in[i * 2].gl_Position;
      EmitVertex();
   }
   EndPrimitive();
}
)";

/* F1: interface blocks all the way through. */
static const char kBlockVS[] = R"(
#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec4 aColor;
out VS_OUT {
   vec4 color;
   float scale;
} vs_out;
void main() {
   vs_out.color = aColor;
   vs_out.scale = 1.0;
   gl_Position = vec4(aPosition, 0.0, 1.0);
}
)";

static const char kBlockGS[] = R"(
#version 330 core
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;
in VS_OUT {
   vec4 color;
   float scale;
} gs_in[];
out GS_OUT {
   vec4 color;
} gs_out;
void main() {
   for (int i = 0; i < 3; ++i) {
      gs_out.color = gs_in[i].color * gs_in[i].scale;
      gl_Position = gl_in[i].gl_Position;
      EmitVertex();
   }
   EndPrimitive();
}
)";

static const char kBlockFS[] = R"(
#version 330 core
in GS_OUT {
   vec4 color;
} fs_in;
out vec4 fragColor;
void main() { fragColor = fs_in.color; }
)";

/* F2: a mat4 shared by VS and GS, and one only the GS declares. */
static const char kMatrixVS[] = R"(
#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec4 aColor;
out vec4 vColor;
uniform mat4 uFlip;
void main() {
   vColor = aColor * uFlip[3][3];   // uFlip[3][3] is 1.0
   gl_Position = vec4(aPosition, 0.0, 1.0);
}
)";

static const char kMatrixGS[] = R"(
#version 330 core
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;
in vec4 vColor[];
out vec4 gColor;
uniform mat4 uFlip;
uniform mat4 uPlace;
void main() {
   for (int i = 0; i < 3; ++i) {
      gColor = vColor[i];
      gl_Position = uPlace * uFlip * gl_in[i].gl_Position;
      EmitVertex();
   }
   EndPrimitive();
}
)";

/* ---- geometry ----------------------------------------------------------- */

struct Mesh {
   GLuint vao = 0;
   GLuint vbo = 0;
   GLsizei count = 0;
};

/* x, y, r, g, b, a per vertex */
static Mesh CreateMesh(const std::vector<float> &vertices)
{
   Mesh mesh;
   mesh.count = (GLsizei)(vertices.size() / 6);
   glGenVertexArrays(1, &mesh.vao);
   glBindVertexArray(mesh.vao);
   glGenBuffers(1, &mesh.vbo);
   glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
   glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(),
                GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (const void *)0);
   glEnableVertexAttribArray(0);
   glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                         (const void *)(2 * sizeof(float)));
   glEnableVertexAttribArray(1);
   glBindVertexArray(0);
   return mesh;
}

static void CellCenter(int cell, float &x, float &y)
{
   x = -0.66f + 0.66f * (float)(cell % 3);
   y = 0.66f - 0.66f * (float)(cell / 3);
}

static std::vector<float> Triangle(float x, float y)
{
   return {x - 0.22f, y - 0.26f, 1, 0, 0, 1,
           x + 0.22f, y - 0.26f, 0, 1, 0, 1,
           x, y + 0.26f, 0, 0, 1, 1};
}

/* Four points whose colour .rg is the centre of one texel of the 2x2 texture. */
static std::vector<float> TexturePoints(float x, float y)
{
   return {x - 0.12f, y - 0.15f, 0.25f, 0.25f, 0, 1,
           x + 0.12f, y - 0.15f, 0.75f, 0.25f, 0, 1,
           x - 0.12f, y + 0.15f, 0.25f, 0.75f, 0, 1,
           x + 0.12f, y + 0.15f, 0.75f, 0.75f, 0, 1};
}

struct Panel {
   const char *name;
   GLuint program;
   Mesh mesh;
   GLenum primitive;
};

int main(int argc, char **argv)
{
   WHBProcInit();
   WHBLogCafeInit();
   WHBLogUdpInit();
   example::OpenLog("gx2gl-features-test.log");
   Log("gx2gl feature test started");

   GX2GL_Context context = GX2GL_CreateContext();
   if (!context) {
      Log("GX2GL_CreateContext FAILED");
      WHBProcShutdown();
      return 0;
   }
   Log("GL_VERSION %s", (const char *)glGetString(GL_VERSION));

   using example::CompileShader;
   using example::LinkProgram;
   const GLuint plain_vs = CompileShader("plain-vs", GL_VERTEX_SHADER, kPlainVS);
   const GLuint plain_fs = CompileShader("plain-fs", GL_FRAGMENT_SHADER, kPlainFS);
   const GLuint geometry_fs = CompileShader("geometry-fs", GL_FRAGMENT_SHADER, kGeometryFS);
   const GLuint buffer_vs = CompileShader("D1-vs", GL_VERTEX_SHADER, kBufferVS);
   const GLuint two_buffer_fs = CompileShader("D2-fs", GL_FRAGMENT_SHADER, kTwoBufferFS);
   const GLuint texture_pass_gs = CompileShader("E0-gs", GL_GEOMETRY_SHADER, kTexturePassGS);
   const GLuint texture_fs = CompileShader("E0-fs", GL_FRAGMENT_SHADER, kTextureFS);
   const GLuint texture_gs = CompileShader("E1-gs", GL_GEOMETRY_SHADER, kTextureSampleGS);
   const GLuint lines_gs = CompileShader("E2-gs", GL_GEOMETRY_SHADER, kLinesGS);
   const GLuint adjacency_gs = CompileShader("E3-gs", GL_GEOMETRY_SHADER, kAdjacencyGS);
   const GLuint block_vs = CompileShader("F1-vs", GL_VERTEX_SHADER, kBlockVS);
   const GLuint block_gs = CompileShader("F1-gs", GL_GEOMETRY_SHADER, kBlockGS);
   const GLuint block_fs = CompileShader("F1-fs", GL_FRAGMENT_SHADER, kBlockFS);
   const GLuint matrix_vs = CompileShader("F2-vs", GL_VERTEX_SHADER, kMatrixVS);
   const GLuint matrix_gs = CompileShader("F2-gs", GL_GEOMETRY_SHADER, kMatrixGS);

   float x, y;
   std::vector<float> lines, adjacency;
   CellCenter(5, x, y);
   lines = {x - 0.16f, y - 0.2f, 1, 0, 0, 1,   x - 0.06f, y + 0.2f, 0, 1, 0, 1,
            x + 0.06f, y - 0.2f, 0, 0, 1, 1,   x + 0.16f, y + 0.2f, 1, 1, 1, 1};
   CellCenter(6, x, y);
   /* Main vertices 0, 2, 4 are black; adjacency vertices 1, 3, 5 carry the colour. */
   adjacency = {x - 0.22f, y - 0.26f, 0, 0, 0, 1,   x, y - 0.4f, 1, 1, 0, 1,
                x + 0.22f, y - 0.26f, 0, 0, 0, 1,   x + 0.3f, y + 0.1f, 0, 1, 1, 1,
                x, y + 0.26f, 0, 0, 0, 1,           x - 0.3f, y + 0.1f, 1, 0, 1, 1};

   Panel panels[9];
   CellCenter(0, x, y);
   panels[0] = {"A", LinkProgram("A", plain_vs, 0, plain_fs, false),
                CreateMesh(Triangle(x, y)), GL_TRIANGLES};
   panels[1] = {"D1", LinkProgram("D1", buffer_vs, 0, plain_fs, false),
                CreateMesh(Triangle(0.0f, 0.0f)), GL_TRIANGLES};
   CellCenter(2, x, y);
   panels[2] = {"D2", LinkProgram("D2", plain_vs, 0, two_buffer_fs, false),
                CreateMesh(Triangle(x, y)), GL_TRIANGLES};
   CellCenter(3, x, y);
   panels[3] = {"E0", LinkProgram("E0", plain_vs, texture_pass_gs, texture_fs, true),
                CreateMesh(TexturePoints(x, y)), GL_POINTS};
   CellCenter(4, x, y);
   panels[4] = {"E1", LinkProgram("E1", plain_vs, texture_gs, geometry_fs, true),
                CreateMesh(TexturePoints(x, y)), GL_POINTS};
   panels[5] = {"E2", LinkProgram("E2", plain_vs, lines_gs, geometry_fs, true),
                CreateMesh(lines), GL_LINES};
   panels[6] = {"E3", LinkProgram("E3", plain_vs, adjacency_gs, geometry_fs, true),
                CreateMesh(adjacency), GL_TRIANGLES_ADJACENCY};
   CellCenter(7, x, y);
   panels[7] = {"F1", LinkProgram("F1", block_vs, block_gs, block_fs, true),
                CreateMesh(Triangle(x, y)), GL_TRIANGLES};
   panels[8] = {"F2", LinkProgram("F2", matrix_vs, matrix_gs, geometry_fs, true),
                CreateMesh(Triangle(0.0f, 0.0f)), GL_TRIANGLES};
   example::LogGLError("setup");

   /* Uniform buffers, each on its own binding point. */
   struct Buffer {
      const char *block;
      GLuint binding;
      float data[4];
   };
   const Buffer buffers[] = {
      {"Transform", 1, {1.0f, 1.0f, 0.0f, 0.0f}},
      {"Alpha", 3, {1.0f, 0.0f, 0.0f, 1.0f}},
      {"Beta", 4, {0.0f, 0.0f, 1.0f, 1.0f}},
   };
   for (const Buffer &buffer : buffers) {
      GLuint ubo = 0;
      glGenBuffers(1, &ubo);
      glBindBuffer(GL_UNIFORM_BUFFER, ubo);
      glBufferData(GL_UNIFORM_BUFFER, sizeof(buffer.data), buffer.data, GL_STATIC_DRAW);
      glBindBufferBase(GL_UNIFORM_BUFFER, buffer.binding, ubo);
      for (const Panel &panel : panels) {
         if (!panel.program)
            continue;
         const GLuint index = glGetUniformBlockIndex(panel.program, buffer.block);
         if (index == GL_INVALID_INDEX)
            continue;
         glUniformBlockBinding(panel.program, index, buffer.binding);
         Log("%s: block %s index %u -> binding %u", panel.name, buffer.block, index,
             buffer.binding);
      }
   }

   /* 2x2 texture: red, green / blue, white (rows bottom to top). */
   const unsigned char texels[] = {255, 0, 0, 255,   0, 255, 0, 255,
                                   0, 0, 255, 255,   255, 255, 255, 255};
   GLuint texture = 0;
   glGenTextures(1, &texture);
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, texture);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   example::LogGLError("texture");

   /* Loose uniforms. */
   example::Mat4 flip = example::Mat4::Identity();
   flip.m[0] = -1.0f;
   flip.m[5] = -1.0f;
   CellCenter(8, x, y);
   const example::Mat4 place = example::Mat4::Translation(x, y, 0.0f);
   float d1_x, d1_y;
   CellCenter(1, d1_x, d1_y);
   for (const Panel &panel : panels) {
      if (!panel.program)
         continue;
      glUseProgram(panel.program);
      struct { const char *name; GLint location; } names[] = {
         {"uOffset", glGetUniformLocation(panel.program, "uOffset")},
         {"uMix", glGetUniformLocation(panel.program, "uMix")},
         {"uTexture", glGetUniformLocation(panel.program, "uTexture")},
         {"uGeometryTexture", glGetUniformLocation(panel.program, "uGeometryTexture")},
         {"uFlip", glGetUniformLocation(panel.program, "uFlip")},
         {"uPlace", glGetUniformLocation(panel.program, "uPlace")},
      };
      for (const auto &uniform : names) {
         if (uniform.location < 0)
            continue;
         Log("%s: %s location %d", panel.name, uniform.name, uniform.location);
      }
      if (names[0].location >= 0) glUniform2f(names[0].location, d1_x, d1_y);
      if (names[1].location >= 0) glUniform1f(names[1].location, 0.5f);
      if (names[2].location >= 0) glUniform1i(names[2].location, 0);
      if (names[3].location >= 0) glUniform1i(names[3].location, 0);
      if (names[4].location >= 0) glUniformMatrix4fv(names[4].location, 1, GL_FALSE, flip.m);
      if (names[5].location >= 0) glUniformMatrix4fv(names[5].location, 1, GL_FALSE, place.m);
   }
   example::LogGLError("uniforms");
   Log("setup done");

   uint32_t frame = 0;
   while (WHBProcIsRunning()) {
      example::PollGamePad();
      glClearColor(0.12f, 0.12f, 0.16f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      for (const Panel &panel : panels) {
         if (!panel.program)
            continue;
         if (frame == 0)
            Log("frame 0: drawing %s", panel.name);
         glUseProgram(panel.program);
         glBindVertexArray(panel.mesh.vao);
         glDrawArrays(panel.primitive, 0, panel.mesh.count);
         if (frame == 0) {
            glFinish();
            example::LogGLError(panel.name);
            Log("frame 0: %s finished on the GPU", panel.name);
         }
      }
      glBindVertexArray(0);
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
