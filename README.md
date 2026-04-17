# gx2gl - Translate OpenGL calls to GX2 calls

Work-In-Progress OpenGL implementation for the Nintendo Wii U. 

inspired by https://github.com/snickerbockers/gx2gl (we take some pointers from snickerbockers)

CafeGLSL: https://github.com/Exzap/CafeGLSL

wut: https://github.com/devkitPro/wut/

"Because ANGLE wasn't enough." - siahisaforker, February 12, 2026

- To clarify, that date was the first day I accomplished a single passing GL function

# Progress:

Only missing functions for 3.3 Core are these:

glBeginTransformFeedback glEndTransformFeedback glPauseTransformFeedback glResumeTransformFeedback glShaderBinary glReleaseShaderCompiler glProgramParameteri glGetTexImage glGetCompressedTexImage glGetBufferSubData glQueryCounter glGetQueryObjecti64v glTexImage2DMultisample glTexImage3DMultisample glSampleMaski glProvokingVertex glPointParameterf glPointParameterfv glPointParameteri glPointParameteriv glPixelStoref glFramebufferTexture1D glFramebufferTexture3D glFramebufferTextureLayer glGetBooleani_v glGetSamplerParameterIiv glGetSamplerParameterIuiv glGetTexParameterIiv glGetTexParameterIuiv glGetVertexAttribdv glSamplerParameterIiv glSamplerParameterIuiv glTexParameterIiv glTexParameterIuiv glFenceSync glWaitSync glClientWaitSync glDeleteSync glGetSynciv glTexBuffer glSampleCoverage glHint glGetShaderPrecisionFormat

They require complex memory mapping or just cannot be mapped to GX2 at all.

# Please read!!

!!This project is being used as a learning device. It is NOT production ready, and by using it you acknowledge that it is NOT accurate and will not be for a long time.
