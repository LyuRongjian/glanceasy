// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>

#include "GLES3/gl32.h"
#include "src/zgl_api_common.h"

namespace zgl {
namespace {

const TextureImage* AttachmentImageOf(Context* context,
                                      const FramebufferAttachment& attachment) {
  if (attachment.texture) {
    const Texture* texture = context->textures.Get(attachment.texture);
    if (!texture)
      return nullptr;
    return texture->image(attachment.level, 0);
  }
  if (attachment.renderbuffer) {
    const Renderbuffer* renderbuffer =
        context->renderbuffers.Get(attachment.renderbuffer);
    if (!renderbuffer)
      return nullptr;
    return &renderbuffer->image;
  }
  return nullptr;
}

bool ValidFramebufferTarget(GLenum target) {
  return target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER ||
         target == GL_READ_FRAMEBUFFER;
}

Framebuffer* CurrentFramebuffer(Context* context, GLenum target) {
  const GLuint name = target == GL_READ_FRAMEBUFFER
                          ? context->state.read_framebuffer
                          : context->state.framebuffer;
  if (name == 0)
    return nullptr;
  return context->framebuffers.Get(name);
}

GLenum CheckCompleteness(Context* context, const Framebuffer& framebuffer) {
  bool any_attachment =
      framebuffer.depth.bound() || framebuffer.stencil.bound();
  for (int i = 0; i < kMaxColorAttachments; ++i)
    any_attachment |= framebuffer.colors[i].bound();
  if (!any_attachment)
    return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
  const TextureImage* color = AttachmentImageOf(context, framebuffer.colors[0]);
  if (!color)
    return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
  if (color->width <= 0 || color->height <= 0)
    return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
  const int width = color->width;
  const int height = color->height;
  for (int i = 1; i < kMaxColorAttachments; ++i) {
    if (!framebuffer.colors[i].bound())
      continue;
    const TextureImage* image =
        AttachmentImageOf(context, framebuffer.colors[i]);
    if (!image || image->width != width || image->height != height)
      return GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS;
  }
  if (framebuffer.depth.bound()) {
    const TextureImage* image = AttachmentImageOf(context, framebuffer.depth);
    if (!image || image->width != width || image->height != height)
      return GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS;
  }
  if (framebuffer.stencil.bound()) {
    const TextureImage* image = AttachmentImageOf(context, framebuffer.stencil);
    if (!image || image->width != width || image->height != height)
      return GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS;
  }
  return GL_FRAMEBUFFER_COMPLETE;
}

size_t PixelBytes(GLenum format, GLenum type) {
  if (type == GL_UNSIGNED_BYTE) {
    switch (format) {
      case GL_RGBA:
        return 4;
      case GL_RGB:
        return 3;
      case GL_RG:
      case GL_LUMINANCE_ALPHA:
        return 2;
      default:
        return 1;
    }
  }
  if (type == GL_FLOAT)
    return 4 * (format == GL_RGBA ? 4 : format == GL_RGB ? 3 : 1);
  if (type == GL_UNSIGNED_SHORT_5_6_5 || type == GL_UNSIGNED_SHORT_4_4_4_4 ||
      type == GL_UNSIGNED_SHORT_5_5_5_1 || type == GL_UNSIGNED_SHORT)
    return 2;
  return 4;
}

// Writes one pixel in the requested read format.  |source| points at four
// RGBA8 bytes in memory order.
void WritePixel(const uint8_t* source,
                GLenum format,
                GLenum type,
                uint8_t* destination) {
  const uint8_t r = source[0];
  const uint8_t g = source[1];
  const uint8_t b = source[2];
  const uint8_t a = source[3];
  if (type == GL_FLOAT) {
    float values[4] = {Unorm8ToFloat(r), Unorm8ToFloat(g), Unorm8ToFloat(b),
                       Unorm8ToFloat(a)};
    if (format == GL_RGBA) {
      memcpy(destination, values, sizeof(values));
    } else if (format == GL_RGB) {
      memcpy(destination, values, sizeof(float) * 3);
    } else {
      memcpy(destination, values, sizeof(float));
    }
    return;
  }
  if (type == GL_UNSIGNED_SHORT_5_6_5) {
    const uint16_t value =
        static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    memcpy(destination, &value, sizeof(value));
    return;
  }
  if (type == GL_UNSIGNED_SHORT_4_4_4_4) {
    const uint16_t value = static_cast<uint16_t>(
        ((r >> 4) << 12) | ((g >> 4) << 8) | ((b >> 4) << 4) | (a >> 4));
    memcpy(destination, &value, sizeof(value));
    return;
  }
  if (type == GL_UNSIGNED_SHORT_5_5_5_1) {
    const uint16_t value = static_cast<uint16_t>(
        ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | (a >> 7));
    memcpy(destination, &value, sizeof(value));
    return;
  }
  switch (format) {
    case GL_RGBA: {
      uint8_t values[4] = {r, g, b, a};
      memcpy(destination, values, 4);
      return;
    }
    case GL_RGB: {
      uint8_t values[3] = {r, g, b};
      memcpy(destination, values, 3);
      return;
    }
    case GL_RG: {
      uint8_t values[2] = {r, g};
      memcpy(destination, values, 2);
      return;
    }
    case GL_LUMINANCE_ALPHA: {
      uint8_t values[2] = {r, a};
      memcpy(destination, values, 2);
      return;
    }
    case GL_LUMINANCE: {
      destination[0] = r;
      return;
    }
    case GL_ALPHA: {
      destination[0] = a;
      return;
    }
    case GL_RED:
    default: {
      destination[0] = r;
      return;
    }
  }
}

// Writes |packed| to every pixel of the colour draw target after checking that
// the requested draw buffer exists in the currently bound draw framebuffer.
void ClearColourBuffer(Context* context, GLint drawbuffer, uint32_t packed) {
  if (drawbuffer < 0 || drawbuffer >= kMaxColorAttachments) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  const Framebuffer* framebuffer =
      context->framebuffers.Get(context->state.framebuffer);
  if (framebuffer && !framebuffer->colors[drawbuffer].bound()) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  RenderTarget target = context->GetDrawTarget();
  if (!target.valid()) {
    context->SetError(GL_INVALID_FRAMEBUFFER_OPERATION);
    return;
  }
  for (size_t index = 0;
       index < static_cast<size_t>(target.width) * target.height; ++index) {
    memcpy(target.color + index * 4, &packed, sizeof(packed));
  }
}

// Validates the attachment list passed to glInvalidate* and reports whether
// every entry names storage owned by the currently bound framebuffer.  The
// backend keeps all attachments resident, so discarding is a no-op once the
// arguments are known to be legal.
bool InvalidateAccepted(Context* context,
                        GLenum target,
                        GLsizei num_attachments,
                        const GLenum* attachments) {
  if (!ValidFramebufferTarget(target)) {
    context->SetError(GL_INVALID_ENUM);
    return false;
  }
  if (num_attachments < 0) {
    context->SetError(GL_INVALID_VALUE);
    return false;
  }
  const Framebuffer* framebuffer = CurrentFramebuffer(context, target);
  for (GLsizei i = 0; i < num_attachments; ++i) {
    const GLenum attachment = attachments ? attachments[i] : GL_NONE;
    const bool depth_stencil = attachment == GL_DEPTH_ATTACHMENT ||
                               attachment == GL_STENCIL_ATTACHMENT ||
                               attachment == GL_DEPTH_STENCIL_ATTACHMENT;
    const bool colour =
        attachment >= GL_COLOR_ATTACHMENT0 &&
        attachment < GL_COLOR_ATTACHMENT0 + kMaxColorAttachments;
    if (!depth_stencil && !colour) {
      context->SetError(GL_INVALID_ENUM);
      return false;
    }
    if (!framebuffer)
      continue;
    bool bound = false;
    if (colour) {
      bound = framebuffer->colors[attachment - GL_COLOR_ATTACHMENT0].bound();
    } else if (attachment == GL_DEPTH_ATTACHMENT) {
      bound = framebuffer->depth.bound();
    } else if (attachment == GL_STENCIL_ATTACHMENT) {
      bound = framebuffer->stencil.bound();
    } else {
      bound = framebuffer->depth.bound() || framebuffer->stencil.bound();
    }
    if (!bound) {
      context->SetError(GL_INVALID_OPERATION);
      return false;
    }
  }
  return true;
}

}  // namespace
}  // namespace zgl

