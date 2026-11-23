// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <vector>

#include "GLES3/gl32.h"
#include "src/zgl_api_common.h"
#include "src/zgl_texture.h"

namespace zgl {
namespace {

GLenum TextureFamily(GLenum target, int* face) {
  if (face)
    *face = 0;
  switch (target) {
    case GL_TEXTURE_2D:
      return GL_TEXTURE_2D;
    case GL_TEXTURE_3D:
      return GL_TEXTURE_3D;
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
    case GL_RG8:
    case GL_DEPTH_COMPONENT16:
    case GL_DEPTH_COMPONENT24:
    case GL_DEPTH24_STENCIL8:
      return true;
    default:
      return false;
  }
}

bool ValidFormatType(GLenum format, GLenum type) {
  if (format == GL_DEPTH_COMPONENT) {
    return type == GL_UNSIGNED_SHORT || type == GL_UNSIGNED_INT ||
           type == GL_FLOAT;
  }
  if (format == GL_DEPTH_STENCIL)
    return type == GL_UNSIGNED_INT_24_8;
  if (type == GL_UNSIGNED_BYTE)
    return format == GL_RGBA || format == GL_RGB || format == GL_ALPHA ||
           format == GL_LUMINANCE || format == GL_LUMINANCE_ALPHA ||
           format == GL_RED || format == GL_RG;
  if (type == GL_UNSIGNED_SHORT_5_6_5)
    return format == GL_RGB;
  if (type == GL_UNSIGNED_SHORT_4_4_4_4 || type == GL_UNSIGNED_SHORT_5_5_5_1)
    return format == GL_RGBA;
  return false;
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
  Texture* texture = context->textures.Get(context->state.textures[unit]);
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
                 const void* pixels) {
  TextureImage source;
  if (!UploadTextureImage(&source, width, height, format, type, row_length,
                          skip_rows, skip_pixels, pixels)) {
    return;
  }
  if (!destination || destination->width < xoffset + width ||
      destination->height < yoffset + height || destination->depth <= zoffset) {
    return;
  }
  const size_t source_stride = static_cast<size_t>(width);
  const size_t destination_stride = static_cast<size_t>(destination->width);
  const size_t slice = destination_stride * destination->height;
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
    case GL_TEXTURE_COMPARE_FUNC:
      return;
    case GL_TEXTURE_IMMUTABLE_FORMAT:
    case GL_TEXTURE_IMMUTABLE_LEVELS:
    case GL_TEXTURE_SWIZZLE_R:
    case GL_TEXTURE_SWIZZLE_G:
    case GL_TEXTURE_SWIZZLE_B:
    case GL_TEXTURE_SWIZZLE_A:
      return;
    default: {
      Context* context = ApiContext();
      if (context)
        context->SetError(GL_INVALID_ENUM);
      return;
    }
  }
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
  if (width > kMaxTextureSize || height > kMaxTextureSize ||
      depth > kMax3DTextureSize) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!ValidInternalFormat(internal_format)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (!ValidFormatType(format, type)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  const int extent = texture->target == GL_TEXTURE_3D
                         ? std::max(std::max(width, height), depth)
                         : std::max(width, height);
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
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (level == 0) {
    texture->width = width;
    texture->height = height;
    texture->depth = depth;
    texture->internal_format = static_cast<GLenum>(internal_format);
  }
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
  if (internal_format == GL_DEPTH_COMPONENT16 ||
      internal_format == GL_DEPTH_COMPONENT24 ||
      internal_format == GL_DEPTH24_STENCIL8) {
    image->width = width;
    image->height = height;
    image->depth = 1;
    image->depth16.assign(static_cast<size_t>(width) * height, 0xFFFFu);
    if (internal_format == GL_DEPTH24_STENCIL8)
      image->stencil8.assign(static_cast<size_t>(width) * height, 0u);
    if (pixels) {
      const PixelStoreState& store = context->state.pixel_store;
      UploadSlice(image, 0, 0, 0, width, height, format, type,
                  store.unpack_row_length, store.unpack_skip_rows,
                  store.unpack_skip_pixels, pixels);
    }
    return;
  }
  image->width = width;
  image->height = height;
  image->depth = texture->target == GL_TEXTURE_3D ? depth : 1;
  const size_t texels = static_cast<size_t>(width) * height * image->depth;
  image->rgba.assign(texels, PackRgba(0, 0, 0, 0));
  if (pixels) {
    const PixelStoreState& store = context->state.pixel_store;
    if (texture->target == GL_TEXTURE_3D) {
      for (int z = 0; z < depth; ++z) {
        const uint8_t* slice = static_cast<const uint8_t*>(pixels) +
                               static_cast<size_t>(z) * width * height * 4;
        UploadSlice(image, 0, 0, z, width, height, format, type,
                    store.unpack_row_length, 0, 0, slice);
      }
    } else {
      UploadSlice(image, 0, 0, 0, width, height, format, type,
                  store.unpack_row_length, store.unpack_skip_rows,
                  store.unpack_skip_pixels, pixels);
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
  Texture* texture = BindTargetTexture(context, target, &face);
  if (!texture)
    return;
  if (levels < 1 || width < 1 || height < 1) {
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
  const int extent = std::max(width, height);
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
  for (int level = 0; level < levels; ++level) {
    for (int face_index = 0; face_index < texture->faces(); ++face_index) {
      TextureImage* image = texture->mutable_image(level, face_index);
      image->width = level_width;
      image->height = level_height;
      image->depth = texture->target == GL_TEXTURE_3D ? depth : 1;
      image->rgba.assign(
          static_cast<size_t>(level_width) * level_height * image->depth, 0u);
    }
    level_width = std::max(1, level_width / 2);
    level_height = std::max(1, level_height / 2);
  }
}

// Copies |width| by |height| pixels starting at the GL window coordinate
// (x, y) from the current read framebuffer into |destination|.  Texture rows
// are stored bottom up, matching glReadPixels and glTexImage2D.
void CopyFramebufferPixels(zgl::Context* context,
                           int x,
                           int y,
                           int width,
                           int height,
                           uint32_t* destination,
                           int destination_stride,
                           int destination_x,
                           int destination_y,
                           int destination_height) {
  zgl::RenderTarget source = context->GetReadTarget();
  const uint32_t transparent = zgl::PackRgba(0, 0, 0, 0);
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
}  // namespace zgl

extern "C" {

void glGenTextures(GLsizei n, GLuint* textures) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !textures)
    return;
  if (n < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  for (GLsizei i = 0; i < n; ++i)
    textures[i] = context->textures.Create();
}

void glDeleteTextures(GLsizei n, const GLuint* textures) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !textures)
    return;
  if (n < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  for (GLsizei i = 0; i < n; ++i) {
    context->textures.Delete(textures[i]);
    for (int unit = 0; unit < zgl::kMaxCombinedTextureUnits; ++unit) {
      if (context->state.textures[unit] == textures[i])
        context->state.textures[unit] = 0;
    }
  }
}

GLboolean glIsTexture(GLuint texture) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || texture == 0)
    return GL_FALSE;
  return context->textures.Get(texture) ? GL_TRUE : GL_FALSE;
}

void glActiveTexture(GLenum texture) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  const GLint unit =
      static_cast<GLint>(texture) - static_cast<GLint>(GL_TEXTURE0);
  if (unit < 0 || unit >= zgl::kMaxCombinedTextureUnits) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  context->state.active_texture = texture;
}

void glBindTexture(GLenum target, GLuint texture) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  int face = 0;
  const GLenum family = zgl::TextureFamily(target, &face);
  if (family == 0) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  const int unit = static_cast<int>(context->state.active_texture) -
                   static_cast<int>(GL_TEXTURE0);
  if (unit < 0 || unit >= zgl::kMaxCombinedTextureUnits) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (texture == 0) {
    context->state.textures[unit] = 0;
    return;
  }
  zgl::Texture* object = context->textures.Get(texture);
  if (!object) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (object->target == 0)
    object->target = family;
  else if (object->target != family) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  context->state.textures[unit] = texture;
}

