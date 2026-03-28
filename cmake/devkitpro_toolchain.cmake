#!/usr/bin/env cmake
# Minimal devkitPPC / devkitPro toolchain template for CMake
# Adjust `DEVKITPPC_ROOT` or set environment toolchain vars as needed.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR powerpc)

# Toolchain binaries (default names for devkitPPC/devkitPro in WSL)
set(DEVKITPPC_CC "${DEVKITPPC_CC}" CACHE STRING "C compiler")
if(NOT DEVKITPPC_CC)
  set(DEVKITPPC_CC powerpc-eabi-gcc)
endif()
set(CMAKE_C_COMPILER ${DEVKITPPC_CC})

set(DEVKITPPC_AR "${DEVKITPPC_AR}" CACHE STRING "Archiver")
if(NOT DEVKITPPC_AR)
  set(DEVKITPPC_AR powerpc-eabi-ar)
endif()
set(CMAKE_AR ${DEVKITPPC_AR})

# Default flags (tweak for your SDK)
set(CMAKE_C_FLAGS "-O2 -mcpu=750 -mhard-float -fno-exceptions -fno-rtti")

# Users must point include/link paths for Wii U SDK and wut/GX2 libraries
# Example usage:
# cmake -DCMAKE_TOOLCHAIN_FILE=cmake/devkitpro_toolchain.cmake \
#       -DWUT_ROOT=/path/to/wut -DCMAKE_BUILD_TYPE=Release ..
