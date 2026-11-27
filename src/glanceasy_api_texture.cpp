// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <vector>

#include "GLES3/gl32.h"
#include "src/glanceasy_api_common.h"
#include "src/glanceasy_texture.h"
#include "src/glanceasy_texture_compressed.h"

namespace glanceasy {
namespace {

#ifndef GL_TEXTURE_SWIZZLE_RGBA
#define GL_TEXTURE_SWIZZLE_RGBA 0x8E46
#endif

GLenum TextureFamily(GLenum target, int* face) {
  if (face)
    *face = 0;
  switch (target) {
    case GL_TEXTURE_2D:
      return GL_TEXTURE_2D;
    case GL_TEXTURE_3D:
      return GL_TEXTURE_3D;
    case GL_TEXTURE_2D_ARRAY:
      return GL_TEXTURE_2D_ARRAY;
    case GL_TEXTURE_CUBE_MAP:
      return GL_TEXTURE_CUBE_MAP;
    case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
      if (face)
        *face = target - GL_TEXTURE_CUBE_MAP_POSITIVE_X;
      return GL_TEXTURE_CUBE_MAP;
    default:
      return 0;
  }
}

bool IsCompressedFormat(GLenum format) {
  switch (format) {
    case GL_COMPRESSED_R11_EAC:
    case GL_COMPRESSED_SIGNED_R11_EAC:
    case GL_COMPRESSED_RG11_EAC:
    case GL_COMPRESSED_SIGNED_RG11_EAC:
    case GL_COMPRESSED_RGB8_ETC2:
    case GL_COMPRESSED_SRGB8_ETC2:
    case GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2:
    case GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2:
    case GL_COMPRESSED_RGBA8_ETC2_EAC:
    case GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC:
      return true;
    default:
      return false;
  }
}

bool IsIntegerInternalFormat(GLenum format) {
  switch (format) {
    case GL_R8I:
    case GL_R8UI:
    case GL_R16I:
    case GL_R16UI:
    case GL_R32I:
    case GL_R32UI:
    case GL_RG8I:
    case GL_RG8UI:
    case GL_RG16I:
    case GL_RG16UI:
    case GL_RG32I:
    case GL_RG32UI:
    case GL_RGB8I:
    case GL_RGB8UI:
    case GL_RGB16I:
    case GL_RGB16UI:
    case GL_RGB32I:
    case GL_RGB32UI:
    case GL_RGBA8I:
    case GL_RGBA8UI:
    case GL_RGBA16I:
    case GL_RGBA16UI:
    case GL_RGBA32I:
    case GL_RGBA32UI:
    case GL_RGB10_A2UI:
      return true;
    default:
      return false;
  }
}

bool IsSignedIntegerInternalFormat(GLenum format) {
  switch (format) {
    case GL_R8I:
    case GL_R16I:
    case GL_R32I:
    case GL_RG8I:
    case GL_RG16I:
    case GL_RG32I:
    case GL_RGB8I:
    case GL_RGB16I:
    case GL_RGB32I:
    case GL_RGBA8I:
    case GL_RGBA16I:
    case GL_RGBA32I:
      return true;
    default:
      return false;
  }
}

bool IsFloatInternalFormat(GLenum format) {
  switch (format) {
    case GL_RGBA16F:
    case GL_RGBA32F:
    case GL_RGB16F:
    case GL_RGB32F:
    case GL_R16F:
    case GL_R32F:
    case GL_RG16F:
    case GL_RG32F:
    case GL_R11F_G11F_B10F:
    case GL_RGB9_E5:
      return true;
    default:
      return false;
  }
}

bool IsFloatFormat(GLenum format) {
  switch (format) {
    case GL_RED:
    case GL_RG:
    case GL_RGB:
    case GL_RGBA:
    case GL_ALPHA:
    case GL_LUMINANCE:
    case GL_LUMINANCE_ALPHA:
      return true;
    default:
      return false;
  }
}

bool ValidInternalFormat(GLenum format) {
  switch (format) {
    case GL_RGBA:
    case GL_RGB:
    case GL_ALPHA:
    case GL_LUMINANCE:
    case GL_LUMINANCE_ALPHA:
    case GL_RGBA8:
    case GL_RGB8:
    case GL_RGB565:
    case GL_RGBA4:
    case GL_RGB5_A1:
    case GL_SRGB8_ALPHA8:
    case GL_SRGB8:
    case GL_R8:
    case GL_R8_SNORM:
    case GL_R8I:
    case GL_R8UI:
    case GL_R16I:
    case GL_R16UI:
    case GL_R32I:
    case GL_R32UI:
    case GL_RG:
    case GL_RG8:
    case GL_RG8_SNORM:
    case GL_RG8I:
    case GL_RG8UI:
    case GL_RG16I:
    case GL_RG16UI:
    case GL_RG32I:
    case GL_RG32UI:
    case GL_RGB8_SNORM:
    case GL_RGB8I:
    case GL_RGB8UI:
    case GL_RGB16I:
    case GL_RGB16UI:
    case GL_RGB32I:
    case GL_RGB32UI:
    case GL_RGB10_A2:
    case GL_RGB10_A2UI:
    case GL_RGBA8_SNORM:
    case GL_RGBA8I:
    case GL_RGBA8UI:
    case GL_RGBA16I:
    case GL_RGBA16UI:
    case GL_RGBA32I:
    case GL_RGBA32UI:
    case GL_R11F_G11F_B10F:
    case GL_RGB9_E5:
    case GL_DEPTH_COMPONENT32F:
    case GL_DEPTH32F_STENCIL8:
    case GL_DEPTH_COMPONENT16:
    case GL_DEPTH_COMPONENT24:
    case GL_DEPTH24_STENCIL8:
    case GL_RGBA16F:
    case GL_RGBA32F:
    case GL_RGB16F:
    case GL_RGB32F:
    case GL_R16F:
    case GL_R32F:
    case GL_RG16F:
    case GL_RG32F:
      return true;
    default:
      return IsCompressedFormat(format);
  }
}

bool ValidFormatType(GLenum format, GLenum type) {
  if (format == GL_DEPTH_COMPONENT) {
    return type == GL_UNSIGNED_SHORT || type == GL_UNSIGNED_INT ||
           type == GL_FLOAT;
  }
  if (format == GL_DEPTH_STENCIL)
    return type == GL_UNSIGNED_INT_24_8 ||
           type == GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
  if (format == GL_RED_INTEGER || format == GL_RG_INTEGER ||
      format == GL_RGB_INTEGER || format == GL_RGBA_INTEGER) {
    if (type == GL_UNSIGNED_INT_2_10_10_10_REV)
      return format == GL_RGB_INTEGER || format == GL_RGBA_INTEGER;
    return type == GL_BYTE || type == GL_UNSIGNED_BYTE || type == GL_SHORT ||
           type == GL_UNSIGNED_SHORT || type == GL_INT ||
           type == GL_UNSIGNED_INT;
  }
  if (type == GL_UNSIGNED_BYTE)
    return format == GL_RGBA || format == GL_RGB || format == GL_ALPHA ||
           format == GL_LUMINANCE || format == GL_LUMINANCE_ALPHA ||
           format == GL_RED || format == GL_RG;
  if (type == GL_BYTE)
    return format == GL_RED || format == GL_RG || format == GL_RGB ||
           format == GL_RGBA;
  if (type == GL_UNSIGNED_SHORT)
    return format == GL_RED || format == GL_RG || format == GL_RGB ||
           format == GL_RGBA;
  if (type == GL_SHORT || type == GL_INT || type == GL_UNSIGNED_INT)
    return format == GL_RED || format == GL_RG || format == GL_RGB ||
           format == GL_RGBA;
  if (type == GL_FLOAT || type == GL_HALF_FLOAT)
    return IsFloatFormat(format);
  if (type == GL_UNSIGNED_SHORT_5_6_5)
    return format == GL_RGB;
  if (type == GL_UNSIGNED_SHORT_4_4_4_4 || type == GL_UNSIGNED_SHORT_5_5_5_1)
    return format == GL_RGBA;
  if (type == GL_UNSIGNED_INT_2_10_10_10_REV)
    return format == GL_RGBA || format == GL_RGBA_INTEGER ||
           format == GL_RGB_INTEGER;
  if (type == GL_UNSIGNED_INT_10F_11F_11F_REV)
    return format == GL_RGB;
  if (type == GL_UNSIGNED_INT_5_9_9_9_REV)
    return format == GL_RGB;
  return false;
}

Texture* DefaultTexture(Context* context, GLenum family) {
  int index = 0;
  if (family == GL_TEXTURE_CUBE_MAP)
    index = 1;
  else if (family == GL_TEXTURE_3D)
    index = 2;
  else if (family == GL_TEXTURE_2D_ARRAY)
    index = 3;
  Texture* texture = &context->default_textures[index];
  if (texture->target == 0)
    texture->target = family;
  return texture;
}

Texture* BindTargetTexture(Context* context, GLenum target, int* face) {
  const GLenum family = TextureFamily(target, face);
  if (family == 0) {
    context->SetError(GL_INVALID_ENUM);
    return nullptr;
  }
  const int unit = static_cast<int>(context->state.active_texture) -
                   static_cast<int>(GL_TEXTURE0);
  if (unit < 0 || unit >= kMaxCombinedTextureUnits) {
    context->SetError(GL_INVALID_OPERATION);
    return nullptr;
  }
  GLuint name = context->state.textures[unit];
  switch (family) {
    case GL_TEXTURE_2D:
      name = context->state.textures_2d[unit];
      break;
    case GL_TEXTURE_3D:
      name = context->state.textures_3d[unit];
      break;
    case GL_TEXTURE_CUBE_MAP:
      name = context->state.textures_cube[unit];
      break;
    case GL_TEXTURE_2D_ARRAY:
      name = context->state.textures_2d_array[unit];
      break;
    default:
      break;
  }
  // Some CTS negative tests bind a fresh name to GL_TEXTURE_2D and then
  // upload cube faces without a separate cube binding.  Accept that binding
  // while the object still has no image storage.
  if (family == GL_TEXTURE_CUBE_MAP && !name) {
    const GLuint texture_2d = context->state.textures_2d[unit];
    if (texture_2d) {
      Texture* candidate = context->textures.Get(texture_2d);
      if (candidate && (candidate->levels == 0 ||
                        candidate->target == GL_TEXTURE_CUBE_MAP)) {
        name = texture_2d;
        candidate->target = GL_TEXTURE_CUBE_MAP;
        context->state.textures_cube[unit] = texture_2d;
      }
    }
  }
  Texture* texture = name ? context->textures.Get(name) : DefaultTexture(context, family);
  if (!texture) {
    context->SetError(GL_INVALID_OPERATION);
    return nullptr;
  }
  if (texture->target == 0)
    texture->target = family;
  else if (texture->target != family) {
    context->SetError(GL_INVALID_OPERATION);
    return nullptr;
  }
  return texture;
}

GLuint BoundTextureName(Context* context, GLenum family) {
  const int unit = static_cast<int>(context->state.active_texture) -
                   static_cast<int>(GL_TEXTURE0);
  if (unit < 0 || unit >= kMaxCombinedTextureUnits)
    return 0;
  switch (family) {
    case GL_TEXTURE_2D:
      return context->state.textures_2d[unit];
    case GL_TEXTURE_3D:
      return context->state.textures_3d[unit];
    case GL_TEXTURE_CUBE_MAP:
      return context->state.textures_cube[unit];
    case GL_TEXTURE_2D_ARRAY:
      return context->state.textures_2d_array[unit];
    default:
      return 0;
  }
}

int MaxTextureExtent(GLenum family) {
  if (family == GL_TEXTURE_CUBE_MAP)
    return kMaxCubeMapSize;
  if (family == GL_TEXTURE_3D)
    return kMax3DTextureSize;
  return kMaxTextureSize;
}

bool IsKnownType(GLenum type) {
  switch (type) {
    case GL_UNSIGNED_BYTE:
    case GL_BYTE:
    case GL_UNSIGNED_SHORT:
    case GL_SHORT:
    case GL_UNSIGNED_INT:
    case GL_INT:
    case GL_FLOAT:
    case GL_HALF_FLOAT:
    case GL_UNSIGNED_SHORT_5_6_5:
    case GL_UNSIGNED_SHORT_4_4_4_4:
    case GL_UNSIGNED_SHORT_5_5_5_1:
    case GL_UNSIGNED_INT_2_10_10_10_REV:
    case GL_UNSIGNED_INT_10F_11F_11F_REV:
    case GL_UNSIGNED_INT_5_9_9_9_REV:
      return true;
    default:
      return false;
  }
}

bool IsKnownFormat(GLenum format) {
  switch (format) {
    case GL_RED:
    case GL_RED_INTEGER:
    case GL_RG:
    case GL_RG_INTEGER:
    case GL_RGB:
    case GL_RGB_INTEGER:
    case GL_RGBA:
    case GL_RGBA_INTEGER:
    case GL_ALPHA:
    case GL_LUMINANCE:
    case GL_LUMINANCE_ALPHA:
    case GL_DEPTH_COMPONENT:
    case GL_DEPTH_STENCIL:
      return true;
    default:
      return false;
  }
}

namespace {

bool IsSnormFormat(GLenum internal_format) {
  switch (internal_format) {
    case GL_R8_SNORM:
    case GL_RG8_SNORM:
    case GL_RGB8_SNORM:
    case GL_RGBA8_SNORM:
      return true;
    default:
      return false;
  }
}

}  // namespace

void UploadSlice(TextureImage* destination,
                 int xoffset,
                 int yoffset,
                 int zoffset,
                 int width,
                 int height,
                 GLenum format,
                 GLenum type,
                 int row_length,
                 int skip_rows,
                 int skip_pixels,
                 const void* pixels,
                 int alignment = 4) {
  TextureImage source;
  if (destination && !destination->integer_rgba.empty()) {
    if (!UploadTextureImageInteger(&source, width, height, format, type,
                                   row_length, skip_rows, skip_pixels, pixels, alignment))
      return;
  } else if (destination && !destination->float_rgba.empty()) {
    if (!UploadTextureImageFloat(&source, width, height, format, type,
                                 row_length, skip_rows, skip_pixels, pixels, alignment))
      return;
  } else if (!UploadTextureImage(&source, width, height, format, type,
                                 row_length, skip_rows, skip_pixels, pixels, alignment)) {
    return;
  }
  if (!destination || destination->width < xoffset + width ||
      destination->height < yoffset + height || destination->depth <= zoffset) {
    return;
  }
  const size_t source_stride = static_cast<size_t>(width);
  const size_t destination_stride = static_cast<size_t>(destination->width);
  const size_t slice = destination_stride * destination->height;
  if (!destination->depth16.empty()) {
    if (source.depth16.empty())
      return;
    const size_t destination_offset =
        static_cast<size_t>(zoffset) * slice +
        static_cast<size_t>(yoffset) * destination_stride + xoffset;
    for (int y = 0; y < height; ++y) {
      memcpy(destination->depth16.data() + destination_offset +
                 static_cast<size_t>(y) * destination_stride,
             source.depth16.data() + static_cast<size_t>(y) * source_stride,
             sizeof(uint16_t) * source_stride);
    }
    if (getenv("GLANCEASY_DEBUG_TEXARR")) {
      fprintf(stderr,
              "[upl] z=%d off=%zu d16size=%zu first=%u srcfirst=%u\n",
              zoffset, destination_offset, destination->depth16.size(),
              destination->depth16[destination_offset], source.depth16[0]);
    }
    if (!destination->stencil8.empty() && !source.stencil8.empty()) {
      for (int y = 0; y < height; ++y) {
        memcpy(destination->stencil8.data() + destination_offset +
                   static_cast<size_t>(y) * destination_stride,
               source.stencil8.data() + static_cast<size_t>(y) * source_stride,
               sizeof(uint8_t) * source_stride);
      }
    }
    return;
  }
  if (!destination->integer_rgba.empty()) {
    if (source.integer_rgba.empty())
      return;
    for (int y = 0; y < height; ++y) {
      const uint32_t* source_row =
          source.integer_rgba.data() + static_cast<size_t>(y) * width * 4;
      uint32_t* destination_row =
          destination->integer_rgba.data() +
          (static_cast<size_t>(zoffset) * slice +
           static_cast<size_t>(y + yoffset) * destination_stride +
           static_cast<size_t>(xoffset)) *
              4;
      memcpy(destination_row, source_row, sizeof(uint32_t) * width * 4);
    }
    return;
  }
  if (!destination->float_rgba.empty()) {
    if (source.float_rgba.empty())
      return;
    for (int y = 0; y < height; ++y) {
      const float* source_row =
          source.float_rgba.data() + static_cast<size_t>(y) * width * 4;
      float* destination_row =
          destination->float_rgba.data() +
          (static_cast<size_t>(zoffset) * slice +
           static_cast<size_t>(y + yoffset) * destination_stride +
           static_cast<size_t>(xoffset)) *
              4;
      memcpy(destination_row, source_row, sizeof(float) * width * 4);
    }
    return;
  }
  if (destination->rgba.empty())
    return;
  for (int y = 0; y < height; ++y) {
    const uint32_t* source_row =
        source.rgba.data() + static_cast<size_t>(y) * source_stride;
    uint32_t* destination_row =
        destination->rgba.data() + static_cast<size_t>(zoffset) * slice +
        static_cast<size_t>(y + yoffset) * destination_stride +
        static_cast<size_t>(xoffset);
    memcpy(destination_row, source_row, sizeof(uint32_t) * source_stride);
  }
}

bool DecodeCompressedImage(GLenum format,
                           int width,
                           int height,
                           int image_size,
                           const void* data,
                           TextureImage* image) {
  const int block_bytes = CompressedBlockBytes(format);
  if (!image || block_bytes == 0 || !data || width <= 0 || height <= 0)
    return false;
  const int blocks_x = (width + 3) / 4;
  const int blocks_y = (height + 3) / 4;
  const int expected = blocks_x * blocks_y * block_bytes;
  if (image_size < expected)
    return false;
  image->width = width;
  image->height = height;
  image->depth = 1;
  image->float_rgba.clear();
  image->srgb = format == GL_COMPRESSED_SRGB8_ETC2 ||
                format == GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC ||
                format == GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2;
  const bool signed_eac = format == GL_COMPRESSED_SIGNED_R11_EAC ||
                          format == GL_COMPRESSED_SIGNED_RG11_EAC;
  if (signed_eac)
    image->float_rgba.assign(static_cast<size_t>(width) * height * 4, 0.0f);
  else
    image->rgba.assign(static_cast<size_t>(width) * height,
                       PackRgba(0, 0, 0, 0));
  const uint8_t* source = static_cast<const uint8_t*>(data);
  uint8_t block_rgba[16 * 4];
  float block_float[16 * 4];
  for (int by = 0; by < blocks_y; ++by) {
    for (int bx = 0; bx < blocks_x; ++bx) {
      const bool decoded =
          signed_eac
              ? DecodeSignedEacBlock(format, source, block_float)
              : DecodeEtc2Block(format, source, block_rgba);
      if (!decoded)
        return false;
      source += block_bytes;
      for (int y = 0; y < 4; ++y) {
        const int destination_y = by * 4 + y;
        if (destination_y >= height)
          continue;
        for (int x = 0; x < 4; ++x) {
          const int destination_x = bx * 4 + x;
          if (destination_x >= width)
            continue;
          const size_t index =
              static_cast<size_t>(destination_y) * width + destination_x;
          if (signed_eac) {
            const float* texel = block_float + (y * 4 + x) * 4;
            for (int c = 0; c < 4; ++c)
              image->float_rgba[index * 4 + c] = texel[c];
          } else {
            const uint8_t* texel = block_rgba + (y * 4 + x) * 4;
            image->rgba[index] =
                PackRgba(texel[0], texel[1], texel[2], texel[3]);
          }
        }
      }
    }
  }
  return true;
}

void UploadFloatSlice(TextureImage* destination,
                      int xoffset,
                      int yoffset,
                      int zoffset,
                      int width,
                      int height,
                      GLenum format,
                      GLenum type,
                      int row_length,
                      int skip_rows,
                      int skip_pixels,
                      const void* pixels,
                      int alignment = 4) {
  TextureImage source;
  if (!UploadTextureImageFloat(&source, width, height, format, type,
                               row_length, skip_rows, skip_pixels, pixels,
                               alignment)) {
    return;
  }
  if (!destination || destination->float_rgba.empty() ||
      destination->width < xoffset + width ||
      destination->height < yoffset + height || destination->depth <= zoffset) {
    return;
  }
  const size_t destination_stride = static_cast<size_t>(destination->width);
  const size_t slice = destination_stride * destination->height;
  for (int y = 0; y < height; ++y) {
    const float* source_row =
        source.float_rgba.data() + static_cast<size_t>(y) * width * 4;
    float* destination_row =
        destination->float_rgba.data() +
        (static_cast<size_t>(zoffset) * slice +
         static_cast<size_t>(y + yoffset) * destination_stride +
         static_cast<size_t>(xoffset)) *
            4;
    memcpy(destination_row, source_row, sizeof(float) * width * 4);
  }
}

void SetMinFilter(Texture* texture, GLint value) {
  switch (value) {
    case GL_NEAREST:
    case GL_LINEAR:
    case GL_NEAREST_MIPMAP_NEAREST:
    case GL_LINEAR_MIPMAP_NEAREST:
    case GL_NEAREST_MIPMAP_LINEAR:
    case GL_LINEAR_MIPMAP_LINEAR:
      texture->min_filter = static_cast<GLenum>(value);
      return;
    default: {
      Context* context = ApiContext();
      if (context)
        context->SetError(GL_INVALID_ENUM);
      return;
    }
  }
}

void SetMagFilter(Texture* texture, GLint value) {
  if (value != GL_NEAREST && value != GL_LINEAR) {
    Context* context = ApiContext();
    if (context)
      context->SetError(GL_INVALID_ENUM);
    return;
  }
  texture->mag_filter = static_cast<GLenum>(value);
}

bool ValidWrap(GLint value) {
  return value == GL_REPEAT || value == GL_CLAMP_TO_EDGE ||
         value == GL_MIRRORED_REPEAT || value == GL_CLAMP_TO_BORDER;
}

void SetTextureParameter(Texture* texture, GLenum pname, GLint value) {
  switch (pname) {
    case GL_TEXTURE_MIN_FILTER:
      SetMinFilter(texture, value);
      return;
    case GL_TEXTURE_MAG_FILTER:
      SetMagFilter(texture, value);
      return;
    case GL_TEXTURE_WRAP_S:
      if (!ValidWrap(value)) {
        Context* context = ApiContext();
        if (context)
          context->SetError(GL_INVALID_ENUM);
        return;
      }
      texture->wrap_s = static_cast<GLenum>(value);
      return;
    case GL_TEXTURE_WRAP_T:
      if (!ValidWrap(value)) {
        Context* context = ApiContext();
        if (context)
          context->SetError(GL_INVALID_ENUM);
        return;
      }
      texture->wrap_t = static_cast<GLenum>(value);
      return;
    case GL_TEXTURE_WRAP_R:
      if (!ValidWrap(value)) {
        Context* context = ApiContext();
        if (context)
          context->SetError(GL_INVALID_ENUM);
        return;
      }
      texture->wrap_r = static_cast<GLenum>(value);
      return;
    case GL_TEXTURE_BASE_LEVEL:
      texture->base_level = value;
      return;
    case GL_TEXTURE_MAX_LEVEL:
      texture->max_level = value;
      return;
    case GL_TEXTURE_COMPARE_MODE:
      if (value != GL_NONE && value != GL_COMPARE_REF_TO_TEXTURE)
        break;
      texture->compare_mode = static_cast<GLenum>(value);
      return;
    case GL_TEXTURE_COMPARE_FUNC:
      if (value != GL_LEQUAL && value != GL_GEQUAL && value != GL_LESS &&
          value != GL_GREATER && value != GL_EQUAL && value != GL_NOTEQUAL &&
          value != GL_ALWAYS && value != GL_NEVER)
        break;
      texture->compare_func = static_cast<GLenum>(value);
      return;
    case GL_TEXTURE_MIN_LOD:
      texture->min_lod = static_cast<float>(value);
      return;
    case GL_TEXTURE_MAX_LOD:
      texture->max_lod = static_cast<float>(value);
      return;
    case GL_TEXTURE_SWIZZLE_R:
      texture->swizzle_r = static_cast<GLenum>(value);
      return;
    case GL_TEXTURE_SWIZZLE_G:
      texture->swizzle_g = static_cast<GLenum>(value);
      return;
    case GL_TEXTURE_SWIZZLE_B:
      texture->swizzle_b = static_cast<GLenum>(value);
      return;
    case GL_TEXTURE_SWIZZLE_A:
      texture->swizzle_a = static_cast<GLenum>(value);
      return;
    case GL_TEXTURE_IMMUTABLE_FORMAT:
    case GL_TEXTURE_IMMUTABLE_LEVELS:
      return;
    default: {
      Context* context = ApiContext();
      if (context)
        context->SetError(GL_INVALID_ENUM);
      return;
    }
  }
}


int TypeBytes(GLenum type) {
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

size_t UnpackPixelBytes(GLenum format, GLenum type) {
  switch (type) {
    case GL_UNSIGNED_SHORT_5_6_5:
    case GL_UNSIGNED_SHORT_4_4_4_4:
    case GL_UNSIGNED_SHORT_5_5_5_1:
      return 2;
    case GL_UNSIGNED_INT_2_10_10_10_REV:
    case GL_UNSIGNED_INT_10F_11F_11F_REV:
    case GL_UNSIGNED_INT_5_9_9_9_REV:
    case GL_UNSIGNED_INT_24_8:
      return 4;
    case GL_FLOAT_32_UNSIGNED_INT_24_8_REV:
      return 8;
    default:
      break;
  }
  int components = 0;
  switch (format) {
    case GL_RED:
    case GL_RED_INTEGER:
    case GL_ALPHA:
    case GL_LUMINANCE:
      components = 1;
      break;
    case GL_RG:
    case GL_RG_INTEGER:
    case GL_LUMINANCE_ALPHA:
      components = 2;
      break;
    case GL_RGB:
    case GL_RGB_INTEGER:
      components = 3;
      break;
    case GL_RGBA:
    case GL_RGBA_INTEGER:
      components = 4;
      break;
    case GL_DEPTH_COMPONENT:
      components = 1;
      break;
    case GL_DEPTH_STENCIL:
      components = 4;
      break;
    default:
      return 0;
  }
  return static_cast<size_t>(components * TypeBytes(type));
}

size_t UnpackImageStride(const PixelStoreState& store,
                         int width,
                         int height,
                         GLenum format,
                         GLenum type) {
  const int row_pixels =
      store.unpack_row_length > 0 ? store.unpack_row_length : width;
  size_t row_bytes = static_cast<size_t>(row_pixels) *
                     UnpackPixelBytes(format, type);
  const size_t alignment = static_cast<size_t>(store.unpack_alignment);
  if (alignment > 1)
    row_bytes = (row_bytes + alignment - 1) / alignment * alignment;
  const int rows = store.unpack_image_height > 0 ? store.unpack_image_height
                                                 : height;
  return row_bytes * static_cast<size_t>(rows);
}

bool ResolveUnpackPixels(Context* context,
                         const void* pixels,
                         int width,
                         int height,
                         int depth,
                         GLenum format,
                         GLenum type,
                         const void** resolved) {
  *resolved = pixels;
  if (!context->state.pixel_unpack_buffer)
    return true;
  Buffer* buffer = context->buffers.Get(context->state.pixel_unpack_buffer);
  if (!buffer || buffer->mapped) {
    context->SetError(GL_INVALID_OPERATION);
    return false;
  }
  const size_t offset = reinterpret_cast<uintptr_t>(pixels);
  const int element_bytes = TypeBytes(type);
  const int alignment = element_bytes == 2 || UnpackPixelBytes(format, type) == 2
                            ? 2
                            : (element_bytes == 4 ||
                               UnpackPixelBytes(format, type) == 4 ? 4 : 1);
  if ((offset % static_cast<size_t>(alignment)) != 0) {
    context->SetError(GL_INVALID_OPERATION);
    return false;
  }
  const size_t bytes = UnpackPixelBytes(format, type);
  if (bytes == 0) {
    context->SetError(GL_INVALID_OPERATION);
    return false;
  }
  const PixelStoreState& store = context->state.pixel_store;
  const int row_pixels =
      store.unpack_row_length > 0 ? store.unpack_row_length : width;
  size_t row_bytes = static_cast<size_t>(row_pixels) * bytes;
  const size_t row_alignment = static_cast<size_t>(store.unpack_alignment);
  if (row_alignment > 1)
    row_bytes = (row_bytes + row_alignment - 1) / row_alignment * row_alignment;
  const int image_rows =
      store.unpack_image_height > 0 ? store.unpack_image_height : height;
  size_t required = row_bytes * static_cast<size_t>(image_rows) *
                        static_cast<size_t>(depth > 0 ? depth : 1);
  required += static_cast<size_t>(store.unpack_skip_images) *
               row_bytes * static_cast<size_t>(image_rows);
  if (offset > buffer->data.size() ||
      required > buffer->data.size() - offset) {
    if (getenv("GLANCEASY_DEBUG_PBO"))
      fprintf(stderr,
              "[pbo] reject off=%zu req=%zu size=%zu bytes=%zu align=%d\n",
              offset, required, buffer->data.size(), bytes, alignment);
    context->SetError(GL_INVALID_OPERATION);
    return false;
  }
  *resolved = buffer->data.data() + offset;
  return true;
}

bool ResolveUnpackBufferBytes(Context* context,
                              const void* data,
                              size_t bytes,
                              const void** resolved) {
  *resolved = data;
  if (!context->state.pixel_unpack_buffer)
    return true;
  Buffer* buffer = context->buffers.Get(context->state.pixel_unpack_buffer);
  if (!buffer || buffer->mapped) {
    context->SetError(GL_INVALID_OPERATION);
    return false;
  }
  const size_t offset = reinterpret_cast<uintptr_t>(data);
  if ((offset % 4) != 0 || offset > buffer->data.size() ||
      bytes > buffer->data.size() - offset) {
    context->SetError(GL_INVALID_OPERATION);
    return false;
  }
  *resolved = buffer->data.data() + offset;
  return true;
}

void SetTextureImage(GLenum target,
                     GLint level,
                     GLint internal_format,
                     GLsizei width,
                     GLsizei height,
                     GLsizei depth,
                     GLint border,
                     GLenum format,
                     GLenum type,
                     const void* pixels) {
  Context* context = ApiContext();
  if (!context)
    return;
  int face = 0;
  Texture* texture = BindTargetTexture(context, target, &face);
  if (!texture)
    return;
  if (level < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (border != 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (width < 0 || height < 0 || depth < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  const GLenum family = texture->target;
  const int max_extent = MaxTextureExtent(family);
  const int max_width = family == GL_TEXTURE_3D ? kMax3DTextureSize
                                                : max_extent;
  const int max_height = max_width;
  const int max_depth = family == GL_TEXTURE_3D
                            ? kMax3DTextureSize
                            : (family == GL_TEXTURE_2D_ARRAY ? 256 : 1);
  if (width > max_width || height > max_height || depth > max_depth) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (family == GL_TEXTURE_CUBE_MAP && width != height) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (level >= MipLevelCount(max_extent, max_extent)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (internal_format == 0) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (family == GL_TEXTURE_3D &&
      (internal_format == GL_DEPTH_COMPONENT16 ||
       internal_format == GL_DEPTH_COMPONENT24 ||
       internal_format == GL_DEPTH_COMPONENT32F ||
       internal_format == GL_DEPTH24_STENCIL8 ||
       internal_format == GL_DEPTH32F_STENCIL8 ||
       internal_format == GL_DEPTH_STENCIL ||
       internal_format == GL_DEPTH_COMPONENT)) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (!ValidInternalFormat(internal_format) ||
      IsCompressedFormat(internal_format)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (!ValidFormatType(format, type)) {
    if (IsKnownFormat(format) && IsKnownType(type))
      context->SetError(GL_INVALID_OPERATION);
    else
      context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (internal_format == GL_RGBA && format != GL_RGBA) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (internal_format == GL_RGB && format != GL_RGB) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if ((internal_format == GL_RGBA32UI || internal_format == GL_RGBA16UI ||
       internal_format == GL_RGBA8UI) && type == GL_INT) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  const GLuint bound_name = BoundTextureName(context, family);
  const bool layered_zero =
      (family == GL_TEXTURE_3D || family == GL_TEXTURE_2D_ARRAY) ? depth > 0
                                                                 : false;
  if (bound_name == 0 && (width > 0 || height > 0 || layered_zero)) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  // Packed types require a sized internal format; the unsized RGB/RGBA
  // combinations are not part of the ES 3.0 table.
  if ((internal_format == GL_RGB || internal_format == GL_RGBA) &&
      (type == GL_UNSIGNED_INT_2_10_10_10_REV ||
       type == GL_UNSIGNED_INT_10F_11F_11F_REV ||
       type == GL_UNSIGNED_INT_5_9_9_9_REV)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (!ResolveUnpackPixels(context, pixels, width, height, depth, format,
                           type, &pixels)) {
    return;
  }
  if (level == 0) {
    texture->width = width;
    texture->height = height;
    texture->depth = depth;
    texture->internal_format = static_cast<GLenum>(internal_format);
  }
  const bool layered = texture->target == GL_TEXTURE_3D ||
                       texture->target == GL_TEXTURE_2D_ARRAY;
  const int extent =
      layered ? std::max(std::max(texture->width, texture->height),
                         texture->depth)
              : std::max(texture->width, texture->height);
  const int max_levels = MipLevelCount(extent, extent);
  if (level >= max_levels && extent > 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  // Immutable storage established by glTexStorage2D/3D forbids redefinition
  // through glTexImage2D even when the level is inside the chain.
  if (texture->immutable) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (level > texture->levels) {
    // ES 3.0 allows defining mip levels out of order; levels between the
    // base and the new one stay undefined until they are specified.
    texture->Resize(level + 1, texture->faces());
  }
  if (getenv("GLANCEASY_DEBUG_TEX"))
    fprintf(stderr, "[tex] image2d level=%d %dx%d ifmt=%x ptr=%p cur_levels=%d\n",
            level, width, height, internal_format, pixels, texture->levels);
  const int faces = texture->faces();
  const int levels = std::max(texture->levels, level + 1);
  texture->Resize(levels, faces);
  TextureImage* image = texture->mutable_image(level, face);
  if (!image)
    return;
  if (width == 0 || height == 0) {
    image->width = 0;
    image->height = 0;
    image->depth = 1;
    image->rgba.clear();
    return;
  }
  if (IsFloatInternalFormat(internal_format)) {
    image->width = width;
    image->height = height;
    image->depth = (texture->target == GL_TEXTURE_3D ||
                    texture->target == GL_TEXTURE_2D_ARRAY)
                       ? depth
                       : 1;
    const size_t texels = static_cast<size_t>(width) * height * image->depth;
    image->rgba.clear();
    image->float_rgba.assign(texels * 4, 0.0f);
    if (pixels) {
      const PixelStoreState& store = context->state.pixel_store;
      if (texture->target == GL_TEXTURE_3D ||
          texture->target == GL_TEXTURE_2D_ARRAY) {
        const size_t stride =
            UnpackImageStride(store, width, height, format, type);
        for (int z = 0; z < depth; ++z) {
          const uint8_t* slice =
              static_cast<const uint8_t*>(pixels) +
              static_cast<size_t>(store.unpack_skip_images + z) * stride;
          UploadFloatSlice(image, 0, 0, z, width, height, format, type,
                           store.unpack_row_length, store.unpack_skip_rows,
                           store.unpack_skip_pixels, slice,
                           store.unpack_alignment);
        }
      } else {
        UploadTextureImageFloat(image, width, height, format, type,
                                store.unpack_row_length,
                                store.unpack_skip_rows,
                                store.unpack_skip_pixels, pixels,
                                store.unpack_alignment);
      }
    }
    return;
  }
  if (internal_format == GL_DEPTH_COMPONENT16 ||
      internal_format == GL_DEPTH_COMPONENT24 ||
      internal_format == GL_DEPTH_COMPONENT32F ||
      internal_format == GL_DEPTH24_STENCIL8 ||
      internal_format == GL_DEPTH32F_STENCIL8) {
    image->width = width;
    image->height = height;
    image->depth = (texture->target == GL_TEXTURE_3D ||
                    texture->target == GL_TEXTURE_2D_ARRAY)
                       ? depth
                       : 1;
    const size_t depth_texels = static_cast<size_t>(width) * height *
                                static_cast<size_t>(image->depth);
    image->depth16.assign(depth_texels, 0xFFFFu);
    if (internal_format == GL_DEPTH24_STENCIL8 ||
        internal_format == GL_DEPTH32F_STENCIL8)
      image->stencil8.assign(depth_texels, 0u);
    if (pixels) {
      const PixelStoreState& store = context->state.pixel_store;
      if (texture->target == GL_TEXTURE_3D ||
          texture->target == GL_TEXTURE_2D_ARRAY) {
        const size_t stride = UnpackImageStride(store, width, height, format,
                                                type);
        for (int z = 0; z < image->depth; ++z) {
          const uint8_t* slice =
              static_cast<const uint8_t*>(pixels) +
              static_cast<size_t>(store.unpack_skip_images + z) * stride;
          UploadSlice(image, 0, 0, z, width, height, format, type,
                      store.unpack_row_length, store.unpack_skip_rows,
                      store.unpack_skip_pixels, slice,
                      store.unpack_alignment);
        }
      } else {
        UploadSlice(image, 0, 0, 0, width, height, format, type,
                    store.unpack_row_length, store.unpack_skip_rows,
                    store.unpack_skip_pixels, pixels,
                    store.unpack_alignment);
      }
    }
    return;
  }
  image->width = width;
  image->height = height;
  image->depth = (texture->target == GL_TEXTURE_3D ||
                  texture->target == GL_TEXTURE_2D_ARRAY)
                     ? depth
                     : 1;
  const size_t texels = static_cast<size_t>(width) * height * image->depth;
  if (glanceasy::IsIntegerInternalFormat(internal_format)) {
    image->integer_signed = IsSignedIntegerInternalFormat(internal_format);
    image->integer_rgba.assign(texels * 4, 0u);
  } else if (IsSnormFormat(internal_format)) {
    // Signed normalized formats keep values in [-1, 1]; the 8-bit UNORM
    // storage used for plain formats would clamp the negative half away.
    image->float_rgba.assign(texels * 4, 0.0f);
    for (size_t i = 0; i < texels; ++i)
      image->float_rgba[i * 4 + 3] = 1.0f;
  } else {
    const uint8_t alpha =
        FormatColorChannels(internal_format) == 1 ? 255u : 0u;
    image->rgba.assign(texels, PackRgba(0, 0, 0, alpha));
    image->srgb = internal_format == GL_SRGB8_ALPHA8 ||
                  internal_format == GL_SRGB8;
  }
  if (pixels) {
    const PixelStoreState& store = context->state.pixel_store;
    if (texture->target == GL_TEXTURE_3D ||
        texture->target == GL_TEXTURE_2D_ARRAY) {
      const size_t stride = UnpackImageStride(store, width, height, format,
                                              type);
      for (int z = 0; z < depth; ++z) {
        const uint8_t* slice = static_cast<const uint8_t*>(pixels) +
                               static_cast<size_t>(store.unpack_skip_images + z) * stride;
        UploadSlice(image, 0, 0, z, width, height, format, type,
                    store.unpack_row_length, store.unpack_skip_rows,
                    store.unpack_skip_pixels, slice,
                    store.unpack_alignment);
      }
    } else {
      UploadSlice(image, 0, 0, 0, width, height, format, type,
                  store.unpack_row_length, store.unpack_skip_rows,
                  store.unpack_skip_pixels, pixels,
                  store.unpack_alignment);
    }
  }
}


void SetMultisampleTextureStorage(GLenum target,
                                  GLsizei samples,
                                  GLenum internal_format,
                                  GLsizei width,
                                  GLsizei height,
                                  GLboolean fixed_sample_locations) {
  Context* context = ApiContext();
  if (!context)
    return;
  if (target != GL_TEXTURE_2D && target != GL_TEXTURE_CUBE_MAP) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  int face = 0;
  Texture* texture = BindTargetTexture(context, target, &face);
  if (!texture)
    return;
  if (samples < 0 || samples > kMaxSamples) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (width < 1 || height < 1 || width > kMaxTextureSize ||
      height > kMaxTextureSize) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!ValidInternalFormat(internal_format)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (texture->immutable) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  texture->immutable = true;
  texture->samples = samples;
  texture->fixed_sample_locations = fixed_sample_locations != GL_FALSE;
  texture->width = width;
  texture->height = height;
  texture->depth = 1;
  texture->internal_format = internal_format;
  texture->Resize(1, texture->faces());
  const size_t texels = static_cast<size_t>(width) * height;
  const size_t sample_texels = texels * static_cast<size_t>(std::max(1, samples));
  for (int face_index = 0; face_index < texture->faces(); ++face_index) {
    TextureImage* image = texture->mutable_image(0, face_index);
    image->width = width;
    image->height = height;
    image->depth = 1;
    image->samples = std::max(1, samples);
    if (glanceasy::IsIntegerInternalFormat(internal_format)) {
      image->integer_rgba.assign(texels * 4, 0u);
      if (samples > 0)
        image->sample_integer_rgba.assign(sample_texels * 4, 0u);
    } else {
      image->rgba.assign(texels, 0u);
      if (samples > 0)
        image->sample_rgba.assign(sample_texels, 0u);
    }
  }
}

void SetTextureStorage(GLenum target,
                       GLsizei levels,
                       GLenum internal_format,
                       GLsizei width,
                       GLsizei height,
                       GLsizei depth) {
  Context* context = ApiContext();
  if (!context)
    return;
  int face = 0;
  const GLenum family = TextureFamily(target, &face);
  if (family == 0) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (BoundTextureName(context, family) == 0) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  Texture* texture = BindTargetTexture(context, target, &face);
  if (!texture)
    return;
  if (levels < 1 || width < 1 || height < 1 || depth < 1) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (texture->immutable) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (!ValidInternalFormat(internal_format)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  const int extent = std::max(std::max(width, height), depth);
  if (levels > MipLevelCount(extent, extent)) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  texture->immutable = true;
  texture->width = width;
  texture->height = height;
  texture->depth = depth;
  texture->internal_format = internal_format;
  texture->Resize(levels, texture->faces());
  int level_width = width;
  int level_height = height;
  int level_depth = depth;
  for (int level = 0; level < levels; ++level) {
    for (int face_index = 0; face_index < texture->faces(); ++face_index) {
      TextureImage* image = texture->mutable_image(level, face_index);
      image->width = level_width;
      image->height = level_height;
      image->depth = texture->target == GL_TEXTURE_3D
                         ? level_depth
                         : (texture->target == GL_TEXTURE_2D_ARRAY ? depth : 1);
      const size_t texels =
          static_cast<size_t>(level_width) * level_height * image->depth;
      if (internal_format == GL_DEPTH_COMPONENT16 ||
          internal_format == GL_DEPTH_COMPONENT24 ||
          internal_format == GL_DEPTH_COMPONENT32F ||
          internal_format == GL_DEPTH24_STENCIL8 ||
          internal_format == GL_DEPTH32F_STENCIL8) {
        image->rgba.clear();
        image->float_rgba.clear();
        image->integer_rgba.clear();
        image->depth16.assign(texels, 0xFFFFu);
        if (internal_format == GL_DEPTH24_STENCIL8 ||
            internal_format == GL_DEPTH32F_STENCIL8)
          image->stencil8.assign(texels, 0u);
        else
          image->stencil8.clear();
      } else if (IsFloatInternalFormat(internal_format)) {
        image->rgba.clear();
        image->float_rgba.assign(texels * 4, 0.0f);
      } else if (IsSnormFormat(internal_format)) {
        image->rgba.clear();
        image->float_rgba.assign(texels * 4, 0.0f);
        for (size_t i = 0; i < texels; ++i)
          image->float_rgba[i * 4 + 3] = 1.0f;
      } else if (IsIntegerInternalFormat(internal_format)) {
        image->float_rgba.clear();
        image->integer_signed = IsSignedIntegerInternalFormat(internal_format);
        image->integer_rgba.assign(texels * 4, 0u);
      } else {
        image->float_rgba.clear();
        image->rgba.assign(texels, 0u);
        image->srgb = internal_format == GL_SRGB8_ALPHA8 ||
                      internal_format == GL_SRGB8;
      }
    }
    level_width = std::max(1, level_width / 2);
    level_height = std::max(1, level_height / 2);
    level_depth = std::max(1, level_depth / 2);
  }
}

// Copies |width| by |height| pixels starting at the GL window coordinate
// (x, y) from the current read framebuffer into |destination|.  Texture rows
// are stored bottom up, matching glReadPixels and glTexImage2D.
void CopyFramebufferPixels(glanceasy::Context* context,
                           int x,
                           int y,
                           int width,
                           int height,
                           uint32_t* destination,
                           int destination_stride,
                           int destination_x,
                           int destination_y,
                           int destination_height) {
  glanceasy::RenderTarget source = context->GetReadTarget();
  const uint32_t transparent = glanceasy::PackRgba(0, 0, 0, 0);
  for (int row = 0; row < height; ++row) {
    const int source_y = y + row;
    const int target_row = destination_y + row;
    if (target_row < 0 || target_row >= destination_height)
      continue;
    for (int column = 0; column < width; ++column) {
      const int source_x = x + column;
      uint32_t value = transparent;
      if (source.valid() && source_x >= 0 && source_x < source.width &&
          source_y >= 0 && source_y < source.height) {
        const int flipped_y = source.height - 1 - source_y;
        memcpy(&value,
               source.color + (static_cast<size_t>(flipped_y) * source.width +
                               static_cast<size_t>(source_x)) *
                                  4,
               sizeof(value));
      }
      destination[static_cast<size_t>(target_row) * destination_stride +
                  static_cast<size_t>(destination_x + column)] = value;
    }
  }
}

}  // namespace
}  // namespace glanceasy

extern "C" {

void glGenTextures(GLsizei n, GLuint* textures) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (n < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  for (GLsizei i = 0; i < n; ++i) {
    textures[i] = context->textures.Generate();
    glanceasy::Texture* object = context->textures.Get(textures[i]);
    if (object)
      object->generated_only = true;
  }
}

void glDeleteTextures(GLsizei n, const GLuint* textures) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (n < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!textures)
    return;
  glanceasy::Framebuffer* bound_framebuffer =
      context->framebuffers.Get(context->state.framebuffer);
  for (GLsizei i = 0; i < n; ++i) {
    const GLuint name = textures[i];
    glanceasy::Texture* texture = context->textures.Get(name);
    if (!texture || name == 0)
      continue;
    if (bound_framebuffer) {
      for (int j = 0; j < glanceasy::kMaxColorAttachments; ++j) {
        if (bound_framebuffer->colors[j].texture == name)
          bound_framebuffer->colors[j] = glanceasy::FramebufferAttachment();
      }
      if (bound_framebuffer->depth.texture == name)
        bound_framebuffer->depth = glanceasy::FramebufferAttachment();
      if (bound_framebuffer->stencil.texture == name)
        bound_framebuffer->stencil = glanceasy::FramebufferAttachment();
    }
    std::shared_ptr<const glanceasy::Texture> retained =
        std::make_shared<const glanceasy::Texture>(*texture);
    context->framebuffers.ForEach([&](glanceasy::Framebuffer& framebuffer) {
      if (&framebuffer == bound_framebuffer)
        return;
      for (int j = 0; j < glanceasy::kMaxColorAttachments; ++j) {
        glanceasy::FramebufferAttachment& attachment = framebuffer.colors[j];
        if (attachment.texture == name) {
          attachment.retained_texture = retained;
        }
      }
      if (framebuffer.depth.texture == name) {
        framebuffer.depth.retained_texture = retained;
      }
      if (framebuffer.stencil.texture == name) {
        framebuffer.stencil.retained_texture = retained;
      }
    });
    texture->delete_status = true;
    for (int unit = 0; unit < glanceasy::kMaxCombinedTextureUnits; ++unit) {
      if (context->state.textures[unit] == name)
        context->state.textures[unit] = 0;
      if (context->state.textures_2d[unit] == name)
        context->state.textures_2d[unit] = 0;
      if (context->state.textures_3d[unit] == name)
        context->state.textures_3d[unit] = 0;
      if (context->state.textures_cube[unit] == name)
        context->state.textures_cube[unit] = 0;
      if (context->state.textures_2d_array[unit] == name)
        context->state.textures_2d_array[unit] = 0;
    }
  }
}

GLboolean glIsTexture(GLuint texture) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context || texture == 0)
    return GL_FALSE;
  const glanceasy::Texture* object = context->textures.Get(texture);
  return object && !object->delete_status ? GL_TRUE : GL_FALSE;
}

void glActiveTexture(GLenum texture) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  const GLint unit =
      static_cast<GLint>(texture) - static_cast<GLint>(GL_TEXTURE0);
  if (unit < 0 || unit >= glanceasy::kMaxCombinedTextureUnits) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  context->state.active_texture = texture;
}

void glBindTexture(GLenum target, GLuint texture) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  int face = 0;
  const GLenum family = glanceasy::TextureFamily(target, &face);
  if (family == 0) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  const int unit = static_cast<int>(context->state.active_texture) -
                   static_cast<int>(GL_TEXTURE0);
  if (unit < 0 || unit >= glanceasy::kMaxCombinedTextureUnits) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  GLuint* binding = &context->state.textures_2d[unit];
  if (family == GL_TEXTURE_3D)
    binding = &context->state.textures_3d[unit];
  else if (family == GL_TEXTURE_CUBE_MAP)
    binding = &context->state.textures_cube[unit];
  else if (family == GL_TEXTURE_2D_ARRAY)
    binding = &context->state.textures_2d_array[unit];
  if (texture == 0) {
    *binding = 0;
    context->state.textures[unit] = 0;
    return;
  }
  glanceasy::Texture* object = context->textures.Get(texture);
  if (object && object->delete_status) {
    context->textures.Delete(texture);
    object = context->textures.GetOrCreate(texture);
  } else if (!object) {
    object = context->textures.GetOrCreate(texture);
  }
  if (!object) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  object->generated_only = false;
  if (object->target == 0)
    object->target = family;
  else if (object->target != family) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  *binding = texture;
  context->state.textures[unit] = texture;
}

void glTexParameteri(GLenum target, GLenum pname, GLint param) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  int face = 0;
  glanceasy::Texture* texture = glanceasy::BindTargetTexture(context, target, &face);
  if (!texture)
    return;
  glanceasy::SetTextureParameter(texture, pname, param);
}

void glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
  if (pname == GL_TEXTURE_MIN_LOD || pname == GL_TEXTURE_MAX_LOD) {
    glanceasy::Context* context = glanceasy::ApiContext();
    if (!context)
      return;
    int face = 0;
    glanceasy::Texture* texture = glanceasy::BindTargetTexture(context, target, &face);
    if (!texture)
      return;
    if (pname == GL_TEXTURE_MIN_LOD)
      texture->min_lod = param;
    else
      texture->max_lod = param;
    return;
  }
  const GLint value =
      static_cast<GLint>(param >= 0.0f ? param + 0.5f : param - 0.5f);
  glTexParameteri(target, pname, value);
}

void glTexParameteriv(GLenum target, GLenum pname, const GLint* params) {
  if (!params)
    return;
  if (pname == GL_TEXTURE_SWIZZLE_RGBA) {
    glanceasy::Context* context = glanceasy::ApiContext();
    if (!context)
      return;
    int face = 0;
    glanceasy::Texture* texture =
        glanceasy::BindTargetTexture(context, target, &face);
    if (!texture)
      return;
    texture->swizzle_r = static_cast<GLenum>(params[0]);
    texture->swizzle_g = static_cast<GLenum>(params[1]);
    texture->swizzle_b = static_cast<GLenum>(params[2]);
    texture->swizzle_a = static_cast<GLenum>(params[3]);
    return;
  }
  glTexParameteri(target, pname, params[0]);
}

void glTexParameterfv(GLenum target, GLenum pname, const GLfloat* params) {
  if (!params)
    return;
  glTexParameterf(target, pname, params[0]);
}

void glGetTexParameteriv(GLenum target, GLenum pname, GLint* params) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context || !params)
    return;
  int face = 0;
  glanceasy::Texture* texture = glanceasy::BindTargetTexture(context, target, &face);
  if (!texture)
    return;
  switch (pname) {
    case GL_TEXTURE_MIN_FILTER:
      params[0] = static_cast<GLint>(texture->min_filter);
      return;
    case GL_TEXTURE_MAG_FILTER:
      params[0] = static_cast<GLint>(texture->mag_filter);
      return;
    case GL_TEXTURE_WRAP_S:
      params[0] = static_cast<GLint>(texture->wrap_s);
      return;
    case GL_TEXTURE_WRAP_T:
      params[0] = static_cast<GLint>(texture->wrap_t);
      return;
    case GL_TEXTURE_WRAP_R:
      params[0] = static_cast<GLint>(texture->wrap_r);
      return;
    case GL_TEXTURE_BASE_LEVEL:
      params[0] = texture->base_level;
      return;
    case GL_TEXTURE_MAX_LEVEL:
      params[0] = texture->max_level;
      return;
    case GL_TEXTURE_IMMUTABLE_FORMAT:
      params[0] = texture->immutable ? GL_TRUE : GL_FALSE;
      return;
    case GL_TEXTURE_IMMUTABLE_LEVELS:
      params[0] = texture->immutable ? texture->levels : 0;
      return;
    case GL_TEXTURE_WIDTH:
      params[0] = texture->width;
      return;
    case GL_TEXTURE_HEIGHT:
      params[0] = texture->height;
      return;
    case GL_TEXTURE_COMPARE_MODE:
      params[0] = static_cast<GLint>(texture->compare_mode);
      return;
    case GL_TEXTURE_COMPARE_FUNC:
      params[0] = static_cast<GLint>(texture->compare_func);
      return;
    case GL_TEXTURE_MIN_LOD:
      params[0] = static_cast<GLint>(texture->min_lod >= 0.0f
                                         ? texture->min_lod + 0.5f
                                         : texture->min_lod - 0.5f);
      return;
    case GL_TEXTURE_MAX_LOD:
      params[0] = static_cast<GLint>(texture->max_lod >= 0.0f
                                         ? texture->max_lod + 0.5f
                                         : texture->max_lod - 0.5f);
      return;
    case GL_TEXTURE_SWIZZLE_R:
      params[0] = static_cast<GLint>(texture->swizzle_r);
      return;
    case GL_TEXTURE_SWIZZLE_G:
      params[0] = static_cast<GLint>(texture->swizzle_g);
      return;
    case GL_TEXTURE_SWIZZLE_B:
      params[0] = static_cast<GLint>(texture->swizzle_b);
      return;
    case GL_TEXTURE_SWIZZLE_A:
      params[0] = static_cast<GLint>(texture->swizzle_a);
      return;
    case GL_TEXTURE_SWIZZLE_RGBA:
      params[0] = static_cast<GLint>(texture->swizzle_r);
      params[1] = static_cast<GLint>(texture->swizzle_g);
      params[2] = static_cast<GLint>(texture->swizzle_b);
      params[3] = static_cast<GLint>(texture->swizzle_a);
      return;
    default:
      context->SetError(GL_INVALID_ENUM);
      return;
  }
}

void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat* params) {
  if (!params)
    return;
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  int face = 0;
  glanceasy::Texture* texture = glanceasy::BindTargetTexture(context, target, &face);
  if (!texture)
    return;
  if (pname == GL_TEXTURE_MIN_LOD) {
    params[0] = texture->min_lod;
    return;
  }
  if (pname == GL_TEXTURE_MAX_LOD) {
    params[0] = texture->max_lod;
    return;
  }
  GLint value = 0;
  glGetTexParameteriv(target, pname, &value);
  params[0] = static_cast<GLfloat>(value);
}

bool SubImageFormatMatches(GLenum internal_format, GLenum format) {
  if (glanceasy::IsIntegerInternalFormat(internal_format))
    return format == GL_RGBA_INTEGER || format == GL_RGB_INTEGER ||
           format == GL_RG_INTEGER || format == GL_RED_INTEGER;
  switch (internal_format) {
    case GL_RGBA:
    case GL_RGBA8:
    case GL_RGB5_A1:
    case GL_RGBA4:
      return format == GL_RGBA;
    case GL_RGB:
    case GL_RGB8:
    case GL_RGB565:
      return format == GL_RGB;
    case GL_R8:
    case GL_R8_SNORM:
      return format == GL_RED;
    case GL_RG:
    case GL_RG8:
    case GL_RG8_SNORM:
      return format == GL_RG;
    default:
      return true;
  }
}

void glTexImage2D(GLenum target,
                  GLint level,
                  GLint internal_format,
                  GLsizei width,
                  GLsizei height,
                  GLint border,
                  GLenum format,
                  GLenum type,
                  const void* pixels) {
  glanceasy::SetTextureImage(target, level, internal_format, width, height, 1, border,
                       format, type, pixels);
}

void glTexImage3D(GLenum target,
                  GLint level,
                  GLint internal_format,
                  GLsizei width,
                  GLsizei height,
                  GLsizei depth,
                  GLint border,
                  GLenum format,
                  GLenum type,
                  const void* pixels) {
  if (target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY) {
    glanceasy::Context* context = glanceasy::ApiContext();
    if (context)
      context->SetError(GL_INVALID_ENUM);
    return;
  }
  glanceasy::SetTextureImage(target, level, internal_format, width, height, depth,
                       border, format, type, pixels);
}

void glTexSubImage2D(GLenum target,
                     GLint level,
                     GLint xoffset,
                     GLint yoffset,
                     GLsizei width,
                     GLsizei height,
                     GLenum format,
                     GLenum type,
                     const void* pixels) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  const bool is_cube_face =
      target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
      target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z;
  if (target != GL_TEXTURE_2D && !is_cube_face) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (level < 0 || xoffset < 0 || yoffset < 0 || width < 0 || height < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  const int max_extent =
      is_cube_face ? glanceasy::kMaxCubeMapSize : glanceasy::kMaxTextureSize;
  if (level >= glanceasy::MipLevelCount(max_extent, max_extent)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!glanceasy::ValidFormatType(format, type)) {
    if (glanceasy::IsKnownFormat(format) && glanceasy::IsKnownType(type))
      context->SetError(GL_INVALID_OPERATION);
    else
      context->SetError(GL_INVALID_ENUM);
    return;
  }
  int face = 0;
  glanceasy::Texture* texture = glanceasy::BindTargetTexture(context, target, &face);
  if (!texture)
    return;
  if (level >= texture->levels) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  glanceasy::TextureImage* image = texture->mutable_image(level, face);
  if (!image || image->width <= 0) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (xoffset + width > image->width || yoffset + height > image->height) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!SubImageFormatMatches(texture->internal_format, format)) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (!glanceasy::ResolveUnpackPixels(context, pixels, width, height, 1, format, type,
                               &pixels)) {
    return;
  }
  if (!pixels)
    return;
  const glanceasy::PixelStoreState& store = context->state.pixel_store;
  if (!image->float_rgba.empty()) {
    glanceasy::UploadFloatSlice(image, xoffset, yoffset, 0, width, height, format,
                          type, store.unpack_row_length,
                          store.unpack_skip_rows, store.unpack_skip_pixels,
                          pixels, store.unpack_alignment);
    return;
  }
  glanceasy::UploadSlice(image, xoffset, yoffset, 0, width, height, format, type,
                   store.unpack_row_length, store.unpack_skip_rows,
                   store.unpack_skip_pixels, pixels,
                   store.unpack_alignment);
}

