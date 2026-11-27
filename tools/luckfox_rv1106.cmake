# Copyright 2026 The Glanceasy Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# CMake toolchain for the Rockchip RV1106G3 (Cortex-A7, ARMv7-A, NEON-VFPv4)
# using the cross compiler shipped by the Luckfox Pico SDK.  The SDK toolchain
# is a uClibc hard-float toolchain, so the resulting executables are static
# ARM32 ELF binaries that run under qemu-arm-static -cpu cortex-a7.
#
# Usage:
#   cmake -S . -B build-rv1106 -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=tools/luckfox_rv1106.cmake \
#     -DLUCKFOX_TOOLCHAIN=/path/to/arm-rockchip830-linux-uclibcgnueabihf

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(NOT LUCKFOX_TOOLCHAIN)
  if(DEFINED ENV{LUCKFOX_TOOLCHAIN})
    set(LUCKFOX_TOOLCHAIN "$ENV{LUCKFOX_TOOLCHAIN}" CACHE PATH
        "Luckfox RV1106 cross compiler directory")
  else()
    set(LUCKFOX_TOOLCHAIN
        "$ENV{HOME}/luckfox-pico/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf"
        CACHE PATH "Luckfox RV1106 cross compiler directory")
  endif()
endif()

set(_luckfox_prefix
    "${LUCKFOX_TOOLCHAIN}/bin/arm-rockchip830-linux-uclibcgnueabihf")
if(NOT EXISTS "${_luckfox_prefix}-gcc")
  message(FATAL_ERROR
          "Luckfox toolchain not found at ${LUCKFOX_TOOLCHAIN}. "
          "Set LUCKFOX_TOOLCHAIN to the arm-rockchip830-linux-uclibcgnueabihf "
          "directory from the Luckfox Pico SDK.")
endif()

set(CMAKE_C_COMPILER "${_luckfox_prefix}-gcc")
set(CMAKE_CXX_COMPILER "${_luckfox_prefix}-g++")
set(CMAKE_AR "${_luckfox_prefix}-ar")
set(CMAKE_RANLIB "${_luckfox_prefix}-ranlib")

# The RV1106G3 has one Cortex-A7 core with NEON and VFPv4.  The Luckfox
# userspace is hard-float, unlike the Android armeabi-v7a softfp ABI.
set(CMAKE_C_FLAGS_INIT
    "-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard")
set(CMAKE_CXX_FLAGS_INIT
    "-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")

set(CMAKE_FIND_ROOT_PATH "${LUCKFOX_TOOLCHAIN}/arm-rockchip830-linux-uclibcgnueabihf")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
