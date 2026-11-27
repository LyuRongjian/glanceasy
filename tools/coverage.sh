#!/usr/bin/env bash
# Copyright 2026 The Glanceasy Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Builds the library with gcov instrumentation, runs the unit tests and emits a
# per-file line coverage summary.  Requires CMake, Ninja and gcov.
#
# Usage:
#   tools/coverage.sh [build-directory]
#
# The report is written to <build-directory>/coverage/ as summary.txt plus one
# annotated .gcov file per source, so the output can be archived as a CI
# artifact.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-${repo_root}/out/coverage}"

cmake -S "${repo_root}" -B "${build_dir}" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DGLANCEASY_COVERAGE=ON \
  -DGLANCEASY_BUILD_TESTS=ON > /dev/null
cmake --build "${build_dir}" > /dev/null

"${build_dir}/glanceasy_unittests"

report_dir="${build_dir}/coverage"
rm -rf "${report_dir}"
mkdir -p "${report_dir}"
summary="${report_dir}/summary.txt"
: > "${summary}"

total_lines=0
total_hit=0

for gcda in $(find "${build_dir}/CMakeFiles/glanceasy.dir" -name '*.gcda' | sort); do
  object_dir="$(dirname "${gcda}")"
  base="$(basename "${gcda}" .gcda)"
  raw="$(cd "${object_dir}" && gcov -b -c "${base}.gcda" 2>/dev/null)"
  percent="$(printf '%s\n' "${raw}" | awk -v want="${base}" '
    /^File / { file = $0; sub(/^File /, "", file); gsub(/\047/, "", file) }
    /Lines executed:/ && file ~ ("/" want "$") { print $0; exit }')"
  [ -z "${percent}" ] && continue
  pct="$(printf '%s' "${percent}" | sed -E 's/.*Lines executed:([0-9.]+)% of ([0-9]+).*/\1/')"
  count="$(printf '%s' "${percent}" | sed -E 's/.*executed:[0-9.]+% of ([0-9]+).*/\1/')"
  hit="$(awk -v p="${pct}" -v n="${count}" 'BEGIN { printf "%d", (p * n / 100.0) + 0.5 }')"
  total_lines=$((total_lines + count))
  total_hit=$((total_hit + hit))
  printf '%6s%% %6s/%-6s %s\n' "${pct}" "${hit}" "${count}" "${base}" >> "${summary}"
  mv "${object_dir}/${base}.gcov" "${report_dir}/" 2>/dev/null || true
  rm -f "${object_dir}"/*.gcov
done

if [ "${total_lines}" -eq 0 ]; then
  echo "No coverage data was produced." >&2
  exit 1
fi

overall="$(awk -v h="${total_hit}" -v n="${total_lines}" \
  'BEGIN { printf "%.1f", (100.0 * h) / n }')"
printf '\nTOTAL %s%% %s/%s\n' "${overall}" "${total_hit}" "${total_lines}" >> "${summary}"
cat "${summary}"