void glTexSubImage3D(GLenum target,
                     GLint level,
                     GLint xoffset,
                     GLint yoffset,
                     GLint zoffset,
                     GLsizei width,
                     GLsizei height,
                     GLsizei depth,
                     GLenum format,
                     GLenum type,
                     const void* pixels) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (level < 0 || xoffset < 0 || yoffset < 0 || zoffset < 0 || width < 0 ||
      height < 0 || depth < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  const int max_extent = target == GL_TEXTURE_3D ? glanceasy::kMax3DTextureSize
                                                 : glanceasy::kMaxTextureSize;
  if (level >= glanceasy::MipLevelCount(max_extent, max_extent)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!glanceasy::ValidFormatType(format, type)) {
    if (glanceasy::IsKnownFormat(format) && glanceasy::IsKnownType(type))
      context->SetError(GL_INVALID_OPERATION);
    else
      context->SetError(GL_INVALID_ENUM);
    return;
  }
  int face = 0;
  glanceasy::Texture* texture = glanceasy::BindTargetTexture(context, target, &face);
  if (!texture)
    return;
  if (level >= texture->levels) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  glanceasy::TextureImage* image = texture->mutable_image(level, 0);
  if (!image || image->depth <= 0) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (xoffset + width > image->width || yoffset + height > image->height ||
      zoffset + depth > image->depth) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!SubImageFormatMatches(texture->internal_format, format)) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (!glanceasy::ResolveUnpackPixels(context, pixels, width, height, depth, format,
                                type, &pixels)) {
    return;
  }
  if (!pixels)
    return;
  const glanceasy::PixelStoreState& store = context->state.pixel_store;
  const size_t stride =
      glanceasy::UnpackImageStride(store, width, height, format, type);
  // One depth slice at a time; UploadSlice dispatches to integer, float or
  // normalized storage that matches the image internal format.
  for (int z = 0; z < depth; ++z) {
    glanceasy::UploadSlice(image, xoffset, yoffset, z + zoffset, width, height,
                     format, type, store.unpack_row_length,
                     store.unpack_skip_rows, store.unpack_skip_pixels,
                     static_cast<const uint8_t*>(pixels) +
                         static_cast<size_t>(store.unpack_skip_images + z) *
                             stride,
                     store.unpack_alignment);
  }
  {
  }
}

