// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/zgl_texture.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace zgl {

namespace {

float ReadComponent(const uint8_t* source, int component, GLenum type) {
  switch (type) {
    case GL_UNSIGNED_BYTE:
      return static_cast<float>(source[component]) / 255.0f;
    case GL_BYTE:
      return std::max(-1.0f, static_cast<float>(reinterpret_cast<const int8_t*>(
                                 source)[component]) /
                                 127.0f);
    case GL_UNSIGNED_SHORT: {
      uint16_t value;
      memcpy(&value, source + component * 2, sizeof(value));
      return static_cast<float>(value) / 65535.0f;
    }
    case GL_SHORT: {
      int16_t value;
      memcpy(&value, source + component * 2, sizeof(value));
      return std::max(-1.0f, static_cast<float>(value) / 32767.0f);
    }
    case GL_UNSIGNED_INT: {
      uint32_t value;
      memcpy(&value, source + component * 4, sizeof(value));
      return static_cast<float>(value) / 4294967295.0f;
    }
    case GL_INT: {
      int32_t value;
      memcpy(&value, source + component * 4, sizeof(value));
      return std::max(-1.0f, static_cast<float>(value) / 2147483647.0f);
    }
    case GL_FLOAT: {
      float value;
      memcpy(&value, source + component * 4, sizeof(value));
      return value;
    }
    case GL_HALF_FLOAT: {
      uint16_t bits;
      memcpy(&bits, source + component * 2, sizeof(bits));
      const uint32_t sign = (bits & 0x8000u) << 16;
      const uint32_t exponent = (bits >> 10) & 0x1fu;
      const uint32_t mantissa = bits & 0x3ffu;
      uint32_t out;
      if (exponent == 0) {
        out = sign;
      } else if (exponent == 31) {
        out = sign | 0x7f800000u | (mantissa << 13);
      } else {
        out = sign | ((exponent + 112u) << 23) | (mantissa << 13);
      }
      float value;
      memcpy(&value, &out, sizeof(value));
      return value;
    }
    default:
      return 0.0f;
  }
}

int ComponentCount(GLenum format) {
  switch (format) {
    case GL_RED:
    case GL_ALPHA:
    case GL_LUMINANCE:
    case GL_DEPTH_COMPONENT:
      return 1;
    case GL_RG:
    case GL_LUMINANCE_ALPHA:
      return 2;
    case GL_RGB:
      return 3;
    case GL_RGBA:
    case GL_DEPTH_STENCIL:
      return 4;
    default:
      return 0;
  }
}

int BytesPerComponent(GLenum type) {
  switch (type) {
    case GL_UNSIGNED_BYTE:
    case GL_BYTE:
      return 1;
    case GL_UNSIGNED_SHORT:
    case GL_SHORT:
    case GL_HALF_FLOAT:
      return 2;
    case GL_UNSIGNED_INT:
    case GL_INT:
    case GL_FLOAT:
      return 4;
    default:
      return 0;
  }
}

uint32_t ReadPackedColor(const uint8_t* source, GLenum type) {
  if (type == GL_UNSIGNED_SHORT_5_6_5) {
    uint16_t value;
    memcpy(&value, source, sizeof(value));
    const uint32_t r = ((value >> 11) & 0x1fu) * 255u / 31u;
    const uint32_t g = ((value >> 5) & 0x3fu) * 255u / 63u;
    const uint32_t b = (value & 0x1fu) * 255u / 31u;
    return PackRgba(static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                    static_cast<uint8_t>(b), 255u);
  }
  if (type == GL_UNSIGNED_SHORT_4_4_4_4) {
    uint16_t value;
    memcpy(&value, source, sizeof(value));
    const uint32_t r = (value >> 12) & 0xfu;
    const uint32_t g = (value >> 8) & 0xfu;
    const uint32_t b = (value >> 4) & 0xfu;
    const uint32_t a = value & 0xfu;
    return PackRgba(
        static_cast<uint8_t>(r * 17u), static_cast<uint8_t>(g * 17u),
        static_cast<uint8_t>(b * 17u), static_cast<uint8_t>(a * 17u));
  }
  if (type == GL_UNSIGNED_SHORT_5_5_5_1) {
    uint16_t value;
    memcpy(&value, source, sizeof(value));
    const uint32_t r = ((value >> 11) & 0x1fu) * 255u / 31u;
    const uint32_t g = ((value >> 6) & 0x1fu) * 255u / 31u;
    const uint32_t b = ((value >> 1) & 0x1fu) * 255u / 31u;
    const uint32_t a = (value & 0x1u) ? 255u : 0u;
    return PackRgba(static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                    static_cast<uint8_t>(b), static_cast<uint8_t>(a));
  }
  if (type == GL_UNSIGNED_INT_2_10_10_10_REV) {
    uint32_t value;
    memcpy(&value, source, sizeof(value));
    const uint32_t r = (value & 0x3ffu) * 255u / 1023u;
    const uint32_t g = ((value >> 10) & 0x3ffu) * 255u / 1023u;
    const uint32_t b = ((value >> 20) & 0x3ffu) * 255u / 1023u;
    const uint32_t a = ((value >> 30) & 0x3u) * 255u / 3u;
    return PackRgba(static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                    static_cast<uint8_t>(b), static_cast<uint8_t>(a));
  }
  return 0;
}

bool IsPackedType(GLenum type) {
  return type == GL_UNSIGNED_SHORT_5_6_5 || type == GL_UNSIGNED_SHORT_4_4_4_4 ||
         type == GL_UNSIGNED_SHORT_5_5_5_1 ||
         type == GL_UNSIGNED_INT_2_10_10_10_REV ||
         type == GL_UNSIGNED_INT_10F_11F_11F_REV ||
         type == GL_UNSIGNED_INT_5_9_9_9_REV;
}

int PackedPixelBytes(GLenum type) {
  return (type == GL_UNSIGNED_INT_2_10_10_10_REV ||
          type == GL_UNSIGNED_INT_10F_11F_11F_REV ||
          type == GL_UNSIGNED_INT_5_9_9_9_REV)
             ? 4
             : 2;
}

float DecodeFloat11(uint32_t bits) {
  const uint32_t exponent = (bits >> 6) & 0x1fu;
  const uint32_t mantissa = bits & 0x3fu;
  if (exponent == 0)
    return static_cast<float>(mantissa) / 64.0f * std::pow(2.0f, -14.0f);
  if (exponent == 31)
    return mantissa ? NAN : INFINITY;
  return (1.0f + static_cast<float>(mantissa) / 64.0f) *
         std::pow(2.0f, static_cast<float>(exponent) - 15.0f);
}

float DecodeFloat10(uint32_t bits) {
  const uint32_t exponent = (bits >> 5) & 0x1fu;
  const uint32_t mantissa = bits & 0x1fu;
  if (exponent == 0)
    return static_cast<float>(mantissa) / 32.0f * std::pow(2.0f, -14.0f);
  if (exponent == 31)
    return mantissa ? NAN : INFINITY;
  return (1.0f + static_cast<float>(mantissa) / 32.0f) *
         std::pow(2.0f, static_cast<float>(exponent) - 15.0f);
}

bool DecodePackedFloat(const uint8_t* source, GLenum type, float rgba[4]) {
  if (type == GL_UNSIGNED_INT_10F_11F_11F_REV) {
    uint32_t value;
    memcpy(&value, source, sizeof(value));
    rgba[0] = DecodeFloat11(value & 0x7ffu);
    rgba[1] = DecodeFloat11((value >> 11) & 0x7ffu);
    rgba[2] = DecodeFloat10((value >> 22) & 0x3ffu);
    rgba[3] = 1.0f;
    return true;
  }
  if (type == GL_UNSIGNED_INT_5_9_9_9_REV) {
    uint32_t value;
    memcpy(&value, source, sizeof(value));
    const uint32_t exponent = (value >> 27) & 0x1fu;
    const float scale = std::pow(2.0f, static_cast<float>(exponent) - 24.0f);
    rgba[0] = static_cast<float>(value & 0x1ffu) * scale;
    rgba[1] = static_cast<float>((value >> 9) & 0x1ffu) * scale;
    rgba[2] = static_cast<float>((value >> 18) & 0x1ffu) * scale;
    rgba[3] = 1.0f;
    return true;
  }
  if (type == GL_UNSIGNED_INT_2_10_10_10_REV) {
    uint32_t value;
    memcpy(&value, source, sizeof(value));
    rgba[0] = static_cast<float>(value & 0x3ffu) / 1023.0f;
    rgba[1] = static_cast<float>((value >> 10) & 0x3ffu) / 1023.0f;
    rgba[2] = static_cast<float>((value >> 20) & 0x3ffu) / 1023.0f;
    rgba[3] = static_cast<float>((value >> 30) & 0x3u) / 3.0f;
    return true;
  }
  return false;
}


uint16_t FloatToDepth16(float value) {
  return static_cast<uint16_t>(Clamp01(value) * 65535.0f + 0.5f);
}

bool ImageHasTexels(const TextureImage& image) {
  return !image.rgba.empty() || !image.float_rgba.empty() ||
         !image.integer_rgba.empty() || !image.depth16.empty();
}

void FetchTexel(const TextureImage& image, size_t index, float rgba[4]) {
  if (!image.depth16.empty()) {
    const float depth = static_cast<float>(image.depth16[index]) / 65535.0f;
    rgba[0] = depth;
    rgba[1] = depth;
    rgba[2] = depth;
    rgba[3] = 1.0f;
    return;
  }
  if (!image.integer_rgba.empty()) {
    const uint32_t* source = image.integer_rgba.data() + index * 4;
    for (int i = 0; i < 4; ++i)
      rgba[i] = image.integer_signed
                    ? static_cast<float>(static_cast<int32_t>(source[i]))
                    : static_cast<float>(source[i]);
    return;
  }
  if (!image.float_rgba.empty()) {
    const float* source = image.float_rgba.data() + index * 4;
    rgba[0] = source[0];
    rgba[1] = source[1];
    rgba[2] = source[2];
    rgba[3] = source[3];
    return;
  }
  UnpackRgba(image.rgba[index], rgba);
  if (getenv("ZGL_DEBUG_TEXFETCH")) {
    static int dbg = 0;
    if (dbg++ < 3) {
      uint8_t px[4];
      px[0] = static_cast<uint8_t>(rgba[0] * 255.0f + 0.5f);
      px[1] = static_cast<uint8_t>(rgba[1] * 255.0f + 0.5f);
      px[2] = static_cast<uint8_t>(rgba[2] * 255.0f + 0.5f);
      px[3] = static_cast<uint8_t>(rgba[3] * 255.0f + 0.5f);
      fprintf(stderr, "[texfetch] %u %u %u %u\n", px[0], px[1], px[2], px[3]);
    }
  }
  if (image.srgb) {
    if (getenv("ZGL_DEBUG_SRGB")) {
      static int dbg = 0;
      if (dbg++ < 3)
        fprintf(stderr, "[fetch] raw=%u %.4f %.4f %.4f\n",
                static_cast<uint8_t>(rgba[0] * 255.0f + 0.5f), rgba[0], rgba[1],
                rgba[2]);
    }
    rgba[0] = SrgbToLinear(static_cast<uint8_t>(rgba[0] * 255.0f + 0.5f));
    rgba[1] = SrgbToLinear(static_cast<uint8_t>(rgba[1] * 255.0f + 0.5f));
    rgba[2] = SrgbToLinear(static_cast<uint8_t>(rgba[2] * 255.0f + 0.5f));
  }
}

}  // namespace

