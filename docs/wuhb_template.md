# WUHB CafeGLSL Template

gx2gl now packages a `.wuhb` for [gl33_test.rpx](gx2gl/build/gl33_test.rpx) with `wuhbtool`, and it exposes the same pattern as a reusable template for other Wii U homebrew apps.

## Template Layout

The starter content tree lives at [templates/wuhb/cafeglsl/content].

To embed the runtime compiler, place:

```text
glslcompiler.rpl
```

at the root of that content folder, or pass an external path through `GX2GL_CAFEGLSL_RPL`.

## CMake Pattern

Use the stock `wut` helpers so the app keeps building a normal `.rpx` and a bundled `.wuhb`:

```cmake
add_executable(my_app source/main.cpp)
target_link_libraries(my_app PRIVATE gx2gl33 wut)

wut_create_rpx(my_app)
wut_create_wuhb(my_app
    CONTENT "${CMAKE_SOURCE_DIR}/wuhb/content"
    NAME "My App"
    SHORTNAME "MyApp"
    AUTHOR "Your Name"
)
```

The content directory should include `glslcompiler.rpl` at its root:

```text
wuhb/content/glslcompiler.rpl
```

## gx2gl-Specific

gx2gl’s own test build supports:

- `GX2GL_WUHB_CONTENT_TEMPLATE_DIR`
  - source content tree that gets staged before `wuhbtool` runs
- `GX2GL_WUHB_CONTENT_STAGING_DIR`
  - generated content tree passed to `wuhbtool`
- `GX2GL_CAFEGLSL_RPL`
  - optional external `glslcompiler.rpl` copied into the staged content root

Examples:

```powershell
$env:GX2GL_CAFEGLSL_RPL = 'C:/path/to/glslcompiler.rpl'
python compile.py
```

```powershell
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE=C:/devkitPro/cmake/WiiU.cmake `
  -DGX2GL_CAFEGLSL_RPL=C:/path/to/glslcompiler.rpl
```

## Runtime Lookup

gx2gl’s runtime loader in [gx2gl_cafeglsl.cpp](/C:/Users/CTE%20USER/Documents/gx2gl/src/core/gx2gl_cafeglsl.cpp) now probes both module-style and packaged-content-style names, including:

- `glslcompiler`
- `glslcompiler.rpl`
- `/vol/content/glslcompiler.rpl`

That keeps the normal installed-library path working while also supporting a `wuhbtool`-bundled compiler.