extern "C" {

// ---- Framebuffer objects --------------------------------------------------

void glGenFramebuffers(GLsizei n, GLuint* framebuffers) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !framebuffers)
    return;
  if (n < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  for (GLsizei i = 0; i < n; ++i)
    framebuffers[i] = context->framebuffers.Create();
}

void glDeleteFramebuffers(GLsizei n, const GLuint* framebuffers) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !framebuffers)
    return;
  if (n < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  for (GLsizei i = 0; i < n; ++i) {
    context->framebuffers.Delete(framebuffers[i]);
    if (context->state.framebuffer == framebuffers[i])
      context->state.framebuffer = 0;
    if (context->state.read_framebuffer == framebuffers[i])
      context->state.read_framebuffer = 0;
  }
}

GLboolean glIsFramebuffer(GLuint framebuffer) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || framebuffer == 0)
    return GL_FALSE;
  return context->framebuffers.Get(framebuffer) ? GL_TRUE : GL_FALSE;
}

void glBindFramebuffer(GLenum target, GLuint framebuffer) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (!zgl::ValidFramebufferTarget(target)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (framebuffer != 0 && !context->framebuffers.Get(framebuffer)) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER)
    context->state.framebuffer = framebuffer;
  if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER)
    context->state.read_framebuffer = framebuffer;
}

