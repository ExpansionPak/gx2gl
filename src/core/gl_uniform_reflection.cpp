#include "gl_uniform_reflection.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Token {
  std::string Text;
};

std::vector<Token> Tokenize(const char *Source) {
  std::vector<Token> Result;
  if (!Source) return Result;
  const char *Cursor = Source;
  while (*Cursor) {
    if (std::isspace((unsigned char)*Cursor)) {
      ++Cursor;
      continue;
    }
    if (*Cursor == '#' && (Cursor == Source || Cursor[-1] == '\n')) {
      while (*Cursor && *Cursor != '\n') ++Cursor;
      continue;
    }
    if (Cursor[0] == '/' && Cursor[1] == '/') {
      while (*Cursor && *Cursor != '\n') ++Cursor;
      continue;
    }
    if (Cursor[0] == '/' && Cursor[1] == '*') {
      Cursor += 2;
      while (*Cursor && !(Cursor[0] == '*' && Cursor[1] == '/')) ++Cursor;
      if (*Cursor) Cursor += 2;
      continue;
    }
    const char *Start = Cursor;
    if (std::isalnum((unsigned char)*Cursor) || *Cursor == '_') {
      while (std::isalnum((unsigned char)*Cursor) || *Cursor == '_') ++Cursor;
    } else {
      ++Cursor;
    }
    Result.push_back({std::string(Start, Cursor)});
  }
  return Result;
}

bool Identifier(const std::string &Name) {
  return !Name.empty() &&
      (std::isalpha((unsigned char)Name[0]) || Name[0] == '_');
}

bool Qualifier(const std::string &Value) {
  return Value == "highp" || Value == "mediump" || Value == "lowp" ||
         Value == "const" || Value == "readonly" || Value == "restrict" ||
         Value == "coherent" || Value == "volatile" || Value == "precise";
}

uint32_t TypeEnum(const std::string &Name, uint32_t *Alignment,
                  uint32_t *Bytes, int32_t *MatrixStride, bool RowMajor) {
  *Alignment = 4;
  *Bytes = 4;
  *MatrixStride = 0;
  if (Name == "float") return 0x1406;
  if (Name == "int") return 0x1404;
  if (Name == "uint") return 0x1405;
  if (Name == "bool") return 0x8B56;
  const struct { const char *Name; uint32_t Enum; uint32_t Components; } Vectors[] = {
      {"vec2",0x8B50,2},{"vec3",0x8B51,3},{"vec4",0x8B52,4},
      {"ivec2",0x8B53,2},{"ivec3",0x8B54,3},{"ivec4",0x8B55,4},
      {"bvec2",0x8B57,2},{"bvec3",0x8B58,3},{"bvec4",0x8B59,4},
      {"uvec2",0x8DC6,2},{"uvec3",0x8DC7,3},{"uvec4",0x8DC8,4}};
  for (const auto &Vector : Vectors) {
    if (Name == Vector.Name) {
      *Alignment = Vector.Components == 2 ? 8 : 16;
      *Bytes = Vector.Components * 4;
      return Vector.Enum;
    }
  }
  if (Name.compare(0,3,"mat") != 0 || Name.size() < 4 ||
      Name[3] < '2' || Name[3] > '4') return 0;
  uint32_t Columns = (uint32_t)(Name[3] - '0');
  uint32_t Rows = Columns;
  if (Name.size() == 6 && Name[4] == 'x' && Name[5] >= '2' && Name[5] <= '4')
    Rows = (uint32_t)(Name[5] - '0');
  else if (Name.size() != 4) return 0;
  static const uint32_t MatrixTypes[3][3] = {
      {0x8B5A,0x8B65,0x8B66},
      {0x8B67,0x8B5B,0x8B68},
      {0x8B69,0x8B6A,0x8B5C}};
  *Alignment = 16;
  *MatrixStride = 16;
  *Bytes = (RowMajor ? Rows : Columns) * 16;
  return MatrixTypes[Columns - 2][Rows - 2];
}

uint32_t Align(uint32_t Value, uint32_t Alignment) {
  return (Value + Alignment - 1) & ~(Alignment - 1);
}

bool Active(const std::vector<Token> &Tokens, size_t Start,
            const std::string &Instance, const std::string &Member) {
  for (size_t i = Start; i < Tokens.size(); ++i) {
    if (Tokens[i].Text != (Instance.empty() ? Member : Instance)) continue;
    if (Instance.empty()) return true;
    size_t j = i + 1;
    if (j < Tokens.size() && Tokens[j].Text == "[") {
      while (j < Tokens.size() && Tokens[j].Text != "]") ++j;
      if (j == Tokens.size()) continue;
      ++j;
    }
    if (j + 1 < Tokens.size() && Tokens[j].Text == "." &&
        Tokens[j + 1].Text == Member) return true;
  }
  return false;
}

