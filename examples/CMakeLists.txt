cmake_minimum_required(VERSION 3.13)
project(gx2gl_examples C CXX)

# Builds gx2gl and its examples as Wii U apps (.rpx and .wuhb). Configure with
# devkitPro's Wii U toolchain and a CafeGLSL build with geometry shader support:
#   cmake -S examples -B build-examples \
#       -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/WiiU.cmake \
#       -DGX2GL_CAFEGLSL_ROOT=<CafeGLSL checkout with build-cafe/>
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/.." gx2gl)

function(gx2gl_example target directory name short_name)
   add_executable(${target} ${directory}/main.cpp)
   set_target_properties(${target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
   target_link_libraries(${target} PRIVATE gl33_gx2_core)
   wut_create_rpx(${target})
   wut_create_wuhb(${target} NAME "${name}" SHORTNAME "${short_name}" AUTHOR "gx2gl")
endfunction()

gx2gl_example(gx2gl_gs_test gl-gs-test "gx2gl GS test" "gx2gl GS")
gx2gl_example(gx2gl_features_test gl-features-test "gx2gl feature test" "gx2gl features")
gx2gl_example(gx2gl_gs_demo gl-gs-demo "gx2gl GS demo" "GS demo")
gx2gl_example(gx2gl_fbo_test gl-fbo-test "gx2gl FBO test" "gx2gl FBO")
