/* Small helpers shared by the gx2gl examples: an SD card log that survives a
 * GPU hang, shader compile/link with logging, a GamePad poll for Aroma's button
 * combos, and a tiny column-major matrix type.
 */
#pragma once

#include <gl/gl.h>

#include <vpad/input.h>
#include <whb/log.h>
#include <whb/sdcard.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

namespace example {

inline std::string &LogPath()
{
   static std::string path;
   return path;
}

/* Opens sd:/<name>, emptied. Every Log() line reopens it so it survives a hang. */
inline void OpenLog(const char *name)
{
   if (!WHBMountSdCard())
      return;
   LogPath() = std::string(WHBGetSdCardMountPath()) + "/" + name;
   if (FILE *file = fopen(LogPath().c_str(), "w"))
      fclose(file);
}

inline void Log(const char *format, ...)
{
   char line[2048];
   va_list args;
   va_start(args, format);
   vsnprintf(line, sizeof(line), format, args);
   va_end(args);

   WHBLogPrintf("%s", line);
   if (LogPath().empty())
      return;
   if (FILE *file = fopen(LogPath().c_str(), "a")) {
      fprintf(file, "%s\n", line);
      fclose(file);
   }
}

inline void LogGLError(const char *where)
{
   for (GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError())
      Log("  GL error 0x%04x after %s", error, where);
}

inline GLuint CompileShader(const char *name, GLenum type, const char *source)
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

/* gs may be 0 for a program without a geometry shader. */
inline GLuint LinkProgram(const char *name, GLuint vs, GLuint gs, GLuint fs, bool needs_gs)
{
   if (!vs || !fs || (needs_gs && !gs)) {
      Log("%s: not linked, a shader failed to compile", name);
      return 0;
   }
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

/* Returns the buttons pressed this frame. Aroma's button combos (screenshots and
 * the like) only see input the app reads, so call this every frame. */
inline uint32_t PollGamePad()
{
   VPADStatus vpad;
   VPADReadError error;
   if (VPADRead(VPAD_CHAN_0, &vpad, 1, &error) > 0 && error == VPAD_READ_SUCCESS)
      return vpad.trigger;
   return 0;
}

struct Mat4 {
   float m[16]; /* column-major, as glUniformMatrix4fv expects */

   static Mat4 Identity()
   {
      Mat4 r = {};
      r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
      return r;
   }
   static Mat4 Perspective(float fovy, float aspect, float near_z, float far_z)
   {
      Mat4 r = {};
      const float f = 1.0f / std::tan(fovy * 0.5f);
      r.m[0] = f / aspect;
      r.m[5] = f;
      r.m[10] = (far_z + near_z) / (near_z - far_z);
      r.m[11] = -1.0f;
      r.m[14] = 2.0f * far_z * near_z / (near_z - far_z);
      return r;
   }
   static Mat4 Translation(float x, float y, float z)
   {
      Mat4 r = Identity();
      r.m[12] = x;
      r.m[13] = y;
      r.m[14] = z;
      return r;
   }
   static Mat4 RotationY(float angle)
   {
      Mat4 r = Identity();
      r.m[0] = std::cos(angle);
      r.m[2] = -std::sin(angle);
      r.m[8] = std::sin(angle);
      r.m[10] = std::cos(angle);
      return r;
   }
   static Mat4 RotationX(float angle)
   {
      Mat4 r = Identity();
      r.m[5] = std::cos(angle);
      r.m[6] = std::sin(angle);
      r.m[9] = -std::sin(angle);
      r.m[10] = std::cos(angle);
      return r;
   }
   Mat4 operator*(const Mat4 &b) const
   {
      Mat4 r = {};
      for (int c = 0; c < 4; ++c)
         for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
               r.m[c * 4 + row] += m[k * 4 + row] * b.m[c * 4 + k];
      return r;
   }
};

} // namespace example
