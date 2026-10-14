cmake_minimum_required(VERSION 3.10)
project(gl33_gx2 C CXX)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    message(FATAL_ERROR "Please specify -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/WiiU.cmake")
endif()

set(COMMON_FLAGS "-mcpu=750 -meabi -mhard-float")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_FLAGS}")

include_directories(include)
include_directories(src)

set(GX2GL_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(GX2GL_PROC_TABLE "${GX2GL_GENERATED_DIR}/gx2gl_proc_table.inc")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/gl/gl.h"
)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/gl/gl.h" GX2GL_GL_HEADER_LINES)
set(GX2GL_PROC_SYMBOLS)
foreach(GX2GL_LINE IN LISTS GX2GL_GL_HEADER_LINES)
    string(STRIP "${GX2GL_LINE}" GX2GL_LINE)
    if(NOT GX2GL_LINE MATCHES "\\);$")
        continue()
    endif()
    if(GX2GL_LINE MATCHES "typedef|\\(\\*")
        continue()
    endif()
    if(GX2GL_LINE MATCHES "(gl[A-Za-z0-9_]+)[ \t]*\\(")
        list(FIND GX2GL_PROC_SYMBOLS "${CMAKE_MATCH_1}" GX2GL_PROC_SYMBOL_INDEX)
        if(GX2GL_PROC_SYMBOL_INDEX EQUAL -1)
            list(APPEND GX2GL_PROC_SYMBOLS "${CMAKE_MATCH_1}")
        endif()
    endif()
endforeach()

list(LENGTH GX2GL_PROC_SYMBOLS GX2GL_PROC_SYMBOL_COUNT)
if(GX2GL_PROC_SYMBOL_COUNT LESS 300)
    message(FATAL_ERROR
        "gx2gl proc-table generation found only ${GX2GL_PROC_SYMBOL_COUNT} GL entrypoints; "
        "include/gl/gl.h parsing is broken")
endif()

file(MAKE_DIRECTORY "${GX2GL_GENERATED_DIR}")
set(GX2GL_PROC_TABLE_TEXT "\n")
string(APPEND GX2GL_PROC_TABLE_TEXT "static const GX2GLProcEntry kGX2GLCoreProcTable[] = {\n")
foreach(GX2GL_SYMBOL IN LISTS GX2GL_PROC_SYMBOLS)
    string(APPEND GX2GL_PROC_TABLE_TEXT "    {\"${GX2GL_SYMBOL}\", reinterpret_cast<GX2GL_Proc>(${GX2GL_SYMBOL})},\n")
endforeach()
string(APPEND GX2GL_PROC_TABLE_TEXT "};\n\n")
string(APPEND GX2GL_PROC_TABLE_TEXT "static const size_t kGX2GLCoreProcCount =\n")
string(APPEND GX2GL_PROC_TABLE_TEXT "    sizeof(kGX2GLCoreProcTable) / sizeof(kGX2GLCoreProcTable[0]);\n")
file(WRITE "${GX2GL_PROC_TABLE}" "${GX2GL_PROC_TABLE_TEXT}")

set(SOURCES
    src/core/gl_context.cpp
    src/core/gl_dispatch.cpp
    src/core/gl_entrypoints.cpp
    src/core/gl_buffer.cpp
    src/core/gl_buffer_compat.cpp
    src/core/gl_texture.cpp
    src/core/gl_shader.cpp
    src/core/gl_uniform_reflection.cpp
    src/core/gx2gl_cafeglsl.cpp
    src/core/gx2gl_shader_compat.cpp
    src/core/gx2gl_present.cpp
    src/core/gx2gl_proc.cpp
    src/core/gx2gl_sdl.cpp
    src/core/gl_vao.cpp
    src/core/gl_framebuffer.cpp
    src/core/gl_draw.cpp
    src/core/gl_query.cpp
    src/core/gl_transform_feedback.cpp
    src/core/gl_vertex_attrib.cpp
    src/core/gl_indexed_state.cpp
    src/core/gl_clear.cpp
    src/core/gl_program_interface.cpp
    src/core/gl_state_compat.cpp
    src/core/gl_texture_compat.cpp
    src/core/gl_framebuffer_compat.cpp
    src/core/gl_get.cpp
    src/state/gl_state.cpp
    src/mem/gl_mem.cpp
)

add_library(gl33_gx2_core STATIC ${SOURCES})
target_include_directories(gl33_gx2_core
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${GX2GL_GENERATED_DIR}"
)
target_link_libraries(gl33_gx2_core PUBLIC wut)

set(GX2GL_CAFEGLSL_ROOT "" CACHE PATH
    "Path to a CafeGLSL 0.3 checkout with build-cafe/cafecompiler/libcafeglsl.a")
if(GX2GL_CAFEGLSL_ROOT)
    get_filename_component(GX2GL_CAFEGLSL_ROOT "${GX2GL_CAFEGLSL_ROOT}" ABSOLUTE)
    set(GX2GL_CAFEGLSL_INCLUDE_DIR "${GX2GL_CAFEGLSL_ROOT}/cafecompiler")
    set(GX2GL_CAFEGLSL_LIBRARY
        "${GX2GL_CAFEGLSL_ROOT}/build-cafe/cafecompiler/libcafeglsl.a")

    if(NOT EXISTS "${GX2GL_CAFEGLSL_INCLUDE_DIR}/CafeGLSLCompiler.h")
        message(FATAL_ERROR
            "GX2GL_CAFEGLSL_ROOT does not contain cafecompiler/CafeGLSLCompiler.h")
    endif()
    if(NOT EXISTS "${GX2GL_CAFEGLSL_LIBRARY}")
        message(FATAL_ERROR
            "CafeGLSL static archive not found: ${GX2GL_CAFEGLSL_LIBRARY}")
    endif()

    target_compile_definitions(gl33_gx2_core PRIVATE GX2GL_CAFEGLSL_STATIC=1)
    set_property(SOURCE src/core/gx2gl_cafeglsl.cpp APPEND PROPERTY
        INCLUDE_DIRECTORIES "${GX2GL_CAFEGLSL_INCLUDE_DIR}")
    target_link_libraries(gl33_gx2_core PUBLIC "${GX2GL_CAFEGLSL_LIBRARY}" wut m)
    message(STATUS "gx2gl: using static CafeGLSL from ${GX2GL_CAFEGLSL_ROOT}")
endif()

add_library(gx2gl33 ALIAS gl33_gx2_core)