void Copy(char *Destination, size_t Capacity, const std::string &Text) {
  if (!Capacity) return;
  std::snprintf(Destination, Capacity, "%s", Text.c_str());
}

}

uint32_t GX2GLReflectSamplerType(const char *Source, const char *Name) {
  if (!Source || !Name || !*Name) return 0;
  const auto Tokens = Tokenize(Source);
  std::string Target(Name);
  if (Target.size() > 3 && Target.compare(Target.size() - 3, 3, "[0]") == 0)
    Target.resize(Target.size() - 3);
  int Depth = 0;
  for (size_t i = 0; i < Tokens.size(); ++i) {
    const auto &Word = Tokens[i].Text;
    if (Word == "{") { ++Depth; continue; }
    if (Word == "}") { --Depth; continue; }
    if (Depth || Word != "uniform") continue;
    size_t TypeIndex = i + 1;
    while (TypeIndex < Tokens.size() && Qualifier(Tokens[TypeIndex].Text))
      ++TypeIndex;
    if (TypeIndex >= Tokens.size()) continue;
    const std::string &Type = Tokens[TypeIndex].Text;
    uint32_t Result = 0;
    if (Type == "sampler1DArray") Result = 0x8DC0;
    else if (Type == "isampler1DArray") Result = 0x8DCE;
    else if (Type == "usampler1DArray") Result = 0x8DD6;
    else if (Type == "sampler2DArray") Result = 0x8DC1;
    else if (Type == "isampler2DArray") Result = 0x8DCF;
    else if (Type == "usampler2DArray") Result = 0x8DD7;
    else if (Type == "sampler2DRect") Result = 0x8B63;
    else if (Type == "isampler2DRect") Result = 0x8DCD;
    else if (Type == "usampler2DRect") Result = 0x8DD5;
    if (!Result) continue;
    for (size_t j = TypeIndex + 1; j < Tokens.size() && Tokens[j].Text != ";"; ++j) {
      if (Tokens[j].Text == Target &&
          (j == TypeIndex + 1 || Tokens[j - 1].Text == ","))
        return Result;
    }
  }
  return 0;
}


