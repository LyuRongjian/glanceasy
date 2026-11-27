// Copyright 2026 The Glanceasy Authors. All rights reserved.
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
#include "src/glanceasy_api_common.h"
#include "src/glanceasy_context.h"
#include "src/glanceasy_texture_compressed.h"

namespace {

glanceasy::Texture* Bound3DTexture(glanceasy::Context* context, GLenum target) {
  const int unit = static_cast<int>(context->state.active_texture) -
                   static_cast<int>(GL_TEXTURE0);
  if (unit < 0 || unit >= glanceasy::kMaxCombinedTextureUnits)
    return nullptr;
  if (target == GL_TEXTURE_3D)
    return context->textures.Get(context->state.textures_3d[unit]);
  if (target == GL_TEXTURE_2D_ARRAY)
    return context->textures.Get(context->state.textures_2d_array[unit]);
  return nullptr;
}

bool CheckUnpackBuffer(glanceasy::Context* context, GLsizei size) {
  if (context->state.pixel_unpack_buffer == 0)
    return true;
  glanceasy::Buffer* buffer = context->buffers.Get(context->state.pixel_unpack_buffer);
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
  const int block = glanceasy::CompressedBlockBytes(format);
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
  glanceasy::Context* context = glanceasy::ApiContext();
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
  const int max_width =
      target == GL_TEXTURE_3D ? glanceasy::kMax3DTextureSize : glanceasy::kMaxTextureSize;
  const int max_height = max_width;
  const int max_depth =
      target == GL_TEXTURE_3D ? glanceasy::kMax3DTextureSize : 256;
  if (width > max_width || height > max_height || depth > max_depth) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (glanceasy::CompressedBlockBytes(internal_format) <= 0) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (level >= glanceasy::MipLevelCount(max_width, max_height)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  const int required =
      RequiredCompressedBytes(internal_format, width, height, depth);
  if (image_size != required) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!CheckUnpackBuffer(context, image_size))
    return;
  glanceasy::Texture* texture = Bound3DTexture(context, target);
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
  glanceasy::Context* context = glanceasy::ApiContext();
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
  const int max_width =
      target == GL_TEXTURE_3D ? glanceasy::kMax3DTextureSize : glanceasy::kMaxTextureSize;
  if (level >= glanceasy::MipLevelCount(max_width, max_width)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  glanceasy::Texture* texture = Bound3DTexture(context, target);
  if (!texture) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (format != texture->internal_format) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (xoffset % 4 != 0 || yoffset % 4 != 0 || width % 4 != 0 ||
      height % 4 != 0) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  const int required = RequiredCompressedBytes(format, width, height, depth);
  if (image_size != required) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (required <= 0) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (!CheckUnpackBuffer(context, image_size))
    return;
  if (level >= texture->levels) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (xoffset + width > texture->width || yoffset + height > texture->height ||
      zoffset + depth > texture->depth) {
    context->SetError(GL_INVALID_VALUE);
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
  glanceasy::Context* context = glanceasy::ApiContext();
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
  glanceasy::RenderTarget source = context->GetReadTarget();
  if (!source.valid() || !source.color) {
    context->SetError(GL_INVALID_FRAMEBUFFER_OPERATION);
    return;
  }
  if (x + width > source.width || y + height > source.height) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  glanceasy::Texture* texture = Bound3DTexture(context, target);
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
