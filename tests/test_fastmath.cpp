// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cmath>

#include "src/zgl_fastmath.h"
#include "tests/zgl_test.h"

using namespace zgl::fastmath;

TEST(FastSinCosStayWithinTolerance) {
  float worst_sin = 0.0f;
  float worst_cos = 0.0f;
  for (float x = -20.0f; x <= 20.0f; x += 0.01f) {
    worst_sin = std::max(worst_sin, std::fabs(Sin(x) - std::sin(x)));
    worst_cos = std::max(worst_cos, std::fabs(Cos(x) - std::cos(x)));
  }
  EXPECT_TRUE(worst_sin < 5.0e-4f);
  EXPECT_TRUE(worst_cos < 5.0e-4f);
}

TEST(FastAtanFamilyStayWithinTolerance) {
  float worst_atan = 0.0f;
  float worst_atan2 = 0.0f;
  float worst_asin = 0.0f;
  float worst_acos = 0.0f;
  for (float x = -10.0f; x <= 10.0f; x += 0.01f) {
    worst_atan = std::max(worst_atan, std::fabs(Atan(x) - std::atan(x)));
    for (float y = -10.0f; y <= 10.0f; y += 0.5f) {
      worst_atan2 =
          std::max(worst_atan2, std::fabs(Atan2(y, x) - std::atan2(y, x)));
    }
    if (x >= -1.0f && x <= 1.0f) {
      worst_asin = std::max(worst_asin, std::fabs(Asin(x) - std::asin(x)));
      worst_acos = std::max(worst_acos, std::fabs(Acos(x) - std::acos(x)));
    }
  }
  EXPECT_TRUE(worst_atan < 5.0e-5f);
  EXPECT_TRUE(worst_atan2 < 5.0e-5f);
  EXPECT_TRUE(worst_asin < 5.0e-5f);
  EXPECT_TRUE(worst_acos < 5.0e-5f);
}

TEST(FastExpLogStayWithinTolerance) {
  float worst_exp2 = 0.0f;
  float worst_log2 = 0.0f;
  float worst_pow = 0.0f;
  for (float x = -20.0f; x <= 20.0f; x += 0.01f) {
    const float expected = std::exp2(x);
    worst_exp2 = std::max(worst_exp2, std::fabs(Exp2(x) - expected) /
                                               std::max(1.0f, std::fabs(expected)));
  }
  for (float x = 1.0e-6f; x <= 1.0e6f; x *= 1.1f) {
    worst_log2 = std::max(worst_log2, std::fabs(Log2(x) - std::log2(x)));
  }
  for (float x = 0.01f; x <= 10.0f; x += 0.05f) {
    for (float y = -4.0f; y <= 4.0f; y += 0.25f) {
      const float expected = std::pow(x, y);
      worst_pow = std::max(worst_pow, std::fabs(Pow(x, y) - expected) /
                                            std::max(1.0f, std::fabs(expected)));
    }
  }
  EXPECT_TRUE(worst_exp2 < 5.0e-5f);
  EXPECT_TRUE(worst_log2 < 5.0e-5f);
  EXPECT_TRUE(worst_pow < 5.0e-4f);
}

TEST(FastMathBatchMatchesScalar) {
  float worst_sin = 0.0f;
  float worst_cos = 0.0f;
  for (float base = -6.0f; base <= 6.0f; base += 0.37f) {
    float input[4] = {base, base + 0.11f, base + 0.23f, base + 0.41f};
    float sin_batch[4];
    float cos_batch[4];
    Sin4(input, sin_batch);
    Cos4(input, cos_batch);
    for (int i = 0; i < 4; ++i) {
      worst_sin = std::max(worst_sin, std::fabs(sin_batch[i] - Sin(input[i])));
      worst_cos = std::max(worst_cos, std::fabs(cos_batch[i] - Cos(input[i])));
    }
  }
  EXPECT_TRUE(worst_sin < 1.0e-5f);
  EXPECT_TRUE(worst_cos < 1.0e-5f);
}

TEST(FastMathEdgeCases) {
  EXPECT_NEAR(Sin(0.0f), 0.0f, 1.0e-6f);
  EXPECT_NEAR(Cos(0.0f), 1.0f, 1.0e-6f);
  EXPECT_NEAR(Exp2(0.0f), 1.0f, 1.0e-6f);
  EXPECT_NEAR(Log2(1.0f), 0.0f, 1.0e-6f);
  EXPECT_NEAR(Log2(5.877471754e-39f), -127.0f, 0.05f);
  EXPECT_NEAR(Log2(1.401298464e-45f), -149.0f, 0.05f);
  EXPECT_TRUE(std::isinf(Log2(0.0f)));
  EXPECT_TRUE(std::isnan(Sqrt(-1.0f)));
  EXPECT_NEAR(Atan2(-0.0f, -1.0f), -3.14159265358979323846f, 1.0e-6f);
  EXPECT_NEAR(Sqrt(16.0f), 4.0f, 1.0e-6f);
  EXPECT_NEAR(InverseSqrt(4.0f), 0.5f, 1.0e-5f);
  EXPECT_NEAR(Pow(2.0f, 3.0f), 8.0f, 1.0e-5f);
  EXPECT_TRUE(Exp2(-149.0f) > 0.0f);
  EXPECT_TRUE(Exp2(-150.0f) == 0.0f);
  EXPECT_TRUE(Pow(0.0f, 0.0f) == 1.0f);
  EXPECT_TRUE(std::isnan(Pow(-2.0f, 3.0f)));
}
