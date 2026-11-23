#!/bin/bash
# Copyright 2026 The ZCat-GLES Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Runs a 32-bit ARM executable under qemu-user with the Cortex-A7 CPU model.
# This is the validation path for the RV1106G3 target; the executable is
# expected to come from tools/build_luckfox_arm.sh.
#
# Static bionic binaries built with the Android NDK need a 32-byte aligned TLS
# segment, which the NDK linker does not always provide.  Set ZGL_PATCH_TLS=1
# for those binaries; the Luckfox uClibc toolchain does not need the patch.
#
# Usage: tools/run_qemu_arm.sh <executable> [arguments...]
#
# QEMU_ARM_STATIC selects the emulator.  If unset, qemu-arm-static is looked up
# in PATH, then in the historical ~/qemu-local/usr/bin location.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="${1:-}"
if [[ -z "$binary" ]]; then
  echo "usage: $0 <executable> [arguments...]" >&2
  exit 2
fi
shift

qemu="${QEMU_ARM_STATIC:-}"
if [[ -z "$qemu" ]]; then
  qemu="$(command -v qemu-arm-static || true)"
fi
if [[ -z "$qemu" && -x "$HOME/qemu-local/usr/bin/qemu-arm-static" ]]; then
  qemu="$HOME/qemu-local/usr/bin/qemu-arm-static"
fi
if [[ ! -x "$qemu" ]]; then
  echo "qemu-arm-static not found; set QEMU_ARM_STATIC." >&2
  exit 1
fi

if [[ "${ZGL_PATCH_TLS:-0}" == "1" ]]; then
  python3 "$repo_root/tools/fix_tls_align.py" "$binary"
fi

exec "$qemu" -cpu cortex-a7 "$binary" "$@"
