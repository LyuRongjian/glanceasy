# Copyright 2026 The ZCat-GLES Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.16)
project(zcat_gles VERSION 0.1.0 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ZGL_COVERAGE "Instrument the library for gcov coverage" OFF)
option(ZGL_BUILD_TESTS "Build the unit test binary" ON)
option(ZGL_BUILD_EXAMPLES "Build the example programs" ON)
option(ZGL_ENABLE_JIT "Compile GLSL shaders to native code with SLJIT" ON)

# SLJIT is a small, platform-independent JIT backend.  It is vendored under
# deps/sljit so that builds never fetch code from the network.  Only
# sljitLir.c is compiled; it includes the native backend for the target CPU.
add_library(sljit STATIC
    deps/sljit/sljit_src/sljitLir.c)
target_include_directories(sljit PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/sljit/sljit_src)
set_target_properties(sljit PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
if(MSVC)
  target_compile_options(sljit PRIVATE /W0)
else()
  target_compile_options(sljit PRIVATE -w)
endif()

set(ZGL_SOURCES
    src/zgl_context.cpp
    src/zgl_raster.cpp
    src/zgl_texture.cpp
    src/zgl_draw.cpp
    src/zgl_api_common.cpp
    src/zgl_api_state.cpp
    src/zgl_api_buffer.cpp
    src/zgl_api_texture.cpp
    src/zgl_api_framebuffer.cpp
    src/zgl_api_program.cpp
    src/zgl_api_draw.cpp
    src/zgl_api_stubs.cpp
    src/glsl/glsl_types.cpp
    src/glsl/glsl_lexer.cpp
    src/glsl/glsl_parser.cpp
    src/glsl/glsl_program.cpp
    src/glsl/glsl_eval.cpp)
if(ZGL_ENABLE_JIT)
  list(APPEND ZGL_SOURCES src/glsl/glsl_jit.cpp)
endif()

add_library(zcat_gles STATIC ${ZGL_SOURCES})
target_include_directories(zcat_gles
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
  target_compile_options(zcat_gles PRIVATE /W4 /permissive- /GR-)
else()
  target_compile_options(zcat_gles PRIVATE
      -Wall -Wextra -fno-exceptions -fno-rtti -fvisibility=hidden)
endif()
if(ZGL_ENABLE_JIT)
  target_compile_definitions(zcat_gles PRIVATE ZGL_ENABLE_JIT=1)
  target_link_libraries(zcat_gles PRIVATE sljit)
endif()
# The public header uses the GL types, so consumers need the include path.
target_compile_features(zcat_gles PUBLIC cxx_std_17)
# The library is built as a static archive on every supported platform.
# KHRONOS_STATIC keeps the Khronos headers from declaring dllimport on MSVC.
target_compile_definitions(zcat_gles PUBLIC KHRONOS_STATIC)
set_target_properties(zcat_gles PROPERTIES OUTPUT_NAME zcat_gles)

if(ZGL_COVERAGE AND NOT MSVC)
  target_compile_options(zcat_gles PRIVATE --coverage -O0 -g)
  target_link_options(zcat_gles PUBLIC --coverage)
endif()

if(ZGL_BUILD_TESTS)
  add_executable(zgl_unittests
      tests/zgl_test_main.cpp
      tests/test_gl.cpp
      tests/test_smoke.cpp
      tests/test_buffers.cpp
      tests/test_draw.cpp
      tests/test_textures.cpp
      tests/test_framebuffer.cpp
      tests/test_state.cpp
      tests/test_program.cpp
      tests/test_glsl.cpp
      tests/test_objects.cpp
      tests/test_texture2.cpp
      tests/test_types.cpp
      tests/test_uniform.cpp
      tests/test_stubs.cpp
      tests/test_texture_unit.cpp
      tests/test_texture_objects.cpp
      tests/test_glsl_semantics.cpp
      tests/test_draw_api.cpp
      tests/test_framebuffer2.cpp
      tests/test_jit.cpp)
  target_link_libraries(zgl_unittests PRIVATE zcat_gles)
  target_include_directories(zgl_unittests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  if(ZGL_ENABLE_JIT)
    target_compile_definitions(zgl_unittests PRIVATE ZGL_ENABLE_JIT=1)
  endif()
  if(ZGL_COVERAGE AND NOT MSVC)
    target_compile_options(zgl_unittests PRIVATE --coverage -O0 -g)
    target_link_options(zgl_unittests PRIVATE --coverage)
  endif()

  add_executable(zgl_visual_tests
      tests/visual/visual_common.cpp
      tests/visual/visual_tests.cpp)
  target_link_libraries(zgl_visual_tests PRIVATE zcat_gles)
  target_include_directories(zgl_visual_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR})
  if(MSVC)
    target_compile_options(zgl_visual_tests PRIVATE /W4)
  else()
    target_compile_options(zgl_visual_tests PRIVATE -Wall -Wextra)
  endif()
endif()

if(ZGL_BUILD_EXAMPLES)
  add_executable(zgl_cube_demo examples/render_cube.cpp)
  target_link_libraries(zgl_cube_demo PRIVATE zcat_gles)
  if(MSVC)
    target_compile_options(zgl_cube_demo PRIVATE /W4)
  else()
    target_compile_options(zgl_cube_demo PRIVATE -Wall -Wextra)
  endif()
endif()
