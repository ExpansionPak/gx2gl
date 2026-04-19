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


TLDR: Use gx2gl api if you want easier gfd shader loading