// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GLANCEASY_SRC_COMMON_H_
#define GLANCEASY_SRC_COMMON_H_

#include <stdint.h>
#include <stdlib.h>

#include <algorithm>
#include <cmath>

namespace glanceasy {

// Debug switches are read once per call site instead of on every pixel.  A
// plain getenv() in a per-fragment branch shows up as several percent of the
// frame on Cortex-A7, so cache the lookup in a function-local static.
#define GLANCEASY_DEBUG_FLAG(name) \
  ([]() { static const char* value = ::getenv(name); return value; }())


// ES 3.0 minimum limits.  The CPU backend advertises exactly these numbers
// so that applications written against the minimum feature set are happy.
constexpr int kMaxVertexAttribs = 16;
constexpr int kMaxTextureUnits = 16;
constexpr int kMaxCombinedTextureUnits = 32;
constexpr int kMaxTextureSize = 4096;
constexpr int kMaxCubeMapSize = 2048;
constexpr int kMaxRenderbufferSize = 4096;
constexpr int kMaxViewportSize = 4096;
constexpr int kMax3DTextureSize = 512;
constexpr int kMaxVaryingVectors = 15;
constexpr int kMaxUniformVectors = 256;
constexpr int kMaxDrawBuffers = 4;
constexpr int kMaxUniformBufferBindings = 24;
constexpr int kMaxTransformFeedbackBuffers = 4;
constexpr int kMaxColorAttachments = 4;
constexpr int kMaxSamples = 4;

// Inline storage for one Value.  Sixteen floats cover a mat4, the largest
// basic GLSL type; arrays and structures use Value::aggregate instead.  This
// used to be 64, which made every scalar temporary 600+ bytes on Cortex-A7.
constexpr int kValueFloats = 16;

struct Rect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;

  bool IsEmpty() const { return width <= 0 || height <= 0; }
  int right() const { return x + width; }
  int bottom() const { return y + height; }

  Rect Intersect(const Rect& other) const {
    int left = std::max(x, other.x);
    int top = std::max(y, other.y);
    int r = std::min(right(), other.right());
    int b = std::min(bottom(), other.bottom());
    if (r <= left || b <= top)
      return Rect();
    return Rect{left, top, r - left, b - top};
  }

  bool Contains(int px, int py) const {
    return px >= x && py >= y && px < right() && py < bottom();
  }
};

struct Rgba {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 255;
};

// Native 32-bit packing: 0xAABBGGRR.  On a little endian target the byte
// order in memory is R, G, B, A, which is what GL_RGBA/GL_UNSIGNED_BYTE
// expects, so ReadPixels and glanceasyGetPixels() need no conversion at all.
inline uint32_t PackRgba(const Rgba& c) {
  return (static_cast<uint32_t>(c.a) << 24) |
         (static_cast<uint32_t>(c.b) << 16) |
         (static_cast<uint32_t>(c.g) << 8) | static_cast<uint32_t>(c.r);
}

inline uint32_t PackRgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  return PackRgba(Rgba{r, g, b, a});
}

inline Rgba UnpackRgba(uint32_t p) {
  Rgba c;
  c.r = static_cast<uint8_t>(p & 0xFF);
  c.g = static_cast<uint8_t>((p >> 8) & 0xFF);
  c.b = static_cast<uint8_t>((p >> 16) & 0xFF);
  c.a = static_cast<uint8_t>((p >> 24) & 0xFF);
  return c;
}

// Float form of the packing above, used by the fixed function blend unit.
inline void UnpackRgba(uint32_t p, float out[4]);

inline float SrgbToLinear(uint8_t value) {
  const float s = static_cast<float>(value) / 255.0f;
  return s <= 0.04045f ? s / 12.92f
                       : std::pow((s + 0.055f) / 1.055f, 2.4f);
}

inline uint8_t LinearToSrgb8(float v) {
  if (!(v > 0.0f))  // Also catches NaN.
    return 0;
  if (v >= 1.0f)
    return 255;
  const float encoded = v <= 0.0031308f
                            ? 12.92f * v
                            : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
  return static_cast<uint8_t>(encoded * 255.0f + 0.5f);
}

inline uint8_t FloatToUnorm8(float v) {
  if (!(v > 0.0f))  // Also catches NaN.
    return 0;
  if (v >= 1.0f)
    return 255;
  return static_cast<uint8_t>(v * 255.0f + 0.5f);
}

inline float Unorm8ToFloat(uint8_t v) {
  return static_cast<float>(v) * (1.0f / 255.0f);
}

inline void UnpackRgba(uint32_t p, float out[4]) {
  const Rgba c = UnpackRgba(p);
  out[0] = Unorm8ToFloat(c.r);
  out[1] = Unorm8ToFloat(c.g);
  out[2] = Unorm8ToFloat(c.b);
  out[3] = Unorm8ToFloat(c.a);
}

inline uint16_t FloatToUnorm16(float v) {
  if (!(v > 0.0f))
    return 0;
  if (v >= 1.0f)
    return 0xFFFFu;
  return static_cast<uint16_t>(v * 65535.0f + 0.5f);
}

inline float Unorm16ToFloat(uint16_t v) {
  return static_cast<float>(v) * (1.0f / 65535.0f);
}

inline float Clamp01(float v) {
  if (!(v > 0.0f))
    return 0.0f;
  return v > 1.0f ? 1.0f : v;
}

inline int ClampInt(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

inline int AlignUp(int value, int alignment) {
  return (value + alignment - 1) / alignment * alignment;
}

// Round-half-up conversion used by the fixed function pipeline stages.
inline uint8_t RoundToUnorm8(float v) {
  return FloatToUnorm8(v);
}

}  // namespace glanceasy

#endif  // GLANCEASY_SRC_COMMON_H_
