// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZGL_SRC_ZGL_SIMD_H_
#define ZGL_SRC_ZGL_SIMD_H_

#include <stddef.h>
#include <stdint.h>

namespace zgl {
namespace simd {

// Four-lane floating point helpers.  The scalar tail keeps these usable for
// vec2/vec3 and for matrices whose component count is not a multiple of four.
float Dot(const float* a, const float* b, int count);
void Scale(const float* input, float scale, float* output, int count);

// Bulk fills for the fixed-function clear and blit paths.  |value| for
// FillRgba is the native 0xAABBGGRR packing used by the colour buffers.
void FillRgba(uint8_t* destination, size_t pixels, uint32_t value);
void FillU16(uint16_t* destination, size_t count, uint16_t value);
void FillU8(uint8_t* destination, size_t count, uint8_t value);

}  // namespace simd
}  // namespace zgl

#endif  // ZGL_SRC_ZGL_SIMD_H_
