// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Validation for the 3D texture entry points that are not used by the CPU
// renderer's rendering paths.  The functions report the errors required by
// GL ES 3.0 and accept valid calls for API-compatibility purposes.

#include <stddef.h>
#include <stdint.h>

#include <algorithm>

#include "GLES3/gl32.h"
#include "src/zgl_api_common.h"
#include "src/zgl_context.h"
#include "src/zgl_texture_compressed.h"

namespace {

zgl::Texture* Bound3DTexture(zgl::Context* context, GLenum target) {
  const int unit = static_cast<int>(context->state.active_texture) -
                   static_cast<int>(GL_TEXTURE0);
  if (unit < 0 || unit >= zgl::kMaxCombinedTextureUnits)
    return nullptr;
  if (target == GL_TEXTURE_3D)
    return context->textures.Get(context->state.textures_3d[unit]);
  if (target == GL_TEXTURE_2D_ARRAY)
    return context->textures.Get(context->state.textures_2d_array[unit]);
  return nullptr;
}

bool CheckUnpackBuffer(zgl::Context* context, GLsizei size) {
  if (context->state.pixel_unpack_buffer == 0)
    return true;
  zgl::Buffer* buffer = context->buffers.Get(context->state.pixel_unpack_buffer);
  if (!buffer || buffer->mapped ||
      static_cast<size_t>(std::max(0, size)) > buffer->data.size()) {
    context->SetError(GL_INVALID_OPERATION);
    return false;
  }
  return true;
}

int RequiredCompressedBytes(GLenum format,
                            GLsizei width,
                            GLsizei height,
                            GLsizei depth) {
  const int block = zgl::CompressedBlockBytes(format);
  if (block <= 0)
    return 0;
  return ((width + 3) / 4) * ((height + 3) / 4) * std::max(1, depth) * block;
}

}  // namespace

extern "C" {

void glCompressedTexImage3D(GLenum target,
                            GLint level,
                            GLenum internal_format,
                            GLsizei width,
                            GLsizei height,
                            GLsizei depth,
                            GLint border,
                            GLsizei image_size,
                            const void* data) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (level < 0 || width < 0 || height < 0 || depth < 0 || border != 0 ||
      image_size < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (width > zgl::kMaxTextureSize || height > zgl::kMaxTextureSize ||
      depth > zgl::kMax3DTextureSize) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (zgl::CompressedBlockBytes(internal_format) <= 0) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  const int required =
      RequiredCompressedBytes(internal_format, width, height, depth);
  if (image_size < required) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!CheckUnpackBuffer(context, image_size))
    return;
  zgl::Texture* texture = Bound3DTexture(context, target);
  if (!texture) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (texture->immutable) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (level == 0) {
    texture->width = width;
    texture->height = height;
    texture->depth = depth;
    texture->internal_format = internal_format;
  }
  texture->Resize(std::max(texture->levels, level + 1), texture->faces());
}

void glCompressedTexSubImage3D(GLenum target,
                               GLint level,
                               GLint xoffset,
                               GLint yoffset,
                               GLint zoffset,
                               GLsizei width,
                               GLsizei height,
                               GLsizei depth,
                               GLenum format,
                               GLsizei image_size,
                               const void* data) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (level < 0 || xoffset < 0 || yoffset < 0 || zoffset < 0 || width < 0 ||
      height < 0 || depth < 0 || image_size < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  const int required = RequiredCompressedBytes(format, width, height, depth);
  if (required <= 0) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (image_size < required) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!CheckUnpackBuffer(context, image_size))
    return;
  zgl::Texture* texture = Bound3DTexture(context, target);
  if (!texture) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (level >= texture->levels) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (xoffset + width > texture->width || yoffset + height > texture->height ||
      zoffset + depth > texture->depth) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (format != texture->internal_format) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
}

void glCopyTexSubImage3D(GLenum target,
                         GLint level,
                         GLint xoffset,
                         GLint yoffset,
                         GLint zoffset,
                         GLint x,
                         GLint y,
                         GLsizei width,
                         GLsizei height) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (level < 0 || xoffset < 0 || yoffset < 0 || zoffset < 0 || width < 0 ||
      height < 0 || x < 0 || y < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  zgl::RenderTarget source = context->GetReadTarget();
  if (!source.valid() || !source.color) {
    context->SetError(GL_INVALID_FRAMEBUFFER_OPERATION);
    return;
  }
  if (x + width > source.width || y + height > source.height) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  zgl::Texture* texture = Bound3DTexture(context, target);
  if (!texture) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (level >= texture->levels) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (xoffset + width > texture->width || yoffset + height > texture->height ||
      zoffset >= texture->depth) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
}

}  // extern "C"
