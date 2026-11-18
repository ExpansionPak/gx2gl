/* gx2gl geometry shader demo, after the geometry shader chapter of
 * LearnOpenGL (https://learnopengl.com/Advanced-OpenGL/Geometry-Shader),
 * with a procedural torus instead of a loaded model.
 *
 *   - "Exploding" torus: a geometry shader pushes every triangle out along its
 *     face normal over time.
 *   - Normal visualisation: a second pass draws each vertex normal as a yellow
 *     line, built by a geometry shader that applies the projection itself.
 *   - Houses: four points turned into little houses (the chapter's first
 *     example), in the top-left corner.
 *
 * Uses interface blocks, mat4 uniforms shared by the vertex and geometry
 * shaders, uniforms only the geometry shader declares, indexed draws and depth.
 *
 * GamePad: A toggles the explosion, B toggles the normals, X pauses rotation.
 * Log: sd:/gx2gl-gs-demo.log
 */
#include "../common/example_util.h"

#include <gx2gl/present.h>
#include <whb/log_cafe.h>
#include <whb/log_udp.h>
#include <whb/proc.h>

#include <cmath>
#include <vector>

using example::Log;
using example::Mat4;

static const char kTorusVS[] = R"(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
out VS_OUT {
   vec3 normal;
   vec3 color;
} vs_out;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
void main() {
   vs_out.normal = mat3(uView * uModel) * aNormal;
   vs_out.color = aNormal * 0.5 + 0.5;
   gl_Position = uProjection * uView * uModel * vec4(aPosition, 1.0);
}
)";

static const char kExplodeGS[] = R"(
#version 330 core
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;
in VS_OUT {
   vec3 normal;
   vec3 color;
} gs_in[];
out GS_OUT {
   vec3 normal;
   vec3 color;
} gs_out;
uniform float uExplode;
void main() {
   vec3 a = vec3(gl_in[0].gl_Position) - vec3(gl_in[1].gl_Position);
   vec3 b = vec3(gl_in[2].gl_Position) - vec3(gl_in[1].gl_Position);
   vec3 faceNormal = normalize(cross(a, b));
   vec4 push = vec4(faceNormal * uExplode, 0.0);
   for (int i = 0; i < 3; ++i) {
      gs_out.normal = gs_in[i].normal;
      gs_out.color = gs_in[i].color;
      gl_Position = gl_in[i].gl_Position + push;
      EmitVertex();
   }
   EndPrimitive();
}
)";

static const char kTorusFS[] = R"(
#version 330 core
in GS_OUT {
   vec3 normal;
   vec3 color;
} fs_in;
out vec4 fragColor;
void main() {
   float light = 0.35 + 0.65 * max(dot(normalize(fs_in.normal), normalize(vec3(0.4, 0.6, 1.0))), 0.0);
   fragColor = vec4(fs_in.color * light, 1.0);
}
)";

static const char kNormalVS[] = R"(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
out VS_OUT {
   vec3 normal;
} vs_out;
uniform mat4 uModel;
uniform mat4 uView;
void main() {
   vs_out.normal = normalize(mat3(uView * uModel) * aNormal);
   gl_Position = uView * uModel * vec4(aPosition, 1.0);
}
)";

static const char kNormalGS[] = R"(
#version 330 core
layout(triangles) in;
layout(line_strip, max_vertices = 6) out;
in VS_OUT {
   vec3 normal;
} gs_in[];
uniform mat4 uProjection;
const float kLength = 0.08;
void main() {
   for (int i = 0; i < 3; ++i) {
      gl_Position = uProjection * gl_in[i].gl_Position;
      EmitVertex();
      gl_Position = uProjection * (gl_in[i].gl_Position + vec4(gs_in[i].normal * kLength, 0.0));
      EmitVertex();
      EndPrimitive();
   }
}
)";

static const char kNormalFS[] = R"(
#version 330 core
out vec4 fragColor;
void main() { fragColor = vec4(1.0, 0.9, 0.1, 1.0); }
)";

static const char kHouseVS[] = R"(
#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec3 aColor;
out VS_OUT {
   vec3 color;
} vs_out;
void main() {
   vs_out.color = aColor;
   gl_Position = vec4(aPosition, 0.0, 1.0);
}
)";

