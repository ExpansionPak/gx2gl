# gx2gl - Translate OpenGL calls to GX2 calls

Work-In-Progress OpenGL implementation for the Nintendo Wii U. 

Inspired by VitaGL and my thirst for Sonic 3 A.I.R. on the Wii U

https://github.com/rinnegatamante/vitagl

CafeGLSL: https://github.com/Exzap/CafeGLSL

wut: https://github.com/devkitPro/wut/

"Because ANGLE wasn't enough." - siahisaforker, February 12, 2026  

-# even though angle is something completely different anyway..

# Please read!!

This project is not production ready yet. The goal is OpenGL 3.3 core behavior on Wii U, but gx2gl will not advertise a Khronos GL version until mandatory systems are actually implemented.

## Testing

Build and run the desktop OpenGL 3.3 reference suite with:

```powershell
cmake -S tests/pc -B build_pc -G Ninja
cmake --build build_pc
ctest --test-dir build_pc --output-on-failure
```

The `gl33_compare` Wii U target runs the same cases through gx2gl. It writes
`gx2gl_results.txt`, `gx2gl_done.flag`, and the diagnostic framebuffer image
to the SD card root.

## Performance

This has only really been tested with Sonic 3 A.I.R. so far, which is kind of a worst-case stress test and not the best performance benchmark.

If you need maximum performance, interface with GX2 directly. If you want to skip the absolute horror of GX2 and actually get something rendering first, this is a good start.