GLenum BaseFilter(GLenum filter) {
  switch (filter) {
    case GL_NEAREST:
    case GL_NEAREST_MIPMAP_NEAREST:
    case GL_NEAREST_MIPMAP_LINEAR:
      return GL_NEAREST;
    default:
      return GL_LINEAR;
  }
}

int WrapTexel(int value, int size, GLenum mode) {
  if (size <= 0)
    return 0;
  if (mode == GL_CLAMP_TO_EDGE)
    return ClampInt(value, 0, size - 1);
  if (mode == GL_MIRRORED_REPEAT) {
    const int period = size * 2;
    int wrapped = ((value % period) + period) % period;
    return wrapped < size ? wrapped : period - 1 - wrapped;
  }
  return ((value % size) + size) % size;
}

float WrapCoordinate(float coordinate, GLenum mode) {
  switch (mode) {
    case GL_CLAMP_TO_EDGE:
      return Clamp01(coordinate);
    case GL_MIRRORED_REPEAT: {
      const float period = coordinate - std::floor(coordinate * 0.5f) * 2.0f;
      return period <= 1.0f ? period : 2.0f - period;
    }
    case GL_CLAMP_TO_BORDER:
      if (coordinate < 0.0f || coordinate > 1.0f)
        return -1.0f;
      return coordinate;
    case GL_REPEAT:
    default:
      return coordinate - std::floor(coordinate);
  }
}

namespace {

// Samples a single mip level of a 2D texture image.  |base| is the texel
// offset of the selected array slice for array textures.
void SampleImage2DLevel(const TextureImage& image,
                        float s,
                        float t,
                        GLenum filter,
                        GLenum wrap_s,
                        GLenum wrap_t,
                        size_t base,
                        float out[4]) {
  out[0] = 0.0f;
  out[1] = 0.0f;
  out[2] = 0.0f;
  out[3] = 1.0f;
  if (image.width <= 0 || image.height <= 0 || !ImageHasTexels(image))
    return;
  if (filter == GL_NEAREST) {
    const int x =
        std::min(image.width - 1, static_cast<int>(s * image.width));
    const int y =
        std::min(image.height - 1, static_cast<int>(t * image.height));
    FetchTexel(image, base + static_cast<size_t>(y) * image.width + x, out);
    return;
  }
  const float px = s * image.width - 0.5f;
  const float py = t * image.height - 0.5f;
  const int x0 = static_cast<int>(std::floor(px));
  const int y0 = static_cast<int>(std::floor(py));
  const float fx = px - static_cast<float>(x0);
  const float fy = py - static_cast<float>(y0);
  float c00[4];
  float c10[4];
  float c01[4];
  float c11[4];
  const auto fetch = [&](int x, int y, float rgba[4]) {
    const int cx = WrapTexel(x, image.width, wrap_s);
    const int cy = WrapTexel(y, image.height, wrap_t);
    FetchTexel(image, base + static_cast<size_t>(cy) * image.width + cx, rgba);
  };
  fetch(x0, y0, c00);
  fetch(x0 + 1, y0, c10);
  fetch(x0, y0 + 1, c01);
  fetch(x0 + 1, y0 + 1, c11);
  for (int i = 0; i < 4; ++i) {
    const float top = c00[i] + (c10[i] - c00[i]) * fx;
    const float bottom = c01[i] + (c11[i] - c01[i]) * fx;
    out[i] = top + (bottom - top) * fy;
  }
}

}  // namespace