void glTexParameteri(GLenum target, GLenum pname, GLint param) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  int face = 0;
  zgl::Texture* texture = zgl::BindTargetTexture(context, target, &face);
  if (!texture)
    return;
  zgl::SetTextureParameter(texture, pname, param);
}

void glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
  glTexParameteri(target, pname, static_cast<GLint>(param));
}

void glTexParameteriv(GLenum target, GLenum pname, const GLint* params) {
  if (!params)
    return;
  glTexParameteri(target, pname, params[0]);
}

void glTexParameterfv(GLenum target, GLenum pname, const GLfloat* params) {
  if (!params)
    return;
  glTexParameteri(target, pname, static_cast<GLint>(params[0]));
}

void glGetTexParameteriv(GLenum target, GLenum pname, GLint* params) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !params)
    return;
  int face = 0;
  zgl::Texture* texture = zgl::BindTargetTexture(context, target, &face);
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
      params[0] = texture->levels;
      return;
    case GL_TEXTURE_WIDTH:
      params[0] = texture->width;
      return;
    case GL_TEXTURE_HEIGHT:
      params[0] = texture->height;
      return;
    default:
      context->SetError(GL_INVALID_ENUM);
      return;
  }
}

void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat* params) {
  if (!params)
    return;
  GLint value = 0;
  glGetTexParameteriv(target, pname, &value);
  params[0] = static_cast<GLfloat>(value);
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
  zgl::SetTextureImage(target, level, internal_format, width, height, 1, border,
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
  if (target != GL_TEXTURE_3D) {
    zgl::Context* context = zgl::ApiContext();
    if (context)
      context->SetError(GL_INVALID_ENUM);
    return;
  }
  zgl::SetTextureImage(target, level, internal_format, width, height, depth,
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
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  int face = 0;
  zgl::Texture* texture = zgl::BindTargetTexture(context, target, &face);
  if (!texture)
    return;
  if (level < 0 || level >= texture->levels) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  zgl::TextureImage* image = texture->mutable_image(level, face);
  if (!image || image->width <= 0) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (xoffset < 0 || yoffset < 0 || width < 0 || height < 0 ||
      xoffset + width > image->width || yoffset + height > image->height) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!pixels)
    return;
  if (!zgl::ValidFormatType(format, type)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  const zgl::PixelStoreState& store = context->state.pixel_store;
  zgl::UploadSlice(image, xoffset, yoffset, 0, width, height, format, type,
                   store.unpack_row_length, store.unpack_skip_rows,
                   store.unpack_skip_pixels, pixels);
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
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (target != GL_TEXTURE_3D) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  int face = 0;
  zgl::Texture* texture = zgl::BindTargetTexture(context, target, &face);
  if (!texture)
    return;
  if (level < 0 || level >= texture->levels) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  zgl::TextureImage* image = texture->mutable_image(level, 0);
  if (!image || image->depth <= 0) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (xoffset < 0 || yoffset < 0 || zoffset < 0 || width < 0 || height < 0 ||
      depth < 0 || xoffset + width > image->width ||
      yoffset + height > image->height || zoffset + depth > image->depth) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!pixels)
    return;
  if (!zgl::ValidFormatType(format, type)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  const zgl::PixelStoreState& store = context->state.pixel_store;
  for (int z = 0; z < depth; ++z) {
    zgl::TextureImage slice;
    if (!zgl::UploadTextureImage(
            &slice, width, height, format, type, store.unpack_row_length,
            store.unpack_skip_rows, store.unpack_skip_pixels,
            static_cast<const uint8_t*>(pixels) +
                static_cast<size_t>(z) * width * height * 4)) {
      continue;
    }
    for (int y = 0; y < height; ++y) {
      uint32_t* destination =
          image->rgba.data() +
          static_cast<size_t>(z + zoffset) * image->width * image->height +
          static_cast<size_t>(y + yoffset) * image->width + xoffset;
      memcpy(destination, slice.rgba.data() + static_cast<size_t>(y) * width,
             sizeof(uint32_t) * width);
    }
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
  if (zgl::TextureFamily(target, &face) == GL_TEXTURE_2D ||
      target == GL_TEXTURE_CUBE_MAP) {
    zgl::SetTextureStorage(target, levels, internal_format, width, height, 1);
    return;
  }
  zgl::Context* context = zgl::ApiContext();
  if (context)
    context->SetError(GL_INVALID_ENUM);
}

void glTexStorage3D(GLenum target,
                    GLsizei levels,
                    GLenum internal_format,
                    GLsizei width,
                    GLsizei height,
                    GLsizei depth) {
  if (target != GL_TEXTURE_3D) {
    zgl::Context* context = zgl::ApiContext();
    if (context)
      context->SetError(GL_INVALID_ENUM);
    return;
  }
  zgl::SetTextureStorage(target, levels, internal_format, width, height, depth);
}

void glCompressedTexImage2D(GLenum target,
                            GLint level,
                            GLenum internal_format,
                            GLsizei width,
                            GLsizei height,
                            GLint border,
                            GLsizei image_size,
                            const void* data) {
  (void)target;
  (void)level;
  (void)internal_format;
  (void)width;
  (void)height;
  (void)border;
  (void)image_size;
  (void)data;
  zgl::Context* context = zgl::ApiContext();
  if (context)
    context->SetError(GL_INVALID_OPERATION);
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
  (void)target;
  (void)level;
  (void)xoffset;
  (void)yoffset;
  (void)width;
  (void)height;
  (void)format;
  (void)image_size;
  (void)data;
  zgl::Context* context = zgl::ApiContext();
  if (context)
    context->SetError(GL_INVALID_OPERATION);
}

void glCopyTexImage2D(GLenum target,
                      GLint level,
                      GLenum internal_format,
                      GLint x,
                      GLint y,
                      GLsizei width,
                      GLsizei height,
                      GLint border) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  int face = 0;
  zgl::Texture* texture = zgl::BindTargetTexture(context, target, &face);
  if (!texture)
    return;
  if (level < 0 || width < 0 || height < 0 || border != 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!zgl::ValidInternalFormat(internal_format)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
  zgl::RenderTarget source = context->GetReadTarget();
  const uint32_t transparent = zgl::PackRgba(0, 0, 0, 0);
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
      memcpy(pixels.data() + (static_cast<size_t>(row) * width + column) * 4,
             &value, sizeof(value));
    }
  }
  zgl::SetTextureImage(target, level, internal_format, width, height, 1, border,
                       GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
}

void glCopyTexSubImage2D(GLenum target,
                         GLint level,
                         GLint xoffset,
                         GLint yoffset,
                         GLint x,
                         GLint y,
                         GLsizei width,
                         GLsizei height) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  int face = 0;
  zgl::Texture* texture = zgl::BindTargetTexture(context, target, &face);
  if (!texture)
    return;
  if (level < 0 || xoffset < 0 || yoffset < 0 || width < 0 || height < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  zgl::TextureImage* image = texture->mutable_image(level, face);
  if (!image || xoffset + width > image->width ||
      yoffset + height > image->height) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  zgl::CopyFramebufferPixels(context, x, y, width, height, image->rgba.data(),
                             image->width, xoffset, yoffset, image->height);
}

void glGenerateMipmap(GLenum target) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  int face = 0;
  zgl::Texture* texture = zgl::BindTargetTexture(context, target, &face);
  if (!texture)
    return;
  if (texture->width <= 0 || texture->height <= 0) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  const int levels = zgl::MipLevelCount(texture->width, texture->height);
  if (!texture->immutable) {
    texture->Resize(levels, texture->faces());
    int width = texture->width;
    int height = texture->height;
    for (int level = 0; level < levels; ++level) {
      for (int face_index = 0; face_index < texture->faces(); ++face_index) {
        zgl::TextureImage* image = texture->mutable_image(level, face_index);
        if (image->width <= 0) {
          image->width = width;
          image->height = height;
          image->depth = 1;
          image->rgba.assign(static_cast<size_t>(width) * height,
                             zgl::PackRgba(0, 0, 0, 0));
        }
      }
      width = std::max(1, width / 2);
      height = std::max(1, height / 2);
    }
  }
  zgl::GenerateMipmap(texture);
}

void glBindSampler(GLuint unit, GLuint sampler) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (unit >= static_cast<GLuint>(zgl::kMaxCombinedTextureUnits)) {
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
  zgl::Context* context = zgl::ApiContext();
  if (!context || !samplers)
    return;
  if (n < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  for (GLsizei i = 0; i < n; ++i)
    samplers[i] = context->samplers.Create();
}

void glDeleteSamplers(GLsizei n, const GLuint* samplers) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !samplers)
    return;
  if (n < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  for (GLsizei i = 0; i < n; ++i) {
    context->samplers.Delete(samplers[i]);
    for (int unit = 0; unit < zgl::kMaxCombinedTextureUnits; ++unit) {
      if (context->state.samplers[unit] == samplers[i])
        context->state.samplers[unit] = 0;
    }
  }
}

GLboolean glIsSampler(GLuint sampler) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || sampler == 0)
    return GL_FALSE;
  return context->samplers.Get(sampler) ? GL_TRUE : GL_FALSE;
}