static const char kHouseGS[] = R"(
#version 330 core
layout(points) in;
layout(triangle_strip, max_vertices = 5) out;
in VS_OUT {
   vec3 color;
} gs_in[];
out vec3 fColor;
void main() {
   vec4 p = gl_in[0].gl_Position;
   fColor = gs_in[0].color;
   gl_Position = p + vec4(-0.05, -0.07, 0.0, 0.0); EmitVertex();
   gl_Position = p + vec4( 0.05, -0.07, 0.0, 0.0); EmitVertex();
   gl_Position = p + vec4(-0.05,  0.03, 0.0, 0.0); EmitVertex();
   gl_Position = p + vec4( 0.05,  0.03, 0.0, 0.0); EmitVertex();
   fColor = vec3(1.0);   // white roof tip
   gl_Position = p + vec4( 0.0,   0.1, 0.0, 0.0); EmitVertex();
   EndPrimitive();
}
)";

static const char kHouseFS[] = R"(
#version 330 core
in vec3 fColor;
out vec4 fragColor;
void main() { fragColor = vec4(fColor, 1.0); }
)";

struct Torus {
   GLuint vao = 0;
   GLsizei index_count = 0;
};

static Torus CreateTorus(int rings, int sides, float major_radius, float minor_radius)
{
   std::vector<float> vertices; /* position, normal */
   std::vector<unsigned short> indices;
   const float kPi = 3.14159265f;
   for (int ring = 0; ring <= rings; ++ring) {
      const float u = (float)ring / (float)rings * 2.0f * kPi;
      for (int side = 0; side <= sides; ++side) {
         const float v = (float)side / (float)sides * 2.0f * kPi;
         const float nx = std::cos(u) * std::cos(v), ny = std::sin(v), nz = std::sin(u) * std::cos(v);
         vertices.insert(vertices.end(),
                         {std::cos(u) * major_radius + nx * minor_radius, ny * minor_radius,
                          std::sin(u) * major_radius + nz * minor_radius, nx, ny, nz});
      }
   }
   for (int ring = 0; ring < rings; ++ring) {
      for (int side = 0; side < sides; ++side) {
         const unsigned short a = (unsigned short)(ring * (sides + 1) + side);
         const unsigned short b = (unsigned short)((ring + 1) * (sides + 1) + side);
         indices.insert(indices.end(), {a, b, (unsigned short)(a + 1),
                                        (unsigned short)(a + 1), b, (unsigned short)(b + 1)});
      }
   }

   Torus torus;
   torus.index_count = (GLsizei)indices.size();
   GLuint buffers[2];
   glGenVertexArrays(1, &torus.vao);
   glBindVertexArray(torus.vao);
   glGenBuffers(2, buffers);
   glBindBuffer(GL_ARRAY_BUFFER, buffers[0]);
   glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
   glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (const void *)0);
   glEnableVertexAttribArray(0);
   glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                         (const void *)(3 * sizeof(float)));
   glEnableVertexAttribArray(1);
   glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers[1]);
   glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned short), indices.data(),
                GL_STATIC_DRAW);
   glBindVertexArray(0);
   return torus;
}

static GLuint CreateHouses()
{
   const float points[] = {
      -0.9f, 0.85f, 1, 0, 0,   -0.75f, 0.85f, 0, 1, 0,
      -0.9f, 0.6f, 0, 0, 1,    -0.75f, 0.6f, 1, 1, 0,
   };
   GLuint vao = 0, vbo = 0;
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(points), points, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (const void *)0);
   glEnableVertexAttribArray(0);
   glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                         (const void *)(2 * sizeof(float)));
   glEnableVertexAttribArray(1);
   glBindVertexArray(0);
   return vao;
}

static void SetMatrix(GLuint program, const char *name, const Mat4 &matrix)
{
   const GLint location = glGetUniformLocation(program, name);
   if (location >= 0)
      glUniformMatrix4fv(location, 1, GL_FALSE, matrix.m);
}