void glFramebufferTexture2D(GLenum target,
                            GLenum attachment,
                            GLenum textarget,
                            GLuint texture,
                            GLint level) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (!zgl::ValidFramebufferTarget(target)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  zgl::Framebuffer* framebuffer = zgl::CurrentFramebuffer(context, target);
  if (!framebuffer) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  zgl::FramebufferAttachment* slot = nullptr;
  if (attachment >= GL_COLOR_ATTACHMENT0 &&
      attachment < GL_COLOR_ATTACHMENT0 + zgl::kMaxColorAttachments) {
    slot = &framebuffer->colors[attachment - GL_COLOR_ATTACHMENT0];
  } else if (attachment == GL_DEPTH_ATTACHMENT) {
    slot = &framebuffer->depth;
  } else if (attachment == GL_STENCIL_ATTACHMENT) {
    slot = &framebuffer->stencil;
  } else if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
    slot = &framebuffer->depth;
  } else {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (level < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (texture == 0) {
    slot->texture = 0;
    slot->level = 0;
    return;
  }
  if (!context->textures.Get(texture)) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (textarget != GL_TEXTURE_2D && textarget != GL_TEXTURE_CUBE_MAP &&
      !(textarget >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
        textarget <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  slot->texture = texture;
  slot->level = level;
  slot->renderbuffer = 0;
  if (attachment == GL_DEPTH_STENCIL_ATTACHMENT)
    framebuffer->stencil = *slot;
}

void glFramebufferRenderbuffer(GLenum target,
                               GLenum attachment,
                               GLenum renderbuffer_target,
                               GLuint renderbuffer) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (!zgl::ValidFramebufferTarget(target)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  zgl::Framebuffer* framebuffer = zgl::CurrentFramebuffer(context, target);
  if (!framebuffer) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  zgl::FramebufferAttachment* slot = nullptr;
  if (attachment >= GL_COLOR_ATTACHMENT0 &&
      attachment < GL_COLOR_ATTACHMENT0 + zgl::kMaxColorAttachments) {
    slot = &framebuffer->colors[attachment - GL_COLOR_ATTACHMENT0];
  } else if (attachment == GL_DEPTH_ATTACHMENT) {
    slot = &framebuffer->depth;
  } else if (attachment == GL_STENCIL_ATTACHMENT) {
    slot = &framebuffer->stencil;
  } else if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
    slot = &framebuffer->depth;
  } else {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (renderbuffer != 0 && !context->renderbuffers.Get(renderbuffer)) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (renderbuffer != 0 && renderbuffer_target != GL_RENDERBUFFER) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  slot->renderbuffer = renderbuffer;
  slot->texture = 0;
  slot->level = 0;
  if (attachment == GL_DEPTH_STENCIL_ATTACHMENT)
    framebuffer->stencil = *slot;
}

GLenum glCheckFramebufferStatus(GLenum target) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return GL_FRAMEBUFFER_UNDEFINED;
  if (!zgl::ValidFramebufferTarget(target)) {
    context->SetError(GL_INVALID_ENUM);
    return 0;
  }
  if (target != GL_READ_FRAMEBUFFER && context->state.framebuffer == 0)
    return GL_FRAMEBUFFER_COMPLETE;
  zgl::Framebuffer* framebuffer = zgl::CurrentFramebuffer(context, target);
  if (!framebuffer)
    return GL_FRAMEBUFFER_COMPLETE;
  return zgl::CheckCompleteness(context, *framebuffer);
}

void glGetFramebufferAttachmentParameteriv(GLenum target,
                                           GLenum attachment,
                                           GLenum pname,
                                           GLint* params) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !params)
    return;
  if (!zgl::ValidFramebufferTarget(target)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  zgl::Framebuffer* framebuffer = zgl::CurrentFramebuffer(context, target);
  if (!framebuffer) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  const zgl::FramebufferAttachment* slot = nullptr;
  if (attachment >= GL_COLOR_ATTACHMENT0 &&
      attachment < GL_COLOR_ATTACHMENT0 + zgl::kMaxColorAttachments) {
    slot = &framebuffer->colors[attachment - GL_COLOR_ATTACHMENT0];
  } else if (attachment == GL_DEPTH_ATTACHMENT) {
    slot = &framebuffer->depth;
  } else if (attachment == GL_STENCIL_ATTACHMENT) {
    slot = &framebuffer->stencil;
  } else {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  switch (pname) {
    case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE:
      params[0] = slot->texture
                      ? GL_TEXTURE
                      : (slot->renderbuffer ? GL_RENDERBUFFER : GL_NONE);
      return;
    case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME:
      params[0] = static_cast<GLint>(slot->texture ? slot->texture
                                                   : slot->renderbuffer);
      return;
    case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL:
      params[0] = slot->level;
      return;
    case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE:
      params[0] = 0;
      return;
    case GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE:
    case GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE:
    case GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE:
    case GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE:
      params[0] = 8;
      return;
    case GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE:
      params[0] = 16;
      return;
    case GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE:
      params[0] = 8;
      return;
    case GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE:
      params[0] = GL_UNSIGNED_NORMALIZED;
      return;
    default:
      context->SetError(GL_INVALID_ENUM);
      return;
  }
}

void glDrawBuffers(GLsizei n, const GLenum* buffers) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !buffers)
    return;
  if (n < 0 || n > zgl::kMaxDrawBuffers) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  zgl::Framebuffer* framebuffer =
      context->framebuffers.Get(context->state.framebuffer);
  if (!framebuffer) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  for (GLsizei i = 0; i < n; ++i) {
    const GLenum value = buffers[i];
    if (value != GL_NONE &&
        (value < GL_COLOR_ATTACHMENT0 ||
         value >= GL_COLOR_ATTACHMENT0 + zgl::kMaxColorAttachments)) {
      context->SetError(GL_INVALID_ENUM);
      return;
    }
  }
  for (GLsizei i = 0; i < n; ++i)
    framebuffer->draw_buffers[i] = buffers[i];
  framebuffer->draw_buffer_count = n;
}

