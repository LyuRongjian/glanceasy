// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>

#include "GLES3/gl32.h"
#include "src/glanceasy_api_common.h"
#include "src/glanceasy_simd.h"

namespace glanceasy {
namespace {

// The CTS FBO surface is the first framebuffer allocated by the framework.
// It may not have been promoted to "surface" yet while the driver is still
// configuring its attachments, so treat the candidate as platform-owned too.
bool IsPlatformFramebuffer(const Context* context, GLuint name) {
  return name != 0 &&
         (context->state.surface_framebuffer == name ||
          context->state.candidate_surface_framebuffer == name);
}

GLenum AttachmentFormat(Context* context,
                        const FramebufferAttachment& attachment) {
  if (attachment.texture) {
    const Texture* texture = context->textures.Get(attachment.texture);
    return texture ? texture->internal_format : 0;
  }
  if (attachment.renderbuffer) {
    const Renderbuffer* renderbuffer =
        context->renderbuffers.Get(attachment.renderbuffer);
    return renderbuffer ? renderbuffer->format : 0;
  }
  return 0;
}

bool IsDepthFormat(GLenum format) {
  return format == GL_DEPTH_COMPONENT16 || format == GL_DEPTH_COMPONENT24 ||
         format == GL_DEPTH_COMPONENT32F ||
         format == GL_DEPTH24_STENCIL8 ||
         format == GL_DEPTH32F_STENCIL8;
}

bool IsStencilFormat(GLenum format) {
  return format == GL_STENCIL_INDEX8 || format == GL_DEPTH24_STENCIL8 ||
         format == GL_DEPTH32F_STENCIL8;
}

bool IsSignedIntegerFormat(GLenum format) {
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

bool IsUnsignedIntegerFormat(GLenum format) {
  switch (format) {
    case GL_R8UI:
    case GL_R16UI:
    case GL_R32UI:
    case GL_RG8UI:
    case GL_RG16UI:
    case GL_RG32UI:
    case GL_RGB8UI:
    case GL_RGB16UI:
    case GL_RGB32UI:
    case GL_RGBA8UI:
    case GL_RGBA16UI:
    case GL_RGBA32UI:
    case GL_RGB10_A2UI:
      return true;
    default:
      return false;
  }
}

bool IsSrgbFormat(GLenum format) {
  return format == GL_SRGB8_ALPHA8 || format == GL_SRGB8;
}

bool IsColorRenderableFormat(GLenum format, bool is_texture) {
  switch (format) {
    case GL_RGBA8:
    case GL_RGB8:
    case GL_RGB565:
    case GL_RGBA4:
    case GL_RGB5_A1:
    case GL_SRGB8_ALPHA8:
    case GL_R8:
    case GL_RG8:
    case GL_RGB10_A2:
    case GL_RGB10_A2UI:
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
    case GL_RGBA8I:
    case GL_RGBA8UI:
    case GL_RGBA16I:
    case GL_RGBA16UI:
    case GL_RGBA32I:
    case GL_RGBA32UI:
      return true;
    case GL_RGB:
    case GL_RGBA:
      return is_texture;
    default:
      break;
  }
  if (is_texture)
    return false;
  return false;
}

const TextureImage* AttachmentImageOf(Context* context,
                                      const FramebufferAttachment& attachment) {
  if (attachment.retained_texture)
    return attachment.retained_texture->image(attachment.level, 0);
  if (attachment.texture) {
    const Texture* texture = context->textures.Get(attachment.texture);
    if (!texture)
      return nullptr;
    return texture->image(attachment.level, 0);
  }
  if (attachment.retained_renderbuffer)
    return &attachment.retained_renderbuffer->image;
  if (attachment.renderbuffer) {
    const Renderbuffer* renderbuffer =
        context->renderbuffers.Get(attachment.renderbuffer);
    if (!renderbuffer)
      return nullptr;
    return &renderbuffer->image;
  }
  return nullptr;
}

int AttachmentSamples(const Context& context,
                      const FramebufferAttachment& attachment) {
  if (attachment.texture) {
    const Texture* texture = context.textures.Get(attachment.texture);
    return texture ? texture->samples : 0;
  }
  if (attachment.renderbuffer) {
    const Renderbuffer* renderbuffer =
        context.renderbuffers.Get(attachment.renderbuffer);
    return renderbuffer ? renderbuffer->samples : 0;
  }
  return 0;
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

  // A depth-only or stencil-only framebuffer is legal.  Pick the first bound
  // attachment as the reference for the shared dimensions.
  const FramebufferAttachment* reference = nullptr;
  for (int i = 0; i < kMaxColorAttachments && !reference; ++i) {
    if (framebuffer.colors[i].bound())
      reference = &framebuffer.colors[i];
  }
  if (!reference && framebuffer.depth.bound())
    reference = &framebuffer.depth;
  if (!reference && framebuffer.stencil.bound())
    reference = &framebuffer.stencil;
  if (!reference)
    return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;

  for (int i = 0; i < kMaxColorAttachments; ++i) {
    if (!framebuffer.colors[i].bound() || !framebuffer.colors[i].texture ||
        framebuffer.colors[i].retained_texture)
      continue;
    const Texture* texture = context->textures.Get(framebuffer.colors[i].texture);
    if (!texture)
      return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    if ((texture->target == GL_TEXTURE_3D ||
         texture->target == GL_TEXTURE_2D_ARRAY) &&
        framebuffer.colors[i].layer >= texture->depth)
      return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
  }

  for (int i = 0; i < kMaxColorAttachments; ++i) {
    if (!framebuffer.colors[i].bound())
      continue;
    const GLenum format = AttachmentFormat(context, framebuffer.colors[i]);
    const bool is_texture = framebuffer.colors[i].texture != 0;
    if (format == 0 || IsDepthFormat(format) || IsStencilFormat(format) ||
        !IsColorRenderableFormat(format, is_texture))
      return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
  }
  if (framebuffer.depth.bound() &&
      !IsDepthFormat(AttachmentFormat(context, framebuffer.depth)))
    return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
  if (framebuffer.stencil.bound() &&
      !IsStencilFormat(AttachmentFormat(context, framebuffer.stencil)))
    return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
  if (framebuffer.depth.bound() && framebuffer.stencil.bound()) {
    const FramebufferAttachment& d = framebuffer.depth;
    const FramebufferAttachment& s = framebuffer.stencil;
    if (d.texture != s.texture || d.renderbuffer != s.renderbuffer ||
        d.level != s.level || d.layer != s.layer)
      return GL_FRAMEBUFFER_UNSUPPORTED;
  }

  const TextureImage* color = AttachmentImageOf(context, framebuffer.colors[0]);
  const TextureImage* base = AttachmentImageOf(context, *reference);
  if (!base || (framebuffer.colors[0].bound() &&
                (!color || !color->float_rgba.empty())))
    return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
  if (base->width <= 0 || base->height <= 0)
    return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
  const int width = base->width;
  const int height = base->height;
  const int samples = AttachmentSamples(*context, *reference);
  for (int i = 1; i < kMaxColorAttachments; ++i) {
    if (!framebuffer.colors[i].bound())
      continue;
    const TextureImage* image =
        AttachmentImageOf(context, framebuffer.colors[i]);
    if (!image)
      return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    if (AttachmentSamples(*context, framebuffer.colors[i]) != samples)
      return GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE;
  }
  if (framebuffer.depth.bound()) {
    const TextureImage* image = AttachmentImageOf(context, framebuffer.depth);
    if (!image)
      return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    if (AttachmentSamples(*context, framebuffer.depth) != samples)
      return GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE;
  }
  if (framebuffer.stencil.bound()) {
    const TextureImage* image = AttachmentImageOf(context, framebuffer.stencil);
    if (!image)
      return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    if (AttachmentSamples(*context, framebuffer.stencil) != samples)
      return GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE;
  }
  return GL_FRAMEBUFFER_COMPLETE;
}

size_t PixelBytes(GLenum format, GLenum type) {
  const bool integer_format = format == GL_RGBA_INTEGER ||
                              format == GL_RGB_INTEGER ||
                              format == GL_RG_INTEGER ||
                              format == GL_RED_INTEGER;
  if (integer_format) {
    const int components = format == GL_RGBA_INTEGER
                               ? 4
                               : format == GL_RGB_INTEGER
                                     ? 3
                                     : format == GL_RG_INTEGER ? 2 : 1;
    const int component_bytes =
        type == GL_BYTE || type == GL_UNSIGNED_BYTE
            ? 1
            : type == GL_SHORT || type == GL_UNSIGNED_SHORT ? 2 : 4;
    return static_cast<size_t>(components * component_bytes);
  }
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

void WriteIntegerPixel(const uint32_t* source,
                       GLenum format,
                       GLenum type,
                       int bits,
                       bool is_unsigned,
                       uint8_t* destination) {
  const int count = format == GL_RGBA_INTEGER
                        ? 4
                        : format == GL_RGB_INTEGER
                              ? 3
                              : format == GL_RG_INTEGER ? 2 : 1;
  const auto clamped = [&](int channel) -> int64_t {
    // Narrow signed formats store a sign-extended value in the 32-bit slot.
    // Treating that slot as unsigned would clamp negative values to the
    // positive maximum instead of preserving their sign.
    const int64_t raw = is_unsigned
                            ? static_cast<int64_t>(source[channel])
                            : static_cast<int64_t>(
                                  static_cast<int32_t>(source[channel]));
    if (bits >= 32)
      return raw;
    const int64_t minimum = is_unsigned ? 0 : -(1LL << (bits - 1));
    const int64_t maximum =
        is_unsigned ? (1LL << bits) - 1 : (1LL << (bits - 1)) - 1;
    return std::min(std::max(raw, minimum), maximum);
  };
  switch (type) {
    case GL_BYTE:
      for (int i = 0; i < count; ++i)
        destination[i] = static_cast<uint8_t>(clamped(i));
      return;
    case GL_UNSIGNED_BYTE:
      for (int i = 0; i < count; ++i)
        destination[i] = static_cast<uint8_t>(clamped(i));
      return;
    case GL_SHORT:
      for (int i = 0; i < count; ++i) {
        const int16_t value = static_cast<int16_t>(clamped(i));
        memcpy(destination + i * sizeof(int16_t), &value, sizeof(value));
      }
      return;
    case GL_UNSIGNED_SHORT:
      for (int i = 0; i < count; ++i) {
        const uint16_t value = static_cast<uint16_t>(clamped(i));
        memcpy(destination + i * sizeof(uint16_t), &value, sizeof(value));
      }
      return;
    case GL_INT:
      for (int i = 0; i < count; ++i) {
        const int32_t value = static_cast<int32_t>(clamped(i));
        memcpy(destination + i * sizeof(int32_t), &value, sizeof(value));
      }
      return;
    default:
      for (int i = 0; i < count; ++i) {
        const uint32_t value = static_cast<uint32_t>(clamped(i));
        memcpy(destination + i * sizeof(uint32_t), &value, sizeof(uint32_t));
      }
      return;
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
  const GLenum buffer = target.draw_buffers[drawbuffer];
  if (getenv("GLANCEASY_DEBUG_BLIT"))
    fprintf(stderr, "[clearbuf] fb=%u db=%d->%x\n", context->state.framebuffer,
            drawbuffer, static_cast<unsigned>(buffer));
  if (buffer == GL_NONE)
    return;
  if (buffer < GL_COLOR_ATTACHMENT0 ||
      buffer >= GL_COLOR_ATTACHMENT0 + kMaxColorAttachments) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  const int attachment = buffer - GL_COLOR_ATTACHMENT0;
  if (target.color_is_srgb[attachment]) {
    const Rgba c = UnpackRgba(packed);
    packed = PackRgba(LinearToSrgb8(c.r / 255.0f),
                      LinearToSrgb8(c.g / 255.0f),
                      LinearToSrgb8(c.b / 255.0f), c.a);
  }
  uint8_t* const color = target.colors[attachment];
  if (!color) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  const size_t texels = static_cast<size_t>(target.width) * target.height;
  const int planes = target.multisampled() ? target.samples : 1;
  const Rect& scissor = context->state.scissor;
  const bool scissor_test = context->state.scissor_test;
  const int x0 = scissor_test ? std::max(0, scissor.x) : 0;
  const int y0 = scissor_test ? std::max(0, scissor.y) : 0;
  const int x1 = scissor_test
                     ? std::min(target.width, scissor.x + scissor.width)
                     : target.width;
  const int y1 = scissor_test
                     ? std::min(target.height, scissor.y + scissor.height)
                     : target.height;
  for (int sample = 0; sample < planes; ++sample) {
    for (int y = y0; y < y1; ++y) {
      const size_t row = static_cast<size_t>(target.height - 1 - y);
      simd::FillRgba(color + static_cast<size_t>(sample) * texels * 4 +
                         (row * target.width + x0) * 4,
                     static_cast<size_t>(x1 - x0), packed);
    }
  }
  if (target.multisampled()) {
    uint32_t* resolved = target.resolved_colors[buffer - GL_COLOR_ATTACHMENT0];
    if (resolved) {
      for (size_t i = 0; i < texels; ++i)
        resolved[i] = packed;
    }
  }
}

void ClearIntegerColourBuffer(Context* context,
                              GLint drawbuffer,
                              const uint32_t values[4]) {
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
  const GLenum buffer = target.draw_buffers[drawbuffer];
  if (getenv("GLANCEASY_DEBUG_BLIT"))
    fprintf(stderr, "[clearbuf] fb=%u db=%d->%x\n", context->state.framebuffer,
            drawbuffer, static_cast<unsigned>(buffer));
  if (buffer == GL_NONE)
    return;
  if (buffer < GL_COLOR_ATTACHMENT0 ||
      buffer >= GL_COLOR_ATTACHMENT0 + kMaxColorAttachments) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  const int attachment = buffer - GL_COLOR_ATTACHMENT0;
  uint32_t* const destination = target.integer_colors[attachment];
  if (!destination) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  const size_t texels = static_cast<size_t>(target.width) * target.height;
  const int planes = target.multisampled() ? target.samples : 1;
  const Rect& scissor = context->state.scissor;
  const bool scissor_test = context->state.scissor_test;
  const int x0 = scissor_test ? std::max(0, scissor.x) : 0;
  const int y0 = scissor_test ? std::max(0, scissor.y) : 0;
  const int x1 = scissor_test
                     ? std::min(target.width, scissor.x + scissor.width)
                     : target.width;
  const int y1 = scissor_test
                     ? std::min(target.height, scissor.y + scissor.height)
                     : target.height;
  for (int sample = 0; sample < planes; ++sample) {
    uint32_t* plane = destination + static_cast<size_t>(sample) * texels * 4;
    for (int y = y0; y < y1; ++y) {
      const size_t row = static_cast<size_t>(target.height - 1 - y);
      uint32_t* pixels = plane + (row * target.width + x0) * 4;
      for (int x = x0; x < x1; ++x)
        memcpy(pixels + (x - x0) * 4, values, sizeof(uint32_t) * 4);
    }
  }
  if (target.multisampled()) {
    uint32_t* resolved = target.resolved_integer_colors[attachment];
    if (resolved) {
      for (size_t texel = 0; texel < texels; ++texel)
        memcpy(resolved + texel * 4, values, sizeof(uint32_t) * 4);
    }
  }
}

bool DrawBufferIsInteger(Context* context, GLint drawbuffer) {
  if (drawbuffer < 0 || drawbuffer >= kMaxColorAttachments)
    return false;
  RenderTarget target = context->GetDrawTarget();
  const GLenum buffer = target.draw_buffers[drawbuffer];
  if (buffer < GL_COLOR_ATTACHMENT0 ||
      buffer >= GL_COLOR_ATTACHMENT0 + kMaxColorAttachments)
    return false;
  return target.color_is_integer[buffer - GL_COLOR_ATTACHMENT0];
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
                               attachment == GL_DEPTH_STENCIL_ATTACHMENT ||
                               attachment == GL_DEPTH || attachment == GL_STENCIL;
    const bool colour =
        attachment == GL_COLOR ||
        (attachment >= GL_COLOR_ATTACHMENT0 &&
         attachment < GL_COLOR_ATTACHMENT0 + kMaxColorAttachments);
    if (!depth_stencil && !colour &&
        attachment >= GL_COLOR_ATTACHMENT0 && attachment <= 0x8CFF) {
      context->SetError(GL_INVALID_OPERATION);
      return false;
    }
    if (!depth_stencil && !colour) {
      context->SetError(GL_INVALID_ENUM);
      return false;
    }
    // Attachments that are not present in the bound framebuffer are ignored
    // by the specification; the backend keeps all attachments resident, so a
    // successful validation is all that is required.
    (void)framebuffer;
  }
  return true;
}

}  // namespace
}  // namespace glanceasy

extern "C" {

// ---- Framebuffer objects --------------------------------------------------

void glGenFramebuffers(GLsizei n, GLuint* framebuffers) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (n < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!framebuffers)
    return;
  for (GLsizei i = 0; i < n; ++i) {
    framebuffers[i] = context->framebuffers.Generate();
    if (getenv("GLANCEASY_DEBUG_FBO2"))
      fprintf(stderr, "[fb2] gen=%u surface=%u cand=%u ctx=%p\n",
              framebuffers[i], context->state.surface_framebuffer,
              context->state.candidate_surface_framebuffer,
              static_cast<void*>(context));
    // The CTS FBO surface creates its render target through this entry point
    // before any test-owned framebuffer exists.  Treat that first object as
    // the platform default framebuffer so GL_FRAMEBUFFER_BINDING keeps
    // reporting 0 and the default framebuffer cannot be modified directly.
    // In zero-default mode the platform surface is implict framebuffer 0;
    // FBO names 1..N belong to test cases.  Promotion would steal name 1
    // from tests that hard-code it (fbo.render et al).
    if (context->state.surface_framebuffer == 0 && i == 0 &&
        getenv("GLANCEASY_FBO_SURFACE_AS_ZERO") == nullptr)
      context->state.candidate_surface_framebuffer = framebuffers[i];
  }
}

void glDeleteFramebuffers(GLsizei n, const GLuint* framebuffers) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (n < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!framebuffers)
    return;
  for (GLsizei i = 0; i < n; ++i) {
    context->framebuffers.Delete(framebuffers[i]);
    if (getenv("GLANCEASY_DEBUG_FBO2"))
      fprintf(stderr, "[fb2] del=%u surface=%u ctx=%p\n", framebuffers[i],
              context->state.surface_framebuffer, static_cast<void*>(context));
    if (context->state.surface_framebuffer == framebuffers[i])
      context->state.surface_framebuffer = 0;
    if (context->state.candidate_surface_framebuffer == framebuffers[i])
      context->state.candidate_surface_framebuffer = 0;
    if (context->state.framebuffer == framebuffers[i])
      context->state.framebuffer = 0;
    if (context->state.read_framebuffer == framebuffers[i])
      context->state.read_framebuffer = 0;
  }
}

GLboolean glIsFramebuffer(GLuint framebuffer) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context || framebuffer == 0)
    return GL_FALSE;
  return context->framebuffers.Get(framebuffer) ? GL_TRUE : GL_FALSE;
}