void SampleTexture2D(const Texture& texture,
                     float u,
                     float v,
                     float lod,
                     GLenum min_filter,
                     GLenum mag_filter,
                     GLenum wrap_s,
                     GLenum wrap_t,
                     float out[4]) {
  out[0] = 0.0f;
  out[1] = 0.0f;
  out[2] = 0.0f;
  out[3] = 1.0f;
  if (getenv("ZGL_DEBUG_TEX")) {
    static int sdbg = 0;
    if (sdbg < 8) {
      const TextureImage* im0 = texture.image(0, 0);
      fprintf(stderr, "[sample] complete=%d levels=%d img0=%dx%d lod=%.2f min=%x\n",
              texture.IsComplete() ? 1 : 0, texture.levels,
              im0 ? im0->width : -1, im0 ? im0->height : -1, lod,
              texture.min_filter);
      ++sdbg;
    }
  }
  if (!texture.IsComplete())
    return;
  if (getenv("ZGL_DEBUG_TEX")) {
    const TextureImage* im = texture.image(0, 0);
    if (im && im->width != 128) {
      static int idbg = 0;
      if (idbg < 4) {
        const uint32_t* ip = im->integer_rgba.empty() ? nullptr : im->integer_rgba.data();
        fprintf(stderr, "[simg] %dx%d int=%d signed=%d float=%d rgba=%d i0=%u,%u,%u,%u\n",
                im->width, im->height, ip ? 1 : 0, im->integer_signed ? 1 : 0,
                im->float_rgba.empty() ? 0 : 1, im->rgba.empty() ? 0 : 1,
                ip ? ip[0] : 0, ip ? ip[1] : 0, ip ? ip[2] : 0, ip ? ip[3] : 0);
        ++idbg;
      }
    }
  }
  const float t = WrapCoordinate(texture.render_target_flip_y ? 1.0f - v : v,
                                 wrap_t);
  if (t < 0.0f)
    return;
  const float s = WrapCoordinate(u, wrap_s);
  if (s < 0.0f)
    return;
  const float max_level = static_cast<float>(std::max(0, texture.levels - 1));
  const bool mipmapped =
      min_filter != GL_NEAREST && min_filter != GL_LINEAR;
  const float clamped_lod =
      std::clamp(mipmapped ? lod : 0.0f, 0.0f, max_level);
  const bool mipmap_linear =
      min_filter == GL_LINEAR_MIPMAP_LINEAR ||
      min_filter == GL_NEAREST_MIPMAP_LINEAR;
  int level0 = static_cast<int>(std::floor(clamped_lod));
  if (min_filter == GL_LINEAR_MIPMAP_NEAREST ||
      min_filter == GL_NEAREST_MIPMAP_NEAREST) {
    level0 = std::clamp(static_cast<int>(std::floor(clamped_lod + 0.5f)), 0,
                        static_cast<int>(max_level));
  }
  const float level_frac =
      mipmap_linear ? clamped_lod - static_cast<float>(level0) : 0.0f;
  const bool magnified = lod <= 0.0f;
  const GLenum filter =
      magnified ? mag_filter : BaseFilter(min_filter);
  if (getenv("ZGL_DEBUG_FILT") && clamped_lod >= 0.0f) {
    static int fdbg = 0;
    if (fdbg < 8) {
      fprintf(stderr, "[filt] lod=%.3f clamped=%.3f min=%x mag=%x filter=%x s=%.5f t=%.5f\n",
              lod, clamped_lod, min_filter, mag_filter, filter, s, t);
      ++fdbg;
    }
  }
  const auto sample_level = [&](int level, GLenum level_filter,
                                float result[4]) {
    result[0] = 0.0f;
    result[1] = 0.0f;
    result[2] = 0.0f;
    result[3] = 1.0f;
    const TextureImage* image = texture.image(level, 0);
    if (!image || image->width <= 0 || image->height <= 0 ||
        !ImageHasTexels(*image))
      return;
    if (level_filter == GL_NEAREST) {
      const int x =
          std::min(image->width - 1, static_cast<int>(s * image->width));
      const int y =
          std::min(image->height - 1, static_cast<int>(t * image->height));
      FetchTexel(*image, static_cast<size_t>(y) * image->width + x, result);
      return;
    }
    const float px = s * image->width - 0.5f;
    const float py = t * image->height - 0.5f;
    const int x0 = static_cast<int>(std::floor(px));
    const int y0 = static_cast<int>(std::floor(py));
    const float fx = px - static_cast<float>(x0);
    const float fy = py - static_cast<float>(y0);
    float c00[4];
    float c10[4];
    float c01[4];
    float c11[4];
    const auto fetch = [&](int x, int y, float rgba[4]) {
      const int cx = WrapTexel(x, image->width, wrap_s);
      const int cy = WrapTexel(y, image->height, wrap_t);
      FetchTexel(*image, static_cast<size_t>(cy) * image->width + cx, rgba);
    };
    fetch(x0, y0, c00);
    fetch(x0 + 1, y0, c10);
    fetch(x0, y0 + 1, c01);
    fetch(x0 + 1, y0 + 1, c11);
    if (getenv("ZGL_DEBUG_TEX")) {
      static int tdbg = 0;
      if (tdbg < 4) {
        fprintf(stderr, "[tap] s=%.6f t=%.6f px=%.3f py=%.3f xy=%d,%d f=%.4f,%.4f c00=%.4f c10=%.4f c01=%.4f c11=%.4f\n",
                s, t, px, py, x0, y0, fx, fy, c00[0], c10[0], c01[0], c11[0]);
        ++tdbg;
      }
    }
    for (int i = 0; i < 4; ++i) {
      const float top = c00[i] + (c10[i] - c00[i]) * fx;
      const float bottom = c01[i] + (c11[i] - c01[i]) * fx;
      result[i] = top + (bottom - top) * fy;
    }
  };
  sample_level(level0, filter, out);
  if (lod > 0.0f && mipmap_linear && level_frac > 0.0f &&
      level0 + 1 < texture.levels) {
    float next[4];
    sample_level(level0 + 1, BaseFilter(min_filter), next);
    for (int i = 0; i < 4; ++i)
      out[i] += (next[i] - out[i]) * level_frac;
  }
}