void glDrawBuffer(GLenum mode) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  zgl::Framebuffer* framebuffer =
      context->framebuffers.Get(context->state.framebuffer);
  if (!framebuffer) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  framebuffer->draw_buffers[0] = mode;
  framebuffer->draw_buffer_count = 1;
}

void glReadBuffer(GLenum mode) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (mode != GL_NONE &&
      (mode < GL_COLOR_ATTACHMENT0 ||
       mode >= GL_COLOR_ATTACHMENT0 + zgl::kMaxColorAttachments)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  zgl::Framebuffer* framebuffer =
      context->framebuffers.Get(context->state.read_framebuffer);
  if (framebuffer)
    framebuffer->read_buffer = mode;
}

// ---- Renderbuffer objects -------------------------------------------------

void glGenRenderbuffers(GLsizei n, GLuint* renderbuffers) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !renderbuffers)
    return;
  if (n < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  for (GLsizei i = 0; i < n; ++i)
    renderbuffers[i] = context->renderbuffers.Create();
}

void glDeleteRenderbuffers(GLsizei n, const GLuint* renderbuffers) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !renderbuffers)
    return;
  if (n < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  for (GLsizei i = 0; i < n; ++i) {
    context->renderbuffers.Delete(renderbuffers[i]);
    if (context->state.renderbuffer == renderbuffers[i])
      context->state.renderbuffer = 0;
  }
}

