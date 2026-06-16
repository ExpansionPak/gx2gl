# gx2gl - Translate OpenGL calls to GX2 calls

Work-In-Progress OpenGL implementation for the Nintendo Wii U. 

Inspired by VitaGL and my thirst for Sonic 3 A.I.R. on the Wii U

https://github.com/rinnegatamante/vitagl

CafeGLSL: https://github.com/Exzap/CafeGLSL

wut: https://github.com/devkitPro/wut/

"Because ANGLE wasn't enough." - siahisaforker, February 12, 2026  

-# even though angle is something completely different anyway..

# Please read!!

!!This project is being used as a learning device. It is NOT production ready, and by using it you acknowledge that it is NOT accurate and will not be for a long time.

# gx2vk

The current `gx2vk` slice is intentionally narrow:

- instance / physical device / logical device creation

- Vulkan 1.3 feature advertisement for `dynamicRendering`, `synchronization2`, and `maintenance4`

- queue, command-pool, and command-buffer scaffolding

- `vkCmdBeginRendering` / `vkCmdEndRendering`

- `vkCmdPipelineBarrier2`

- `vkGetDeviceBufferMemoryRequirements`

`gx2vk_test.rpx` is a loader check, not a renderer yet. It should hold:

- green on success

- red on failure

Don't use gx2vk. I'm warning you.

(unless you wanna contribute)

## Performance

This has only really been tested with Sonic 3 A.I.R. so far, which is kind of a worst-case stress test and not the best performance benchmark.

If you need maximum performance, interface with GX2 directly. If you want to skip the absolute horror of GX2 and actually get something rendering first, this is a good start.