void SampleTexture2DArray(const Texture& texture,
                          float u,
                          float v,
                          float layer,
                          float lod,
                          GLenum min_filter,
                          GLenum mag_filter,
                          GLenum wrap_s,
                          GLenum wrap_t,
                          float out[4]) {
  out[0] = 0.0f;
  out[1] = 0.0f;
  out[2] = 0.0f;
  out[3] = 1.0f;
  if (!texture.IsComplete())
    return;
  const float max_level = static_cast<float>(std::max(0, texture.levels - 1));
  const bool mipmapped =
      min_filter != GL_NEAREST && min_filter != GL_LINEAR;
  const float clamped_lod =
      std::clamp(mipmapped ? lod : 0.0f, 0.0f, max_level);
  const bool mipmap_linear =
      min_filter == GL_LINEAR_MIPMAP_LINEAR ||
      min_filter == GL_NEAREST_MIPMAP_LINEAR;
  int level0 = static_cast<int>(std::floor(clamped_lod));
  if (min_filter == GL_LINEAR_MIPMAP_NEAREST ||
      min_filter == GL_NEAREST_MIPMAP_NEAREST) {
    level0 = std::clamp(static_cast<int>(std::floor(clamped_lod + 0.5f)), 0,
                        static_cast<int>(max_level));
  }
  const float level_frac =
      mipmap_linear ? clamped_lod - static_cast<float>(level0) : 0.0f;
  const bool magnified = lod <= 0.0f;
  const GLenum filter =
      magnified ? mag_filter : BaseFilter(min_filter);
  const float s = WrapCoordinate(u, wrap_s);
  const float t = WrapCoordinate(texture.render_target_flip_y ? 1.0f - v : v,
                                 wrap_t);
  if (s < 0.0f || t < 0.0f)
    return;
  const auto sample_level = [&](int level, GLenum level_filter,
                                float result[4]) {
    result[0] = 0.0f;
    result[1] = 0.0f;
    result[2] = 0.0f;
    result[3] = 1.0f;
    const TextureImage* image = texture.image(level, 0);
    if (!image || image->width <= 0 || image->height <= 0 ||
        image->depth <= 0 || !ImageHasTexels(*image))
      return;
    const int slice = std::max(
        0, std::min(image->depth - 1,
                    static_cast<int>(std::floor(layer + 0.5f))));
    const size_t base =
        static_cast<size_t>(slice) * image->width * image->height;
    if (getenv("ZGL_DEBUG_TEXARR")) {
      static int dbg = 0;
      if (dbg++ < 4)
        fprintf(stderr, "[arr] level=%d %dx%d depth=%d rgba=%zu slice=%.1f s=%.3f t=%.3f base=%zu words=%08x,%08x,%08x\n",
                level, image->width, image->height, image->depth,
                image->rgba.size(), layer, s, t, base,
                image->rgba[0], image->rgba[32], image->rgba[63]);
    }
    SampleImage2DLevel(*image, s, t, level_filter, wrap_s, wrap_t, base,
                       result);
  };
  sample_level(level0, filter, out);
  if (lod > 0.0f && mipmap_linear && level_frac > 0.0f &&
      level0 + 1 < texture.levels) {
    float next[4];
    sample_level(level0 + 1, BaseFilter(min_filter), next);
    for (int i = 0; i < 4; ++i)
      out[i] += (next[i] - out[i]) * level_frac;
  }
}