GLboolean glIsRenderbuffer(GLuint renderbuffer) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || renderbuffer == 0)
    return GL_FALSE;
  return context->renderbuffers.Get(renderbuffer) ? GL_TRUE : GL_FALSE;
}

void glBindRenderbuffer(GLenum target, GLuint renderbuffer) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (target != GL_RENDERBUFFER) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (renderbuffer != 0 && !context->renderbuffers.Get(renderbuffer)) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  context->state.renderbuffer = renderbuffer;
}

void glRenderbufferStorage(GLenum target,
                           GLenum internal_format,
                           GLsizei width,
                           GLsizei height) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (target != GL_RENDERBUFFER) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  zgl::Renderbuffer* renderbuffer =
      context->renderbuffers.Get(context->state.renderbuffer);
  if (!renderbuffer) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (width < 0 || height < 0 || width > zgl::kMaxRenderbufferSize ||
      height > zgl::kMaxRenderbufferSize) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  switch (internal_format) {
    case GL_RGBA4:
    case GL_RGB5_A1:
    case GL_RGB565:
    case GL_RGBA8:
    case GL_RGB8:
    case GL_SRGB8_ALPHA8:
    case GL_RGB:
    case GL_RGBA:
      break;
    case GL_DEPTH_COMPONENT16:
    case GL_DEPTH_COMPONENT24:
    case GL_DEPTH_COMPONENT32F:
    case GL_STENCIL_INDEX8:
    case GL_DEPTH24_STENCIL8:
    case GL_DEPTH32F_STENCIL8:
      break;
    default:
      context->SetError(GL_INVALID_ENUM);
      return;
  }
  renderbuffer->width = width;
  renderbuffer->height = height;
  renderbuffer->format = internal_format;
  renderbuffer->image.width = width;
  renderbuffer->image.height = height;
  renderbuffer->image.depth = 1;
  renderbuffer->image.rgba.clear();
  renderbuffer->image.depth16.clear();
  renderbuffer->image.stencil8.clear();
  const size_t texels = static_cast<size_t>(width) * height;
  switch (internal_format) {
    case GL_DEPTH_COMPONENT16:
    case GL_DEPTH_COMPONENT24:
    case GL_DEPTH_COMPONENT32F:
      renderbuffer->image.depth16.assign(texels, 0xFFFFu);
      break;
    case GL_STENCIL_INDEX8:
      renderbuffer->image.stencil8.assign(texels, 0u);
      break;
    case GL_DEPTH24_STENCIL8:
    case GL_DEPTH32F_STENCIL8:
      renderbuffer->image.depth16.assign(texels, 0xFFFFu);
      renderbuffer->image.stencil8.assign(texels, 0u);
      break;
    default:
      renderbuffer->image.rgba.assign(texels, zgl::PackRgba(0, 0, 0, 0));
      break;
  }
}

void glRenderbufferStorageMultisample(GLenum target,
                                      GLsizei samples,
                                      GLenum internal_format,
                                      GLsizei width,
                                      GLsizei height) {
  if (samples != 0) {
    zgl::Context* context = zgl::ApiContext();
    if (context)
      context->SetError(GL_INVALID_OPERATION);
    return;
  }
  glRenderbufferStorage(target, internal_format, width, height);
}

void glGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint* params) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !params)
    return;
  if (target != GL_RENDERBUFFER) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  const zgl::Renderbuffer* renderbuffer =
      context->renderbuffers.Get(context->state.renderbuffer);
  if (!renderbuffer) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  switch (pname) {
    case GL_RENDERBUFFER_WIDTH:
      params[0] = renderbuffer->width;
      return;
    case GL_RENDERBUFFER_HEIGHT:
      params[0] = renderbuffer->height;
      return;
    case GL_RENDERBUFFER_INTERNAL_FORMAT:
      params[0] = static_cast<GLint>(renderbuffer->format);
      return;
    case GL_RENDERBUFFER_RED_SIZE:
    case GL_RENDERBUFFER_GREEN_SIZE:
    case GL_RENDERBUFFER_BLUE_SIZE:
    case GL_RENDERBUFFER_ALPHA_SIZE:
      params[0] = renderbuffer->image.rgba.empty() ? 0 : 8;
      return;
    case GL_RENDERBUFFER_DEPTH_SIZE:
      params[0] = renderbuffer->image.depth16.empty() ? 0 : 16;
      return;
    case GL_RENDERBUFFER_STENCIL_SIZE:
      params[0] = renderbuffer->image.stencil8.empty() ? 0 : 8;
      return;
    case GL_RENDERBUFFER_SAMPLES:
      params[0] = 0;
      return;
    default:
      context->SetError(GL_INVALID_ENUM);
      return;
  }
}

// ---- Pixel transfer -------------------------------------------------------

