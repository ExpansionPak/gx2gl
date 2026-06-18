cmake_minimum_required(VERSION 3.20)
project(gx2gl_pc_runner C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(GENERATED_HEADER "${GENERATED_DIR}/desktop_gl_bindings.h")
set(GENERATED_SOURCE "${GENERATED_DIR}/desktop_gl_bindings.cpp")

add_custom_command(
    OUTPUT "${GENERATED_HEADER}" "${GENERATED_SOURCE}"
    COMMAND "${Python3_EXECUTABLE}"
            "${REPO_ROOT}/tools/generate_desktop_gl_bindings.py"
            --header "${REPO_ROOT}/include/gl/gl.h"
            --test-source "${REPO_ROOT}/tests/test_main.cpp"
            --test-source "${CMAKE_CURRENT_LIST_DIR}/test_main.cpp"
            --out-header "${GENERATED_HEADER}"
            --out-source "${GENERATED_SOURCE}"
    DEPENDS
        "${REPO_ROOT}/tools/generate_desktop_gl_bindings.py"
        "${REPO_ROOT}/include/gl/gl.h"
        "${REPO_ROOT}/tests/test_main.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/test_main.cpp"
)

add_executable(gx2gl_pc_runner
    test_main.cpp
    desktop_wgl_platform.cpp
    "${GENERATED_SOURCE}"
)

target_include_directories(gx2gl_pc_runner PRIVATE
    "${REPO_ROOT}/include"
    "${CMAKE_CURRENT_LIST_DIR}"
    "${GENERATED_DIR}"
)

target_link_libraries(gx2gl_pc_runner PRIVATE
    opengl32
    user32
    gdi32
)

add_test(NAME gx2gl_reference COMMAND gx2gl_pc_runner)
set_tests_properties(gx2gl_reference PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