void glSamplerParameteri(GLuint sampler, GLenum pname, GLint param) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  zgl::Sampler* object = context->samplers.Get(sampler);
  if (!object) {
    context->SetError(GL_INVALID_OPERATION);
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
    default:
      context->SetError(GL_INVALID_ENUM);
      return;
  }
}

void glSamplerParameterf(GLuint sampler, GLenum pname, GLfloat param) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  zgl::Sampler* object = context->samplers.Get(sampler);
  if (!object) {
    context->SetError(GL_INVALID_OPERATION);
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
  zgl::Context* context = zgl::ApiContext();
  if (!context || !params)
    return;
  int face = 0;
  zgl::Texture* texture = zgl::BindTargetTexture(context, target, &face);
  if (!texture)
    return;
  if (level < 0 || level >= texture->levels) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  const zgl::TextureImage* image = texture->image(level, face);
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
      params[0] = 0;
      return;
    default:
      context->SetError(GL_INVALID_ENUM);
      return;
  }
}

void glGetSamplerParameteriv(GLuint sampler, GLenum pname, GLint* params) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !params)
    return;
  zgl::Sampler* object = context->samplers.Get(sampler);
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
    default:
      context->SetError(GL_INVALID_ENUM);
      return;
  }
}

void glGetSamplerParameterfv(GLuint sampler, GLenum pname, GLfloat* params) {
  if (!params)
    return;
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  zgl::Sampler* object = context->samplers.Get(sampler);
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