void SampleTextureCube(const Texture& texture,
                       const float direction[3],
                       float lod,
                       GLenum min_filter,
                       GLenum mag_filter,
                       float out[4]) {
  out[0] = 0.0f;
  out[1] = 0.0f;
  out[2] = 0.0f;
  out[3] = 1.0f;
  if (!texture.IsComplete())
    return;
  const float ax = std::fabs(direction[0]);
  const float ay = std::fabs(direction[1]);
  const float az = std::fabs(direction[2]);
  int face = 0;
  float u = 0.0f;
  float v = 0.0f;
  if (ay < ax && az < ax) {
    if (direction[0] >= 0.0f) {
      face = 0;  // GL_TEXTURE_CUBE_MAP_POSITIVE_X
      u = -direction[2] / ax;
      v = -direction[1] / ax;
    } else {
      face = 1;  // GL_TEXTURE_CUBE_MAP_NEGATIVE_X
      u = direction[2] / ax;
      v = -direction[1] / ax;
    }
  } else if (ax < ay && az < ay) {
    if (direction[1] >= 0.0f) {
      face = 2;  // GL_TEXTURE_CUBE_MAP_POSITIVE_Y
      u = direction[0] / ay;
      v = direction[2] / ay;
    } else {
      face = 3;  // GL_TEXTURE_CUBE_MAP_NEGATIVE_Y
      u = direction[0] / ay;
      v = -direction[2] / ay;
    }
  } else if (ax < az && ay < az) {
    if (direction[2] >= 0.0f) {
      face = 4;  // GL_TEXTURE_CUBE_MAP_POSITIVE_Z
      u = direction[0] / az;
      v = -direction[1] / az;
    } else {
      face = 5;  // GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
      u = -direction[0] / az;
      v = -direction[1] / az;
    }
  } else if (ax == ay) {
    if (ax < az) {
      face = direction[2] >= 0.0f ? 4 : 5;
      u = (face == 4 ? 1.0f : -1.0f) * direction[0] / az;
      v = -direction[1] / az;
    } else {
      face = direction[0] >= 0.0f ? 0 : 1;
      u = (face == 0 ? -1.0f : 1.0f) * direction[2] / ax;
      v = -direction[1] / ax;
    }
  } else if (ax == az) {
    if (az < ay) {
      face = direction[1] >= 0.0f ? 2 : 3;
      u = direction[0] / ay;
      v = (face == 2 ? 1.0f : -1.0f) * direction[2] / ay;
    } else {
      face = direction[2] >= 0.0f ? 4 : 5;
      u = (face == 4 ? 1.0f : -1.0f) * direction[0] / az;
      v = -direction[1] / az;
    }
  } else if (ay == az) {
    if (ay < ax) {
      face = direction[0] >= 0.0f ? 0 : 1;
      u = (face == 0 ? -1.0f : 1.0f) * direction[2] / ax;
      v = -direction[1] / ax;
    } else {
      face = direction[1] >= 0.0f ? 2 : 3;
      u = direction[0] / ay;
      v = (face == 2 ? 1.0f : -1.0f) * direction[2] / ay;
    }
  }
  const float max_level = static_cast<float>(std::max(0, texture.levels - 1));
  const bool mipmapped =
      min_filter != GL_NEAREST && min_filter != GL_LINEAR;
  const float clamped_lod =
      std::clamp(mipmapped ? lod : 0.0f, 0.0f, max_level);
  const bool mipmap_linear =
      min_filter == GL_LINEAR_MIPMAP_LINEAR ||
      min_filter == GL_NEAREST_MIPMAP_LINEAR;
  int level0 = static_cast<int>(std::floor(clamped_lod));
  if (min_filter == GL_LINEAR_MIPMAP_NEAREST ||
      min_filter == GL_NEAREST_MIPMAP_NEAREST) {
    level0 = std::clamp(static_cast<int>(std::floor(clamped_lod + 0.5f)), 0,
                        static_cast<int>(max_level));
  }
  const float level_frac =
      mipmap_linear ? clamped_lod - static_cast<float>(level0) : 0.0f;
  const TextureImage* image = texture.image(level0, face);
  if (!image || image->width <= 0 || !ImageHasTexels(*image))
    return;
  const float s = Clamp01((u * 0.5f) + 0.5f);
  float t = Clamp01((v * 0.5f) + 0.5f);
  if (texture.render_target_flip_y)
    t = 1.0f - t;
  const bool magnified = lod <= 0.0f;
  const GLenum filter =
      magnified ? mag_filter : BaseFilter(min_filter);
  const auto face_direction = [](int face_index, float fs, float ft,
                                 float direction_out[3]) {
    const float x = fs * 2.0f - 1.0f;
    const float y = ft * 2.0f - 1.0f;
    switch (face_index) {
      case 0:
        direction_out[0] = 1.0f;
        direction_out[1] = -y;
        direction_out[2] = -x;
        break;
      case 1:
        direction_out[0] = -1.0f;
        direction_out[1] = -y;
        direction_out[2] = x;
        break;
      case 2:
        direction_out[0] = x;
        direction_out[1] = 1.0f;
        direction_out[2] = y;
        break;
      case 3:
        direction_out[0] = x;
        direction_out[1] = -1.0f;
        direction_out[2] = -y;
        break;
      case 4:
        direction_out[0] = x;
        direction_out[1] = -y;
        direction_out[2] = 1.0f;
        break;
      default:
        direction_out[0] = -x;
        direction_out[1] = -y;
        direction_out[2] = -1.0f;
        break;
    }
  };
  const auto face_texel = [&](int level, int face_index, float fs, float ft,
                              float result[4]) {
    result[0] = 0.0f;
    result[1] = 0.0f;
    result[2] = 0.0f;
    result[3] = 1.0f;
    const TextureImage* level_image = texture.image(level, face_index);
    if (!level_image || level_image->width <= 0 || level_image->height <= 0 ||
        !ImageHasTexels(*level_image))
      return;
    const int x = std::min(
        level_image->width - 1,
        std::max(0, static_cast<int>(fs * level_image->width)));
    const int y = std::min(
        level_image->height - 1,
        std::max(0, static_cast<int>(ft * level_image->height)));
    FetchTexel(*level_image,
               static_cast<size_t>(y) * level_image->width + x, result);
  };
  const auto remap_tap = [&](int level, int base_face, int tx, int ty,
                             float result[4], bool* both_out) {
    result[0] = 0.0f;
    result[1] = 0.0f;
    result[2] = 0.0f;
    result[3] = 1.0f;
    const TextureImage* level_image = texture.image(level, base_face);
    if (!level_image || level_image->width <= 0 ||
        !ImageHasTexels(*level_image))
      return;
    const int size = level_image->width;
    const bool u_in_bounds = tx >= 0 && tx < size;
    const bool v_in_bounds = ty >= 0 && ty < size;
    if (u_in_bounds && v_in_bounds) {
      FetchTexel(*level_image,
                 static_cast<size_t>(ty) * size + tx, result);
      return;
    }
    if (!u_in_bounds && !v_in_bounds) {
      *both_out = true;
      return;
    }
    if (false) {
      float direction_out[3];
      face_direction(base_face, (static_cast<float>(tx) + 0.5f) / size,
                     (static_cast<float>(ty) + 0.5f) / size, direction_out);
      const float ax = std::fabs(direction_out[0]);
      const float ay = std::fabs(direction_out[1]);
      const float az = std::fabs(direction_out[2]);
      int tap_face = 0;
      float tu = 0.0f;
      float tv = 0.0f;
      if (ax >= ay && ax >= az) {
        if (direction_out[0] >= 0.0f) {
          tap_face = 0;
          tu = -direction_out[2] / ax;
          tv = -direction_out[1] / ax;
        } else {
          tap_face = 1;
          tu = direction_out[2] / ax;
          tv = -direction_out[1] / ax;
        }
      } else if (ay >= az) {
        if (direction_out[1] >= 0.0f) {
          tap_face = 2;
          tu = direction_out[0] / ay;
          tv = direction_out[2] / ay;
        } else {
          tap_face = 3;
          tu = direction_out[0] / ay;
          tv = -direction_out[2] / ay;
        }
      } else {
        if (direction_out[2] >= 0.0f) {
          tap_face = 4;
          tu = direction_out[0] / az;
          tv = -direction_out[1] / az;
        } else {
          tap_face = 5;
          tu = -direction_out[0] / az;
          tv = -direction_out[1] / az;
        }
      }
      face_texel(level, tap_face, (tu + 1.0f) * 0.5f,
                 (tv + 1.0f) * 0.5f, result);
      return;
    }
    const int u = tx < 0 ? -1 : (tx >= size ? size : tx);
    const int v = ty < 0 ? -1 : (ty >= size ? size : ty);
    int face = base_face;
    int s = u;
    int t = v;
    auto remap_face = [&](int canonical_x, int canonical_y, int canonical_z) {
      if (canonical_x == -1) {
        face = 1;
        s = canonical_z;
        t = size - 1 - canonical_y;
      } else if (canonical_x == size) {
        face = 0;
        s = size - 1 - canonical_z;
        t = size - 1 - canonical_y;
      } else if (canonical_y == -1) {
        face = 3;
        s = canonical_x;
        t = size - 1 - canonical_z;
      } else if (canonical_y == size) {
        face = 2;
        s = canonical_x;
        t = canonical_z;
      } else if (canonical_z == -1) {
        face = 5;
        s = size - 1 - canonical_x;
        t = size - 1 - canonical_y;
      } else if (canonical_z == size) {
        face = 4;
        s = canonical_x;
        t = size - 1 - canonical_y;
      }
    };
    switch (base_face) {
      case 1:
        remap_face(0, size - 1 - v, u);
        break;
      case 0:
        remap_face(size - 1, size - 1 - v, size - 1 - u);
        break;
      case 3:
        remap_face(u, 0, size - 1 - v);
        break;
      case 2:
        remap_face(u, size - 1, v);
        break;
      case 5:
        remap_face(size - 1 - u, size - 1 - v, 0);
        break;
      default:
        remap_face(u, size - 1 - v, size - 1);
        break;
    }
    const TextureImage* target = texture.image(level, face);
    if (!target || target->width <= 0 || !ImageHasTexels(*target))
      return;
    FetchTexel(*target, static_cast<size_t>(t) * target->width + s, result);
  };
  const auto sample_level = [&](int level, GLenum level_filter,
                                float result[4]) {
    result[0] = 0.0f;
    result[1] = 0.0f;
    result[2] = 0.0f;
    result[3] = 1.0f;
    const TextureImage* level_image = texture.image(level, face);
    if (!level_image || level_image->width <= 0 ||
        !ImageHasTexels(*level_image))
      return;
    if (getenv("ZGL_DEBUG_CUBE") && level == 2 && face == 0) {
      static int td = 0;
      if (td++ == 0) {
        float rgba[4];
        FetchTexel(*level_image, 0, rgba);
        float rgba0[4];
        const TextureImage* im0 = texture.image(0, face);
        if (im0) FetchTexel(*im0, 0, rgba0); else for (int k=0;k<4;k++) rgba0[k]=-1.0f;
        fprintf(stderr, "[tex2] ifmt=%x %dx%d first=%.3f %.3f %.3f %.3f l0=%.3f %.3f %.3f %.3f rgba=%zu\n",
                texture.internal_format, level_image->width, level_image->height,
                rgba[0], rgba[1], rgba[2], rgba[3], rgba0[0], rgba0[1], rgba0[2], rgba0[3],
                level_image->rgba.size());
      }
    }
    if (getenv("ZGL_DEBUG_CUBE")) {
      static int cdbg = 0;
      if (level > 0 && cdbg++ < 8)
        fprintf(stderr, "[cube] base=%dx%d level=%d face=%d %dx%d lod=%.3f frac=%.3f filter=%x\n",
                texture.width, texture.height, level, face, level_image->width,
                level_image->height, lod, level_frac, level_filter);
    }
    float ls = s;
    float lt = t;
    if (level_filter == GL_NEAREST) {
      ls = std::clamp(s, 0.5f / level_image->width,
                      1.0f - 0.5f / level_image->width);
      lt = std::clamp(t, 0.5f / level_image->height,
                      1.0f - 0.5f / level_image->height);
      const int x =
          std::min(level_image->width - 1,
                   static_cast<int>(ls * level_image->width));
      const int y =
          std::min(level_image->height - 1,
                   static_cast<int>(lt * level_image->height));
      FetchTexel(*level_image,
                 static_cast<size_t>(y) * level_image->width + x, result);
      return;
    }
    const float px = ls * level_image->width - 0.5f;
    const float py = lt * level_image->height - 0.5f;
    const int x0 = static_cast<int>(std::floor(px));
    const int y0 = static_cast<int>(std::floor(py));
    if (getenv("ZGL_DEBUG_CUBE") && level > 0) {
      static int tdbg = 0;
      if (tdbg++ < 4)
        fprintf(stderr, "[ctap] level=%d s=%.5f t=%.5f px=%.3f py=%.3f x0=%d y0=%d\n",
                level, s, t, px, py, x0, y0);
    }
    const float fx = px - static_cast<float>(x0);
    const float fy = py - static_cast<float>(y0);
    float c00[4];
    float c10[4];
    float c01[4];
    float c11[4];
    bool b00 = false;
    bool b10 = false;
    bool b01 = false;
    bool b11 = false;
    remap_tap(level, face, x0, y0, c00, &b00);
    remap_tap(level, face, x0 + 1, y0, c10, &b10);
    remap_tap(level, face, x0, y0 + 1, c01, &b01);
    remap_tap(level, face, x0 + 1, y0 + 1, c11, &b11);
    float* colors[4] = {c00, c10, c01, c11};
    bool* both[4] = {&b00, &b10, &b01, &b11};
    for (int i = 0; i < 4; ++i) {
      if (!*both[i])
        continue;
      colors[i][0] = 0.0f;
      colors[i][1] = 0.0f;
      colors[i][2] = 0.0f;
      colors[i][3] = 0.0f;
      int count = 0;
      for (int j = 0; j < 4; ++j) {
        if (j == i || *both[j])
          continue;
        for (int c = 0; c < 4; ++c)
          colors[i][c] += colors[j][c];
        ++count;
      }
      if (count > 0) {
        for (int c = 0; c < 4; ++c)
          colors[i][c] /= static_cast<float>(count);
      }
    }
    for (int i = 0; i < 4; ++i) {
      const float top = c00[i] + (c10[i] - c00[i]) * fx;
      const float bottom = c01[i] + (c11[i] - c01[i]) * fx;
      result[i] = top + (bottom - top) * fy;
    }
  };
  sample_level(level0, filter, out);
  if (lod > 0.0f && mipmap_linear && level_frac > 0.0f &&
      level0 + 1 < texture.levels) {
    float next[4];
    sample_level(level0 + 1, BaseFilter(min_filter), next);
    for (int i = 0; i < 4; ++i)
      out[i] += (next[i] - out[i]) * level_frac;
  }
}

