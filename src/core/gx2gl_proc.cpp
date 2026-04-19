#include "gx2gl/proc.h"

#include "gl/gl.h"

#include <stddef.h>
#include <string.h>

typedef struct
{
    const char *name;
    GX2GL_Proc proc;
} GX2GLProcEntry;

extern "C" {

static void gx2glDeleteObjectARB(GLuint obj)
{
    if (glIsProgram(obj)) {
        glDeleteProgram(obj);
    } else if (glIsShader(obj)) {
        glDeleteShader(obj);
    }
}

static void gx2glGetObjectParameterivARB(GLuint obj, GLenum pname, GLint *params)
{
    if (!params) {
        return;
    }

    if (glIsProgram(obj)) {
        glGetProgramiv(obj, pname, params);
    } else if (glIsShader(obj)) {
        glGetShaderiv(obj, pname, params);
    } else {
        *params = 0;
    }
}

static void gx2glGetInfoLogARB(GLuint obj, GLsizei maxLength, GLsizei *length, GLchar *infoLog)
{
    if (glIsProgram(obj)) {
        glGetProgramInfoLog(obj, maxLength, length, infoLog);
    } else if (glIsShader(obj)) {
        glGetShaderInfoLog(obj, maxLength, length, infoLog);
    } else {
        if (length) {
            *length = 0;
        }
        if (infoLog && maxLength > 0) {
            infoLog[0] = '\0';
        }
    }
}

static void gx2glGetAttachedObjectsARB(GLuint program, GLsizei maxCount, GLsizei *count, GLuint *obj)
{
    glGetAttachedShaders(program, maxCount, count, obj);
}

}

#include "gx2gl_proc_table.inc"