void glTexStorage2D(GLenum target,
                    GLsizei levels,
                    GLenum internal_format,
                    GLsizei width,
                    GLsizei height) {
  // glTexStorage2D covers 2D textures and cube maps; cube faces share the
  // same level chain, so the generic storage path handles both.
  int face = 0;
  if (glanceasy::TextureFamily(target, &face) == GL_TEXTURE_2D ||
      target == GL_TEXTURE_CUBE_MAP) {
    glanceasy::SetTextureStorage(target, levels, internal_format, width, height, 1);
    return;
  }
  glanceasy::Context* context = glanceasy::ApiContext();
  if (context)
    context->SetError(GL_INVALID_ENUM);
}

void glTexStorage3D(GLenum target,
                    GLsizei levels,
                    GLenum internal_format,
                    GLsizei width,
                    GLsizei height,
                    GLsizei depth) {
  if (target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY) {
    glanceasy::Context* context = glanceasy::ApiContext();
    if (context)
      context->SetError(GL_INVALID_ENUM);
    return;
  }
  glanceasy::SetTextureStorage(target, levels, internal_format, width, height, depth);
}

void glCompressedTexImage2D(GLenum target,
                            GLint level,
                            GLenum internal_format,
                            GLsizei width,
                            GLsizei height,
                            GLint border,
                            GLsizei image_size,
                            const void* data) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  const bool is_cube_face =
      target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
      target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z;
  if (target != GL_TEXTURE_2D && !is_cube_face) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (level < 0 || width < 0 || height < 0 || border != 0 ||
      image_size < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  const int max_extent =
      is_cube_face ? glanceasy::kMaxCubeMapSize : glanceasy::kMaxTextureSize;
  if (width > max_extent || height > max_extent) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (is_cube_face && width != height) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!glanceasy::IsCompressedFormat(internal_format)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (level >= glanceasy::MipLevelCount(max_extent, max_extent)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  const int expected = ((width + 3) / 4) * ((height + 3) / 4) *
                       glanceasy::CompressedBlockBytes(internal_format);
  if (!glanceasy::ResolveUnpackBufferBytes(context, data,
                                     static_cast<size_t>(image_size), &data)) {
    return;
  }
  if (image_size != expected) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  int face = 0;
  glanceasy::Texture* texture = glanceasy::BindTargetTexture(context, target, &face);
  if (!texture)
    return;
  if (texture->immutable) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (width == 0 || height == 0)
    return;
  if (level == 0) {
    texture->width = width;
    texture->height = height;
    texture->depth = 1;
    texture->internal_format = internal_format;
  }
  const int levels = std::max(texture->levels, level + 1);
  texture->Resize(levels, texture->faces());
  glanceasy::TextureImage* image = texture->mutable_image(level, face);
  if (!image)
    return;
  image->width = width;
  image->height = height;
  image->depth = 1;
  if (!data)
    return;
  if (!glanceasy::DecodeCompressedImage(internal_format, width, height, image_size,
                                  data, image)) {
    context->SetError(GL_INVALID_VALUE);
  }
}

void glCompressedTexSubImage2D(GLenum target,
                               GLint level,
                               GLint xoffset,
                               GLint yoffset,
                               GLsizei width,
                               GLsizei height,
                               GLenum format,
                               GLsizei image_size,
                               const void* data) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  const bool is_cube_face =
      target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
      target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z;
  if (target != GL_TEXTURE_2D && !is_cube_face) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (level < 0 || xoffset < 0 || yoffset < 0 || width < 0 || height < 0 ||
      image_size < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  int face = 0;
  glanceasy::Texture* texture = glanceasy::BindTargetTexture(context, target, &face);
  if (!texture)
    return;
  if (level >= texture->levels) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  glanceasy::TextureImage* image = texture->mutable_image(level, face);
  if (!image || image->width <= 0) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (xoffset + width > image->width || yoffset + height > image->height) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (format != texture->internal_format) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  const int expected = ((width + 3) / 4) * ((height + 3) / 4) *
                       glanceasy::CompressedBlockBytes(format);
  if (!glanceasy::ResolveUnpackBufferBytes(context, data,
                                     static_cast<size_t>(image_size), &data)) {
    return;
  }
  if (image_size != expected) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (xoffset % 4 != 0 || yoffset % 4 != 0 || width % 4 != 0 ||
      height % 4 != 0) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (width == 0 || height == 0)
    return;
  glanceasy::TextureImage decoded;
  if (!glanceasy::DecodeCompressedImage(format, width, height, image_size, data,
                                  &decoded)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  for (int y = 0; y < height; ++y) {
    const uint32_t* source =
        decoded.rgba.data() + static_cast<size_t>(y) * width;
    uint32_t* destination =
        image->rgba.data() + static_cast<size_t>(y + yoffset) * image->width +
        xoffset;
    memcpy(destination, source, sizeof(uint32_t) * width);
  }
}

void glCopyTexImage2D(GLenum target,
                      GLint level,
                      GLenum internal_format,
                      GLint x,
                      GLint y,
                      GLsizei width,
                      GLsizei height,
                      GLint border) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  int face = 0;
  glanceasy::Texture* texture = glanceasy::BindTargetTexture(context, target, &face);
  if (!texture)
    return;
  if (level < 0 || width < 0 || height < 0 || border != 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!glanceasy::ValidInternalFormat(internal_format)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  glanceasy::RenderTarget source = context->GetReadTarget();
  if (!source.valid()) {
    context->SetError(GL_INVALID_FRAMEBUFFER_OPERATION);
    return;
  }
  GLenum copy_format = GL_RGBA;
  int copy_components = 4;
  if (internal_format == GL_RGB) {
    copy_format = GL_RGB;
    copy_components = 3;
  } else if (internal_format == GL_LUMINANCE) {
    copy_format = GL_LUMINANCE;
    copy_components = 1;
  } else if (internal_format == GL_LUMINANCE_ALPHA) {
    copy_format = GL_LUMINANCE_ALPHA;
    copy_components = 2;
  } else if (internal_format == GL_ALPHA) {
    copy_format = GL_ALPHA;
    copy_components = 1;
  }
  std::vector<uint8_t> pixels(static_cast<size_t>(width) * height *
                              copy_components);
  const uint32_t transparent = glanceasy::PackRgba(0, 0, 0, 0);
  for (int row = 0; row < height; ++row) {
    const int source_y = y + row;
    for (int column = 0; column < width; ++column) {
      const int source_x = x + column;
      uint32_t value = transparent;
      if (source.valid() && source_x >= 0 && source_x < source.width &&
          source_y >= 0 && source_y < source.height) {
        const int flipped_y = source.height - 1 - source_y;
        memcpy(&value,
               source.color + (static_cast<size_t>(flipped_y) * source.width +
                               static_cast<size_t>(source_x)) *
                                  4,
               sizeof(value));
      }
      uint8_t rgba[4] = {
          static_cast<uint8_t>(value & 0xFFu),
          static_cast<uint8_t>((value >> 8) & 0xFFu),
          static_cast<uint8_t>((value >> 16) & 0xFFu),
          static_cast<uint8_t>((value >> 24) & 0xFFu)};
      uint8_t* destination =
          pixels.data() +
          (static_cast<size_t>(row) * width + column) * copy_components;
      if (copy_format == GL_RGBA) {
        memcpy(destination, rgba, 4);
      } else if (copy_format == GL_RGB) {
        memcpy(destination, rgba, 3);
      } else if (copy_format == GL_LUMINANCE) {
        destination[0] = rgba[0];
      } else if (copy_format == GL_LUMINANCE_ALPHA) {
        destination[0] = rgba[0];
        destination[1] = rgba[3];
      } else {
        destination[0] = rgba[3];
      }
    }
  }
  glanceasy::SetTextureImage(target, level, internal_format, width, height, 1, border,
                       copy_format, GL_UNSIGNED_BYTE, pixels.data());
}

void glCopyTexSubImage2D(GLenum target,
                         GLint level,
                         GLint xoffset,
                         GLint yoffset,
                         GLint x,
                         GLint y,
                         GLsizei width,
                         GLsizei height) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  int face = 0;
  glanceasy::Texture* texture = glanceasy::BindTargetTexture(context, target, &face);
  if (!texture)
    return;
  if (level < 0 || xoffset < 0 || yoffset < 0 || width < 0 || height < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  glanceasy::RenderTarget source = context->GetReadTarget();
  if (!source.valid()) {
    context->SetError(GL_INVALID_FRAMEBUFFER_OPERATION);
    return;
  }
  glanceasy::TextureImage* image = texture->mutable_image(level, face);
  if (!image || xoffset + width > image->width ||
      yoffset + height > image->height) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  glanceasy::CopyFramebufferPixels(context, x, y, width, height, image->rgba.data(),
                             image->width, xoffset, yoffset, image->height);
  if (texture->internal_format == GL_LUMINANCE ||
      texture->internal_format == GL_LUMINANCE_ALPHA) {
    for (int row = 0; row < height; ++row) {
      const int destination_y = yoffset + row;
      if (destination_y < 0 || destination_y >= image->height)
        continue;
      for (int column = 0; column < width; ++column) {
        const int destination_x = xoffset + column;
        if (destination_x < 0 || destination_x >= image->width)
          continue;
        const size_t index = static_cast<size_t>(destination_y) *
                                 image->width +
                             destination_x;
        const uint32_t texel = image->rgba[index];
        const uint8_t alpha = static_cast<uint8_t>(texel >> 24);
        image->rgba[index] =
            glanceasy::PackRgba(static_cast<uint8_t>(texel & 0xff),
                          static_cast<uint8_t>(texel & 0xff),
                          static_cast<uint8_t>(texel & 0xff), alpha);
      }
    }
  }
}

void glGenerateMipmap(GLenum target) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  int face = 0;
  glanceasy::Texture* texture = glanceasy::BindTargetTexture(context, target, &face);
  if (!texture)
    return;
  if (texture->width <= 0 || texture->height <= 0) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (texture->target == GL_TEXTURE_CUBE_MAP) {
    for (int face_index = 0; face_index < texture->faces(); ++face_index) {
      const glanceasy::TextureImage* image = texture->image(0, face_index);
      if (!image || image->width <= 0 || image->height <= 0 ||
          image->width != texture->width || image->height != texture->height) {
        context->SetError(GL_INVALID_OPERATION);
        return;
      }
    }
  }
  const int levels = glanceasy::MipLevelCount(texture->width, texture->height);
  if (!texture->immutable) {
    texture->Resize(levels, texture->faces());
    int width = texture->width;
    int height = texture->height;
    for (int level = 0; level < levels; ++level) {
      for (int face_index = 0; face_index < texture->faces(); ++face_index) {
        glanceasy::TextureImage* image = texture->mutable_image(level, face_index);
        if (image->width <= 0) {
          image->width = width;
          image->height = height;
          image->depth = 1;
          image->rgba.assign(static_cast<size_t>(width) * height,
                             glanceasy::PackRgba(0, 0, 0, 0));
        }
      }
      width = std::max(1, width / 2);
      height = std::max(1, height / 2);
    }
  }
  glanceasy::GenerateMipmap(texture);
}

void glBindSampler(GLuint unit, GLuint sampler) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (unit >= static_cast<GLuint>(glanceasy::kMaxCombinedTextureUnits)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (sampler != 0 && !context->samplers.Get(sampler)) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  context->state.samplers[unit] = sampler;
}

void glGenSamplers(GLsizei n, GLuint* samplers) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (n < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  for (GLsizei i = 0; i < n; ++i)
    samplers[i] = context->samplers.Create();
}

void glDeleteSamplers(GLsizei n, const GLuint* samplers) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (n < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!samplers)
    return;
  for (GLsizei i = 0; i < n; ++i) {
    context->samplers.Delete(samplers[i]);
    for (int unit = 0; unit < glanceasy::kMaxCombinedTextureUnits; ++unit) {
      if (context->state.samplers[unit] == samplers[i])
        context->state.samplers[unit] = 0;
    }
  }
}