void SampleTexture3D(const Texture& texture,
                     const float position[3],
                     float lod,
                     GLenum min_filter,
                     GLenum mag_filter,
                     GLenum wrap_s,
                     GLenum wrap_t,
                     GLenum wrap_r,
                     float out[4]) {
  out[0] = 0.0f;
  out[1] = 0.0f;
  out[2] = 0.0f;
  out[3] = 1.0f;
  if (!texture.IsComplete())
    return;
  const float max_level = static_cast<float>(std::max(0, texture.levels - 1));
  const bool mipmapped =
      min_filter != GL_NEAREST && min_filter != GL_LINEAR;
  const float clamped_lod =
      std::clamp(mipmapped ? lod : 0.0f, 0.0f, max_level);
  const bool mipmap_linear =
      min_filter == GL_LINEAR_MIPMAP_LINEAR ||
      min_filter == GL_NEAREST_MIPMAP_LINEAR;
  int level0 = static_cast<int>(std::floor(clamped_lod));
  if (min_filter == GL_LINEAR_MIPMAP_NEAREST ||
      min_filter == GL_NEAREST_MIPMAP_NEAREST) {
    level0 = std::clamp(static_cast<int>(std::floor(clamped_lod + 0.5f)), 0,
                        static_cast<int>(max_level));
  }
  const float level_frac =
      mipmap_linear ? clamped_lod - static_cast<float>(level0) : 0.0f;
  const float s = WrapCoordinate(position[0], wrap_s);
  const float t = WrapCoordinate(
      texture.render_target_flip_y ? 1.0f - position[1] : position[1],
      wrap_t);
  const float r = WrapCoordinate(position[2], wrap_r);
  if (s < 0.0f || t < 0.0f || r < 0.0f)
    return;
  const bool magnified = lod <= 0.0f;
  const GLenum filter =
      magnified ? mag_filter : BaseFilter(min_filter);
  const auto sample_level = [&](int level, GLenum level_filter,
                                float result[4]) {
    result[0] = 0.0f;
    result[1] = 0.0f;
    result[2] = 0.0f;
    result[3] = 1.0f;
    const TextureImage* image = texture.image(level, 0);
    if (!image || image->width <= 0 || !ImageHasTexels(*image))
      return;
    const int depth = std::max(1, image->depth);
    const size_t slice = static_cast<size_t>(image->width) * image->height;
    const auto sample_slice = [&](int z, float rgba[4]) {
      if (level_filter == GL_NEAREST) {
        const int x =
            std::min(image->width - 1, static_cast<int>(s * image->width));
        const int y =
            std::min(image->height - 1, static_cast<int>(t * image->height));
        FetchTexel(*image,
                   z * slice + static_cast<size_t>(y) * image->width + x,
                   rgba);
        return;
      }
      const float px = s * image->width - 0.5f;
      const float py = t * image->height - 0.5f;
      const int x0 = static_cast<int>(std::floor(px));
      const int y0 = static_cast<int>(std::floor(py));
      const float fx = px - static_cast<float>(x0);
      const float fy = py - static_cast<float>(y0);
      float c00[4];
      float c10[4];
      float c01[4];
      float c11[4];
      const auto fetch = [&](int x, int y, float out_rgba[4]) {
        const int cx = WrapTexel(x, image->width, wrap_s);
        const int cy = WrapTexel(y, image->height, wrap_t);
        FetchTexel(*image,
                   z * slice + static_cast<size_t>(cy) * image->width + cx,
                   out_rgba);
      };
      fetch(x0, y0, c00);
      fetch(x0 + 1, y0, c10);
      fetch(x0, y0 + 1, c01);
      fetch(x0 + 1, y0 + 1, c11);
      for (int i = 0; i < 4; ++i) {
        const float row0 = c00[i] + (c10[i] - c00[i]) * fx;
        const float row1 = c01[i] + (c11[i] - c01[i]) * fx;
        rgba[i] = row0 + (row1 - row0) * fy;
      }
    };
    if (depth == 1) {
      sample_slice(0, result);
      return;
    }
    if (level_filter == GL_NEAREST) {
      const int z = WrapTexel(static_cast<int>(r * depth), depth, wrap_r);
      sample_slice(z, result);
      return;
    }
    const float pz = r * depth - 0.5f;
    const int base_z = static_cast<int>(std::floor(pz));
    const int z0 = WrapTexel(base_z, depth, wrap_r);
    const int z1 = WrapTexel(base_z + 1, depth, wrap_r);
    const float fz = pz - static_cast<float>(base_z);
    float a[4];
    float b[4];
    sample_slice(z0, a);
    sample_slice(z1, b);
    for (int i = 0; i < 4; ++i)
      result[i] = a[i] + (b[i] - a[i]) * fz;
  };
  sample_level(level0, filter, out);
  if (lod > 0.0f && mipmap_linear && level_frac > 0.0f &&
      level0 + 1 < texture.levels) {
    float next[4];
    sample_level(level0 + 1, BaseFilter(min_filter), next);
    for (int i = 0; i < 4; ++i)
      out[i] += (next[i] - out[i]) * level_frac;
  }
}