static const GX2GLProcEntry kGX2GLAliasProcTable[] = {
    {"glActiveTextureARB", reinterpret_cast<GX2GL_Proc>(glActiveTexture)},
    {"glAttachObjectARB", reinterpret_cast<GX2GL_Proc>(glAttachShader)},
    {"glBindBufferARB", reinterpret_cast<GX2GL_Proc>(glBindBuffer)},
    {"glBindAttribLocationARB", reinterpret_cast<GX2GL_Proc>(glBindAttribLocation)},
    {"glBindFramebufferEXT", reinterpret_cast<GX2GL_Proc>(glBindFramebuffer)},
    {"glBindRenderbufferEXT", reinterpret_cast<GX2GL_Proc>(glBindRenderbuffer)},
    {"glBufferDataARB", reinterpret_cast<GX2GL_Proc>(glBufferData)},
    {"glBufferSubDataARB", reinterpret_cast<GX2GL_Proc>(glBufferSubData)},
    {"glCheckFramebufferStatusEXT", reinterpret_cast<GX2GL_Proc>(glCheckFramebufferStatus)},
    {"glCompileShaderARB", reinterpret_cast<GX2GL_Proc>(glCompileShader)},
    {"glCreateProgramObjectARB", reinterpret_cast<GX2GL_Proc>(glCreateProgram)},
    {"glCreateShaderObjectARB", reinterpret_cast<GX2GL_Proc>(glCreateShader)},
    {"glDeleteBuffersARB", reinterpret_cast<GX2GL_Proc>(glDeleteBuffers)},
    {"glDeleteFramebuffersEXT", reinterpret_cast<GX2GL_Proc>(glDeleteFramebuffers)},
    {"glDeleteObjectARB", reinterpret_cast<GX2GL_Proc>(gx2glDeleteObjectARB)},
    {"glDeleteRenderbuffersEXT", reinterpret_cast<GX2GL_Proc>(glDeleteRenderbuffers)},
    {"glDetachObjectARB", reinterpret_cast<GX2GL_Proc>(glDetachShader)},
    {"glFramebufferRenderbufferEXT", reinterpret_cast<GX2GL_Proc>(glFramebufferRenderbuffer)},
    {"glFramebufferTexture2DEXT", reinterpret_cast<GX2GL_Proc>(glFramebufferTexture2D)},
    {"glGetActiveAttribARB", reinterpret_cast<GX2GL_Proc>(glGetActiveAttrib)},
    {"glGenBuffersARB", reinterpret_cast<GX2GL_Proc>(glGenBuffers)},
    {"glGenerateMipmapEXT", reinterpret_cast<GX2GL_Proc>(glGenerateMipmap)},
    {"glGenFramebuffersEXT", reinterpret_cast<GX2GL_Proc>(glGenFramebuffers)},
    {"glGenRenderbuffersEXT", reinterpret_cast<GX2GL_Proc>(glGenRenderbuffers)},
    {"glGetActiveUniformARB", reinterpret_cast<GX2GL_Proc>(glGetActiveUniform)},
    {"glGetAttachedObjectsARB", reinterpret_cast<GX2GL_Proc>(gx2glGetAttachedObjectsARB)},
    {"glGetBufferParameterivARB", reinterpret_cast<GX2GL_Proc>(glGetBufferParameteriv)},
    {"glGetBufferPointervARB", reinterpret_cast<GX2GL_Proc>(glGetBufferPointerv)},
    {"glGetInfoLogARB", reinterpret_cast<GX2GL_Proc>(gx2glGetInfoLogARB)},
    {"glGetObjectParameterivARB", reinterpret_cast<GX2GL_Proc>(gx2glGetObjectParameterivARB)},
    {"glGetShaderSourceARB", reinterpret_cast<GX2GL_Proc>(glGetShaderSource)},
    {"glGetUniformfvARB", reinterpret_cast<GX2GL_Proc>(glGetUniformfv)},
    {"glGetUniformivARB", reinterpret_cast<GX2GL_Proc>(glGetUniformiv)},
    {"glGetUniformLocationARB", reinterpret_cast<GX2GL_Proc>(glGetUniformLocation)},
    {"glIsBufferARB", reinterpret_cast<GX2GL_Proc>(glIsBuffer)},
    {"glIsFramebufferEXT", reinterpret_cast<GX2GL_Proc>(glIsFramebuffer)},
    {"glIsRenderbufferEXT", reinterpret_cast<GX2GL_Proc>(glIsRenderbuffer)},
    {"glLinkProgramARB", reinterpret_cast<GX2GL_Proc>(glLinkProgram)},
    {"glMapBufferARB", reinterpret_cast<GX2GL_Proc>(glMapBuffer)},
    {"glRenderbufferStorageEXT", reinterpret_cast<GX2GL_Proc>(glRenderbufferStorage)},
    {"glShaderSourceARB", reinterpret_cast<GX2GL_Proc>(glShaderSource)},
    {"glUniform1fARB", reinterpret_cast<GX2GL_Proc>(glUniform1f)},
    {"glUniform1fvARB", reinterpret_cast<GX2GL_Proc>(glUniform1fv)},
    {"glUniform1iARB", reinterpret_cast<GX2GL_Proc>(glUniform1i)},
    {"glUniform1ivARB", reinterpret_cast<GX2GL_Proc>(glUniform1iv)},
    {"glUniform2fvARB", reinterpret_cast<GX2GL_Proc>(glUniform2fv)},
    {"glUniform2ivARB", reinterpret_cast<GX2GL_Proc>(glUniform2iv)},
    {"glUniform3fvARB", reinterpret_cast<GX2GL_Proc>(glUniform3fv)},
    {"glUniform3ivARB", reinterpret_cast<GX2GL_Proc>(glUniform3iv)},
    {"glUniform4fvARB", reinterpret_cast<GX2GL_Proc>(glUniform4fv)},
    {"glUniform4ivARB", reinterpret_cast<GX2GL_Proc>(glUniform4iv)},
    {"glUniformMatrix2fvARB", reinterpret_cast<GX2GL_Proc>(glUniformMatrix2fv)},
    {"glUniformMatrix3fvARB", reinterpret_cast<GX2GL_Proc>(glUniformMatrix3fv)},
    {"glUniformMatrix4fvARB", reinterpret_cast<GX2GL_Proc>(glUniformMatrix4fv)},
    {"glUnmapBufferARB", reinterpret_cast<GX2GL_Proc>(glUnmapBuffer)},
    {"glUseProgramObjectARB", reinterpret_cast<GX2GL_Proc>(glUseProgram)},
    {"glValidateProgramARB", reinterpret_cast<GX2GL_Proc>(glValidateProgram)},
};

static const size_t kGX2GLAliasProcCount =
    sizeof(kGX2GLAliasProcTable) / sizeof(kGX2GLAliasProcTable[0]);

void* GX2GL_GetProcAddress(const char *name)
{
    size_t i;

    if (!name) {
        return NULL;
    }

    for (i = 0; i < kGX2GLCoreProcCount; ++i) {
        if (strcmp(kGX2GLCoreProcTable[i].name, name) == 0) {
            return reinterpret_cast<void*>(kGX2GLCoreProcTable[i].proc);
        }
    }

    for (i = 0; i < kGX2GLAliasProcCount; ++i) {
        if (strcmp(kGX2GLAliasProcTable[i].name, name) == 0) {
            return reinterpret_cast<void*>(kGX2GLAliasProcTable[i].proc);
        }
    }

    return NULL;
}
