# gx2gl

OpenGL 3.3 Core for Wii U using GX2.

## Building

Requires the devkitPro Wii U toolchain, CMake, and Ninja to function

```sh
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/WiiU.cmake"
cmake --build build -j2
```

Output: `build/libgl33_gx2_core.a`.

note: Repo is undergoing a huge rewrite, bare with me if your game doesn't work! Submit detailed issues if possible
