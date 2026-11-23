// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZGL_SRC_ZGL_FASTMATH_H_
#define ZGL_SRC_ZGL_FASTMATH_H_

namespace zgl {
namespace fastmath {

// Scalar approximations used by the GLSL interpreter and the SLJIT runtime.
// They favour a small, branch-light instruction stream over last-bit accuracy:
// the error stays below one 8-bit output LSB for the ranges exercised by the
// renderer, while the exact integer, depth, stencil and blend paths never call
// into this file.
float Sin(float x);
float Cos(float x);
float Tan(float x);
float Asin(float x);
float Acos(float x);
float Atan(float x);
float Atan2(float y, float x);
float Exp(float x);
float Exp2(float x);
float Log(float x);
float Log2(float x);
float Pow(float x, float y);
float Sqrt(float x);
float InverseSqrt(float x);

// Four-lane variants.  Lanes beyond the logical vector width may be ignored by
// the caller, but must contain defined values.  On ARMv7 these compile to
// NEON; elsewhere they are plain scalar loops so that host tests exercise the
// same polynomial coefficients.
void Sin4(const float in[4], float out[4]);
void Cos4(const float in[4], float out[4]);

}  // namespace fastmath
}  // namespace zgl

#endif  // ZGL_SRC_ZGL_FASTMATH_H_
