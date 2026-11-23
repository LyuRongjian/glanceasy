#!/bin/bash
# Copyright 2026 The ZCat-GLES Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Cross-compiles the library and test binaries for the Rockchip RV1106G3
# (Cortex-A7, ARMv7-A, NEON-VFPv4) with the Luckfox Pico SDK toolchain.
# The output is a statically linked ARM32 ELF that can be run with
# qemu-arm-static -cpu cortex-a7.
#
# Usage: tools/build_luckfox_arm.sh [build-directory]
#
# LUCKFOX_TOOLCHAIN may point directly at the toolchain directory:
#   <SDK>/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf
# LUCKFOX_SDK is used as a fallback, defaulting to ~/luckfox-pico.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-$HOME/build/zcat-gles-rv1106}"
sdk="${LUCKFOX_SDK:-$HOME/luckfox-pico}"
toolchain="${LUCKFOX_TOOLCHAIN:-$sdk/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf}"

if [[ ! -x "$toolchain/bin/arm-rockchip830-linux-uclibcgnueabihf-gcc" ]]; then
  echo "Luckfox toolchain not found at $toolchain." >&2
  echo "Set LUCKFOX_TOOLCHAIN to the arm-rockchip830-linux-uclibcgnueabihf directory." >&2
  exit 1
fi

cmake -S "$repo_root" -B "$build_dir" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$repo_root/tools/luckfox_rv1106.cmake" \
  -DLUCKFOX_TOOLCHAIN="$toolchain" \
  -DCMAKE_BUILD_TYPE=Release \
  -DZGL_BUILD_TESTS=ON

cmake --build "$build_dir"

for binary in "$build_dir/zgl_unittests" "$build_dir/zgl_visual_tests" \
              "$build_dir/zgl_cube_demo"; do
  if [[ -f "$binary" ]]; then
    file "$binary"
  fi
done

echo "RV1106 ARM32 build complete: $build_dir"