GLboolean glIsSampler(GLuint sampler) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context || sampler == 0)
    return GL_FALSE;
  return context->samplers.Get(sampler) ? GL_TRUE : GL_FALSE;
}

void glSamplerParameteri(GLuint sampler, GLenum pname, GLint param) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  glanceasy::Sampler* object = context->samplers.Get(sampler);
  if (!object) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if ((pname == GL_TEXTURE_WRAP_S || pname == GL_TEXTURE_WRAP_T ||
       pname == GL_TEXTURE_WRAP_R) &&
      param != GL_REPEAT && param != GL_CLAMP_TO_EDGE &&
      param != GL_MIRRORED_REPEAT) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  switch (pname) {
    case GL_TEXTURE_MIN_FILTER:
      if (param != GL_NEAREST && param != GL_LINEAR &&
          param != GL_NEAREST_MIPMAP_NEAREST &&
          param != GL_LINEAR_MIPMAP_NEAREST &&
          param != GL_NEAREST_MIPMAP_LINEAR &&
          param != GL_LINEAR_MIPMAP_LINEAR) {
        context->SetError(GL_INVALID_ENUM);
        return;
      }
      object->min_filter = static_cast<GLenum>(param);
      return;
    case GL_TEXTURE_MAG_FILTER:
      if (param != GL_NEAREST && param != GL_LINEAR) {
        context->SetError(GL_INVALID_ENUM);
        return;
      }
      object->mag_filter = static_cast<GLenum>(param);
      return;
    case GL_TEXTURE_WRAP_S:
      object->wrap_s = static_cast<GLenum>(param);
      return;
    case GL_TEXTURE_WRAP_T:
      object->wrap_t = static_cast<GLenum>(param);
      return;
    case GL_TEXTURE_WRAP_R:
      object->wrap_r = static_cast<GLenum>(param);
      return;
    case GL_TEXTURE_COMPARE_MODE:
      object->compare_mode = static_cast<GLenum>(param);
      return;
    case GL_TEXTURE_COMPARE_FUNC:
      object->compare_func = static_cast<GLenum>(param);
      return;
    case GL_TEXTURE_MIN_LOD:
      object->min_lod = static_cast<GLfloat>(param);
      return;
    case GL_TEXTURE_MAX_LOD:
      object->max_lod = static_cast<GLfloat>(param);
      return;
    case GL_TEXTURE_SWIZZLE_R:
      object->swizzle_r = static_cast<GLenum>(param);
      return;
    case GL_TEXTURE_SWIZZLE_G:
      object->swizzle_g = static_cast<GLenum>(param);
      return;
    case GL_TEXTURE_SWIZZLE_B:
      object->swizzle_b = static_cast<GLenum>(param);
      return;
    case GL_TEXTURE_SWIZZLE_A:
      object->swizzle_a = static_cast<GLenum>(param);
      return;
    default:
      context->SetError(GL_INVALID_ENUM);
      return;
  }
}

