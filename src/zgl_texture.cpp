// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/zgl_texture.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <cmath>

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
  return 0;
}

bool IsPackedType(GLenum type) {
  return type == GL_UNSIGNED_SHORT_5_6_5 || type == GL_UNSIGNED_SHORT_4_4_4_4 ||
         type == GL_UNSIGNED_SHORT_5_5_5_1;
}

uint16_t FloatToDepth16(float value) {
  return static_cast<uint16_t>(Clamp01(value) * 65535.0f + 0.5f);
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
  if (!texture.IsComplete())
    return;
  int level = 0;
  if (lod > 0.0f) {
    const float max_level = static_cast<float>(texture.levels - 1);
    level = std::min(static_cast<int>(std::floor(lod)), texture.levels - 1);
    if (max_level < 0.0f)
      level = 0;
  }
  const TextureImage* image = texture.image(level, 0);
  if (!image || image->width <= 0 || image->height <= 0 || image->rgba.empty())
    return;
  const GLenum filter = lod > 0.0f ? BaseFilter(min_filter) : mag_filter;
  const float s = WrapCoordinate(u, wrap_s);
  const float t = WrapCoordinate(v, wrap_t);
  if (s < 0.0f || t < 0.0f)
    return;
  if (filter == GL_NEAREST) {
    const int x =
        std::min(image->width - 1, static_cast<int>(s * image->width));
    const int y =
        std::min(image->height - 1, static_cast<int>(t * image->height));
    UnpackRgba(image->rgba[static_cast<size_t>(y) * image->width + x], out);
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
    const int cx = ClampInt(x, 0, image->width - 1);
    const int cy = ClampInt(y, 0, image->height - 1);
    UnpackRgba(image->rgba[static_cast<size_t>(cy) * image->width + cx], rgba);
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
  if (ax >= ay && ax >= az) {
    if (direction[0] >= 0.0f) {
      face = 0;  // GL_TEXTURE_CUBE_MAP_POSITIVE_X
      u = -direction[2] / ax;
      v = -direction[1] / ax;
    } else {
      face = 1;  // GL_TEXTURE_CUBE_MAP_NEGATIVE_X
      u = direction[2] / ax;
      v = -direction[1] / ax;
    }
  } else if (ay >= az) {
    if (direction[1] >= 0.0f) {
      face = 2;  // GL_TEXTURE_CUBE_MAP_POSITIVE_Y
      u = direction[0] / ay;
      v = direction[2] / ay;
    } else {
      face = 3;  // GL_TEXTURE_CUBE_MAP_NEGATIVE_Y
      u = direction[0] / ay;
      v = -direction[2] / ay;
    }
  } else {
    if (direction[2] >= 0.0f) {
      face = 4;  // GL_TEXTURE_CUBE_MAP_POSITIVE_Z
      u = direction[0] / az;
      v = -direction[1] / az;
    } else {
      face = 5;  // GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
      u = -direction[0] / az;
      v = -direction[1] / az;
    }
  }
  int level = 0;
  if (lod > 0.0f)
    level = std::min(static_cast<int>(std::floor(lod)), texture.levels - 1);
  const TextureImage* image = texture.image(level, face);
  if (!image || image->width <= 0 || image->rgba.empty())
    return;
  const GLenum filter = lod > 0.0f ? BaseFilter(min_filter) : mag_filter;
  const float s = Clamp01((u * 0.5f) + 0.5f);
  const float t = Clamp01((v * 0.5f) + 0.5f);
  if (filter == GL_NEAREST) {
    const int x =
        std::min(image->width - 1, static_cast<int>(s * image->width));
    const int y =
        std::min(image->height - 1, static_cast<int>(t * image->height));
    UnpackRgba(image->rgba[static_cast<size_t>(y) * image->width + x], out);
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
    const int cx = ClampInt(x, 0, image->width - 1);
    const int cy = ClampInt(y, 0, image->height - 1);
    UnpackRgba(image->rgba[static_cast<size_t>(cy) * image->width + cx], rgba);
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
  int level = 0;
  if (lod > 0.0f)
    level = std::min(static_cast<int>(std::floor(lod)), texture.levels - 1);
  const TextureImage* image = texture.image(level, 0);
  if (!image || image->width <= 0 || image->rgba.empty())
    return;
  const int depth = std::max(1, image->depth);
  const float s = WrapCoordinate(position[0], wrap_s);
  const float t = WrapCoordinate(position[1], wrap_t);
  const float r = WrapCoordinate(position[2], wrap_r);
  if (s < 0.0f || t < 0.0f || r < 0.0f)
    return;
  const GLenum filter = lod > 0.0f ? BaseFilter(min_filter) : mag_filter;
  const size_t slice = static_cast<size_t>(image->width) * image->height;
  const auto sample_slice = [&](int z, float rgba[4]) {
    rgba[0] = 0.0f;
    rgba[1] = 0.0f;
    rgba[2] = 0.0f;
    rgba[3] = 1.0f;
    if (filter == GL_NEAREST) {
      const int x =
          std::min(image->width - 1, static_cast<int>(s * image->width));
      const int y =
          std::min(image->height - 1, static_cast<int>(t * image->height));
      UnpackRgba(
          image->rgba[z * slice + static_cast<size_t>(y) * image->width + x],
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
      const int cx = ClampInt(x, 0, image->width - 1);
      const int cy = ClampInt(y, 0, image->height - 1);
      UnpackRgba(
          image->rgba[z * slice + static_cast<size_t>(cy) * image->width + cx],
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
    sample_slice(0, out);
    return;
  }
  const float pz = r * depth - 0.5f;
  const int z0 = ClampInt(static_cast<int>(std::floor(pz)), 0, depth - 1);
  const int z1 = ClampInt(z0 + 1, 0, depth - 1);
  const float fz = Clamp01(pz - static_cast<float>(z0));
  float a[4];
  float b[4];
  sample_slice(z0, a);
  sample_slice(z1, b);
  for (int i = 0; i < 4; ++i)
    out[i] = a[i] + (b[i] - a[i]) * fz;
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
    const int element =
        (type == GL_UNSIGNED_INT || type == GL_UNSIGNED_INT_24_8) ? 4 : 2;
    for (int y = 0; y < height; ++y) {
      const uint8_t* row = source + (static_cast<size_t>(skip_rows + y) *
                                         (row_length > 0 ? row_length : width) +
                                     skip_pixels) *
                                        element;
      for (int x = 0; x < width; ++x) {
        if (type == GL_FLOAT) {
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
  const int packed_size = IsPackedType(type) ? 2 : components * bytes;
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
      if (!source || !destination || source->rgba.empty()) {
        width = next_width;
        height = next_height;
        continue;
      }
      destination->width = next_width;
      destination->height = next_height;
      destination->depth = 1;
      destination->rgba.assign(static_cast<size_t>(next_width) * next_height,
                               0u);
      for (int y = 0; y < next_height; ++y) {
        for (int x = 0; x < next_width; ++x) {
          float accum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
          int count = 0;
          for (int dy = 0; dy < 2; ++dy) {
            for (int dx = 0; dx < 2; ++dx) {
              const int sx = ClampInt(x * 2 + dx, 0, width - 1);
              const int sy = ClampInt(y * 2 + dy, 0, height - 1);
              float rgba[4];
              UnpackRgba(source->rgba[static_cast<size_t>(sy) * width + sx],
                         rgba);
              for (int c = 0; c < 4; ++c)
                accum[c] += rgba[c];
              ++count;
            }
          }
          uint8_t bytes[4];
          for (int c = 0; c < 4; ++c)
            bytes[c] = FloatToUnorm8(accum[c] / static_cast<float>(count));
          destination->rgba[static_cast<size_t>(y) * next_width + x] =
              PackRgba(bytes[0], bytes[1], bytes[2], bytes[3]);
        }
      }
      width = next_width;
      height = next_height;
    }
  }
}

}  // namespace zgl
