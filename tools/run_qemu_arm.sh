#!/bin/bash
# Copyright 2026 The ZCat-GLES Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Runs an ARM32 ELF executable produced by build_android.sh under qemu-arm-static.
# The executable is patched first because static bionic binaries need a 32-byte
# aligned TLS segment, which the NDK linker does not guarantee.
#
# Usage: tools/run_qemu_arm.sh <executable> [arguments...]

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
qemu="${QEMU_ARM_STATIC:-$HOME/qemu-local/usr/bin/qemu-arm-static}"
binary="$1"
shift

if [[ ! -x "$qemu" ]]; then
  echo "qemu-arm-static not found at $qemu; set QEMU_ARM_STATIC." >&2
  exit 1
fi

python3 "$repo_root/tools/fix_tls_align.py" "$binary"
exec "$qemu" -cpu cortex-a15 "$binary" "$@"