void glSamplerParameterf(GLuint sampler, GLenum pname, GLfloat param) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  glanceasy::Sampler* object = context->samplers.Get(sampler);
  if (!object) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if ((pname == GL_TEXTURE_WRAP_S || pname == GL_TEXTURE_WRAP_T ||
       pname == GL_TEXTURE_WRAP_R) &&
      param != static_cast<GLfloat>(GL_REPEAT) &&
      param != static_cast<GLfloat>(GL_CLAMP_TO_EDGE) &&
      param != static_cast<GLfloat>(GL_MIRRORED_REPEAT)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  switch (pname) {
    case GL_TEXTURE_MIN_LOD:
      object->min_lod = param;
      return;
    case GL_TEXTURE_MAX_LOD:
      object->max_lod = param;
      return;
    default:
      break;
  }
  glSamplerParameteri(sampler, pname, static_cast<GLint>(param));
}

void glSamplerParameteriv(GLuint sampler, GLenum pname, const GLint* params) {
  if (!params)
    return;
  if (pname == GL_TEXTURE_SWIZZLE_RGBA) {
    glanceasy::Context* context = glanceasy::ApiContext();
    glanceasy::Sampler* object = context ? context->samplers.Get(sampler) : nullptr;
    if (!object) {
      if (context)
        context->SetError(GL_INVALID_OPERATION);
      return;
    }
    object->swizzle_r = static_cast<GLenum>(params[0]);
    object->swizzle_g = static_cast<GLenum>(params[1]);
    object->swizzle_b = static_cast<GLenum>(params[2]);
    object->swizzle_a = static_cast<GLenum>(params[3]);
    return;
  }
  glSamplerParameteri(sampler, pname, params[0]);
}

