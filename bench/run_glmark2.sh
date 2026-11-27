#!/usr/bin/env bash
# Run glmark2-es2-null against a Glanceasy build that has GLANCEASY_BUILD_EGL=ON.
#
# Required:
#   GLMARK2_BIN          path to glmark2-es2-null
#   GLANCEASY_LIB_DIR    directory containing libEGL.so and libGLESv2.so
# Optional:
#   GLMARK2_DATA         glmark2 data directory (passed as --data-path)
#   SIZE                 WxH, default 800x600
#   BENCH                benchmark spec, default :duration=1
#   QEMU                 qemu-arm-static path; when set, QEMU_LD_PREFIX is required
#   QEMU_CPU             optional -cpu value, e.g. cortex-a7
set -euo pipefail

: "${GLMARK2_BIN:?set GLMARK2_BIN to glmark2-es2-null}"
: "${GLANCEASY_LIB_DIR:?set GLANCEASY_LIB_DIR to the build-egl directory}"
SIZE=${SIZE:-800x600}
BENCH=${BENCH:-:duration=1}

args=(--off-screen=3 --size="$SIZE" -b "$BENCH")
if [[ -n ${GLMARK2_DATA:-} ]]; then
  args=(--data-path "$GLMARK2_DATA" "${args[@]}")
fi

export LD_LIBRARY_PATH="$GLANCEASY_LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

if [[ -n ${QEMU:-} ]]; then
  : "${QEMU_LD_PREFIX:?set QEMU_LD_PREFIX to the ARM sysroot when using QEMU}"
  qemu_args=()
  if [[ -n ${QEMU_CPU:-} ]]; then
    qemu_args=(-cpu "$QEMU_CPU")
  fi
  exec "$QEMU" "${qemu_args[@]}" "$GLMARK2_BIN" "${args[@]}"
fi

exec "$GLMARK2_BIN" "${args[@]}"
