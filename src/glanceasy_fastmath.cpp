// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/glanceasy_fastmath.h"

#include <stdint.h>
#include <string.h>

#include <cmath>
#include <limits>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace glanceasy {
namespace fastmath {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kInvTwoPi = 0.15915494309189533577f;

// 1.5 * 2^23.  Adding and subtracting this rounds a float to the nearest
// integer for the range where the range-reduction quotient is representable.
constexpr float kRoundMagic = 12582912.0f;

float Nan() {
  return std::numeric_limits<float>::quiet_NaN();
}

float Inf() {
  return std::numeric_limits<float>::infinity();
}

// Range-reduces |x| to [-pi, pi].  This is exact enough for shader inputs;
// very large arguments fall back to libm in Sin()/Cos().
float ReduceTwoPi(float x) {
  const float rounded = x * kInvTwoPi + kRoundMagic;
  const float multiple = rounded - kRoundMagic;
  return x - multiple * kTwoPi;
}

float SinPolynomial(float x) {
  const float x2 = x * x;
  float p = 1.3697423152315939e-10f;
  p = -2.4752158430246845e-08f + x2 * p;
  p = 2.753825702281984e-06f + x2 * p;
  p = -0.00019840647498683763f + x2 * p;
  p = 0.008333323684270609f + x2 * p;
  p = -0.16666666108510464f + x2 * p;
  p = 0.9999999994768137f + x2 * p;
  return x * p;
}

float CosPolynomial(float x) {
  const float x2 = x * x;
  float p = 1.7369072458538055e-09f;
  p = -2.7113357448549664e-07f + x2 * p;
  p = 2.4773422692275223e-05f + x2 * p;
  p = -0.0013887970346310452f + x2 * p;
  p = 0.04166652435266188f + x2 * p;
  p = -0.4999999177190987f + x2 * p;
  p = 0.9999999922903373f + x2 * p;
  return p;
}

// atan(x) / x as a polynomial in x^2 on [0, 1].  The fit is a degree-7
// Chebyshev least-squares approximation with a maximum error below 3e-7.
float AtanPolynomial(float x) {
  const float x2 = x * x;
  float p = -0.005020633429277061f;
  p = 0.025330362691336757f + x2 * p;
  p = -0.0608728671947501f + x2 * p;
  p = 0.10002110154622305f + x2 * p;
  p = -0.14047793148948912f + x2 * p;
  p = 0.19974024787623312f + x2 * p;
  p = -0.3333223244657906f + x2 * p;
  p = 0.9999999227745422f + x2 * p;
  return x * p;
}

float Exp2Polynomial(float f) {
  // 2^f - 1, factored so that f == 0 produces exactly one.
  float p = 0.00020823112435402593f;
  p = 0.0012683808179469174f + f * p;
  p = 0.009653717346266869f + f * p;
  p = 0.055495068538714956f + f * p;
  p = 0.24022742055953297f + f * p;
  p = 0.6931471586519163f + f * p;
  return 1.0f + f * p;
}

float FloatFromBits(uint32_t bits) {
  float value;
  memcpy(&value, &bits, sizeof(value));
  return value;
}

#if defined(__ARM_NEON)
void SinCosNeon(float32x4_t x, float32x4_t* sin_out, float32x4_t* cos_out) {
  const float32x4_t magic = vdupq_n_f32(kRoundMagic);
  const float32x4_t inv_two_pi = vdupq_n_f32(kInvTwoPi);
  const float32x4_t two_pi = vdupq_n_f32(kTwoPi);
  float32x4_t rounded =
      vaddq_f32(vmulq_f32(x, inv_two_pi), magic);
  rounded = vsubq_f32(rounded, magic);
  x = vsubq_f32(x, vmulq_f32(rounded, two_pi));
  const float32x4_t x2 = vmulq_f32(x, x);

  float32x4_t ps = vdupq_n_f32(1.3697423152315939e-10f);
  ps = vaddq_f32(vdupq_n_f32(-2.4752158430246845e-08f), vmulq_f32(x2, ps));
  ps = vaddq_f32(vdupq_n_f32(2.753825702281984e-06f), vmulq_f32(x2, ps));
  ps = vaddq_f32(vdupq_n_f32(-0.00019840647498683763f), vmulq_f32(x2, ps));
  ps = vaddq_f32(vdupq_n_f32(0.008333323684270609f), vmulq_f32(x2, ps));
  ps = vaddq_f32(vdupq_n_f32(-0.16666666108510464f), vmulq_f32(x2, ps));
  ps = vaddq_f32(vdupq_n_f32(0.9999999994768137f), vmulq_f32(x2, ps));
  *sin_out = vmulq_f32(x, ps);

  float32x4_t pc = vdupq_n_f32(1.7369072458538055e-09f);
  pc = vaddq_f32(vdupq_n_f32(-2.7113357448549664e-07f), vmulq_f32(x2, pc));
  pc = vaddq_f32(vdupq_n_f32(2.4773422692275223e-05f), vmulq_f32(x2, pc));
  pc = vaddq_f32(vdupq_n_f32(-0.0013887970346310452f), vmulq_f32(x2, pc));
  pc = vaddq_f32(vdupq_n_f32(0.04166652435266188f), vmulq_f32(x2, pc));
  pc = vaddq_f32(vdupq_n_f32(-0.4999999177190987f), vmulq_f32(x2, pc));
  pc = vaddq_f32(vdupq_n_f32(0.9999999922903373f), vmulq_f32(x2, pc));
  *cos_out = pc;
}
#endif

}  // namespace

float Sin(float x) {
  if (!(x == x))
    return x;
  if (x > 1.0e6f || x < -1.0e6f)
    return std::sin(x);
  return SinPolynomial(ReduceTwoPi(x));
}