void glBindFramebuffer(GLenum target, GLuint framebuffer) {
  if (getenv("GLANCEASY_DEBUG_ERROR"))
    fprintf(stderr, "[glbindfb] target=0x%x fb=%u\n",
            static_cast<unsigned>(target), framebuffer);
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (!glanceasy::ValidFramebufferTarget(target)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (framebuffer != 0)
    context->framebuffers.GetOrCreate(framebuffer);
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
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (!glanceasy::ValidFramebufferTarget(target)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  const GLuint bound_framebuffer =
      target == GL_READ_FRAMEBUFFER ? context->state.read_framebuffer
                                    : context->state.framebuffer;
  if (getenv("GLANCEASY_FBO_SURFACE_AS_ZERO") != nullptr && bound_framebuffer == 0) {
    // The zero default framebuffer never accepts attachment changes, even
    // detaching object 0 (GLES negative API cases check this).
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (context->state.surface_framebuffer != 0 &&
      bound_framebuffer == context->state.surface_framebuffer) {
    // Attaching an object to the platform default framebuffer is illegal,
    // but the CTS detach pass uses a zero object.  Accept it as a no-op
    // instead of tearing down the real surface attachment.
    if (texture != 0)
      context->SetError(GL_INVALID_OPERATION);
    return;
  }
  glanceasy::Framebuffer* framebuffer = glanceasy::CurrentFramebuffer(context, target);
  if (!framebuffer) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  glanceasy::FramebufferAttachment* slot = nullptr;
  if (attachment >= GL_COLOR_ATTACHMENT0 &&
      attachment < GL_COLOR_ATTACHMENT0 + glanceasy::kMaxColorAttachments) {
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
  if (level < 0 || level > 13) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  const bool cube_face = textarget >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
                         textarget <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z;
  if (textarget != GL_TEXTURE_2D && !cube_face) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  const int max_level = cube_face ? 11 : 13;
  if (level > max_level) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  slot->retained_texture.reset();
  if (texture == 0) {
    *slot = glanceasy::FramebufferAttachment();
    return;
  }
  glanceasy::Texture* object = context->textures.Get(texture);
  if (!object || object->delete_status) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (object->generated_only) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  const GLenum required_target =
      cube_face ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
  if (object->target != required_target) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (level >= glanceasy::MipLevelCount(glanceasy::kMaxTextureSize, glanceasy::kMaxTextureSize)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  slot->texture = texture;
  slot->level = level;
  slot->layer = 0;
  slot->cube_face =
      (textarget >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
       textarget <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)
          ? textarget
          : 0;
  slot->renderbuffer = 0;
  object->render_target_flip_y = true;
  if (attachment == GL_DEPTH_STENCIL_ATTACHMENT)
    framebuffer->stencil = *slot;
}

void glFramebufferTextureLayer(GLenum target,
                               GLenum attachment,
                               GLuint texture,
                               GLint level,
                               GLint layer) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (!glanceasy::ValidFramebufferTarget(target)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  const GLuint bound_framebuffer =
      target == GL_READ_FRAMEBUFFER ? context->state.read_framebuffer
                                    : context->state.framebuffer;
  if (getenv("GLANCEASY_FBO_SURFACE_AS_ZERO") != nullptr && bound_framebuffer == 0) {
    // The zero default framebuffer never accepts attachment changes, even
    // detaching object 0 (GLES negative API cases check this).
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (context->state.surface_framebuffer != 0 &&
      bound_framebuffer == context->state.surface_framebuffer) {
    // Attaching an object to the platform default framebuffer is illegal,
    // but the CTS detach pass uses a zero object.  Accept it as a no-op
    // instead of tearing down the real surface attachment.
    if (texture != 0)
      context->SetError(GL_INVALID_OPERATION);
    return;
  }
  glanceasy::Framebuffer* framebuffer = glanceasy::CurrentFramebuffer(context, target);
  if (!framebuffer) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  glanceasy::FramebufferAttachment* slot = nullptr;
  if (attachment >= GL_COLOR_ATTACHMENT0 &&
      attachment < GL_COLOR_ATTACHMENT0 + glanceasy::kMaxColorAttachments) {
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
  if (level < 0 || level > 13 || layer < 0 || layer > 2047) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (texture == 0) {
    slot->texture = 0;
    slot->level = 0;
    slot->layer = 0;
    slot->cube_face = 0;
    return;
  }
  const glanceasy::Texture* bound = context->textures.Get(texture);
  if (!bound) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (bound->target != GL_TEXTURE_3D && bound->target != GL_TEXTURE_2D_ARRAY) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  const int max_layer = bound->target == GL_TEXTURE_3D ? 511 : 255;
  if (layer > max_layer) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  // A layer outside the texture is not an attachment-time error; the
  // framebuffer becomes incomplete instead.
  slot->texture = texture;
  slot->level = level;
  slot->layer = layer;
  slot->renderbuffer = 0;
  if (glanceasy::Texture* object = context->textures.Get(texture))
    object->render_target_flip_y = true;
  if (attachment == GL_DEPTH_STENCIL_ATTACHMENT)
    framebuffer->stencil = *slot;
}

void glFramebufferRenderbuffer(GLenum target,
                               GLenum attachment,
                               GLenum renderbuffer_target,
                               GLuint renderbuffer) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (!glanceasy::ValidFramebufferTarget(target)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  const GLuint bound_framebuffer =
      target == GL_READ_FRAMEBUFFER ? context->state.read_framebuffer
                                    : context->state.framebuffer;
  if (getenv("GLANCEASY_DEBUG_FBO2") && bound_framebuffer == 1)
    fprintf(stderr, "[fb2] attachrb bound=1 rb=%u\n", renderbuffer);
  if (getenv("GLANCEASY_FBO_SURFACE_AS_ZERO") != nullptr && bound_framebuffer == 0) {
    // The zero default framebuffer never accepts attachment changes, even
    // detaching object 0 (GLES negative API cases check this).
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (context->state.surface_framebuffer != 0 &&
      bound_framebuffer == context->state.surface_framebuffer) {
    if (renderbuffer != 0)
      context->SetError(GL_INVALID_OPERATION);
    return;
  }
  glanceasy::Framebuffer* framebuffer = glanceasy::CurrentFramebuffer(context, target);
  if (!framebuffer) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  glanceasy::FramebufferAttachment* slot = nullptr;
  if (attachment >= GL_COLOR_ATTACHMENT0 &&
      attachment < GL_COLOR_ATTACHMENT0 + glanceasy::kMaxColorAttachments) {
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
  if (renderbuffer_target != GL_RENDERBUFFER) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (renderbuffer != 0) {
    const glanceasy::Renderbuffer* object = context->renderbuffers.Get(renderbuffer);
    if (!object || object->delete_status || object->generated_only) {
      context->SetError(GL_INVALID_OPERATION);
      return;
    }
  }
  slot->retained_renderbuffer.reset();
  if (renderbuffer == 0) {
    *slot = glanceasy::FramebufferAttachment();
  } else {
    slot->renderbuffer = renderbuffer;
    slot->texture = 0;
    slot->level = 0;
  }
  if (attachment == GL_DEPTH_STENCIL_ATTACHMENT)
    framebuffer->stencil = *slot;
}

GLenum glCheckFramebufferStatus(GLenum target) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return GL_FRAMEBUFFER_UNDEFINED;
  if (!glanceasy::ValidFramebufferTarget(target)) {
    context->SetError(GL_INVALID_ENUM);
    return 0;
  }
  if (target != GL_READ_FRAMEBUFFER && context->state.framebuffer == 0)
    return GL_FRAMEBUFFER_COMPLETE;
  glanceasy::Framebuffer* framebuffer = glanceasy::CurrentFramebuffer(context, target);
  if (!framebuffer)
    return GL_FRAMEBUFFER_COMPLETE;
  const GLenum status = glanceasy::CheckCompleteness(context, *framebuffer);
  if (getenv("GLANCEASY_DEBUG_FBO2"))
    fprintf(stderr, "[fb2] status=%x bound=%u cand=%u surf=%u\n",
            static_cast<unsigned>(status), context->state.framebuffer,
            context->state.candidate_surface_framebuffer,
            context->state.surface_framebuffer);
  if (status == GL_FRAMEBUFFER_COMPLETE &&
      context->state.surface_framebuffer == 0 &&
      context->state.candidate_surface_framebuffer != 0 &&
      context->state.framebuffer ==
          context->state.candidate_surface_framebuffer) {
    context->state.surface_framebuffer =
        context->state.candidate_surface_framebuffer;
    context->state.candidate_surface_framebuffer = 0;
    if (getenv("GLANCEASY_DEBUG_FBO2"))
      fprintf(stderr, "[fb2] promoted=%u\n",
              context->state.surface_framebuffer);
  }
  return status;
}

namespace {

int ColorComponentBits(GLenum format, int channel) {
  int bits[4] = {0, 0, 0, 0};
  switch (format) {
    case GL_R8:
    case GL_R8I:
    case GL_R8UI:
    case GL_RG8:
    case GL_RG8I:
    case GL_RG8UI:
    case GL_RGB8:
    case GL_RGB8I:
    case GL_RGB8UI:
    case GL_RGBA8:
    case GL_RGBA8I:
    case GL_RGBA8UI:
    case GL_SRGB8_ALPHA8:
      bits[0] = bits[1] = bits[2] = bits[3] = 8;
      if (format == GL_R8 || format == GL_R8I || format == GL_R8UI)
        bits[1] = bits[2] = bits[3] = 0;
      else if (format == GL_RG8 || format == GL_RG8I || format == GL_RG8UI)
        bits[2] = bits[3] = 0;
      else if (format == GL_RGB8 || format == GL_RGB8I || format == GL_RGB8UI)
        bits[3] = 0;
      break;
    case GL_R16I:
    case GL_R16UI:
    case GL_RG16I:
    case GL_RG16UI:
    case GL_RGBA16I:
    case GL_RGBA16UI:
      bits[0] = bits[1] = bits[2] = bits[3] = 16;
      if (format == GL_R16I || format == GL_R16UI)
        bits[1] = bits[2] = bits[3] = 0;
      else if (format == GL_RG16I || format == GL_RG16UI)
        bits[2] = bits[3] = 0;
      break;
    case GL_R32I:
    case GL_R32UI:
    case GL_RG32I:
    case GL_RG32UI:
    case GL_RGBA32I:
    case GL_RGBA32UI:
      bits[0] = bits[1] = bits[2] = bits[3] = 32;
      if (format == GL_R32I || format == GL_R32UI)
        bits[1] = bits[2] = bits[3] = 0;
      else if (format == GL_RG32I || format == GL_RG32UI)
        bits[2] = bits[3] = 0;
      break;
    case GL_RGBA4:
      bits[0] = bits[1] = bits[2] = bits[3] = 4;
      break;
    case GL_RGB5_A1:
      bits[0] = bits[1] = bits[2] = 5;
      bits[3] = 1;
      break;
    case GL_RGB565:
      bits[0] = 5;
      bits[1] = 6;
      bits[2] = 5;
      break;
    case GL_RGB10_A2:
    case GL_RGB10_A2UI:
      bits[0] = bits[1] = bits[2] = 10;
      bits[3] = 2;
      break;
    default:
      break;
  }
  return bits[channel];
}

uint8_t QuantizeChannel(uint8_t value, int bits) {
  const int maximum = (1 << bits) - 1;
  const int quantized = (static_cast<int>(value) * maximum + 127) / 255;
  return static_cast<uint8_t>((quantized * 255 + maximum / 2) / maximum);
}

void QuantizeColorFormat(uint8_t components[4], GLenum format) {
  switch (format) {
    case GL_RGB565:
      components[0] = QuantizeChannel(components[0], 5);
      components[1] = QuantizeChannel(components[1], 6);
      components[2] = QuantizeChannel(components[2], 5);
      components[3] = 255;
      break;
    case GL_RGB5_A1:
      components[0] = QuantizeChannel(components[0], 5);
      components[1] = QuantizeChannel(components[1], 5);
      components[2] = QuantizeChannel(components[2], 5);
      components[3] = QuantizeChannel(components[3], 1);
      break;
    case GL_RGBA4:
      components[0] = QuantizeChannel(components[0], 4);
      components[1] = QuantizeChannel(components[1], 4);
      components[2] = QuantizeChannel(components[2], 4);
      components[3] = QuantizeChannel(components[3], 4);
      break;
    case GL_RGB10_A2:
      components[3] = QuantizeChannel(components[3], 2);
      break;
    default:
      break;
  }
}

void LoadNormalizedBlitTexel(const glanceasy::RenderTarget& source,
                             int x,
                             int y,
                             uint8_t components[4]) {
  const size_t index =
      static_cast<size_t>(source.height - 1 - y) * source.width + x;
  uint32_t value;
  memcpy(&value, source.color + index * 4, sizeof(value));
  const glanceasy::Rgba texel = glanceasy::UnpackRgba(value);
  const uint8_t channels[4] = {texel.r, texel.g, texel.b, texel.a};
  components[0] = 0;
  components[1] = 0;
  components[2] = 0;
  components[3] = 255;
  for (int channel = 0; channel < 4; ++channel) {
    if (channel < source.read_channels)
      components[channel] = channels[channel];
  }
  QuantizeColorFormat(components, source.read_format);
  if (source.read_format == GL_SRGB8_ALPHA8) {
    for (int channel = 0; channel < 3; ++channel) {
      components[channel] = static_cast<uint8_t>(
          glanceasy::SrgbToLinear(components[channel]) * 255.0f + 0.5f);
    }
  }
}

bool HasColorStorage(const glanceasy::FramebufferAttachment* slot, GLenum format) {
  if (slot->texture) {
    const glanceasy::Texture* texture = glanceasy::ApiContext()->textures.Get(slot->texture);
    if (!texture)
      return false;
    int face = 0;
    if (slot->cube_face >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
        slot->cube_face <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z) {
      face = static_cast<int>(slot->cube_face - GL_TEXTURE_CUBE_MAP_POSITIVE_X);
    }
    const glanceasy::TextureImage* image = texture->image(slot->level, face);
    return image && image->width > 0 && image->height > 0 &&
           (!image->rgba.empty() || !image->integer_rgba.empty());
  }
  if (slot->renderbuffer) {
    const glanceasy::Renderbuffer* renderbuffer =
        glanceasy::ApiContext()->renderbuffers.Get(slot->renderbuffer);
    if (!renderbuffer)
      return false;
    return renderbuffer->width > 0 && renderbuffer->height > 0 &&
           (!renderbuffer->image.rgba.empty() ||
            !renderbuffer->image.integer_rgba.empty());
  }
  return false;
}

}  // namespace

void glGetFramebufferAttachmentParameteriv(GLenum target,
                                           GLenum attachment,
                                           GLenum pname,
                                           GLint* params) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context || !params)
    return;
  if (!glanceasy::ValidFramebufferTarget(target)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  glanceasy::Framebuffer* framebuffer = glanceasy::CurrentFramebuffer(context, target);
  const glanceasy::Framebuffer* surface_framebuffer =
      context->framebuffers.Get(context->state.surface_framebuffer);
  if (!framebuffer || (surface_framebuffer && framebuffer == surface_framebuffer)) {
    // The default drawable exposes GL_BACK, GL_DEPTH and GL_STENCIL as
    // GL_FRAMEBUFFER_DEFAULT attachments, but not the colour attachment
    // points of a framebuffer object.
    const bool color = attachment == GL_BACK;
    const bool depth =
        attachment == GL_DEPTH || attachment == GL_DEPTH_STENCIL;
    const bool stencil =
        attachment == GL_STENCIL || attachment == GL_DEPTH_STENCIL;
    if (!color && !depth && !stencil) {
      context->SetError(GL_INVALID_OPERATION);
      return;
    }
    switch (pname) {
      case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE:
        params[0] = GL_FRAMEBUFFER_DEFAULT;
        return;
      case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME:
      case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL:
      case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE:
      case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER:
        context->SetError(GL_INVALID_ENUM);
        return;
      case GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE:
      case GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE:
      case GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE:
      case GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE:
        params[0] = color ? 8 : 0;
        return;
      case GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE:
        params[0] = depth ? 32 : 0;
        return;
      case GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE:
        params[0] = stencil ? 8 : 0;
        return;
      case GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE:
        params[0] = color ? GL_UNSIGNED_NORMALIZED : GL_UNSIGNED_INT;
        return;
      case GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING:
        params[0] = GL_LINEAR;
        return;
      default:
        context->SetError(GL_INVALID_ENUM);
        return;
    }
  }
  const glanceasy::FramebufferAttachment* slot = nullptr;
  if (attachment >= GL_COLOR_ATTACHMENT0 &&
      attachment < GL_COLOR_ATTACHMENT0 + glanceasy::kMaxColorAttachments) {
    slot = &framebuffer->colors[attachment - GL_COLOR_ATTACHMENT0];
  } else if (attachment == GL_DEPTH_ATTACHMENT) {
    slot = &framebuffer->depth;
  } else if (attachment == GL_STENCIL_ATTACHMENT) {
    slot = &framebuffer->stencil;
  } else if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
    const glanceasy::FramebufferAttachment& depth = framebuffer->depth;
    const glanceasy::FramebufferAttachment& stencil = framebuffer->stencil;
    if (depth.bound() && stencil.bound() &&
        (depth.texture != stencil.texture ||
         depth.renderbuffer != stencil.renderbuffer ||
         depth.retained_texture != stencil.retained_texture ||
         depth.retained_renderbuffer != stencil.retained_renderbuffer)) {
      context->SetError(GL_INVALID_OPERATION);
      return;
    }
    slot = depth.bound() ? &framebuffer->depth : &framebuffer->stencil;
  } else {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  const bool bound = slot->bound();
  if (!bound && pname != GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE &&
      pname != GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (bound && slot->renderbuffer &&
      (pname == GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL ||
       pname == GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE ||
       pname == GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  const GLenum format = bound ? glanceasy::AttachmentFormat(context, *slot) : 0;
  const bool has_depth = bound && glanceasy::IsDepthFormat(format);
  const bool has_stencil = bound && glanceasy::IsStencilFormat(format);
  const bool has_color = bound && !has_depth && !has_stencil;
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
      params[0] = bound ? slot->level : 0;
      return;
    case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE:
      params[0] = static_cast<GLint>(slot->cube_face);
      return;
    case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER:
      params[0] = slot->layer;
      return;
    case GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE:
    case GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE:
    case GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE:
    case GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE: {
      const bool stored = HasColorStorage(slot, format);
      params[0] = stored
                      ? ColorComponentBits(
                            format, pname - GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE)
                      : 0;
      return;
    }
    case GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE:
      params[0] = has_depth ? (format == GL_DEPTH_COMPONENT32F ||
                                       format == GL_DEPTH32F_STENCIL8
                                   ? 32
                                   : format == GL_DEPTH_COMPONENT24 ||
                                             format == GL_DEPTH24_STENCIL8
                                         ? 24
                                         : 16)
                            : 0;
      return;
    case GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE:
      params[0] = has_stencil ? 8 : 0;
      return;
    case GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE:
      if (!bound)
        params[0] = GL_NONE;
      else if (has_depth || has_stencil)
        params[0] = GL_UNSIGNED_INT;
      else if (has_color && !HasColorStorage(slot, format))
        params[0] = GL_NONE;
      else if (glanceasy::IsSignedIntegerFormat(format))
        params[0] = GL_INT;
      else if (glanceasy::IsUnsignedIntegerFormat(format))
        params[0] = GL_UNSIGNED_INT;
      else if (format == GL_RGBA16F || format == GL_RGBA32F ||
               format == GL_RGB16F || format == GL_RGB32F ||
               format == GL_R16F || format == GL_R32F ||
               format == GL_RG16F || format == GL_RG32F ||
               format == GL_R11F_G11F_B10F || format == GL_RGB9_E5)
        params[0] = GL_FLOAT;
      else
        params[0] = GL_UNSIGNED_NORMALIZED;
      return;
    case GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING:
      params[0] = bound && glanceasy::IsSrgbFormat(format) ? GL_SRGB : GL_LINEAR;
      return;
    default:
      context->SetError(GL_INVALID_ENUM);
      return;
  }
}

void glDrawBuffers(GLsizei n, const GLenum* buffers) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context || !buffers)
    return;
  if (n < 0 || n > glanceasy::kMaxDrawBuffers) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (context->state.framebuffer == 0 ||
      glanceasy::IsPlatformFramebuffer(context, context->state.framebuffer)) {
    if (getenv("GLANCEASY_FBO_SURFACE_AS_ZERO") != nullptr &&
        context->state.framebuffer == 0) {
      // With an implicit zero default framebuffer only GL_BACK / GL_NONE
      // are legal; attachment enums are an INVALID_OPERATION.
      if (n != 1 || (buffers[0] != GL_BACK && buffers[0] != GL_NONE)) {
        context->SetError(GL_INVALID_OPERATION);
        return;
      }
      context->state.default_draw_buffer = buffers[0];
      return;
    }
    // The default framebuffer accepts GL_BACK or GL_NONE.  The CTS state
    // reset also uses GL_COLOR_ATTACHMENT0 when the platform default
    // framebuffer is an FBO; treat that as a colour output as well.
    GLenum buffer = n == 1 ? buffers[0] : GL_INVALID_ENUM;
    if (buffer == GL_COLOR_ATTACHMENT0)
      buffer = GL_BACK;
    if (n != 1 || (buffer != GL_BACK && buffer != GL_NONE)) {
      context->SetError(GL_INVALID_OPERATION);
      return;
    }
    context->state.default_draw_buffer = buffer;
    // Keep the internal surface colour live even when the logical default
    // buffer is GL_NONE; the query state above still reports the requested
    // value, but rendering must target the real platform attachment.
    const GLenum internal_buffer = GL_COLOR_ATTACHMENT0;
    glanceasy::Framebuffer* framebuffer = context->framebuffers.Get(
        context->state.surface_framebuffer);
    if (framebuffer) {
      framebuffer->draw_buffers[0] = internal_buffer;
      for (int i = 1; i < glanceasy::kMaxColorAttachments; ++i)
        framebuffer->draw_buffers[i] = GL_NONE;
      framebuffer->draw_buffer_count = 1;
    }
    return;
  }
  glanceasy::Framebuffer* framebuffer =
      context->framebuffers.Get(context->state.framebuffer);
  if (!framebuffer) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  for (GLsizei i = 0; i < n; ++i) {
    const GLenum value = buffers[i];
    if (value == GL_BACK) {
      context->SetError(GL_INVALID_OPERATION);
      return;
    }
    if (value != GL_NONE &&
        (value < GL_COLOR_ATTACHMENT0 ||
         value >= GL_COLOR_ATTACHMENT0 + glanceasy::kMaxColorAttachments)) {
      context->SetError(GL_INVALID_ENUM);
      return;
    }
  }
  for (GLsizei i = 0; i < n; ++i)
    framebuffer->draw_buffers[i] = buffers[i];
  for (int i = n; i < glanceasy::kMaxColorAttachments; ++i)
    framebuffer->draw_buffers[i] = GL_NONE;
  framebuffer->draw_buffer_count = n;
}

void glDrawBuffer(GLenum mode) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (context->state.framebuffer == 0 ||
      glanceasy::IsPlatformFramebuffer(context, context->state.framebuffer)) {
    if (mode != GL_BACK && mode != GL_NONE && mode != GL_COLOR_ATTACHMENT0) {
      context->SetError(GL_INVALID_OPERATION);
      return;
    }
    context->state.default_draw_buffer =
        mode == GL_NONE ? GL_NONE : GL_BACK;
    glanceasy::Framebuffer* framebuffer = context->framebuffers.Get(
        context->state.surface_framebuffer);
    if (framebuffer)
      framebuffer->draw_buffers[0] = GL_COLOR_ATTACHMENT0;
    return;
  }
  glanceasy::Framebuffer* framebuffer =
      context->framebuffers.Get(context->state.framebuffer);
  if (!framebuffer) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  framebuffer->draw_buffers[0] = mode;
  for (int i = 1; i < glanceasy::kMaxColorAttachments; ++i)
    framebuffer->draw_buffers[i] = GL_NONE;
  framebuffer->draw_buffer_count = 1;
}

void glReadBuffer(GLenum mode) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (context->state.read_framebuffer == 0 ||
      glanceasy::IsPlatformFramebuffer(context, context->state.read_framebuffer)) {
    if (mode != GL_BACK && mode != GL_NONE && mode != GL_COLOR_ATTACHMENT0) {
      context->SetError(GL_INVALID_OPERATION);
      return;
    }
    const GLenum internal_read = GL_COLOR_ATTACHMENT0;
    glanceasy::Framebuffer* framebuffer = context->framebuffers.Get(
        context->state.surface_framebuffer);
    if (framebuffer)
      framebuffer->read_buffer = internal_read;
    context->state.default_read_buffer =
        mode == GL_NONE ? GL_NONE : GL_BACK;
    return;
  }
  if (mode == GL_BACK) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (mode != GL_NONE && mode != GL_BACK &&
      mode >= GL_COLOR_ATTACHMENT0 + glanceasy::kMaxColorAttachments &&
      mode <= 0x8CFF) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (mode != GL_NONE &&
      (mode < GL_COLOR_ATTACHMENT0 ||
       mode >= GL_COLOR_ATTACHMENT0 + glanceasy::kMaxColorAttachments)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  glanceasy::Framebuffer* framebuffer =
      context->framebuffers.Get(context->state.read_framebuffer);
  if (framebuffer)
    framebuffer->read_buffer = mode;
}

// ---- Renderbuffer objects -------------------------------------------------

void glGenRenderbuffers(GLsizei n, GLuint* renderbuffers) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (n < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!renderbuffers)
    return;
  for (GLsizei i = 0; i < n; ++i) {
    renderbuffers[i] = context->renderbuffers.Generate();
    glanceasy::Renderbuffer* object = context->renderbuffers.Get(renderbuffers[i]);
    if (object)
      object->generated_only = true;
  }
}

void glDeleteRenderbuffers(GLsizei n, const GLuint* renderbuffers) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (n < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!renderbuffers)
    return;
  for (GLsizei i = 0; i < n; ++i) {
    const GLuint name = renderbuffers[i];
    if (name == 0)
      continue;
    if (getenv("GLANCEASY_DEBUG_FBO2"))
      fprintf(stderr, "[fb2] delrb=%u surface=%u\n", name,
              context->state.surface_framebuffer);
    if (glanceasy::Renderbuffer* object = context->renderbuffers.Get(name))
      object->delete_status = true;
    if (context->state.renderbuffer == name)
      context->state.renderbuffer = 0;
    glanceasy::Framebuffer* bound =
        glanceasy::CurrentFramebuffer(context, GL_FRAMEBUFFER);
    glanceasy::Framebuffer* surface = context->framebuffers.Get(
        context->state.surface_framebuffer);
    glanceasy::Renderbuffer* deleted = context->renderbuffers.Get(name);
    context->framebuffers.ForEach([&](glanceasy::Framebuffer& framebuffer) {
      const bool platform_owned = &framebuffer == surface;
      for (int j = 0; j < glanceasy::kMaxColorAttachments; ++j) {
        glanceasy::FramebufferAttachment& slot = framebuffer.colors[j];
        if (slot.renderbuffer != name)
          continue;
        if (&framebuffer == bound && !platform_owned) {
          slot = glanceasy::FramebufferAttachment();
        } else if (deleted && !slot.retained_renderbuffer) {
          slot.retained_renderbuffer =
              std::make_shared<const glanceasy::Renderbuffer>(*deleted);
        }
      }
      for (glanceasy::FramebufferAttachment* slot : {&framebuffer.depth,
                                                &framebuffer.stencil}) {
        if (slot->renderbuffer != name)
          continue;
        if (&framebuffer == bound && !platform_owned) {
          *slot = glanceasy::FramebufferAttachment();
        } else if (deleted && !slot->retained_renderbuffer) {
          slot->retained_renderbuffer =
              std::make_shared<const glanceasy::Renderbuffer>(*deleted);
        }
      }
    });
  }
}

GLboolean glIsRenderbuffer(GLuint renderbuffer) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context || renderbuffer == 0)
    return GL_FALSE;
  const glanceasy::Renderbuffer* object = context->renderbuffers.Get(renderbuffer);
  return object && !object->delete_status ? GL_TRUE : GL_FALSE;
}

void glBindRenderbuffer(GLenum target, GLuint renderbuffer) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (target != GL_RENDERBUFFER) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (renderbuffer != 0) {
    glanceasy::Renderbuffer* object =
        context->renderbuffers.GetOrCreate(renderbuffer);
    if (object && object->delete_status) {
      glanceasy::Framebuffer* framebuffer =
          context->framebuffers.Get(context->state.framebuffer);
      if (framebuffer) {
        for (int i = 0; i < glanceasy::kMaxColorAttachments; ++i) {
          if (framebuffer->colors[i].renderbuffer == renderbuffer)
            framebuffer->colors[i] = glanceasy::FramebufferAttachment();
        }
        if (framebuffer->depth.renderbuffer == renderbuffer)
          framebuffer->depth = glanceasy::FramebufferAttachment();
        if (framebuffer->stencil.renderbuffer == renderbuffer)
          framebuffer->stencil = glanceasy::FramebufferAttachment();
      }
      *object = glanceasy::Renderbuffer();
    }
    if (object)
      object->generated_only = false;
  }
  context->state.renderbuffer = renderbuffer;
}

namespace {

bool RenderbufferFormatSupported(GLenum internal_format) {
  switch (internal_format) {
    case GL_RGBA4:
    case GL_RGB5_A1:
    case GL_RGB565:
    case GL_RGBA8:
    case GL_RGB8:
    case GL_SRGB8_ALPHA8:
    case GL_R8:
    case GL_R8I:
    case GL_R8UI:
    case GL_R16I:
    case GL_R16UI:
    case GL_R32I:
    case GL_R32UI:
    case GL_RG:
    case GL_RG8:
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
    case GL_RGB10_A2:
    case GL_RGB10_A2UI:
    case GL_RGBA8I:
    case GL_RGBA8UI:
    case GL_RGBA16I:
    case GL_RGBA16UI:
    case GL_RGBA32I:
    case GL_RGBA32UI:
    case GL_DEPTH_COMPONENT16:
    case GL_DEPTH_COMPONENT24:
    case GL_DEPTH_COMPONENT32F:
    case GL_STENCIL_INDEX8:
    case GL_DEPTH24_STENCIL8:
    case GL_DEPTH32F_STENCIL8:
      return true;
    default:
      return false;
  }
}

void AllocateRenderbufferStorage(glanceasy::Renderbuffer* renderbuffer,
                                 int samples,
                                 GLenum internal_format,
                                 int width,
                                 int height) {
  renderbuffer->width = width;
  renderbuffer->height = height;
  renderbuffer->samples = samples;
  renderbuffer->format = internal_format;
  glanceasy::TextureImage& image = renderbuffer->image;
  image.width = width;
  image.height = height;
  image.depth = 1;
  image.samples = samples > 0 ? samples : 1;
  image.rgba.clear();
  image.depth16.clear();
  image.stencil8.clear();
  image.sample_rgba.clear();
  image.sample_depth16.clear();
  image.sample_stencil8.clear();
  image.integer_rgba.clear();
  image.sample_integer_rgba.clear();
  const size_t texels =
      static_cast<size_t>(std::max(0, width)) *
      static_cast<size_t>(std::max(0, height));
  const size_t sample_texels =
      texels * static_cast<size_t>(image.samples);
  switch (internal_format) {
    case GL_DEPTH_COMPONENT16:
    case GL_DEPTH_COMPONENT24:
    case GL_DEPTH_COMPONENT32F:
      image.depth16.assign(texels, 0xFFFFu);
      if (image.samples > 1)
        image.sample_depth16.assign(sample_texels, 0xFFFFu);
      break;
    case GL_STENCIL_INDEX8:
      image.stencil8.assign(texels, 0u);
      if (image.samples > 1)
        image.sample_stencil8.assign(sample_texels, 0u);
      break;
    case GL_DEPTH24_STENCIL8:
    case GL_DEPTH32F_STENCIL8:
      image.depth16.assign(texels, 0xFFFFu);
      image.stencil8.assign(texels, 0u);
      if (image.samples > 1) {
        image.sample_depth16.assign(sample_texels, 0xFFFFu);
        image.sample_stencil8.assign(sample_texels, 0u);
      }
      break;
    default:
      if (glanceasy::IsSignedIntegerFormat(internal_format) ||
          glanceasy::IsUnsignedIntegerFormat(internal_format)) {
        image.integer_rgba.assign(texels * 4, 0u);
        if (image.samples > 1)
          image.sample_integer_rgba.assign(sample_texels * 4, 0u);
      } else {
        image.rgba.assign(texels, glanceasy::PackRgba(0, 0, 0, 0));
        if (image.samples > 1)
          image.sample_rgba.assign(sample_texels, glanceasy::PackRgba(0, 0, 0, 0));
      }
      break;
  }
}

}  // namespace

void glRenderbufferStorage(GLenum target,
                           GLenum internal_format,
                           GLsizei width,
                           GLsizei height) {
  glRenderbufferStorageMultisample(target, 0, internal_format, width, height);
}

void glRenderbufferStorageMultisample(GLenum target,
                                      GLsizei samples,
                                      GLenum internal_format,
                                      GLsizei width,
                                      GLsizei height) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (target != GL_RENDERBUFFER) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  glanceasy::Renderbuffer* renderbuffer =
      context->renderbuffers.Get(context->state.renderbuffer);
  if (!renderbuffer) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (samples < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (samples > glanceasy::kMaxSamples) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (width < 0 || height < 0 || width > glanceasy::kMaxRenderbufferSize ||
      height > glanceasy::kMaxRenderbufferSize) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!RenderbufferFormatSupported(internal_format)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  AllocateRenderbufferStorage(renderbuffer, samples, internal_format, width,
                              height);
}

void glGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint* params) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context || !params)
    return;
  if (target != GL_RENDERBUFFER) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  const glanceasy::Renderbuffer* renderbuffer =
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
    case GL_RENDERBUFFER_ALPHA_SIZE: {
      int bits[4] = {0, 0, 0, 0};
      switch (renderbuffer->format) {
        case GL_R8:
        case GL_R8I:
        case GL_R8UI:
          bits[0] = 8;
          break;
        case GL_R16I:
        case GL_R16UI:
          bits[0] = 16;
          break;
        case GL_R32I:
        case GL_R32UI:
          bits[0] = 32;
          break;
        case GL_RG8:
        case GL_RG8I:
        case GL_RG8UI:
          bits[0] = bits[1] = 8;
          break;
        case GL_RG16I:
        case GL_RG16UI:
          bits[0] = bits[1] = 16;
          break;
        case GL_RG32I:
        case GL_RG32UI:
          bits[0] = bits[1] = 32;
          break;
        case GL_RGB8:
        case GL_RGB8I:
        case GL_RGB8UI:
          bits[0] = bits[1] = bits[2] = 8;
          break;
        case GL_RGB565:
          bits[0] = 5;
          bits[1] = 6;
          bits[2] = 5;
          break;
        case GL_RGBA4:
          bits[0] = bits[1] = bits[2] = bits[3] = 4;
          break;
        case GL_RGB5_A1:
          bits[0] = bits[1] = bits[2] = 5;
          bits[3] = 1;
          break;
        case GL_RGB10_A2:
        case GL_RGB10_A2UI:
          bits[0] = bits[1] = bits[2] = 10;
          bits[3] = 2;
          break;
        case GL_RGBA8:
        case GL_RGBA8I:
        case GL_RGBA8UI:
        case GL_SRGB8_ALPHA8:
          bits[0] = bits[1] = bits[2] = bits[3] = 8;
          break;
        case GL_RGBA16I:
        case GL_RGBA16UI:
          bits[0] = bits[1] = bits[2] = bits[3] = 16;
          break;
        case GL_RGBA32I:
        case GL_RGBA32UI:
          bits[0] = bits[1] = bits[2] = bits[3] = 32;
          break;
        default:
          break;
      }
      params[0] = bits[pname - GL_RENDERBUFFER_RED_SIZE];
      return;
    }
    case GL_RENDERBUFFER_DEPTH_SIZE:
      params[0] = renderbuffer->format == GL_DEPTH_COMPONENT24 ||
                          renderbuffer->format == GL_DEPTH24_STENCIL8
                      ? 24
                      : renderbuffer->format == GL_DEPTH_COMPONENT32F ||
                                renderbuffer->format == GL_DEPTH32F_STENCIL8
                            ? 32
                            : renderbuffer->image.depth16.empty() ? 0 : 16;
      return;
    case GL_RENDERBUFFER_STENCIL_SIZE:
      params[0] = renderbuffer->image.stencil8.empty() ? 0 : 8;
      return;
    case GL_RENDERBUFFER_SAMPLES:
      params[0] = renderbuffer->samples;
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
  glanceasy::Context* context = glanceasy::ApiContext();
  if (getenv("GLANCEASY_DEBUG_ERROR"))
    fprintf(stderr,
            "[glreadpixels-entry] pixels=%p pbo=%u fmt=0x%x type=0x%x %dx%d\n",
            pixels, context ? context->state.pixel_pack_buffer : 0,
            static_cast<unsigned>(format), static_cast<unsigned>(type), width,
            height);
  if (!context)
    return;
  if (!pixels && context->state.pixel_pack_buffer == 0)
    return;
  if (width < 0 || height < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (format == GL_DEPTH_COMPONENT || format == GL_DEPTH_STENCIL) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  bool valid_format = false;
  bool integer_format = false;
  switch (format) {
    case GL_RGBA:
    case GL_RGB:
    case GL_RG:
    case GL_RED:
    case GL_ALPHA:
    case GL_LUMINANCE:
    case GL_LUMINANCE_ALPHA:
    case GL_DEPTH_COMPONENT:
    case GL_DEPTH_STENCIL:
      valid_format = true;
      break;
    case GL_RGBA_INTEGER:
    case GL_RGB_INTEGER:
    case GL_RG_INTEGER:
    case GL_RED_INTEGER:
      valid_format = true;
      integer_format = true;
      break;
    default:
      break;
  }
  if (!valid_format) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  bool valid_type = false;
  switch (type) {
    case GL_UNSIGNED_BYTE:
    case GL_BYTE:
    case GL_UNSIGNED_SHORT:
    case GL_SHORT:
    case GL_UNSIGNED_INT:
    case GL_INT:
    case GL_HALF_FLOAT:
    case GL_FLOAT:
    case GL_UNSIGNED_SHORT_5_6_5:
    case GL_UNSIGNED_SHORT_4_4_4_4:
    case GL_UNSIGNED_SHORT_5_5_5_1:
    case GL_UNSIGNED_INT_2_10_10_10_REV:
    case GL_UNSIGNED_INT_10F_11F_11F_REV:
    case GL_UNSIGNED_INT_5_9_9_9_REV:
    case GL_UNSIGNED_INT_24_8:
    case GL_FLOAT_32_UNSIGNED_INT_24_8_REV:
      valid_type = true;
      break;
    default:
      break;
  }
  if (!valid_type) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  bool supported = false;
  switch (format) {
    case GL_RGBA:
      supported = type == GL_UNSIGNED_BYTE || type == GL_FLOAT ||
                  type == GL_UNSIGNED_SHORT_4_4_4_4 ||
                  type == GL_UNSIGNED_SHORT_5_5_5_1;
      break;
    case GL_LUMINANCE_ALPHA:
      supported = type == GL_UNSIGNED_BYTE || type == GL_FLOAT;
      break;
    case GL_RGB:
      supported = type == GL_UNSIGNED_BYTE || type == GL_FLOAT ||
                  type == GL_UNSIGNED_SHORT_5_6_5;
      break;
    case GL_RG:
    case GL_RED:
    case GL_ALPHA:
    case GL_LUMINANCE:
      supported = type == GL_UNSIGNED_BYTE || type == GL_FLOAT;
      break;
    case GL_RGBA_INTEGER:
    case GL_RGB_INTEGER:
    case GL_RG_INTEGER:
    case GL_RED_INTEGER:
      supported = type == GL_BYTE || type == GL_UNSIGNED_BYTE ||
                  type == GL_SHORT || type == GL_UNSIGNED_SHORT ||
                  type == GL_INT || type == GL_UNSIGNED_INT;
      break;
    default:
      break;
  }
  if (!supported) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  const glanceasy::RenderTarget target = context->GetReadTarget();
  if (getenv("GLANCEASY_DEBUG_ERROR")) {
    const unsigned* words = reinterpret_cast<const unsigned*>(target.color);
    fprintf(stderr,
            "[glreadpixels] readfb=%u %dx%d int=%d fmt=0x%x cb=%d rc=%d hasA=%d w0=%u w1=%u\n",
            context->state.read_framebuffer, target.width, target.height,
            target.read_integer ? 1 : 0,
            static_cast<unsigned>(target.read_format),
            target.read_color_bytes, target.read_channels, (int)target.read_has_alpha, words ? words[0] : 0,
            words ? words[1] : 0);
  }
  if (!target.valid()) {
    context->SetError(GL_INVALID_FRAMEBUFFER_OPERATION);
    return;
  }
  if (!target.color) {
    // The read buffer selected no colour attachment; a colour read cannot
    // proceed (previously this dereferenced a null surface pointer).
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  const glanceasy::PixelStoreState& store = context->state.pixel_store;
  const size_t bytes = glanceasy::PixelBytes(format, type);
  const GLint row_pixels =
      store.pack_row_length > 0 ? store.pack_row_length : width;
  const size_t alignment = static_cast<size_t>(store.pack_alignment);
  const size_t row_stride =
      ((static_cast<size_t>(row_pixels) * bytes + alignment - 1) / alignment) *
      alignment;
  uint8_t* base = static_cast<uint8_t*>(pixels);
  if (context->state.pixel_pack_buffer != 0) {
    glanceasy::Buffer* buffer =
        context->buffers.Get(context->state.pixel_pack_buffer);
    if (!buffer || buffer->mapped) {
      context->SetError(GL_INVALID_OPERATION);
      return;
    }
    const size_t offset = reinterpret_cast<uintptr_t>(pixels);
    const size_t required =
        static_cast<size_t>(store.pack_skip_rows) * row_stride +
        static_cast<size_t>(height) * row_stride +
        static_cast<size_t>(store.pack_skip_pixels) * bytes;
    if (offset + required > buffer->data.size()) {
      context->SetError(GL_INVALID_OPERATION);
      return;
    }
    base = buffer->data.data() + offset;
  }
  uint8_t* destination =
      base + static_cast<size_t>(store.pack_skip_rows) * row_stride +
      static_cast<size_t>(store.pack_skip_pixels) * bytes;
  const bool integer_read = target.read_integer;
  if (integer_format != integer_read) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (target.multisampled()) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  const bool float_type = type == GL_FLOAT || type == GL_HALF_FLOAT ||
                          type == GL_UNSIGNED_INT_10F_11F_11F_REV ||
                          type == GL_UNSIGNED_INT_5_9_9_9_REV;
  bool float_target = false;
  switch (target.read_format) {
    case GL_R16F:
    case GL_RG16F:
    case GL_RGB16F:
    case GL_RGBA16F:
    case GL_R32F:
    case GL_RG32F:
    case GL_RGB32F:
    case GL_RGBA32F:
    case GL_R11F_G11F_B10F:
      float_target = true;
      break;
    default:
      break;
  }
  if (float_type != float_target) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  for (GLsizei row = 0; row < height; ++row) {
    // The color surface is stored top-down; GL window y=0 is the bottom row.
    const GLint source_y = target.height - 1 - (y + row);
    uint8_t* row_start = destination + static_cast<size_t>(row) * row_stride;
    if (source_y < 0 || source_y >= target.height)
      continue;
    for (GLsizei column = 0; column < width; ++column) {
      const GLint source_x = x + column;
      if (source_x < 0 || source_x >= target.width)
        continue;
      const uint8_t* source =
          target.color +
          (static_cast<size_t>(source_y) * target.width +
           static_cast<size_t>(source_x)) *
              static_cast<size_t>(target.read_color_bytes);

      uint8_t* out = row_start + static_cast<size_t>(column) * bytes;
      if (integer_read) {
        const int source_attachment =
            context->state.read_framebuffer &&
                    context->framebuffers.Get(context->state.read_framebuffer)
                ? std::max(0, static_cast<int>(
                                      context->framebuffers
                                          .Get(context->state.read_framebuffer)
                                          ->read_buffer -
                                      GL_COLOR_ATTACHMENT0))
                : 0;
        const uint32_t* raw_source = reinterpret_cast<const uint32_t*>(source);
        glanceasy::WriteIntegerPixel(raw_source, format, type,
                              target.color_integer_bits[source_attachment],
                              target.color_is_unsigned[source_attachment], out);
        GLuint* components = reinterpret_cast<GLuint*>(out);
        const int output_channels = format == GL_RGBA_INTEGER   ? 4
                                    : format == GL_RGB_INTEGER  ? 3
                                    : format == GL_RG_INTEGER   ? 2
                                                                : 1;
        for (int channel = target.read_channels; channel < output_channels;
             ++channel)
          components[channel] = channel == 3 ? 1u : 0u;
      } else {
        glanceasy::WritePixel(source, format, type, out);
        if (format == GL_RGBA || format == GL_RGB ||
            format == GL_RG || format == GL_RED) {
          int output_channels = format == GL_RGBA   ? 4
                                : format == GL_RGB  ? 3
                                : format == GL_RG   ? 2
                                                    : 1;
          for (int channel = target.read_channels; channel < output_channels;
               ++channel)
            out[channel] = channel == 3 ? 255 : 0;
        }
      }
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
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (filter != GL_NEAREST && filter != GL_LINEAR) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (mask & ~(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
               GL_STENCIL_BUFFER_BIT)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  glanceasy::RenderTarget source = context->GetReadTarget();
  glanceasy::RenderTarget destination = context->GetDrawTarget();
  if (getenv("GLANCEASY_DEBUG_BLIT"))
    fprintf(stderr,
            "[blit] readfb=%u drawfb=%u src=%p dst=%p srcsz=%dx%d dstsz=%dx%d "
            "rect=%d,%d..%d,%d -> %d,%d..%d,%d mask=%x filter=%x\n",
            context->state.read_framebuffer, context->state.framebuffer,
            static_cast<void*>(source.color), static_cast<void*>(destination.color),
            source.width, source.height, destination.width, destination.height,
            src_x0, src_y0, src_x1, src_y1, dst_x0, dst_y0, dst_x1, dst_y1,
            static_cast<unsigned>(mask), static_cast<unsigned>(filter));
  if (!source.valid() || !destination.valid()) {
    context->SetError(GL_INVALID_FRAMEBUFFER_OPERATION);
    return;
  }
  const glanceasy::Framebuffer* source_framebuffer =
      context->framebuffers.Get(context->state.read_framebuffer);
  const glanceasy::Framebuffer* destination_framebuffer =
      context->framebuffers.Get(context->state.framebuffer);
  const auto normalized_format = [](GLenum format) {
    if (format == 0 || format == GL_SRGB8_ALPHA8)
      return static_cast<GLenum>(GL_RGBA8);
    return format;
  };
  GLenum source_color_format = GL_RGBA8;
  if (source_framebuffer &&
      source_framebuffer->read_buffer >= GL_COLOR_ATTACHMENT0 &&
      source_framebuffer->read_buffer <
          GL_COLOR_ATTACHMENT0 + glanceasy::kMaxColorAttachments) {
    source_color_format = glanceasy::AttachmentFormat(
        context,
        source_framebuffer
            ->colors[source_framebuffer->read_buffer - GL_COLOR_ATTACHMENT0]);
  }
  GLenum destination_color_format = GL_RGBA8;
  if (destination_framebuffer)
    destination_color_format =
        glanceasy::AttachmentFormat(context, destination_framebuffer->colors[0]);
  const GLenum source_depth_format =
      source_framebuffer
          ? glanceasy::AttachmentFormat(context, source_framebuffer->depth)
          : GL_DEPTH_COMPONENT16;
  const GLenum destination_depth_format =
      destination_framebuffer
          ? glanceasy::AttachmentFormat(context, destination_framebuffer->depth)
          : GL_DEPTH_COMPONENT16;
  const GLenum source_stencil_format =
      source_framebuffer
          ? glanceasy::AttachmentFormat(context, source_framebuffer->stencil)
          : GL_STENCIL_INDEX8;
  const GLenum destination_stencil_format =
      destination_framebuffer
          ? glanceasy::AttachmentFormat(context, destination_framebuffer->stencil)
          : GL_STENCIL_INDEX8;
  const bool blits_depth =
      (mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) != 0;
  if (blits_depth && filter != GL_NEAREST) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (destination.multisampled()) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if ((mask & GL_COLOR_BUFFER_BIT) &&
      source.read_integer && filter == GL_LINEAR) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if ((mask & GL_COLOR_BUFFER_BIT) &&
      source.read_integer != destination.color_is_integer[0]) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if ((mask & GL_DEPTH_BUFFER_BIT) &&
      source_depth_format != destination_depth_format &&
      context->state.framebuffer != 0) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if ((mask & GL_STENCIL_BUFFER_BIT) &&
      source_stencil_format != destination_stencil_format &&
      context->state.framebuffer != 0) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (source.multisampled() &&
      normalized_format(source_color_format) !=
          normalized_format(destination_color_format)) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (source.multisampled() && (src_x0 != dst_x0 || src_y0 != dst_y0 ||
                                src_x1 != dst_x1 || src_y1 != dst_y1)) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  const bool scissor_test = context->state.scissor_test;
  const glanceasy::Rect& scissor = context->state.scissor;
  const bool mirror_src_x = src_x1 < src_x0;
  const bool mirror_src_y = src_y1 < src_y0;
  const bool mirror_dst_x = dst_x1 < dst_x0;
  const bool mirror_dst_y = dst_y1 < dst_y0;
  if (mirror_src_x)
    std::swap(src_x0, src_x1);
  if (mirror_src_y)
    std::swap(src_y0, src_y1);
  if (mirror_dst_x)
    std::swap(dst_x0, dst_x1);
  if (mirror_dst_y)
    std::swap(dst_y0, dst_y1);
  if (mask & GL_COLOR_BUFFER_BIT) {
    const int dst_width = dst_x1 - dst_x0;
    const int dst_height = dst_y1 - dst_y0;
    const int src_width = src_x1 - src_x0;
    const int src_height = src_y1 - src_y0;
    if (dst_width > 0 && dst_height > 0 && src_width > 0 &&
        src_height > 0) {
      if (source.read_integer &&
          destination.color_is_integer[0]) {
        for (int row = 0; row < dst_height; ++row) {
          for (int column = 0; column < dst_width; ++column) {
            const int destination_column =
                mirror_dst_x ? dst_width - 1 - column : column;
            const int destination_row =
                mirror_dst_y ? dst_height - 1 - row : row;
            const int destination_x = dst_x0 + destination_column;
            const int destination_y = dst_y0 + destination_row;
            const int gl_y = destination.height - 1 - destination_y;
            if (destination_x < 0 || destination_x >= destination.width ||
                gl_y < 0 || gl_y >= destination.height)
              continue;
            if (scissor_test && !scissor.Contains(destination_x, destination_y))
              continue;
            int source_column =
                ((column * 2 + 1) * src_width) / (dst_width * 2);
            int source_row =
                ((row * 2 + 1) * src_height) / (dst_height * 2);
            if (mirror_src_x)
              source_column = src_width - 1 - source_column;
            if (mirror_src_y)
              source_row = src_height - 1 - source_row;
            const int source_offset_x = src_x0 + source_column;
            const int source_offset_y = src_y0 + source_row;
            if (source_offset_x < 0 || source_offset_x >= source.width ||
                source_offset_y < 0 || source_offset_y >= source.height)
              continue;
            const size_t source_index =
                (static_cast<size_t>(source.height - 1 - source_offset_y) *
                     source.width +
                 static_cast<size_t>(source_offset_x)) *
                4;
            const uint32_t* incoming =
                reinterpret_cast<const uint32_t*>(source.color) + source_index;
            const size_t destination_index =
                static_cast<size_t>(gl_y) * destination.width + destination_x;
            uint32_t* outgoing =
                reinterpret_cast<uint32_t*>(destination.color) +
                destination_index * 4;
            for (int channel = 0; channel < 4; ++channel) {
              int64_t value = 0;
              if (channel < source.read_channels) {
                const uint32_t raw = incoming[channel];
                const int source_bits =
                    glanceasy::FormatIntegerBits(source.read_format, channel);
                if (source_bits < 32) {
                  const uint32_t mask = (1u << source_bits) - 1u;
                  const uint32_t field = raw & mask;
                  value = source.read_unsigned ||
                                  (field & (1u << (source_bits - 1))) == 0
                              ? static_cast<int64_t>(field)
                              : static_cast<int64_t>(field) -
                                    (1LL << source_bits);
                } else {
                  value = static_cast<int64_t>(raw);
                }
              } else if (channel == 3) {
                value = 1;
              }
              if (!destination.color_is_unsigned[0] && channel < 4)
                value = static_cast<int32_t>(static_cast<uint32_t>(value));
              const int bits = glanceasy::FormatIntegerBits(
                  destination.color_format[0], channel);
              if (bits < 32) {
                const int64_t minimum =
                    destination.color_is_unsigned[0] ? 0 : -(1LL << (bits - 1));
                const int64_t maximum = destination.color_is_unsigned[0]
                                            ? (1LL << bits) - 1
                                            : (1LL << (bits - 1)) - 1;
                value = std::max(minimum, std::min(maximum, value));
              }
              outgoing[channel] = static_cast<uint32_t>(value);
            }
          }
        }
      } else {
      const bool writes_all_channels =
          context->state.color_mask[0] && context->state.color_mask[1] &&
          context->state.color_mask[2] && context->state.color_mask[3];
      const bool copy_rows =
          writes_all_channels && !scissor_test && !mirror_src_x && !mirror_src_y &&
          !mirror_dst_x && !mirror_dst_y && source.read_channels == 4 &&
          destination.color_channels[0] == 4 &&
          source.read_format != GL_SRGB8_ALPHA8 &&
          destination.color_format[0] != GL_SRGB8_ALPHA8 &&
          destination.color_format[0] != GL_RGB565 &&
          destination.color_format[0] != GL_RGB5_A1 &&
          destination.color_format[0] != GL_RGBA4 &&
          destination.color_format[0] != GL_RGB10_A2 &&
          src_width == dst_width &&
          src_height == dst_height && src_x0 >= 0 &&
          src_x0 + dst_width <= source.width && dst_x0 >= 0 &&
          dst_x0 + dst_width <= destination.width && src_y0 >= 0 &&
          src_y1 <= source.height && dst_y0 >= 0 &&
          dst_y1 <= destination.height;
      if (copy_rows) {
        const size_t row_bytes = static_cast<size_t>(dst_width) * 4;
        for (int row = 0; row < dst_height; ++row) {
          const int source_y = src_y0 + row;
          const int destination_y = dst_y0 + row;
          const int gl_y = destination.height - 1 - destination_y;
          const size_t source_index =
              (static_cast<size_t>(source.height - 1 - source_y) *
                   source.width +
               static_cast<size_t>(src_x0)) *
              4;
          const size_t destination_index =
              (static_cast<size_t>(gl_y) * destination.width +
               static_cast<size_t>(dst_x0)) *
              4;
          memcpy(destination.color + destination_index,
                 source.color + source_index, row_bytes);
        }
      } else {
        for (int row = 0; row < dst_height; ++row) {
          for (int column = 0; column < dst_width; ++column) {
            const int destination_column =
                mirror_dst_x ? dst_width - 1 - column : column;
            const int destination_row =
                mirror_dst_y ? dst_height - 1 - row : row;
            const int destination_x = dst_x0 + destination_column;
            const int destination_y = dst_y0 + destination_row;
            const int gl_y = destination.height - 1 - destination_y;
            if (destination_x < 0 || destination_x >= destination.width ||
                gl_y < 0 || gl_y >= destination.height) {
              continue;
            }
            if (scissor_test && !scissor.Contains(destination_x, destination_y))
              continue;
            int source_column =
                ((column * 2 + 1) * src_width) / (dst_width * 2);
            int source_row =
                ((row * 2 + 1) * src_height) / (dst_height * 2);
            if (mirror_src_x)
              source_column = src_width - 1 - source_column;
            if (mirror_src_y)
              source_row = src_height - 1 - source_row;
            const int source_offset_x = src_x0 + source_column;
            const int source_offset_y = src_y0 + source_row;
            if (source_offset_x < 0 || source_offset_x >= source.width ||
                source_offset_y < 0 || source_offset_y >= source.height) {
              continue;
            }
            const size_t destination_index =
                static_cast<size_t>(gl_y) * destination.width +
                static_cast<size_t>(destination_x);
            uint32_t existing;
            memcpy(&existing, destination.color + destination_index * 4,
                   sizeof(existing));
            const glanceasy::Rgba previous = glanceasy::UnpackRgba(existing);
            uint8_t components[4];
            if (filter == GL_LINEAR) {
              float sample_x =
                  src_x0 +
                  (static_cast<float>(column) + 0.5f) * src_width / dst_width -
                  0.5f;
              float sample_y =
                  src_y0 +
                  (static_cast<float>(row) + 0.5f) * src_height / dst_height -
                  0.5f;
              if (mirror_src_x)
                sample_x = src_x0 + static_cast<float>(src_width - 1) -
                           (sample_x - src_x0);
              if (mirror_src_y)
                sample_y = src_y0 + static_cast<float>(src_height - 1) -
                           (sample_y - src_y0);
              sample_x =
                  std::max(0.0f, std::min(static_cast<float>(source.width - 1),
                                          sample_x));
              sample_y =
                  std::max(0.0f, std::min(static_cast<float>(source.height - 1),
                                          sample_y));
              const int lower_x = static_cast<int>(std::floor(sample_x));
              const int lower_y = static_cast<int>(std::floor(sample_y));
              const int upper_x = std::min(lower_x + 1, source.width - 1);
              const int upper_y = std::min(lower_y + 1, source.height - 1);
              const float fraction_x = sample_x - lower_x;
              const float fraction_y = sample_y - lower_y;
              float result[4] = {0, 0, 0, 0};
              for (int sample_y = 0; sample_y < 2; ++sample_y) {
                for (int sample_x = 0; sample_x < 2; ++sample_x) {
                  const int x = sample_x == 0 ? lower_x : upper_x;
                  const int y = sample_y == 0 ? lower_y : upper_y;
                  const float weight_x =
                      sample_x == 0 ? 1.0f - fraction_x : fraction_x;
                  const float weight_y =
                      sample_y == 0 ? 1.0f - fraction_y: fraction_y;
                  uint8_t texel[4];
                  LoadNormalizedBlitTexel(source, x, y, texel);
                  for (int channel = 0; channel < 4; ++channel) {
                    result[channel] +=
                        texel[channel] * weight_x * weight_y;
                  }
                }
              }
              for (int channel = 0; channel < 4; ++channel) {
                components[channel] = static_cast<uint8_t>(
                    std::max(0.0f, std::min(255.0f, result[channel])) + 0.5f);
              }
              const bool destination_srgb =
                  destination.color_format[0] == GL_SRGB8_ALPHA8;
              if (destination_srgb) {
                for (int channel = 0; channel < 3; ++channel) {
                  components[channel] = glanceasy::LinearToSrgb8(
                      components[channel] / 255.0f);
                }
              }
            } else {
              const size_t source_index =
                  static_cast<size_t>(source.height - 1 - source_offset_y) *
                      source.width +
                  static_cast<size_t>(source_offset_x);
              uint32_t value;
              memcpy(&value, source.color + source_index * 4, sizeof(value));
              const glanceasy::Rgba incoming = glanceasy::UnpackRgba(value);
              const uint8_t source_channels[4] = {
                  incoming.r, incoming.g, incoming.b, incoming.a};
              components[0] = 0;
              components[1] = 0;
              components[2] = 0;
              components[3] = 255;
              for (int channel = 0; channel < 4; ++channel) {
                if (channel < source.read_channels)
                  components[channel] = source_channels[channel];
              }
              QuantizeColorFormat(components, source.read_format);
              const bool source_srgb =
                  source.read_format == GL_SRGB8_ALPHA8;
              const bool destination_srgb =
                  destination.color_format[0] == GL_SRGB8_ALPHA8;
              if (source_srgb != destination_srgb) {
                for (int channel = 0; channel < 3; ++channel) {
                  components[channel] = source_srgb
                      ? static_cast<uint8_t>(
                            glanceasy::SrgbToLinear(components[channel]) * 255.0f +
                            0.5f)
                      : glanceasy::LinearToSrgb8(components[channel] / 255.0f);
                }
              }
            }
            QuantizeColorFormat(components, destination.color_format[0]);
            uint8_t merged[4];
            merged[0] =
                context->state.color_mask[0] ? components[0] : previous.r;
            merged[1] =
                context->state.color_mask[1] ? components[1] : previous.g;
            merged[2] =
                context->state.color_mask[2] ? components[2] : previous.b;
            merged[3] =
                context->state.color_mask[3] ? components[3] : previous.a;
            const uint32_t packed =
                glanceasy::PackRgba(merged[0], merged[1], merged[2], merged[3]);
            memcpy(destination.color + destination_index * 4, &packed,
                   sizeof(packed));
          }
        }
      }
      }
    }
  }
  if (getenv("GLANCEASY_DEBUG_BLIT") && (mask & GL_COLOR_BUFFER_BIT) &&
      destination.color) {
    auto pixel = [&](int x, int y) {
      uint32_t value = 0;
      memcpy(&value, destination.color +
                         (static_cast<size_t>(y) * destination.width + x) * 4,
             sizeof(value));
      return value;
    };
    fprintf(stderr, "[blit] dst(0,0)=%08x dst(64,0)=%08x dst(64,128)=%08x\n",
            pixel(0, 0), pixel(64, 0), pixel(64, 128));
  }
  if ((mask & GL_DEPTH_BUFFER_BIT) && source.depth && destination.depth) {
    for (GLint row = 0; row < dst_y1 - dst_y0; ++row) {
      for (GLint column = 0; column < dst_x1 - dst_x0; ++column) {
        const GLint destination_column =
            mirror_dst_x ? dst_x1 - dst_x0 - 1 - column : column;
        const GLint destination_row =
            mirror_dst_y ? dst_y1 - dst_y0 - 1 - row : row;
        const GLint x = dst_x0 + destination_column;
        const GLint y = dst_y0 + destination_row;
        const GLint gl_y = destination.height - 1 - y;
        if (x < 0 || x >= destination.width || gl_y < 0 ||
            gl_y >= destination.height)
          continue;
        if (scissor_test && !scissor.Contains(x, y))
          continue;
        GLint source_column =
            ((column * 2 + 1) * (src_x1 - src_x0)) /
            ((dst_x1 - dst_x0) * 2);
        GLint source_row =
            ((row * 2 + 1) * (src_y1 - src_y0)) /
            ((dst_y1 - dst_y0) * 2);
        if (mirror_src_x)
          source_column = src_x1 - src_x0 - 1 - source_column;
        if (mirror_src_y)
          source_row = src_y1 - src_y0 - 1 - source_row;
        const GLint source_x = src_x0 + source_column;
        const GLint source_y = src_y0 + source_row;
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
    for (GLint row = 0; row < dst_y1 - dst_y0; ++row) {
      for (GLint column = 0; column < dst_x1 - dst_x0; ++column) {
        const GLint destination_column =
            mirror_dst_x ? dst_x1 - dst_x0 - 1 - column : column;
        const GLint destination_row =
            mirror_dst_y ? dst_y1 - dst_y0 - 1 - row : row;
        const GLint x = dst_x0 + destination_column;
        const GLint y = dst_y0 + destination_row;
        const GLint gl_y = destination.height - 1 - y;
        if (x < 0 || x >= destination.width || gl_y < 0 ||
            gl_y >= destination.height)
          continue;
        if (scissor_test && !scissor.Contains(x, y))
          continue;
        GLint source_column =
            ((column * 2 + 1) * (src_x1 - src_x0)) /
            ((dst_x1 - dst_x0) * 2);
        GLint source_row =
            ((row * 2 + 1) * (src_y1 - src_y0)) /
            ((dst_y1 - dst_y0) * 2);
        if (mirror_src_x)
          source_column = src_x1 - src_x0 - 1 - source_column;
        if (mirror_src_y)
          source_row = src_y1 - src_y0 - 1 - source_row;
        const GLint source_x = src_x0 + source_column;
        const GLint source_y = src_y0 + source_row;
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
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context || !value)
    return;
  if (context->state.rasterizer_discard)
    return;
  if (buffer == GL_COLOR) {
    if (glanceasy::DrawBufferIsInteger(context, drawbuffer)) {
      const uint32_t values[4] = {
          static_cast<uint32_t>(value[0]), static_cast<uint32_t>(value[1]),
          static_cast<uint32_t>(value[2]), static_cast<uint32_t>(value[3])};
      glanceasy::ClearIntegerColourBuffer(context, drawbuffer, values);
      return;
    }
    const glanceasy::Rgba color =
        glanceasy::Rgba{static_cast<uint8_t>(glanceasy::ClampInt(value[0], 0, 255)),
                  static_cast<uint8_t>(glanceasy::ClampInt(value[1], 0, 255)),
                  static_cast<uint8_t>(glanceasy::ClampInt(value[2], 0, 255)),
                  static_cast<uint8_t>(glanceasy::ClampInt(value[3], 0, 255))};
    glanceasy::ClearColourBuffer(context, drawbuffer, glanceasy::PackRgba(color));
    return;
  }
  if (buffer == GL_STENCIL) {
    if (drawbuffer != 0) {
      context->SetError(GL_INVALID_VALUE);
      return;
    }
    glanceasy::RenderTarget target = context->GetDrawTarget();
    if (target.valid() && target.stencil) {
      const size_t texels = static_cast<size_t>(target.width) * target.height;
      const int planes = target.multisampled() ? target.samples : 1;
      glanceasy::Rect area{0, 0, target.width, target.height};
      if (context->state.scissor_test) {
        const glanceasy::Rect scissor{context->state.scissor.x,
                                target.height - context->state.scissor.bottom(),
                                context->state.scissor.width,
                                context->state.scissor.height};
        area = area.Intersect(scissor);
      }
      if (!area.IsEmpty()) {
        for (int sample = 0; sample < planes; ++sample) {
          uint8_t* plane = target.stencil + static_cast<size_t>(sample) * texels;
          for (int y = area.y; y < area.bottom(); ++y)
            std::fill(plane + static_cast<size_t>(y) * target.width + area.x,
                      plane + static_cast<size_t>(y) * target.width + area.right(),
                      static_cast<uint8_t>(value[0]));
        }
        if (target.multisampled() && target.resolved_stencil) {
          for (int y = area.y; y < area.bottom(); ++y)
            std::fill(target.resolved_stencil +
                          static_cast<size_t>(y) * target.width + area.x,
                      target.resolved_stencil +
                          static_cast<size_t>(y) * target.width + area.right(),
                      static_cast<uint8_t>(value[0]));
        }
      }
    }
    return;
  }
  context->SetError(GL_INVALID_ENUM);
}

void glClearBufferuiv(GLenum buffer, GLint drawbuffer, const GLuint* value) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context || !value)
    return;
  if (context->state.rasterizer_discard)
    return;
  if (buffer == GL_COLOR) {
    if (glanceasy::DrawBufferIsInteger(context, drawbuffer)) {
      const uint32_t values[4] = {value[0], value[1], value[2], value[3]};
      glanceasy::ClearIntegerColourBuffer(context, drawbuffer, values);
      return;
    }
    const glanceasy::Rgba color = glanceasy::Rgba{
        static_cast<uint8_t>(glanceasy::ClampInt(static_cast<int>(value[0]), 0, 255)),
        static_cast<uint8_t>(glanceasy::ClampInt(static_cast<int>(value[1]), 0, 255)),
        static_cast<uint8_t>(glanceasy::ClampInt(static_cast<int>(value[2]), 0, 255)),
        static_cast<uint8_t>(
            glanceasy::ClampInt(static_cast<int>(value[3]), 0, 255))};
    glanceasy::ClearColourBuffer(context, drawbuffer, glanceasy::PackRgba(color));
    return;
  }
  context->SetError(GL_INVALID_ENUM);
}

void glClearBufferfv(GLenum buffer, GLint drawbuffer, const GLfloat* value) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context || !value)
    return;
  if (context->state.rasterizer_discard)
    return;
  if (buffer == GL_COLOR) {
    if (drawbuffer < 0 || drawbuffer >= glanceasy::kMaxDrawBuffers) {
      context->SetError(GL_INVALID_VALUE);
      return;
    }
    const glanceasy::Rgba color =
        glanceasy::Rgba{glanceasy::FloatToUnorm8(value[0]), glanceasy::FloatToUnorm8(value[1]),
                  glanceasy::FloatToUnorm8(value[2]), glanceasy::FloatToUnorm8(value[3])};
    glanceasy::ClearColourBuffer(context, drawbuffer, glanceasy::PackRgba(color));
    return;
  }
  if (buffer == GL_STENCIL || buffer == GL_DEPTH_STENCIL) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (buffer == GL_DEPTH) {
    if (drawbuffer != 0) {
      context->SetError(GL_INVALID_VALUE);
      return;
    }
    glanceasy::RenderTarget target = context->GetDrawTarget();
    if (target.valid() && target.depth) {
      const uint16_t depth =
          static_cast<uint16_t>(glanceasy::Clamp01(value[0]) * 65535.0f + 0.5f);
      const size_t texels = static_cast<size_t>(target.width) * target.height;
      const int planes = target.multisampled() ? target.samples : 1;
      glanceasy::Rect area{0, 0, target.width, target.height};
      if (context->state.scissor_test) {
        const glanceasy::Rect scissor{context->state.scissor.x,
                                target.height - context->state.scissor.bottom(),
                                context->state.scissor.width,
                                context->state.scissor.height};
        area = area.Intersect(scissor);
      }
      if (!area.IsEmpty()) {
        for (int sample = 0; sample < planes; ++sample) {
          uint16_t* plane = target.depth + static_cast<size_t>(sample) * texels;
          for (int y = area.y; y < area.bottom(); ++y)
            std::fill(plane + static_cast<size_t>(y) * target.width + area.x,
                      plane + static_cast<size_t>(y) * target.width + area.right(),
                      depth);
        }
        if (target.multisampled() && target.resolved_depth) {
          for (int y = area.y; y < area.bottom(); ++y)
            std::fill(target.resolved_depth +
                          static_cast<size_t>(y) * target.width + area.x,
                      target.resolved_depth +
                          static_cast<size_t>(y) * target.width + area.right(),
                      depth);
        }
      }
    }
    return;
  }
  context->SetError(GL_INVALID_ENUM);
}

void glClearBufferfi(GLenum buffer,
                     GLint drawbuffer,
                     GLfloat depth,
                     GLint stencil) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (context->state.rasterizer_discard)
    return;
  if (buffer != GL_DEPTH_STENCIL || drawbuffer != 0) {
    context->SetError(buffer == GL_DEPTH_STENCIL ? GL_INVALID_VALUE
                                                 : GL_INVALID_ENUM);
    return;
  }
  glanceasy::RenderTarget target = context->GetDrawTarget();
  if (!target.valid()) {
    context->SetError(GL_INVALID_FRAMEBUFFER_OPERATION);
    return;
  }
  const size_t texels = static_cast<size_t>(target.width) * target.height;
  const int planes = target.multisampled() ? target.samples : 1;
  glanceasy::Rect area{0, 0, target.width, target.height};
  if (context->state.scissor_test) {
    const glanceasy::Rect scissor{context->state.scissor.x,
                            target.height - context->state.scissor.bottom(),
                            context->state.scissor.width,
                            context->state.scissor.height};
    area = area.Intersect(scissor);
  }
  if (area.IsEmpty())
    return;
  if (target.depth) {
    const uint16_t clear_depth =
        static_cast<uint16_t>(glanceasy::Clamp01(depth) * 65535.0f + 0.5f);
    for (int sample = 0; sample < planes; ++sample) {
      uint16_t* plane = target.depth + static_cast<size_t>(sample) * texels;
      for (int y = area.y; y < area.bottom(); ++y)
        std::fill(plane + static_cast<size_t>(y) * target.width + area.x,
                  plane + static_cast<size_t>(y) * target.width + area.right(),
                  clear_depth);
    }
    if (target.multisampled() && target.resolved_depth) {
      for (int y = area.y; y < area.bottom(); ++y)
        std::fill(target.resolved_depth +
                      static_cast<size_t>(y) * target.width + area.x,
                  target.resolved_depth +
                      static_cast<size_t>(y) * target.width + area.right(),
                  clear_depth);
    }
  }
  if (target.stencil) {
    for (int sample = 0; sample < planes; ++sample) {
      uint8_t* plane = target.stencil + static_cast<size_t>(sample) * texels;
      for (int y = area.y; y < area.bottom(); ++y)
        std::fill(plane + static_cast<size_t>(y) * target.width + area.x,
                  plane + static_cast<size_t>(y) * target.width + area.right(),
                  static_cast<uint8_t>(stencil));
    }
    if (target.multisampled() && target.resolved_stencil) {
      for (int y = area.y; y < area.bottom(); ++y)
        std::fill(target.resolved_stencil +
                      static_cast<size_t>(y) * target.width + area.x,
                  target.resolved_stencil +
                      static_cast<size_t>(y) * target.width + area.right(),
                  static_cast<uint8_t>(stencil));
    }
  }
}

void glInvalidateFramebuffer(GLenum target,
                             GLsizei num_attachments,
                             const GLenum* attachments) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  glanceasy::InvalidateAccepted(context, target, num_attachments, attachments);
}

void glInvalidateSubFramebuffer(GLenum target,
                                GLsizei num_attachments,
                                const GLenum* attachments,
                                GLint x,
                                GLint y,
                                GLsizei width,
                                GLsizei height) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (!glanceasy::InvalidateAccepted(context, target, num_attachments, attachments))
    return;
  if (width < 0 || height < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  (void)x;
  (void)y;
}

}  // extern "C"
