Differences between using WHBGfx vs GX2GL api:

C

 // raw WHB
WHBGfxLoadGFDShaderGroup(&group, 0, gfd_blob);
// you manage GX2 + free group yourself

// hybrid
WHBGfxLoadGFDShaderGroup(&group, 0, gfd_blob);
glGX2GLLoadShaderGroup(program, &group);
// gx2gl uses it, but you still own/free group

// pure convenience
glGX2GLLoadShaderGroupGFD(program, 0, gfd_blob);
// gx2gl loads it and owns/free it


TLDR: Use gx2gl api if you want easier gfd shader loading.

Simple WHBGfx presentation path:

```c
GX2GL_Context ctx = GX2GL_CreateContext();

while (WHBProcIsRunning()) {
    /* issue OpenGL draws to the default framebuffer */
    GX2GL_Present();
}

GX2GL_DeleteContext(ctx);
```

`GX2GL_Present()` uses WHBGfx internally. By default it presents the TV target
and mirrors that image to the GamePad/DRC. Use `GX2GL_SetAutomaticDRCEnabled(0)`
to disable the mirror, or `GX2GL_SetDefaultFramebufferTargetDRC(1)` to draw the
default framebuffer directly to the GamePad instead.
