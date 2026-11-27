# Copyright 2026 The Glanceasy Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.16)
project(glanceasy VERSION 0.1.0 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GLANCEASY_COVERAGE "Instrument the library for gcov coverage" OFF)
option(GLANCEASY_BUILD_TESTS "Build the unit test binary" ON)
option(GLANCEASY_BUILD_EXAMPLES "Build the example programs" ON)
option(GLANCEASY_ENABLE_JIT "Compile GLSL shaders to native code with SLJIT" ON)

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

set(GLANCEASY_SOURCES
    src/glanceasy_context.cpp
    src/glanceasy_raster.cpp
    src/glanceasy_texture.cpp
    src/glanceasy_texture_compressed.cpp
    src/glanceasy_draw.cpp
    src/glanceasy_fastmath.cpp
    src/glanceasy_simd.cpp
    src/glanceasy_api_common.cpp
    src/glanceasy_api_state.cpp
    src/glanceasy_api_buffer.cpp
    src/glanceasy_api_texture.cpp
    src/glanceasy_api_texture3d.cpp
    src/glanceasy_api_framebuffer.cpp
    src/glanceasy_api_program.cpp
    src/glanceasy_api_draw.cpp
    src/glanceasy_api_stubs.cpp
    src/glsl/glsl_types.cpp
    src/glsl/glsl_lexer.cpp
    src/glsl/glsl_parser.cpp
    src/glsl/glsl_program.cpp
    src/glsl/glsl_eval.cpp)
if(GLANCEASY_ENABLE_JIT)
  list(APPEND GLANCEASY_SOURCES src/glsl/glsl_jit.cpp)
endif()

add_library(glanceasy STATIC ${GLANCEASY_SOURCES})
target_include_directories(glanceasy
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
  target_compile_options(glanceasy PRIVATE /W4 /permissive- /GR-)
else()
  target_compile_options(glanceasy PRIVATE
      -Wall -Wextra -fno-exceptions -fno-rtti -fvisibility=hidden)
endif()
if(GLANCEASY_ENABLE_JIT)
  target_compile_definitions(glanceasy PRIVATE GLANCEASY_ENABLE_JIT=1)
  target_link_libraries(glanceasy PRIVATE sljit)
endif()
# The public header uses the GL types, so consumers need the include path.
target_compile_features(glanceasy PUBLIC cxx_std_17)
# The library is built as a static archive on every supported platform.
# KHRONOS_STATIC keeps the Khronos headers from declaring dllimport on MSVC.
target_compile_definitions(glanceasy PUBLIC KHRONOS_STATIC)
set_target_properties(glanceasy PROPERTIES OUTPUT_NAME glanceasy)

if(GLANCEASY_COVERAGE AND NOT MSVC)
  target_compile_options(glanceasy PRIVATE --coverage -O0 -g)
  target_link_options(glanceasy PUBLIC --coverage)
endif()

if(GLANCEASY_BUILD_TESTS)
  add_executable(glanceasy_unittests
      tests/glanceasy_test_main.cpp
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
      tests/test_jit.cpp
      tests/test_fastmath.cpp
      tests/test_simd.cpp
      tests/test_uniform_block.cpp
      tests/test_transform_feedback.cpp)
  target_link_libraries(glanceasy_unittests PRIVATE glanceasy)
  target_include_directories(glanceasy_unittests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  if(GLANCEASY_ENABLE_JIT)
    target_compile_definitions(glanceasy_unittests PRIVATE GLANCEASY_ENABLE_JIT=1)
  endif()
  if(GLANCEASY_COVERAGE AND NOT MSVC)
    target_compile_options(glanceasy_unittests PRIVATE --coverage -O0 -g)
    target_link_options(glanceasy_unittests PRIVATE --coverage)
  endif()

  add_executable(glanceasy_visual_tests
      tests/visual/visual_common.cpp
      tests/visual/visual_tests.cpp)
  target_link_libraries(glanceasy_visual_tests PRIVATE glanceasy)
  target_include_directories(glanceasy_visual_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR})
  if(MSVC)
    target_compile_options(glanceasy_visual_tests PRIVATE /W4)
  else()
    target_compile_options(glanceasy_visual_tests PRIVATE -Wall -Wextra)
  endif()
endif()

if(GLANCEASY_BUILD_EXAMPLES)
  add_executable(glanceasy_cube_demo examples/render_cube.cpp)
  target_link_libraries(glanceasy_cube_demo PRIVATE glanceasy)
  if(MSVC)
    target_compile_options(glanceasy_cube_demo PRIVATE /W4)
  else()
    target_compile_options(glanceasy_cube_demo PRIVATE -Wall -Wextra)
  endif()
endif()