void glReadPixels(GLint x,
                  GLint y,
                  GLsizei width,
                  GLsizei height,
                  GLenum format,
                  GLenum type,
                  void* pixels) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !pixels)
    return;
  if (width < 0 || height < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (format == GL_DEPTH_COMPONENT || format == GL_DEPTH_STENCIL) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  bool supported = false;
  switch (format) {
    case GL_RGBA:
    case GL_RGB:
    case GL_RG:
    case GL_RED:
    case GL_ALPHA:
    case GL_LUMINANCE:
    case GL_LUMINANCE_ALPHA:
      supported = type == GL_UNSIGNED_BYTE || type == GL_FLOAT;
      break;
    default:
      supported = false;
      break;
  }
  if (!supported &&
      !(format == GL_RGBA && (type == GL_UNSIGNED_SHORT_4_4_4_4 ||
                              type == GL_UNSIGNED_SHORT_5_5_5_1)) &&
      !(format == GL_RGB && type == GL_UNSIGNED_SHORT_5_6_5)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  const zgl::RenderTarget target = context->GetReadTarget();
  if (!target.valid()) {
    context->SetError(GL_INVALID_FRAMEBUFFER_OPERATION);
    return;
  }
  const zgl::PixelStoreState& store = context->state.pixel_store;
  const size_t bytes = zgl::PixelBytes(format, type);
  const GLint row_length =
      store.pack_row_length > 0 ? store.pack_row_length : width;
  uint8_t* destination =
      static_cast<uint8_t*>(pixels) +
      static_cast<size_t>(store.pack_skip_rows) * row_length * bytes +
      static_cast<size_t>(store.pack_skip_pixels) * bytes;
  for (GLsizei row = 0; row < height; ++row) {
    const GLint source_y = y + height - 1 - row;
    uint8_t* row_start =
        destination + static_cast<size_t>(row) * row_length * bytes;
    if (source_y < 0 || source_y >= target.height)
      continue;
    for (GLsizei column = 0; column < width; ++column) {
      const GLint source_x = x + column;
      if (source_x < 0 || source_x >= target.width)
        continue;
      const uint8_t* source =
          target.color + (static_cast<size_t>(source_y) * target.width +
                          static_cast<size_t>(source_x)) *
                             4;
      zgl::WritePixel(source, format, type,
                      row_start + static_cast<size_t>(column) * bytes);
    }
  }
}

void glBlitFramebuffer(GLint src_x0,
                       GLint src_y0,
                       GLint src_x1,
                       GLint src_y1,
                       GLint dst_x0,
                       GLint dst_y0,
                       GLint dst_x1,
                       GLint dst_y1,
                       GLbitfield mask,
                       GLenum filter) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (filter != GL_NEAREST && filter != GL_LINEAR) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  zgl::RenderTarget source = context->GetReadTarget();
  zgl::RenderTarget destination = context->GetDrawTarget();
  if (!source.valid() || !destination.valid()) {
    context->SetError(GL_INVALID_FRAMEBUFFER_OPERATION);
    return;
  }
  if (mask & GL_COLOR_BUFFER_BIT) {
    const int dst_width = dst_x1 - dst_x0;
    const int dst_height = dst_y1 - dst_y0;
    const int src_width = src_x1 - src_x0;
    const int src_height = src_y1 - src_y0;
    if (dst_width != 0 && dst_height != 0 && src_width != 0 &&
        src_height != 0) {
      for (int row = 0; row < dst_height; ++row) {
        for (int column = 0; column < dst_width; ++column) {
          const int destination_x = dst_x0 + column;
          const int destination_y = dst_y0 + row;
          const int gl_y = destination.height - 1 - destination_y;
          if (destination_x < 0 || destination_x >= destination.width ||
              gl_y < 0 || gl_y >= destination.height) {
            continue;
          }
          const int source_offset_x = src_x0 + (column * src_width) / dst_width;
          const int source_offset_y =
              src_y1 - 1 - (row * src_height) / dst_height;
          if (source_offset_x < 0 || source_offset_x >= source.width ||
              source_offset_y < 0 || source_offset_y >= source.height) {
            continue;
          }
          const size_t source_index =
              static_cast<size_t>(source.height - 1 - source_offset_y) *
                  source.width +
              static_cast<size_t>(source_offset_x);
          const size_t destination_index =
              static_cast<size_t>(gl_y) * destination.width +
              static_cast<size_t>(destination_x);
          uint32_t value;
          memcpy(&value, source.color + source_index * 4, sizeof(value));
          uint32_t existing;
          memcpy(&existing, destination.color + destination_index * 4,
                 sizeof(existing));
          const zgl::Rgba previous = zgl::UnpackRgba(existing);
          const zgl::Rgba incoming = zgl::UnpackRgba(value);
          uint8_t merged[4];
          merged[0] = context->state.color_mask[0] ? incoming.r : previous.r;
          merged[1] = context->state.color_mask[1] ? incoming.g : previous.g;
          merged[2] = context->state.color_mask[2] ? incoming.b : previous.b;
          merged[3] = context->state.color_mask[3] ? incoming.a : previous.a;
          const uint32_t packed =
              zgl::PackRgba(merged[0], merged[1], merged[2], merged[3]);
          memcpy(destination.color + destination_index * 4, &packed,
                 sizeof(packed));
        }
      }
    }
  }
  if ((mask & GL_DEPTH_BUFFER_BIT) && source.depth && destination.depth) {
    for (GLint y = dst_y0; y < dst_y1; ++y) {
      for (GLint x = dst_x0; x < dst_x1; ++x) {
        const GLint gl_y = destination.height - 1 - y;
        if (x < 0 || x >= destination.width || gl_y < 0 ||
            gl_y >= destination.height)
          continue;
        const GLint source_x = src_x0 + (x - dst_x0);
        const GLint source_y = src_y1 - 1 - (y - dst_y0);
        if (source_x < 0 || source_x >= source.width || source_y < 0 ||
            source_y >= source.height)
          continue;
        destination.depth[static_cast<size_t>(gl_y) * destination.width + x] =
            source.depth[static_cast<size_t>(source.height - 1 - source_y) *
                             source.width +
                         source_x];
      }
    }
  }
  if ((mask & GL_STENCIL_BUFFER_BIT) && source.stencil && destination.stencil) {
    for (GLint y = dst_y0; y < dst_y1; ++y) {
      for (GLint x = dst_x0; x < dst_x1; ++x) {
        const GLint gl_y = destination.height - 1 - y;
        if (x < 0 || x >= destination.width || gl_y < 0 ||
            gl_y >= destination.height)
          continue;
        const GLint source_x = src_x0 + (x - dst_x0);
        const GLint source_y = src_y1 - 1 - (y - dst_y0);
        if (source_x < 0 || source_x >= source.width || source_y < 0 ||
            source_y >= source.height)
          continue;
        destination.stencil[static_cast<size_t>(gl_y) * destination.width + x] =
            source.stencil[static_cast<size_t>(source.height - 1 - source_y) *
                               source.width +
                           source_x];
      }
    }
  }
}

