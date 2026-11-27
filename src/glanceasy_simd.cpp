// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/glanceasy_simd.h"

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace glanceasy {
namespace simd {

float Dot(const float* a, const float* b, int count) {
  if (count <= 0)
    return 0.0f;
  float sum = 0.0f;
  int i = 0;
#if defined(__ARM_NEON)
  float32x4_t lanes = vdupq_n_f32(0.0f);
  for (; i + 4 <= count; i += 4) {
    lanes = vaddq_f32(
        lanes, vmulq_f32(vld1q_f32(a + i), vld1q_f32(b + i)));
  }
  const float32x2_t pair =
      vadd_f32(vget_low_f32(lanes), vget_high_f32(lanes));
  sum = vget_lane_f32(vpadd_f32(pair, pair), 0);
#endif
  for (; i < count; ++i)
    sum += a[i] * b[i];
  return sum;
}

void Scale(const float* input, float scale, float* output, int count) {
  if (count <= 0)
    return;
  int i = 0;
#if defined(__ARM_NEON)
  const float32x4_t factor = vdupq_n_f32(scale);
  for (; i + 4 <= count; i += 4) {
    vst1q_f32(output + i, vmulq_f32(vld1q_f32(input + i), factor));
  }
#endif
  for (; i < count; ++i)
    output[i] = input[i] * scale;
}

void FillRgba(uint8_t* destination, size_t pixels, uint32_t value) {
  if (!destination || pixels == 0)
    return;
  uint32_t* out = reinterpret_cast<uint32_t*>(destination);
  size_t i = 0;
#if defined(__ARM_NEON)
  const uint32x4_t fill = vdupq_n_u32(value);
  for (; i + 4 <= pixels; i += 4)
    vst1q_u32(out + i, fill);
#endif
  for (; i < pixels; ++i)
    out[i] = value;
}

void FillU16(uint16_t* destination, size_t count, uint16_t value) {
  if (!destination || count == 0)
    return;
  size_t i = 0;
#if defined(__ARM_NEON)
  const uint16x8_t fill = vdupq_n_u16(value);
  for (; i + 8 <= count; i += 8)
    vst1q_u16(destination + i, fill);
#endif
  for (; i < count; ++i)
    destination[i] = value;
}

void FillU8(uint8_t* destination, size_t count, uint8_t value) {
  if (!destination || count == 0)
    return;
  size_t i = 0;
#if defined(__ARM_NEON)
  const uint8x16_t fill = vdupq_n_u8(value);
  for (; i + 16 <= count; i += 16)
    vst1q_u8(destination + i, fill);
#endif
  for (; i < count; ++i)
    destination[i] = value;
}

}  // namespace simd
}  // namespace glanceasy
