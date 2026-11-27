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

// Integer exponentiation by squaring.  Shaders commonly use pow() with a
// constant integer exponent (specular shininess, falloff powers); this avoids
// the log2/exp2 polynomial pair and is both faster and more accurate there.
float PowInteger(float x, int exponent) {
  unsigned int n = exponent < 0
                      ? static_cast<unsigned int>(-exponent)
                      : static_cast<unsigned int>(exponent);
  float result = 1.0f;
  float base = x;
  while (n != 0) {
    if (n & 1u)
      result *= base;
    n >>= 1;
    if (n != 0)
      base *= base;
  }
  return exponent < 0 ? 1.0f / result : result;
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
  if (y >= -4096.0f && y <= 4096.0f) {
    const int integer_exponent = static_cast<int>(y);
    if (static_cast<float>(integer_exponent) == y)
      return PowInteger(x, integer_exponent);
  }
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

#if defined(__ARM_NEON)
// log2 of four normal positive floats.  Kept separate from the scalar
// polynomial so Pow4 can reuse the same coefficients.
float32x4_t Log2Neon(float32x4_t x) {
  uint32x4_t bits = vreinterpretq_u32_f32(x);
  const int32x4_t exponent =
      vsubq_s32(vreinterpretq_s32_u32(vshrq_n_u32(bits, 23)),
                vdupq_n_s32(127));
  const uint32x4_t mantissa_bits =
      vorrq_u32(vandq_u32(bits, vdupq_n_u32(0x007FFFFFu)),
                vdupq_n_u32(0x3F800000u));
  const float32x4_t f =
      vsubq_f32(vreinterpretq_f32_u32(mantissa_bits), vdupq_n_f32(1.0f));
  float32x4_t p = vdupq_n_f32(0.0074818443136277675f);
  p = vmlaq_f32(vdupq_n_f32(-0.0421416881996659f), f, p);
  p = vmlaq_f32(vdupq_n_f32(0.11186758511703668f), f, p);
  p = vmlaq_f32(vdupq_n_f32(-0.19597542461228148f), f, p);
  p = vmlaq_f32(vdupq_n_f32(0.27491970550867834f), f, p);
  p = vmlaq_f32(vdupq_n_f32(-0.35815854010426523f), f, p);
  p = vmlaq_f32(vdupq_n_f32(0.4806484607172325f), f, p);
  p = vmlaq_f32(vdupq_n_f32(-0.7213367877175271f), f, p);
  p = vmlaq_f32(vdupq_n_f32(1.4426949267971692f), f, p);
  return vaddq_f32(vcvtq_f32_s32(exponent), vmulq_f32(f, p));
}

float32x4_t Exp2Neon(float32x4_t x) {
  int32x4_t exponent = vcvtq_s32_f32(x);
  const uint32x4_t rounded_down =
      vcltq_f32(x, vcvtq_f32_s32(exponent));
  // rounded_down is all ones for lanes below the truncated integer, i.e.
  // -1, so add it to decrement the exponent.
  exponent = vaddq_s32(exponent, vreinterpretq_s32_u32(rounded_down));
  const float32x4_t fraction =
      vsubq_f32(x, vcvtq_f32_s32(exponent));
  float32x4_t p = vdupq_n_f32(0.00020823112435402593f);
  p = vmlaq_f32(vdupq_n_f32(0.0012683808179469174f), fraction, p);
  p = vmlaq_f32(vdupq_n_f32(0.009653717346266869f), fraction, p);
  p = vmlaq_f32(vdupq_n_f32(0.055495068538714956f), fraction, p);
  p = vmlaq_f32(vdupq_n_f32(0.24022742055953297f), fraction, p);
  p = vmlaq_f32(vdupq_n_f32(0.6931471586519163f), fraction, p);
  const float32x4_t power =
      vaddq_f32(vdupq_n_f32(1.0f), vmulq_f32(fraction, p));

  const uint32x4_t normal_bits = vshlq_n_u32(
      vaddq_u32(vreinterpretq_u32_s32(exponent), vdupq_n_u32(127)), 23);
  const uint32x4_t subnormal_bits = vshlq_u32(
      vdupq_n_u32(1),
      vaddq_s32(exponent, vdupq_n_s32(149)));
  const uint32x4_t normal_mask =
      vcgeq_s32(exponent, vdupq_n_s32(-126));
  return vmulq_f32(
      power, vreinterpretq_f32_u32(
                 vbslq_u32(normal_mask, normal_bits, subnormal_bits)));
}
#endif

void Pow4(const float base[4], const float exponent[4], float out[4]) {
#if defined(__ARM_NEON)
  for (int i = 0; i < 4; ++i) {
    if (!(base[i] > 0.0f) ||
        base[i] < std::numeric_limits<float>::min() ||
        base[i] > std::numeric_limits<float>::max() ||
        !(exponent[i] == exponent[i]) ||
        std::fabs(exponent[i]) > 1.0e6f) {
      for (int j = 0; j < 4; ++j)
        out[j] = Pow(base[j], exponent[j]);
      return;
    }
  }
  float32x4_t z = vmulq_f32(vld1q_f32(exponent), Log2Neon(vld1q_f32(base)));
  float z_values[4];
  vst1q_f32(z_values, z);
  for (int i = 0; i < 4; ++i) {
    if (!(z_values[i] == z_values[i]) || z_values[i] > 127.0f) {
      for (int j = 0; j < 4; ++j)
        out[j] = Pow(base[j], exponent[j]);
      return;
    }
  }
  // Clamp only the lower end; exp2() underflows to zero far below -149.
  z = vmaxq_f32(z, vdupq_n_f32(-200.0f));
  vst1q_f32(out, Exp2Neon(z));
#else
  for (int i = 0; i < 4; ++i)
    out[i] = Pow(base[i], exponent[i]);
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