namespace {

struct BlockMember {
  std::string Type;
  std::string Name;
  uint32_t Count = 1;
  bool Array = false;
  bool RowMajor = false;
  bool ExplicitMajor = false;
};

struct BlockStructure {
  std::string Name;
  std::vector<BlockMember> Members;
};

size_t ClosingBrace(const std::vector<Token> &Tokens, size_t Open) {
  int Depth = 0;
  for (size_t i = Open; i < Tokens.size(); ++i) {
    if (Tokens[i].Text == "{") ++Depth;
    if (Tokens[i].Text == "}" && --Depth == 0) return i;
  }
  return Tokens.size();
}

std::vector<BlockMember> ParseBlockMembers(const std::vector<Token> &Tokens,
                                            size_t Begin, size_t End) {
  std::vector<BlockMember> Members;
  while (Begin < End) {
    size_t Stop = Begin;
    while (Stop < End && Tokens[Stop].Text != ";") ++Stop;
    if (Stop >= End) break;
    size_t First = Begin;
    bool RowMajor = false;
    bool ExplicitMajor = false;
    while (First < Stop && Tokens[First].Text == "layout") {
      ++First;
      if (First < Stop && Tokens[First].Text == "(") {
        ++First;
        while (First < Stop && Tokens[First].Text != ")") {
          if (Tokens[First].Text == "row_major") {
            RowMajor = true;
            ExplicitMajor = true;
          }
          if (Tokens[First].Text == "column_major") {
            RowMajor = false;
            ExplicitMajor = true;
          }
          ++First;
        }
        if (First < Stop) ++First;
      }
    }
    while (First < Stop && Qualifier(Tokens[First].Text)) ++First;
    if (First + 1 < Stop && Identifier(Tokens[First].Text)) {
      const std::string Type = Tokens[First++].Text;
      while (First < Stop && Identifier(Tokens[First].Text)) {
        BlockMember Member;
        Member.Type = Type;
        Member.Name = Tokens[First++].Text;
        Member.RowMajor = RowMajor;
        Member.ExplicitMajor = ExplicitMajor;
        if (First < Stop && Tokens[First].Text == "[") {
          Member.Array = true;
          ++First;
          if (First < Stop) {
            char *After = nullptr;
            unsigned long Value = std::strtoul(Tokens[First].Text.c_str(),
                                               &After, 10);
            if (After && *After == 0 && Value && Value <= 4096)
              Member.Count = (uint32_t)Value;
          }
          while (First < Stop && Tokens[First].Text != "]") ++First;
          if (First < Stop) ++First;
        }
        Members.push_back(Member);
        if (First < Stop && Tokens[First].Text == ",") ++First;
        else break;
      }
    }
    Begin = Stop + 1;
  }
  return Members;
}

bool ResolveBlockType(const std::string &Name,
                      const std::vector<BlockStructure> &Structures,
                      bool RowMajor, uint32_t *Alignment, uint32_t *Bytes,
                      int32_t *MatrixStride, uint32_t *Type,
                      const BlockStructure **Structure, int Depth);

bool ResolveBlockMember(const BlockMember &Member,
                        const std::vector<BlockStructure> &Structures,
                        bool RowMajor, uint32_t *Alignment, uint32_t *Bytes,
                        uint32_t *Stride, int32_t *MatrixStride, uint32_t *Type,
                        const BlockStructure **Structure, int Depth) {
  uint32_t ElementAlignment = 0, ElementBytes = 0;
  if (!ResolveBlockType(Member.Type, Structures,
                        Member.ExplicitMajor ? Member.RowMajor : RowMajor,
                        &ElementAlignment, &ElementBytes, MatrixStride,
                        Type, Structure, Depth)) return false;
  *Alignment = Member.Array ? std::max(ElementAlignment, 16u) : ElementAlignment;
  *Stride = Member.Array ? Align(ElementBytes, 16) : 0;
  *Bytes = Member.Array ? *Stride * Member.Count : ElementBytes;
  return true;
}

bool ResolveBlockType(const std::string &Name,
                      const std::vector<BlockStructure> &Structures,
                      bool RowMajor, uint32_t *Alignment, uint32_t *Bytes,
                      int32_t *MatrixStride, uint32_t *Type,
                      const BlockStructure **Structure, int Depth) {
  if (Depth > 16) return false;
  *Structure = nullptr;
  *Type = TypeEnum(Name, Alignment, Bytes, MatrixStride, RowMajor);
  if (*Type) return true;
  for (const auto &Definition : Structures) {
    if (Definition.Name != Name) continue;
    *Structure = &Definition;
    *Alignment = 16;
    *MatrixStride = 0;
    uint32_t Offset = 0;
    for (const auto &Member : Definition.Members) {
      uint32_t MemberAlignment, MemberBytes, Stride, MemberType;
      int32_t MemberMatrixStride;
      const BlockStructure *Nested;
      if (!ResolveBlockMember(Member, Structures, RowMajor,
                              &MemberAlignment, &MemberBytes, &Stride,
                              &MemberMatrixStride, &MemberType, &Nested,
                              Depth + 1)) return false;
      *Alignment = std::max(*Alignment, MemberAlignment);
      Offset = Align(Offset, MemberAlignment) + MemberBytes;
    }
    *Alignment = Align(*Alignment, 16);
    *Bytes = Align(Offset, *Alignment);
    return true;
  }
  return false;
}

void EmitBlockMembers(const std::vector<BlockMember> &Members,
                      const std::vector<BlockStructure> &Structures,
                      const std::string &Prefix, const std::string &BlockName,
                      uint32_t Base, bool RowMajor, bool Visible,
                      GX2GLUniformReflection *Output, size_t Capacity,
                      size_t *Count, int Depth) {
  if (Depth > 16) return;
  uint32_t Offset = 0;
  for (const auto &Member : Members) {
    uint32_t Alignment, Bytes, Stride, Type;
    int32_t MatrixStride;
    const BlockStructure *Structure;
    bool MemberRowMajor = Member.ExplicitMajor ? Member.RowMajor : RowMajor;
    if (!ResolveBlockMember(Member, Structures, RowMajor, &Alignment,
                            &Bytes, &Stride, &MatrixStride, &Type,
                            &Structure, Depth)) continue;
    Offset = Align(Offset, Alignment);
    std::string Name = Prefix + Member.Name;
    if (Structure) {
      const uint32_t Elements = Member.Array ? Member.Count : 1u;
      for (uint32_t Index = 0; Index < Elements && *Count < Capacity; ++Index) {
        const std::string ElementName = Name +
            (Member.Array ? "[" + std::to_string(Index) + "]" : "");
        EmitBlockMembers(Structure->Members, Structures, ElementName + ".",
                         BlockName, Base + Offset + Index * Stride,
                         MemberRowMajor, Visible, Output, Capacity, Count,
                         Depth + 1);
      }
    } else if (Visible && *Count < Capacity) {
      GX2GLUniformReflection Result = {};
      Copy(Result.Name, sizeof(Result.Name),
           Name + (Member.Array ? "[0]" : ""));
      Copy(Result.BlockName, sizeof(Result.BlockName), BlockName);
      Result.Type = Type;
      Result.Size = (int32_t)Member.Count;
      Result.Offset = (int32_t)(Base + Offset);
      Result.ArrayStride = (int32_t)Stride;
      Result.MatrixStride = MatrixStride;
      Result.RowMajor = MemberRowMajor && MatrixStride != 0;
      Output[(*Count)++] = Result;
    }
    Offset += Bytes;
  }
}

}

