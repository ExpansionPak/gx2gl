#ifndef GX2GL_UNIFORM_REFLECTION_H
#define GX2GL_UNIFORM_REFLECTION_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  char Name[256];
  char BlockName[128];
  uint32_t Type;
  int32_t Size;
  int32_t Offset;
  int32_t ArrayStride;
  int32_t MatrixStride;
  int32_t RowMajor;
} GX2GLUniformReflection;

size_t GX2GLReflectUniformBlocks(const char *Source,
                                GX2GLUniformReflection *Output,
                                size_t Capacity);

bool GX2GLValidateUniformBlockLinks(const char *VertexSource,
                                  const char *FragmentSource, char *Error,
                                  size_t ErrorCapacity);

uint32_t GX2GLReflectSamplerType(const char *Source, const char *Name);

#endif
