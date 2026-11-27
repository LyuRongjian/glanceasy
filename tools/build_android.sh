#!/bin/bash
# Copyright 2026 The Glanceasy Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Cross-compiles the library and the unit test binary for ARMv7-A with NEON
# using the Android NDK, producing statically linked ELF executables that can be
# run under qemu-arm-static (see run_qemu_arm.sh).
#
# Usage: tools/build_android.sh [build-directory]
# The NDK is taken from $ANDROID_NDK_ROOT, defaulting to ~/android-ndk-r27c.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-/home/lrj/build/glanceasy-arm}"
ndk="${ANDROID_NDK_ROOT:-$HOME/android-ndk-r27c}"

if [[ ! -d "$ndk" ]]; then
  echo "Android NDK not found at $ndk; set ANDROID_NDK_ROOT." >&2
  exit 1
fi

# The RV1106 uses a single Cortex-A7 with NEON and VFPv4. Tuning for that core
# keeps the generated code valid on the whole ARMv7-A family; softfp preserves
# compatibility with the Android armeabi-v7a ABI.
cmake -S "$repo_root" -B "$build_dir" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=armeabi-v7a \
  -DANDROID_PLATFORM=android-21 \
  -DANDROID_STL=c++_static \
  -DCMAKE_BUILD_TYPE=Release \
  -DGLANCEASY_BUILD_TESTS=ON \
  -DCMAKE_C_FLAGS="-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=softfp" \
  -DCMAKE_CXX_FLAGS="-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=softfp" \
  -DCMAKE_EXE_LINKER_FLAGS="-static"

cmake --build "$build_dir"

for binary in "$build_dir/glanceasy_unittests" "$build_dir/libglanceasy.a"; do
  if [[ -f "$binary" ]]; then
    file "$binary"
  fi
done

echo "ARM32 build complete: $build_dir"