size_t GX2GLReflectUniformBlocks(const char *Source,
                                GX2GLUniformReflection *Output,
                                size_t Capacity) {
  if (!Source || !Output || !Capacity) return 0;
  const auto Tokens = Tokenize(Source);
  std::vector<BlockStructure> Structures;
  for (size_t i = 0; i + 2 < Tokens.size(); ++i) {
    if (Tokens[i].Text != "struct" || !Identifier(Tokens[i + 1].Text) ||
        Tokens[i + 2].Text != "{") continue;
    size_t Close = ClosingBrace(Tokens, i + 2);
    if (Close >= Tokens.size()) continue;
    Structures.push_back({Tokens[i + 1].Text,
                          ParseBlockMembers(Tokens, i + 3, Close)});
    i = Close;
  }
  size_t Count = 0;
  int Depth = 0;
  for (size_t i = 0; i < Tokens.size(); ++i) {
    const auto &Word = Tokens[i].Text;
    if (Word == "{") { ++Depth; continue; }
    if (Word == "}") { --Depth; continue; }
    if (Depth || Word != "uniform" || i + 2 >= Tokens.size() ||
        !Identifier(Tokens[i + 1].Text) ||
        Tokens[i + 2].Text != "{") continue;
    bool Packed = false;
    bool BlockRowMajor = false;
    for (size_t j = i; j > 0 && i - j < 18; --j) {
      if (Tokens[j - 1].Text == ";" || Tokens[j - 1].Text == "}") break;
      if (Tokens[j - 1].Text == "packed") Packed = true;
      if (Tokens[j - 1].Text == "row_major") BlockRowMajor = true;
      if (Tokens[j - 1].Text == "column_major") BlockRowMajor = false;
    }
    const std::string BlockName = Tokens[i + 1].Text;
    size_t Open = i + 2;
    size_t Close = ClosingBrace(Tokens, Open);
    if (Close >= Tokens.size()) continue;
    size_t Suffix = Close + 1;
    std::string Instance;
    if (Suffix < Tokens.size() && Identifier(Tokens[Suffix].Text))
      Instance = Tokens[Suffix++].Text;
    while (Suffix < Tokens.size() && Tokens[Suffix].Text != ";") ++Suffix;
    if (Suffix >= Tokens.size()) continue;
    auto Members = ParseBlockMembers(Tokens, Open + 1, Close);
    uint32_t Offset = 0;
    for (const auto &Member : Members) {
      uint32_t Alignment, Bytes, Stride, Type;
      int32_t MatrixStride;
      const BlockStructure *Structure;
      if (!ResolveBlockMember(Member, Structures, BlockRowMajor, &Alignment,
                              &Bytes, &Stride, &MatrixStride, &Type,
                              &Structure, 0)) continue;
      Offset = Align(Offset, Alignment);
      bool Visible = !Packed || Active(Tokens, Suffix + 1,
                                       Instance, Member.Name);
      const std::string Prefix = (Instance.empty() ? "" : BlockName + ".");
      std::vector<BlockMember> Single = {Member};
      EmitBlockMembers(Single, Structures, Prefix, BlockName, Offset,
                       BlockRowMajor, Visible, Output, Capacity, &Count, 0);
      Offset += Bytes;
    }
    i = Suffix;
  }
  return Count;
}


