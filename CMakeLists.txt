cmake_minimum_required(VERSION 3.10)
project(gl33_gx2 C CXX)

# Enforce Wii U Toolchain
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    message(FATAL_ERROR "Please specify -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/WiiU.cmake")
endif()

# PowerPC Espresso flags
set(COMMON_FLAGS "-mcpu=750 -meabi -mhard-float")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_FLAGS}")

# Project Structure
include_directories(include)
include_directories(src)
include_directories($ENV{DEVKITPRO}/wut/include)

# Source files
set(SOURCES
    src/core/gl_context.cpp
    src/core/gl_buffer.cpp
    src/core/gl_texture.cpp
    src/core/gl_shader.cpp
    src/core/gl_vao.cpp
    src/core/gl_framebuffer.cpp
    src/core/gl_draw.cpp
    src/core/gl_get.cpp
    src/state/gl_state.cpp
    src/mem/gl_mem.cpp
)

# Build library
add_library(gl33_gx2_core STATIC ${SOURCES})

# Build RPX
add_executable(gl33_test tests/test_main.cpp tests/gles2_compile.cpp)
target_link_libraries(gl33_test gl33_gx2_core wut)

# Output will be gl33_test ELF format, converted via elf2rpl in script or post-build
# wiiu_add_rpx(gl33_test)
