// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <string.h>

#include "src/zgl_simd.h"
#include "tests/zgl_test.h"

using namespace zgl::simd;

TEST(SimdDotAndScaleMatchScalar) {
  float a[16];
  float b[16];
  for (int i = 0; i < 16; ++i) {
    a[i] = static_cast<float>(i + 1) * 0.25f;
    b[i] = static_cast<float>(16 - i) * 0.5f;
  }
  for (int count = 1; count <= 16; ++count) {
    float expected_dot = 0.0f;
    for (int i = 0; i < count; ++i)
      expected_dot += a[i] * b[i];
    EXPECT_NEAR(Dot(a, b, count), expected_dot, 1.0e-5f);

    float scaled[16] = {};
    Scale(a, 2.5f, scaled, count);
    for (int i = 0; i < count; ++i)
      EXPECT_NEAR(scaled[i], a[i] * 2.5f, 1.0e-6f);
  }
}

TEST(SimdFillsWriteEveryElement) {
  uint8_t rgba[4 * 7];
  FillRgba(rgba, 7, 0x44332211u);
  for (int i = 0; i < 7; ++i) {
    uint32_t value;
    memcpy(&value, rgba + i * 4, sizeof(value));
    EXPECT_EQ(value, 0x44332211u);
  }

  uint16_t depth[13];
  FillU16(depth, 13, 0xBEEFu);
  for (int i = 0; i < 13; ++i)
    EXPECT_EQ(depth[i], 0xBEEFu);

  uint8_t stencil[37];
  FillU8(stencil, 37, 0x5Au);
  for (int i = 0; i < 37; ++i)
    EXPECT_EQ(stencil[i], 0x5Au);
}