namespace {

struct LinkUniformSymbol {
  std::string Name;
  std::string Owner;
};

struct LinkUniformBlock {
  std::string Name;
  bool HasInstance = false;
  bool IsArray = false;
  uint32_t ArrayCount = 1;
  std::vector<BlockMember> Members;
};

struct LinkUniformStage {
  std::vector<LinkUniformBlock> Blocks;
  std::vector<LinkUniformSymbol> Symbols;
};

LinkUniformStage CollectLinkUniforms(const char *Source) {
  LinkUniformStage Stage;
  const auto Tokens = Tokenize(Source);
  int Depth = 0;
  for (size_t Index = 0; Index < Tokens.size(); ++Index) {
    if (Tokens[Index].Text == "{") {
      ++Depth;
      continue;
    }
    if (Tokens[Index].Text == "}") {
      --Depth;
      continue;
    }
    if (Depth != 0 || Tokens[Index].Text != "uniform") continue;
    size_t TypeIndex = Index + 1;
    while (TypeIndex < Tokens.size() && Qualifier(Tokens[TypeIndex].Text))
      ++TypeIndex;
    if (TypeIndex + 1 >= Tokens.size() ||
        !Identifier(Tokens[TypeIndex].Text)) continue;
    if (Tokens[TypeIndex + 1].Text != "{") {
      const std::string Type = Tokens[TypeIndex].Text;
      for (size_t Cursor = TypeIndex + 1; Cursor < Tokens.size() &&
            Tokens[Cursor].Text != ";"; ++Cursor) {
        if (Identifier(Tokens[Cursor].Text) &&
            (Cursor == TypeIndex + 1 || Tokens[Cursor - 1].Text == ","))
          Stage.Symbols.push_back({Tokens[Cursor].Text, ""});
      }
      continue;
    }
    size_t Open = TypeIndex + 1;
    size_t Close = ClosingBrace(Tokens, Open);
    if (Close >= Tokens.size()) continue;
    LinkUniformBlock Block;
    Block.Name = Tokens[TypeIndex].Text;
    Block.Members = ParseBlockMembers(Tokens, Open + 1, Close);
    size_t Suffix = Close + 1;
    if (Suffix < Tokens.size() && Identifier(Tokens[Suffix].Text)) {
      Block.HasInstance = true;
      ++Suffix;
    }
    if (Suffix < Tokens.size() && Tokens[Suffix].Text == "[") {
      Block.IsArray = true;
      if (Suffix + 1 < Tokens.size()) {
        char *End = nullptr;
        unsigned long Count = std::strtoul(Tokens[Suffix + 1].Text.c_str(),
                                          &End, 10);
        if (End && *End == 0 && Count > 0 && Count <= UINT32_MAX)
          Block.ArrayCount = (uint32_t)Count;
      }
    }
    if (!Block.HasInstance) {
      for (const auto &Member : Block.Members)
        Stage.Symbols.push_back({Member.Name, Block.Name});
    }
    Stage.Blocks.push_back(std::move(Block));
    Index = Close;
  }
  return Stage;
}

bool SameBlockMembers(const std::vector<BlockMember> &Left,
                      const std::vector<BlockMember> &Right) {
  if (Left.size() != Right.size()) return false;
  for (size_t Index = 0; Index < Left.size(); ++Index) {
    const auto &A = Left[Index];
    const auto &B = Right[Index];
    if (A.Name != B.Name || A.Type != B.Type || A.Count != B.Count ||
        A.Array != B.Array || A.RowMajor != B.RowMajor ||
        A.ExplicitMajor != B.ExplicitMajor)
      return false;
  }
  return true;
}

}

bool GX2GLValidateUniformBlockLinks(const char *VertexSource,
                                  const char *FragmentSource, char *Error,
                                  size_t ErrorCapacity) {
  if (!VertexSource || !FragmentSource) return true;
  const auto Vertex = CollectLinkUniforms(VertexSource);
  const auto Fragment = CollectLinkUniforms(FragmentSource);
  for (const auto &A : Vertex.Blocks) {
    for (const auto &B : Fragment.Blocks) {
      if (A.Name != B.Name) continue;
      if (A.HasInstance != B.HasInstance || A.IsArray != B.IsArray ||
          (A.IsArray && A.ArrayCount != B.ArrayCount) ||
          !SameBlockMembers(A.Members, B.Members)) {
        if (Error && ErrorCapacity)
          std::snprintf(Error, ErrorCapacity,
                        "Uniform block '%s' differs between shader stages.",
                        A.Name.c_str());
        return false;
      }
    }
  }
  for (const auto &A : Vertex.Symbols) {
    for (const auto &B : Fragment.Symbols) {
      if (A.Name != B.Name) continue;
      if (A.Owner == B.Owner) continue;
      if (Error && ErrorCapacity)
        std::snprintf(Error, ErrorCapacity,
                      "Uniform '%s' belongs to incompatible shader blocks.",
                      A.Name.c_str());
      return false;
    }
  }
  return true;
}