bool UploadTextureImage(TextureImage* image,
                        int width,
                        int height,
                        GLenum format,
                        GLenum type,
                        int row_length,
                        int skip_rows,
                        int skip_pixels,
                        const void* pixels) {
  if (!image || width <= 0 || height <= 0 || !pixels)
    return false;
  if (width > kMaxTextureSize || height > kMaxTextureSize)
    return false;
  image->width = width;
  image->height = height;
  image->depth = 1;

  if (format == GL_DEPTH_COMPONENT || format == GL_DEPTH_STENCIL) {
    image->rgba.assign(static_cast<size_t>(width) * height,
                       PackRgba(0, 0, 0, 0));
    image->depth16.assign(static_cast<size_t>(width) * height, 0xFFFFu);
    image->stencil8.assign(static_cast<size_t>(width) * height, 0u);
    const uint8_t* source = static_cast<const uint8_t*>(pixels);
    // GL_UNSIGNED_INT_24_8 packs 24 depth bits and 8 stencil bits into a
    // 32-bit word, so it reads the same way as GL_UNSIGNED_INT here.
    const int element = type == GL_FLOAT_32_UNSIGNED_INT_24_8_REV
                            ? 8
                            : (type == GL_FLOAT || type == GL_UNSIGNED_INT ||
                               type == GL_UNSIGNED_INT_24_8)
                                  ? 4
                                  : 2;
    for (int y = 0; y < height; ++y) {
      const uint8_t* row = source + (static_cast<size_t>(skip_rows + y) *
                                         (row_length > 0 ? row_length : width) +
                                     skip_pixels) *
                                        element;
      for (int x = 0; x < width; ++x) {
        if (type == GL_FLOAT_32_UNSIGNED_INT_24_8_REV) {
          float value;
          uint32_t stencil;
          memcpy(&value, row + x * 8, sizeof(value));
          memcpy(&stencil, row + x * 8 + 4, sizeof(stencil));
          image->depth16[static_cast<size_t>(y) * width + x] =
              FloatToDepth16(value);
          if (format == GL_DEPTH_STENCIL)
            image->stencil8[static_cast<size_t>(y) * width + x] =
                static_cast<uint8_t>(stencil & 0xFFu);
        } else if (type == GL_FLOAT) {
          float value;
          memcpy(&value, row + x * 4, sizeof(value));
          image->depth16[static_cast<size_t>(y) * width + x] =
              FloatToDepth16(value);
        } else if (element == 4) {
          uint32_t value;
          memcpy(&value, row + x * 4, sizeof(value));
          image->depth16[static_cast<size_t>(y) * width + x] =
              static_cast<uint16_t>(value >> 16);
          if (format == GL_DEPTH_STENCIL)
            image->stencil8[static_cast<size_t>(y) * width + x] =
                static_cast<uint8_t>(value & 0xFFu);
        } else {
          uint16_t value;
          memcpy(&value, row + x * 2, sizeof(value));
          image->depth16[static_cast<size_t>(y) * width + x] = value;
          if (format == GL_DEPTH_STENCIL)
            image->stencil8[static_cast<size_t>(y) * width + x] = 0;
        }
      }
    }
    return true;
  }

  const int components = ComponentCount(format);
  const int bytes = BytesPerComponent(type);
  if (components == 0 || (bytes == 0 && !IsPackedType(type)))
    return false;

  image->rgba.assign(static_cast<size_t>(width) * height, 0u);
  const int packed_size =
      IsPackedType(type) ? PackedPixelBytes(type) : components * bytes;
  const int source_stride =
      row_length > 0 ? row_length * packed_size : width * packed_size;
  const uint8_t* source = static_cast<const uint8_t*>(pixels);
  for (int y = 0; y < height; ++y) {
    const uint8_t* row = source +
                         static_cast<size_t>(skip_rows + y) * source_stride +
                         static_cast<size_t>(skip_pixels) * packed_size;
    for (int x = 0; x < width; ++x) {
      const uint8_t* texel = row + static_cast<size_t>(x) * packed_size;
      uint8_t r = 0;
      uint8_t g = 0;
      uint8_t b = 0;
      uint8_t a = 255;
      if (IsPackedType(type)) {
        const uint32_t packed = ReadPackedColor(texel, type);
        image->rgba[static_cast<size_t>(y) * width + x] = packed;
        continue;
      }
      float values[4] = {0.0f, 0.0f, 0.0f, 1.0f};
      for (int c = 0; c < components; ++c)
        values[c] = ReadComponent(texel, c, type);
      switch (format) {
        case GL_RED:
          r = g = b = FloatToUnorm8(values[0]);
          break;
        case GL_RG:
          r = g = b = FloatToUnorm8(values[0]);
          a = FloatToUnorm8(values[1]);
          break;
        case GL_RGB:
          r = FloatToUnorm8(values[0]);
          g = FloatToUnorm8(values[1]);
          b = FloatToUnorm8(values[2]);
          break;
        case GL_ALPHA:
          r = g = b = 0;
          a = FloatToUnorm8(values[0]);
          break;
        case GL_LUMINANCE:
          r = g = b = FloatToUnorm8(values[0]);
          break;
        case GL_LUMINANCE_ALPHA:
          r = g = b = FloatToUnorm8(values[0]);
          a = FloatToUnorm8(values[1]);
          break;
        case GL_RGBA:
        default:
          r = FloatToUnorm8(values[0]);
          g = FloatToUnorm8(values[1]);
          b = FloatToUnorm8(values[2]);
          a = FloatToUnorm8(values[3]);
          break;
      }
      image->rgba[static_cast<size_t>(y) * width + x] = PackRgba(r, g, b, a);
    }
  }
  return true;
}


