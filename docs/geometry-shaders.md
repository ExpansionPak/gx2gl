# Geometry shaders in gx2gl

gx2gl supports `GL_GEOMETRY_SHADER` on the Wii U. Shaders are compiled on the
console at run time by [CafeGLSL](https://github.com/Exzap/CafeGLSL), which
needs its geometry shader support (`CompileGeometryShaderSet`) for this.

**Status: experimental.** Everything in the "Works" table has been run on real
hardware, but only with small test shaders and demos. Expect bugs with bigger
shaders, and please report them with the log your app writes.

## Getting started

### With the prebuilt SDK

Download the `gx2gl-sdk` archive (from the Releases page, or the
`gx2gl-sdk` artifact of the latest CI build). It contains:

```
include/            gx2gl headers: <gl/gl.h>, <gx2gl/present.h>, ...
lib/                libgl33_gx2_core.a (gx2gl) and libcafeglsl.a (CafeGLSL)
template/           a minimal app with a geometry shader
docs/               this guide
```

You need devkitPro with the Wii U toolchain (`wut`) and CMake. Then:

```sh
cd template
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/WiiU.cmake
cmake --build build
```

Copy `build/my_gx2gl_app.wuhb` to `sd:/wiiu/apps/` and start it from the Aroma
menu, or use the `.rpx` from the Homebrew Launcher.

### From source

Build CafeGLSL's Wii U library (see its README), then point gx2gl at the
checkout:

```sh
cmake -S . -B build -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/WiiU.cmake \
    -DGX2GL_CAFEGLSL_ROOT=/path/to/CafeGLSL     # with build-cafe/ inside
cmake --build build
```

`examples/CMakeLists.txt` builds gx2gl and the example apps the same way.

## Writing geometry shaders

Use them as on desktop GL 3.3:

```c
GLuint program = glCreateProgram();
glAttachShader(program, vertex_shader);
glAttachShader(program, geometry_shader);   // glCreateShader(GL_GEOMETRY_SHADER)
glAttachShader(program, fragment_shader);
glLinkProgram(program);
```

Things to know:

- **The three stages are compiled together, at `glLinkProgram`**, because on
  the Wii U the vertex shader has to write its outputs in the layout the
  geometry shader reads. `glCompileShader` still reports geometry shader
  errors, but linking takes longer than for a program without one. Link your
  programs at load time, not while drawing.
- **Uniforms** work in every stage. A uniform declared in both the vertex and
  geometry shader gets one value, as in GL. Uniform buffers (`uniform Block { ... };`)
  and samplers work in the geometry shader too.
- **Passing data from the vertex shader:** plain `out`/`in` variables
  (`in vec3 vNormal[];`) and interface blocks (`out VS_OUT { ... } vs_out;`)
  both work.
- **`max_vertices`:** keep it as low as the shader allows. The Wii U reserves
  ring buffer memory for every vertex a geometry shader can emit.
- Adjacency primitives (`GL_TRIANGLES_ADJACENCY`, ...) go straight to the
  geometry shader.

## What works

| Works (run on hardware) | Not tested on hardware yet | Not supported |
|---|---|---|
| `points` and `triangles` input | `lines`, `triangles_adjacency`, `lines_adjacency` input | transform feedback with a geometry shader |
| `triangle_strip` output | `points` and `line_strip` output | `gl_PrimitiveIDIn`, `invocations` > 1 |
| loose uniforms in one or both stages | `mat4` uniforms shared by VS and GS | `EmitStreamVertex` / multiple streams |
| uniform buffers in the geometry shader | textures in the geometry shader | |
| | interface blocks between stages | |
| | large shaders, many outputs, high `max_vertices`; performance | |

## When something goes wrong

- Check `GL_COMPILE_STATUS` and `GL_LINK_STATUS` and print the info logs:
  CafeGLSL's messages say which stage failed.
- Flicker or geometry in the wrong place that goes away when you call
  `glFinish()` after every draw is a timing problem (the CPU changing memory
  the GPU still reads). Please report it with a minimal example.
- If the console freezes, write a log to the SD card and reopen the file for
  every line (see `examples/common/example_util.h`), so you can see the last
  thing that ran.

## Examples

- `examples/gl-gs-test`: one feature per panel, from a plain geometry shader to
  shared uniforms and uniform buffers. Press A for "serialized" mode.
- `examples/gl-features-test`: textures in the geometry shader, lines,
  adjacency, interface blocks, matrices, and uniform buffer slots.
- `examples/gl-gs-demo`: an exploding torus with its normals drawn as lines,
  after the geometry shader chapter of LearnOpenGL.