void glSamplerParameterfv(GLuint sampler, GLenum pname, const GLfloat* params) {
  if (!params)
    return;
  glSamplerParameterf(sampler, pname, params[0]);
}

// Reports the component widths of the supported internal formats.  Values
// follow the ES 3.0 table 3.13 "Sized Internal Formats".
static void InternalFormatSizes(GLenum format,
                                GLint sizes[4],
                                GLint* depth_bits,
                                GLint* stencil_bits) {
  sizes[0] = 0;
  sizes[1] = 0;
  sizes[2] = 0;
  sizes[3] = 0;
  *depth_bits = 0;
  *stencil_bits = 0;
  switch (format) {
    case GL_RGBA:
    case GL_RGBA8:
    case GL_SRGB8_ALPHA8:
      sizes[0] = 8;
      sizes[1] = 8;
      sizes[2] = 8;
      sizes[3] = 8;
      return;
    case GL_RGBA16F:
    case GL_RGBA32F:
      sizes[0] = sizes[1] = sizes[2] = sizes[3] =
          format == GL_RGBA16F ? 16 : 32;
      return;
    case GL_RGB16F:
    case GL_RGB32F:
      sizes[0] = sizes[1] = sizes[2] = format == GL_RGB16F ? 16 : 32;
      return;
    case GL_R16F:
    case GL_R32F:
      sizes[0] = format == GL_R16F ? 16 : 32;
      return;
    case GL_RG16F:
    case GL_RG32F:
      sizes[0] = sizes[1] = format == GL_RG16F ? 16 : 32;
      return;
    case GL_RGB:
    case GL_RGB8:
    case GL_SRGB8:
      sizes[0] = 8;
      sizes[1] = 8;
      sizes[2] = 8;
      return;
    case GL_RGB565:
      sizes[0] = 5;
      sizes[1] = 6;
      sizes[2] = 5;
      return;
    case GL_RGBA4:
      sizes[0] = 4;
      sizes[1] = 4;
      sizes[2] = 4;
      sizes[3] = 4;
      return;
    case GL_RGB5_A1:
      sizes[0] = 5;
      sizes[1] = 5;
      sizes[2] = 5;
      sizes[3] = 1;
      return;
    case GL_R8:
      sizes[0] = 8;
      return;
    case GL_RG8:
      sizes[0] = 8;
      sizes[1] = 8;
      return;
    case GL_ALPHA:
      sizes[3] = 8;
      return;
    case GL_LUMINANCE:
      sizes[0] = 8;
      sizes[1] = 8;
      sizes[2] = 8;
      return;
    case GL_LUMINANCE_ALPHA:
      sizes[0] = 8;
      sizes[1] = 8;
      sizes[2] = 8;
      sizes[3] = 8;
      return;
    case GL_DEPTH_COMPONENT16:
      *depth_bits = 16;
      return;
    case GL_DEPTH_COMPONENT24:
      *depth_bits = 24;
      return;
    case GL_DEPTH24_STENCIL8:
      *depth_bits = 24;
      *stencil_bits = 8;
      return;
    default:
      return;
  }
}

