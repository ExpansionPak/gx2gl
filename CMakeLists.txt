cmake_minimum_required(VERSION 3.10)
project(gl33_gx2 C CXX)

# Enforce Wii U Toolchain with an iron fist. No getting past this one
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

# Source files
set(SOURCES
    src/core/gl_context.cpp
    src/core/gl_buffer.cpp
    src/core/gl_texture.cpp
    src/core/gl_shader.cpp
    src/core/gx2gl_cafeglsl.cpp
    src/core/gx2gl_shader_compat.cpp
    src/core/gx2gl_proc.cpp
    src/core/gx2gl_sdl.cpp
    src/core/gl_vao.cpp
    src/core/gl_framebuffer.cpp
    src/core/gl_draw.cpp
    src/core/gl_get.cpp
    src/state/gl_state.cpp
    src/mem/gl_mem.cpp
)

set(GX2VK_SOURCES
    src/vk/gx2vk_core.cpp
    src/vk/gx2vk_memory.cpp
    src/vk/gx2vk_cmd.cpp
    src/vk/gx2vk_shader.cpp
    src/vk/gx2vk_pipeline.cpp
    src/vk/gx2vk_descriptor.cpp
)

# Build library
add_library(gl33_gx2_core STATIC ${SOURCES})
add_library(gx2vk_core STATIC ${GX2VK_SOURCES})

# Build RPX
add_executable(gl33_test tests/test_main.cpp tests/gles2_compile.cpp)
target_link_libraries(gl33_test gl33_gx2_core wut)
wut_create_rpx(gl33_test)

add_executable(gl33_compare tests/pc/test_main.cpp)
target_compile_definitions(gl33_compare PRIVATE GX2GL_COMPARE_WIIU=1)
target_link_libraries(gl33_compare gl33_gx2_core wut)
wut_create_rpx(gl33_compare)
if(COMMAND wut_create_wuhb)
    wut_create_wuhb(gl33_compare
        CONTENT "${GX2GL_WUHB_CONTENT_STAGING_DIR}"
        NAME "gx2gl Compare"
        SHORTNAME "gx2glcmp"
        AUTHOR "gx2gl"
    )
endif()

add_executable(gx2vk_test tests/vk13_smoke.cpp)
target_link_libraries(gx2vk_test gx2vk_core wut)
wut_create_rpx(gx2vk_test)