int main(int argc, char **argv)
{
   WHBProcInit();
   WHBLogCafeInit();
   WHBLogUdpInit();
   example::OpenLog("gx2gl-gs-demo.log");
   Log("gx2gl geometry shader demo started");

   GX2GL_Context context = GX2GL_CreateContext();
   if (!context) {
      Log("GX2GL_CreateContext FAILED");
      WHBProcShutdown();
      return 0;
   }
   Log("GL_VERSION %s", (const char *)glGetString(GL_VERSION));

   using example::CompileShader;
   using example::LinkProgram;
   const GLuint torus_program = LinkProgram(
      "explode", CompileShader("torus-vs", GL_VERTEX_SHADER, kTorusVS),
      CompileShader("explode-gs", GL_GEOMETRY_SHADER, kExplodeGS),
      CompileShader("torus-fs", GL_FRAGMENT_SHADER, kTorusFS), true);
   const GLuint normal_program = LinkProgram(
      "normals", CompileShader("normal-vs", GL_VERTEX_SHADER, kNormalVS),
      CompileShader("normal-gs", GL_GEOMETRY_SHADER, kNormalGS),
      CompileShader("normal-fs", GL_FRAGMENT_SHADER, kNormalFS), true);
   const GLuint house_program = LinkProgram(
      "houses", CompileShader("house-vs", GL_VERTEX_SHADER, kHouseVS),
      CompileShader("house-gs", GL_GEOMETRY_SHADER, kHouseGS),
      CompileShader("house-fs", GL_FRAGMENT_SHADER, kHouseFS), true);

   const Torus torus = CreateTorus(48, 24, 0.55f, 0.22f);
   const GLuint houses = CreateHouses();
   const Mat4 projection = Mat4::Perspective(0.9f, 16.0f / 9.0f, 0.1f, 10.0f);
   const Mat4 view = Mat4::Translation(0.0f, 0.0f, -2.2f);
   const GLint explode_location =
      torus_program ? glGetUniformLocation(torus_program, "uExplode") : -1;
   Log("torus: %d indices, uExplode location %d", torus.index_count, explode_location);
   example::LogGLError("setup");

   bool explode = true, normals = true, rotate = true;
   float angle = 0.0f, time = 0.0f;
   uint32_t frame = 0;
   while (WHBProcIsRunning()) {
      const uint32_t pressed = example::PollGamePad();
      if (pressed & VPAD_BUTTON_A) explode = !explode;
      if (pressed & VPAD_BUTTON_B) normals = !normals;
      if (pressed & VPAD_BUTTON_X) rotate = !rotate;
      if (pressed & (VPAD_BUTTON_A | VPAD_BUTTON_B | VPAD_BUTTON_X))
         Log("frame %u: explode %d, normals %d, rotate %d", frame, explode, normals, rotate);
      if (rotate)
         angle += 0.012f;
      time += 1.0f / 60.0f;

      const Mat4 model = Mat4::RotationY(angle) * Mat4::RotationX(0.6f + angle * 0.5f);
      glClearColor(0.08f, 0.08f, 0.11f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glEnable(GL_DEPTH_TEST);

      if (torus_program) {
         glUseProgram(torus_program);
         SetMatrix(torus_program, "uModel", model);
         SetMatrix(torus_program, "uView", view);
         SetMatrix(torus_program, "uProjection", projection);
         glUniform1f(explode_location,
                     explode ? (std::sin(time * 1.5f) + 1.0f) * 0.5f * 0.35f : 0.0f);
         glBindVertexArray(torus.vao);
         glDrawElements(GL_TRIANGLES, torus.index_count, GL_UNSIGNED_SHORT, nullptr);
      }
      if (normals && normal_program) {
         glUseProgram(normal_program);
         SetMatrix(normal_program, "uModel", model);
         SetMatrix(normal_program, "uView", view);
         SetMatrix(normal_program, "uProjection", projection);
         glBindVertexArray(torus.vao);
         glDrawElements(GL_TRIANGLES, torus.index_count, GL_UNSIGNED_SHORT, nullptr);
      }

      glDisable(GL_DEPTH_TEST);
      if (house_program) {
         glUseProgram(house_program);
         glBindVertexArray(houses);
         glDrawArrays(GL_POINTS, 0, 4);
      }
      glBindVertexArray(0);

      if (frame == 0) {
         glFinish();
         example::LogGLError("first frame");
         Log("frame 0: finished on the GPU");
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