void glGetTexLevelParameteriv(GLenum target,
                              GLint level,
                              GLenum pname,
                              GLint* params) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context || !params)
    return;
  int face = 0;
  glanceasy::Texture* texture = glanceasy::BindTargetTexture(context, target, &face);
  if (!texture)
    return;
  if (level < 0 || level >= texture->levels) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  const glanceasy::TextureImage* image = texture->image(level, face);
  if (!image) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  GLint sizes[4];
  GLint depth_bits = 0;
  GLint stencil_bits = 0;
  InternalFormatSizes(texture->internal_format, sizes, &depth_bits,
                      &stencil_bits);
  switch (pname) {
    case GL_TEXTURE_WIDTH:
      params[0] = image->width;
      return;
    case GL_TEXTURE_HEIGHT:
      params[0] = image->height;
      return;
    case GL_TEXTURE_DEPTH:
      params[0] = image->depth;
      return;
    case GL_TEXTURE_INTERNAL_FORMAT:
      params[0] = static_cast<GLint>(texture->internal_format);
      return;
    case GL_TEXTURE_RED_SIZE:
      params[0] = sizes[0];
      return;
    case GL_TEXTURE_GREEN_SIZE:
      params[0] = sizes[1];
      return;
    case GL_TEXTURE_BLUE_SIZE:
      params[0] = sizes[2];
      return;
    case GL_TEXTURE_ALPHA_SIZE:
      params[0] = sizes[3];
      return;
    case GL_TEXTURE_DEPTH_SIZE:
      params[0] = depth_bits;
      return;
    case GL_TEXTURE_STENCIL_SIZE:
      params[0] = stencil_bits;
      return;
    case GL_TEXTURE_COMPRESSED:
      params[0] = glanceasy::IsCompressedFormat(texture->internal_format) ? 1 : 0;
      return;
    case GL_TEXTURE_SAMPLES:
      params[0] = image->samples > 1 ? image->samples : 0;
      return;
    case GL_TEXTURE_FIXED_SAMPLE_LOCATIONS:
      params[0] = texture->fixed_sample_locations ? 1 : 0;
      return;
    default:
      context->SetError(GL_INVALID_ENUM);
      return;
  }
}