float Cos(float x) {
  if (!(x == x))
    return x;
  if (x > 1.0e6f || x < -1.0e6f)
    return std::cos(x);
  return CosPolynomial(ReduceTwoPi(x));
}

float Tan(float x) {
  return Sin(x) / Cos(x);
}

float Atan(float x) {
  const float ax = std::fabs(x);
  if (ax > 1.0f) {
    const float result = kPi * 0.5f - AtanPolynomial(1.0f / ax);
    return x < 0.0f ? -result : result;
  }
  return AtanPolynomial(x);
}

float Atan2(float y, float x) {
  if (x == 0.0f) {
    if (y > 0.0f)
      return kPi * 0.5f;
    if (y < 0.0f)
      return -kPi * 0.5f;
    return 0.0f;
  }
  float result = Atan(y / x);
  if (x < 0.0f)
    result += std::signbit(y) ? -kPi : kPi;
  return result;
}

float Asin(float x) {
  if (x > 1.0f || x < -1.0f)
    return Nan();
  return Atan2(x, Sqrt(1.0f - x * x));
}

float Acos(float x) {
  if (x > 1.0f || x < -1.0f)
    return Nan();
  return Atan2(Sqrt(1.0f - x * x), x);
}

float Exp2(float x) {
  if (!(x == x))
    return x;
  if (x >= 128.0f)
    return Inf();
  if (x < -149.0f)
    return 0.0f;

  int32_t e = static_cast<int32_t>(x);
  if (x < static_cast<float>(e))
    --e;
  const float exponent = static_cast<float>(e);
  const float fraction = x - exponent;
  const float power = Exp2Polynomial(fraction);

  uint32_t bits = 0;
  if (e >= -126) {
    bits = static_cast<uint32_t>(e + 127) << 23;
  } else {
    bits = 1u << (e + 149);
  }
  return power * FloatFromBits(bits);
}

float Log2(float x) {
  if (!(x == x))
    return x;
  if (x < 0.0f)
    return Nan();
  if (x == 0.0f)
    return -Inf();
  if (x > std::numeric_limits<float>::max())
    return Inf();

  uint32_t bits;
  memcpy(&bits, &x, sizeof(bits));
  int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xFFu) - 127;
  if (exponent == -127) {
    x *= 8388608.0f;  // Normalise a subnormal input.
    memcpy(&bits, &x, sizeof(bits));
    exponent = static_cast<int32_t>((bits >> 23) & 0xFFu) - 127 - 23;
  }
  bits = (bits & 0x007FFFFFu) | 0x3F800000u;
  const float mantissa = FloatFromBits(bits);
  const float f = mantissa - 1.0f;

  float p = 0.0074818443136277675f;
  p = -0.0421416881996659f + f * p;
  p = 0.11186758511703668f + f * p;
  p = -0.19597542461228148f + f * p;
  p = 0.27491970550867834f + f * p;
  p = -0.35815854010426523f + f * p;
  p = 0.4806484607172325f + f * p;
  p = -0.7213367877175271f + f * p;
  p = 1.4426949267971692f + f * p;
  return static_cast<float>(exponent) + f * p;
}

float Exp(float x) {
  return Exp2(x * 1.4426950408889634f);
}

float Log(float x) {
  return Log2(x) * 0.6931471805599453f;
}

float Pow(float x, float y) {
  if (!(y == y))
    return y;
  if (y == 0.0f)
    return 1.0f;
  if (x == 1.0f)
    return 1.0f;
  if (x == 0.0f) {
    if (y > 0.0f)
      return 0.0f;
    return Inf();
  }
  if (x < 0.0f)
    return Nan();
  return Exp2(y * Log2(x));
}

float Sqrt(float x) {
  return std::sqrt(x);
}

float InverseSqrt(float x) {
  if (x < 0.0f)
    return Nan();
  if (x == 0.0f)
    return Inf();
  if (x > std::numeric_limits<float>::max())
    return 0.0f;
#if defined(__ARM_NEON)
  const float32x2_t value = vdup_n_f32(x);
  float32x2_t estimate = vrsqrte_f32(value);
  estimate = vmul_f32(estimate, vrsqrts_f32(value, vmul_f32(estimate, estimate)));
  estimate = vmul_f32(estimate, vrsqrts_f32(value, vmul_f32(estimate, estimate)));
  return vget_lane_f32(estimate, 0);
#else
  return 1.0f / std::sqrt(x);
#endif
}

void Sin4(const float in[4], float out[4]) {
#if defined(__ARM_NEON)
  for (int i = 0; i < 4; ++i) {
    if (in[i] > 1.0e6f || in[i] < -1.0e6f) {
      for (int j = 0; j < 4; ++j)
        out[j] = Sin(in[j]);
      return;
    }
  }
  float32x4_t sin_value;
  float32x4_t cos_value;
  SinCosNeon(vld1q_f32(in), &sin_value, &cos_value);
  vst1q_f32(out, sin_value);
#else
  for (int i = 0; i < 4; ++i)
    out[i] = Sin(in[i]);
#endif
}

void Cos4(const float in[4], float out[4]) {
#if defined(__ARM_NEON)
  for (int i = 0; i < 4; ++i) {
    if (in[i] > 1.0e6f || in[i] < -1.0e6f) {
      for (int j = 0; j < 4; ++j)
        out[j] = Cos(in[j]);
      return;
    }
  }
  float32x4_t sin_value;
  float32x4_t cos_value;
  SinCosNeon(vld1q_f32(in), &sin_value, &cos_value);
  vst1q_f32(out, cos_value);
#else
  for (int i = 0; i < 4; ++i)
    out[i] = Cos(in[i]);
#endif
}

}  // namespace fastmath
}  // namespace glanceasy