bool UploadTextureImageInteger(TextureImage* image,
                               int width,
                               int height,
                               GLenum format,
                               GLenum type,
                               int row_length,
                               int skip_rows,
                               int skip_pixels,
                               const void* pixels) {
  if (!image || width <= 0 || height <= 0 || !pixels)
    return false;
  int components = 0;
  switch (format) {
    case GL_RED_INTEGER:
      components = 1;
      break;
    case GL_RG_INTEGER:
      components = 2;
      break;
    case GL_RGB_INTEGER:
      components = 3;
      break;
    case GL_RGBA_INTEGER:
      components = 4;
      break;
    default:
      return false;
  }
  int bytes = 0;
  switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
      bytes = 1;
      break;
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
      bytes = 2;
      break;
    case GL_INT:
    case GL_UNSIGNED_INT:
      bytes = 4;
      break;
    default:
      return false;
  }
  const bool source_signed = type == GL_BYTE || type == GL_SHORT ||
                             type == GL_INT;
  const size_t texels = static_cast<size_t>(width) * height;
  image->width = width;
  image->height = height;
  image->depth = 1;
  image->integer_rgba.assign(texels * 4, 0u);
  const int pixel_bytes = components * bytes;
  const int stride = row_length > 0 ? row_length * pixel_bytes
                                    : width * pixel_bytes;
  const uint8_t* source = static_cast<const uint8_t*>(pixels);
  for (int y = 0; y < height; ++y) {
    const uint8_t* row = source +
                         static_cast<size_t>(skip_rows + y) * stride +
                         static_cast<size_t>(skip_pixels) * pixel_bytes;
    for (int x = 0; x < width; ++x) {
      const uint8_t* texel = row + static_cast<size_t>(x) * pixel_bytes;
      uint32_t values[4] = {0u, 0u, 0u, 1u};
      for (int c = 0; c < components; ++c) {
        const uint8_t* component = texel + c * bytes;
        int64_t raw = 0;
        if (bytes == 1) {
          raw = source_signed
                    ? static_cast<int64_t>(static_cast<int8_t>(component[0]))
                    : static_cast<int64_t>(component[0]);
        } else if (bytes == 2) {
          uint16_t bits;
          memcpy(&bits, component, sizeof(bits));
          raw = source_signed
                    ? static_cast<int64_t>(static_cast<int16_t>(bits))
                    : static_cast<int64_t>(bits);
        } else {
          uint32_t bits;
          memcpy(&bits, component, sizeof(bits));
          raw = source_signed
                    ? static_cast<int64_t>(static_cast<int32_t>(bits))
                    : static_cast<int64_t>(bits);
        }
        values[c] = static_cast<uint32_t>(raw);
      }
      memcpy(image->integer_rgba.data() +
                 (static_cast<size_t>(y) * width + x) * 4,
             values, sizeof(values));
    }
  }
  return true;
}

bool UploadTextureImageFloat(TextureImage* image,
                             int width,
                             int height,
                             GLenum format,
                             GLenum type,
                             int row_length,
                             int skip_rows,
                             int skip_pixels,
                             const void* pixels) {
  if (!image || width <= 0 || height <= 0 || !pixels)
    return false;
  const int components = ComponentCount(format);
  const int bytes = BytesPerComponent(type);
  if (components == 0 || (bytes == 0 && !IsPackedType(type)))
    return false;
  const size_t texels = static_cast<size_t>(width) * height;
  if (image->float_rgba.size() < texels * 4)
    image->float_rgba.assign(texels * 4, 0.0f);
  const int packed_size = IsPackedType(type) ? PackedPixelBytes(type)
                                             : components * bytes;
  const int source_stride =
      row_length > 0 ? row_length * packed_size : width * packed_size;
  const uint8_t* source = static_cast<const uint8_t*>(pixels);
  for (int y = 0; y < height; ++y) {
    const uint8_t* row = source +
                         static_cast<size_t>(skip_rows + y) * source_stride +
                         static_cast<size_t>(skip_pixels) * packed_size;
    for (int x = 0; x < width; ++x) {
      const uint8_t* texel = row + static_cast<size_t>(x) * packed_size;
      float packed_values[4] = {0.0f, 0.0f, 0.0f, 1.0f};
      if (DecodePackedFloat(texel, type, packed_values)) {
        memcpy(image->float_rgba.data() +
                   (static_cast<size_t>(y) * width + x) * 4,
               packed_values, sizeof(packed_values));
        continue;
      }
      float values[4] = {0.0f, 0.0f, 0.0f, 1.0f};
      for (int c = 0; c < components; ++c)
        values[c] = ReadComponent(texel, c, type);
      float rgba[4] = {0.0f, 0.0f, 0.0f, 1.0f};
      switch (format) {
        case GL_RED:
          rgba[0] = values[0];
          break;
        case GL_RG:
          rgba[0] = values[0];
          rgba[1] = values[1];
          break;
        case GL_RGB:
          rgba[0] = values[0];
          rgba[1] = values[1];
          rgba[2] = values[2];
          break;
        case GL_ALPHA:
          rgba[3] = values[0];
          break;
        case GL_LUMINANCE:
          rgba[0] = rgba[1] = rgba[2] = values[0];
          break;
        case GL_LUMINANCE_ALPHA:
          rgba[0] = rgba[1] = rgba[2] = values[0];
          rgba[3] = values[1];
          break;
        case GL_RGBA:
        default:
          rgba[0] = values[0];
          rgba[1] = values[1];
          rgba[2] = values[2];
          rgba[3] = values[3];
          break;
      }
      float* destination =
          image->float_rgba.data() + (static_cast<size_t>(y) * width + x) * 4;
      memcpy(destination, rgba, sizeof(rgba));
    }
  }
  return true;
}

void GenerateMipmap(Texture* texture) {
  if (!texture || texture->width <= 0 || texture->height <= 0)
    return;
  const int faces = texture->faces();
  for (int face = 0; face < faces; ++face) {
    int width = texture->width;
    int height = texture->height;
    for (int level = 1; level < texture->levels; ++level) {
      const int next_width = std::max(1, width / 2);
      const int next_height = std::max(1, height / 2);
      const TextureImage* source = texture->image(level - 1, face);
      TextureImage* destination = texture->mutable_image(level, face);
      if (!source || !destination || !ImageHasTexels(*source)) {
        width = next_width;
        height = next_height;
        continue;
      }
      destination->width = next_width;
      destination->height = next_height;
      destination->depth = 1;
      const size_t destination_texels =
          static_cast<size_t>(next_width) * next_height;
      const bool is_float = !source->float_rgba.empty();
      if (is_float) {
        destination->rgba.clear();
        destination->float_rgba.assign(destination_texels * 4, 0.0f);
      } else {
        destination->float_rgba.clear();
        destination->rgba.assign(destination_texels, 0u);
      }
      for (int y = 0; y < next_height; ++y) {
        for (int x = 0; x < next_width; ++x) {
          float accum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
          int count = 0;
          for (int dy = 0; dy < 2; ++dy) {
            for (int dx = 0; dx < 2; ++dx) {
              const int sx = ClampInt(x * 2 + dx, 0, width - 1);
              const int sy = ClampInt(y * 2 + dy, 0, height - 1);
              float rgba[4];
              FetchTexel(*source, static_cast<size_t>(sy) * width + sx,
                         rgba);
              for (int c = 0; c < 4; ++c)
                accum[c] += rgba[c];
              ++count;
            }
          }
          const size_t destination_index =
              static_cast<size_t>(y) * next_width + x;
          if (is_float) {
            float* float_destination =
                destination->float_rgba.data() + destination_index * 4;
            for (int c = 0; c < 4; ++c)
              float_destination[c] = accum[c] / static_cast<float>(count);
          } else {
            uint8_t bytes[4];
            for (int c = 0; c < 4; ++c)
              bytes[c] = FloatToUnorm8(accum[c] / static_cast<float>(count));
            destination->rgba[destination_index] =
                PackRgba(bytes[0], bytes[1], bytes[2], bytes[3]);
          }
        }
      }
      width = next_width;
      height = next_height;
    }
  }
}

}  // namespace zgl