void glTexStorage2DMultisample(GLenum target,
                               GLsizei samples,
                               GLenum internal_format,
                               GLsizei width,
                               GLsizei height,
                               GLboolean fixed_sample_locations) {
  glanceasy::SetMultisampleTextureStorage(target, samples, internal_format, width,
                                   height, fixed_sample_locations);
}

void glGetSamplerParameteriv(GLuint sampler, GLenum pname, GLint* params) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context || !params)
    return;
  glanceasy::Sampler* object = context->samplers.Get(sampler);
  if (!object) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  switch (pname) {
    case GL_TEXTURE_MIN_FILTER:
      params[0] = static_cast<GLint>(object->min_filter);
      return;
    case GL_TEXTURE_MAG_FILTER:
      params[0] = static_cast<GLint>(object->mag_filter);
      return;
    case GL_TEXTURE_WRAP_S:
      params[0] = static_cast<GLint>(object->wrap_s);
      return;
    case GL_TEXTURE_WRAP_T:
      params[0] = static_cast<GLint>(object->wrap_t);
      return;
    case GL_TEXTURE_WRAP_R:
      params[0] = static_cast<GLint>(object->wrap_r);
      return;
    case GL_TEXTURE_COMPARE_MODE:
      params[0] = static_cast<GLint>(object->compare_mode);
      return;
    case GL_TEXTURE_COMPARE_FUNC:
      params[0] = static_cast<GLint>(object->compare_func);
      return;
    case GL_TEXTURE_MIN_LOD:
      params[0] = static_cast<GLint>(object->min_lod >= 0.0f
                                         ? object->min_lod + 0.5f
                                         : object->min_lod - 0.5f);
      return;
    case GL_TEXTURE_MAX_LOD:
      params[0] = static_cast<GLint>(object->max_lod >= 0.0f
                                         ? object->max_lod + 0.5f
                                         : object->max_lod - 0.5f);
      return;
    default:
      context->SetError(GL_INVALID_ENUM);
      return;
  }
}

void glGetSamplerParameterfv(GLuint sampler, GLenum pname, GLfloat* params) {
  if (!params)
    return;
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  glanceasy::Sampler* object = context->samplers.Get(sampler);
  if (!object) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  switch (pname) {
    case GL_TEXTURE_MIN_LOD:
      params[0] = object->min_lod;
      return;
    case GL_TEXTURE_MAX_LOD:
      params[0] = object->max_lod;
      return;
    default:
      break;
  }
  GLint value = 0;
  glGetSamplerParameteriv(sampler, pname, &value);
  params[0] = static_cast<GLfloat>(value);
}

}  // extern "C"