void glClearBufferiv(GLenum buffer, GLint drawbuffer, const GLint* value) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !value)
    return;
  if (buffer == GL_COLOR) {
    const zgl::Rgba color =
        zgl::Rgba{static_cast<uint8_t>(zgl::ClampInt(value[0], 0, 255)),
                  static_cast<uint8_t>(zgl::ClampInt(value[1], 0, 255)),
                  static_cast<uint8_t>(zgl::ClampInt(value[2], 0, 255)),
                  static_cast<uint8_t>(zgl::ClampInt(value[3], 0, 255))};
    zgl::ClearColourBuffer(context, drawbuffer, zgl::PackRgba(color));
    return;
  }
  if (buffer == GL_STENCIL) {
    if (drawbuffer != 0) {
      context->SetError(GL_INVALID_VALUE);
      return;
    }
    zgl::RenderTarget target = context->GetDrawTarget();
    if (target.valid() && target.stencil) {
      std::fill(target.stencil, target.stencil + target.width * target.height,
                static_cast<uint8_t>(value[0]));
    }
    return;
  }
  context->SetError(GL_INVALID_ENUM);
}

void glClearBufferuiv(GLenum buffer, GLint drawbuffer, const GLuint* value) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !value)
    return;
  if (buffer == GL_COLOR) {
    const zgl::Rgba color = zgl::Rgba{
        static_cast<uint8_t>(zgl::ClampInt(static_cast<int>(value[0]), 0, 255)),
        static_cast<uint8_t>(zgl::ClampInt(static_cast<int>(value[1]), 0, 255)),
        static_cast<uint8_t>(zgl::ClampInt(static_cast<int>(value[2]), 0, 255)),
        static_cast<uint8_t>(
            zgl::ClampInt(static_cast<int>(value[3]), 0, 255))};
    zgl::ClearColourBuffer(context, drawbuffer, zgl::PackRgba(color));
    return;
  }
  context->SetError(GL_INVALID_ENUM);
}

void glClearBufferfv(GLenum buffer, GLint drawbuffer, const GLfloat* value) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !value)
    return;
  if (buffer == GL_COLOR) {
    const zgl::Rgba color =
        zgl::Rgba{zgl::FloatToUnorm8(value[0]), zgl::FloatToUnorm8(value[1]),
                  zgl::FloatToUnorm8(value[2]), zgl::FloatToUnorm8(value[3])};
    zgl::ClearColourBuffer(context, drawbuffer, zgl::PackRgba(color));
    return;
  }
  if (buffer == GL_DEPTH) {
    zgl::RenderTarget target = context->GetDrawTarget();
    if (target.valid() && target.depth) {
      const uint16_t depth =
          static_cast<uint16_t>(zgl::Clamp01(value[0]) * 65535.0f + 0.5f);
      std::fill(
          target.depth,
          target.depth + static_cast<size_t>(target.width) * target.height,
          depth);
    }
    return;
  }
  context->SetError(GL_INVALID_ENUM);
}

void glClearBufferfi(GLenum buffer,
                     GLint drawbuffer,
                     GLfloat depth,
                     GLint stencil) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (buffer != GL_DEPTH_STENCIL || drawbuffer != 0) {
    context->SetError(buffer == GL_DEPTH_STENCIL ? GL_INVALID_VALUE
                                                 : GL_INVALID_ENUM);
    return;
  }
  zgl::RenderTarget target = context->GetDrawTarget();
  if (!target.valid()) {
    context->SetError(GL_INVALID_FRAMEBUFFER_OPERATION);
    return;
  }
  const size_t texels = static_cast<size_t>(target.width) * target.height;
  if (target.depth) {
    std::fill(target.depth, target.depth + texels,
              static_cast<uint16_t>(zgl::Clamp01(depth) * 65535.0f + 0.5f));
  }
  if (target.stencil) {
    std::fill(target.stencil, target.stencil + texels,
              static_cast<uint8_t>(stencil));
  }
}

void glInvalidateFramebuffer(GLenum target,
                             GLsizei num_attachments,
                             const GLenum* attachments) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  zgl::InvalidateAccepted(context, target, num_attachments, attachments);
}

void glInvalidateSubFramebuffer(GLenum target,
                                GLsizei num_attachments,
                                const GLenum* attachments,
                                GLint x,
                                GLint y,
                                GLsizei width,
                                GLsizei height) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (!zgl::InvalidateAccepted(context, target, num_attachments, attachments))
    return;
  if (width < 0 || height < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  (void)x;
  (void)y;
}

}  // extern "C"
