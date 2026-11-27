// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/glanceasy_context.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "glanceasy/glanceasy.h"

namespace glanceasy {

namespace {

thread_local Context* g_current_context = nullptr;
std::unique_ptr<Context> g_default_context;
int g_live_contexts = 0;

GLenum AttachmentFormat(const Context& context,
                        const FramebufferAttachment& attachment) {
  if (attachment.retained_texture)
    return attachment.retained_texture->internal_format;
  if (attachment.texture) {
    const Texture* texture = context.textures.Get(attachment.texture);
    return texture ? texture->internal_format : 0;
  }
  if (attachment.retained_renderbuffer)
    return attachment.retained_renderbuffer->format;
  if (attachment.renderbuffer) {
    const Renderbuffer* renderbuffer =
        context.renderbuffers.Get(attachment.renderbuffer);
    return renderbuffer ? renderbuffer->format : 0;
  }
  return 0;
}

bool IsIntegerFormat(GLenum format) {
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

// Formats whose RGB channels are stored without an alpha channel.  Reads
// back as RGBA must return one for alpha, not the zero in unused storage.
bool FormatHasAlpha(GLenum format) {
  switch (format) {
    case GL_RGBA:
    case GL_LUMINANCE_ALPHA:
    case GL_RGBA4:
    case GL_RGB5_A1:
    case GL_RGBA8:
    case GL_RGBA8_SNORM:
    case GL_RGBA16F:
    case GL_RGBA32F:
    case GL_RGBA8I:
    case GL_RGBA8UI:
    case GL_RGBA16I:
    case GL_RGBA16UI:
    case GL_RGBA32I:
    case GL_RGBA32UI:
    case GL_SRGB8_ALPHA8:
    case GL_RGB10_A2:
    case GL_RGB10_A2UI:
      return true;
    default:
      return false;
  }
}

}  // namespace

int FormatIntegerBits(GLenum format, int channel) {
  if (format == GL_RGB10_A2UI || format == GL_RGB10_A2)
    return channel < 3 ? 10 : 2;
  switch (format) {
    case GL_R8I:
    case GL_R8UI:
    case GL_RG8I:
    case GL_RG8UI:
    case GL_RGB8I:
    case GL_RGB8UI:
    case GL_RGBA8I:
    case GL_RGBA8UI:
      return 8;
    case GL_R16I:
    case GL_R16UI:
    case GL_RG16I:
    case GL_RG16UI:
    case GL_RGB16I:
    case GL_RGB16UI:
    case GL_RGBA16I:
    case GL_RGBA16UI:
      return 16;
    default:
      return 32;
  }
}

int FormatColorChannels(GLenum format) {
  switch (format) {
    case GL_RGBA:
    case GL_RGBA4:
    case GL_RGB5_A1:
    case GL_RGBA8:
    case GL_RGBA8_SNORM:
    case GL_RGBA16F:
    case GL_RGBA32F:
    case GL_RGBA8I:
    case GL_RGBA8UI:
    case GL_RGBA16I:
    case GL_RGBA16UI:
    case GL_RGBA32I:
    case GL_RGBA32UI:
    case GL_SRGB8_ALPHA8:
    case GL_RGB10_A2:
    case GL_RGB10_A2UI:
    case GL_COMPRESSED_RGBA8_ETC2_EAC:
    case GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC:
    case GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2:
    case GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2:
      return 4;
    case GL_RGB:
    case GL_RGB8:
    case GL_RGB8_SNORM:
    case GL_SRGB8:
    case GL_RGB565:
    case GL_RGB16F:
    case GL_RGB32F:
    case GL_R11F_G11F_B10F:
    case GL_RGB9_E5:
    case GL_RGB8I:
    case GL_RGB8UI:
    case GL_RGB16I:
    case GL_RGB16UI:
    case GL_RGB32I:
    case GL_RGB32UI:
    case GL_COMPRESSED_RGB8_ETC2:
    case GL_COMPRESSED_SRGB8_ETC2:
      return 3;
    case GL_RG:
    case GL_LUMINANCE_ALPHA:
    case GL_RG8:
    case GL_RG8_SNORM:
    case GL_RG16F:
    case GL_RG32F:
    case GL_RG8I:
    case GL_RG8UI:
    case GL_RG16I:
    case GL_RG16UI:
    case GL_RG32I:
    case GL_RG32UI:
    case GL_COMPRESSED_RG11_EAC:
    case GL_COMPRESSED_SIGNED_RG11_EAC:
      return 2;
    case GL_COMPRESSED_R11_EAC:
    case GL_COMPRESSED_SIGNED_R11_EAC:
    default:
      return 1;
  }
}

namespace {

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

int CubeFaceIndex(GLenum face) {
  switch (face) {
    case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
      return 0;
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
      return 1;
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
      return 2;
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
      return 3;
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
      return 4;
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
      return 5;
    default:
      return 0;
  }
}

const TextureImage* AttachmentImage(const Context& context,
                                    const FramebufferAttachment& attachment) {
  if (attachment.retained_texture) {
    const Texture* texture = attachment.retained_texture.get();
    const int face = texture->target == GL_TEXTURE_CUBE_MAP
                         ? CubeFaceIndex(attachment.cube_face)
                         : 0;
    return texture->image(attachment.level, face);
  }
  if (attachment.texture) {
    const Texture* texture = context.textures.Get(attachment.texture);
    if (!texture)
      return nullptr;
    const int face = texture->target == GL_TEXTURE_CUBE_MAP
                         ? CubeFaceIndex(attachment.cube_face)
                         : 0;
    return texture->image(attachment.level, face);
  }
  if (attachment.retained_renderbuffer)
    return &attachment.retained_renderbuffer->image;
  if (attachment.renderbuffer) {
    const Renderbuffer* renderbuffer =
        context.renderbuffers.Get(attachment.renderbuffer);
    if (!renderbuffer)
      return nullptr;
    return &renderbuffer->image;
  }
  return nullptr;
}

}  // namespace

RenderTarget ResolveFramebuffer(Context* context, GLuint framebuffer_name) {
  RenderTarget target;
  if (framebuffer_name == 0) {
    target.width = context->surface.width;
    target.height = context->surface.height;
    target.colors[0] = context->surface.rgba.data();
    target.color = target.colors[0];
    target.depth = context->surface.depth16.data();
    target.stencil = context->surface.stencil8.data();
    // The default framebuffer has one color attachment.  Initialize the
    // draw-buffer table so glClearBuffer* and the rasterizer agree on it.
    target.draw_buffers[0] = GL_COLOR_ATTACHMENT0;
    for (int i = 1; i < kMaxColorAttachments; ++i)
      target.draw_buffers[i] = GL_NONE;
    target.draw_buffer_count = 1;
    target.read_color_bytes = 4;
    return target;
  }
  const Framebuffer* framebuffer = context->framebuffers.Get(framebuffer_name);
  if (!framebuffer)
    return target;
  const TextureImage* color = nullptr;
  int samples = 1;
  for (int i = 0; i < kMaxColorAttachments; ++i) {
    const TextureImage* image = AttachmentImage(*context, framebuffer->colors[i]);
    if (image) {
      if (!color) {
        color = image;
        samples = std::max(1, image->samples);
      }
      const GLenum format = AttachmentFormat(*context, framebuffer->colors[i]);
      const bool integer = IsIntegerFormat(format);
      target.color_is_integer[i] = integer;
      target.color_is_unsigned[i] = IsUnsignedIntegerFormat(format);
      target.color_has_alpha[i] = FormatHasAlpha(format);
      target.color_is_srgb[i] = format == GL_SRGB8_ALPHA8;
      if (getenv("GLANCEASY_DEBUG_SRGB"))
        fprintf(stderr, "[rt] attach=%d format=0x%x srgb=%d\n", i, format,
                (int)target.color_is_srgb[i]);
      target.color_format[i] = format;
      target.color_channels[i] = FormatColorChannels(format);
      target.color_integer_bits[i] = FormatIntegerBits(format, 0);
      target.color_bytes[i] = integer ? 16 : 4;
      if (getenv("GLANCEASY_DEBUG_INT")) {
        static int rt_debug = 0;
        if (rt_debug < 12) {
          fprintf(stderr, "[rtint] i=%d fmt=0x%x int=%d sz=%zu rgba=%zu\n", i,
                  static_cast<unsigned>(format), static_cast<int>(integer),
                  image->integer_rgba.size(), image->rgba.size());
          ++rt_debug;
        }
      }
      const size_t layer_offset =
          static_cast<size_t>(framebuffer->colors[i].layer) *
          static_cast<size_t>(image->width) *
          static_cast<size_t>(image->height);
      if (integer) {
        target.integer_colors[i] =
            const_cast<uint32_t*>(
                (image->samples > 1 ? image->sample_integer_rgba
                                    : image->integer_rgba)
                    .data()) +
            layer_offset * 4;
        target.colors[i] =
            reinterpret_cast<uint8_t*>(target.integer_colors[i]);
        if (image->samples > 1) {
          target.resolved_integer_colors[i] =
              const_cast<uint32_t*>(image->integer_rgba.data()) +
              layer_offset * 4;
        }
      } else if (image->samples > 1) {
        target.colors[i] = reinterpret_cast<uint8_t*>(
            const_cast<uint32_t*>(image->sample_rgba.data()) +
            layer_offset * static_cast<size_t>(image->samples));
        target.resolved_colors[i] =
            const_cast<uint32_t*>(image->rgba.data()) + layer_offset;
      } else {
        target.colors[i] = reinterpret_cast<uint8_t*>(
            const_cast<uint32_t*>(image->rgba.data()) + layer_offset);
      }
    }
  }
  const TextureImage* depth = AttachmentImage(*context, framebuffer->depth);
  const TextureImage* stencil = AttachmentImage(*context, framebuffer->stencil);
  const TextureImage* extent_image = color;
  if (!extent_image || !extent_image->width || !extent_image->height)
    extent_image = depth;
  if (!extent_image || !extent_image->width || !extent_image->height)
    extent_image = stencil;
  if (!extent_image || !extent_image->width || !extent_image->height)
    return target;
  target.width = extent_image->width;
  target.height = extent_image->height;
  target.samples = samples;
  target.sample_texels =
      static_cast<size_t>(target.width) * static_cast<size_t>(target.height);
  for (int i = 0; i < kMaxColorAttachments; ++i) {
    target.draw_buffers[i] = framebuffer->draw_buffers[i];
  }
  target.draw_buffer_count = framebuffer->draw_buffer_count;
  target.color = target.colors[0];
  int first = -1;
  for (int i = 0; i < target.draw_buffer_count; ++i) {
    const GLenum buffer = target.draw_buffers[i];
    if (buffer >= GL_COLOR_ATTACHMENT0 &&
        buffer < GL_COLOR_ATTACHMENT0 + kMaxColorAttachments) {
      first = static_cast<int>(buffer - GL_COLOR_ATTACHMENT0);
      break;
    }
  }
  if (first >= 0 && target.colors[first]) {
    target.color = target.colors[first];
    target.read_integer = target.color_is_integer[first];
    target.read_unsigned = target.color_is_unsigned[first];
    target.read_has_alpha = target.color_has_alpha[first];
    target.read_channels = target.color_channels[first];
    target.read_format = AttachmentFormat(*context, framebuffer->colors[first]);
    target.read_color_bytes = target.color_bytes[first];
  }
  const size_t depth_layer_offset =
      static_cast<size_t>(framebuffer->depth.layer) *
      static_cast<size_t>(depth ? depth->width : 0) *
      static_cast<size_t>(depth ? depth->height : 0);
  const size_t stencil_layer_offset =
      static_cast<size_t>(framebuffer->stencil.layer) *
      static_cast<size_t>(stencil ? stencil->width : 0) *
      static_cast<size_t>(stencil ? stencil->height : 0);
  if (depth && depth->width == target.width && depth->height == target.height) {
    target.depth = const_cast<uint16_t*>(depth->depth16.data()) +
                   depth_layer_offset;
    if (depth->samples > 1) {
      target.depth = const_cast<uint16_t*>(depth->sample_depth16.data()) +
                     depth_layer_offset * static_cast<size_t>(depth->samples);
      target.resolved_depth = const_cast<uint16_t*>(depth->depth16.data()) +
                              depth_layer_offset;
    }
  }
  if (stencil && stencil->width == target.width &&
      stencil->height == target.height) {
    target.stencil = const_cast<uint8_t*>(stencil->stencil8.data()) +
                     stencil_layer_offset;
    if (stencil->samples > 1) {
      target.stencil = const_cast<uint8_t*>(stencil->sample_stencil8.data()) +
                       stencil_layer_offset *
                           static_cast<size_t>(stencil->samples);
      target.resolved_stencil = const_cast<uint8_t*>(stencil->stencil8.data());
    }
  }
  return target;
}

TransformFeedback* CurrentTransformFeedback(Context* context) {
  if (!context)
    return nullptr;
  if (context->state.transform_feedback == 0)
    return &context->default_transform_feedback;
  return context->transform_feedbacks.Get(context->state.transform_feedback);
}

void Surface::AllocateColor(int w, int h) {
  width = w;
  height = h;
  rgba.assign(static_cast<size_t>(std::max(0, w)) *
                  static_cast<size_t>(std::max(0, h)) * 4,
              0u);
}

void Surface::AllocateDepth(int w, int h) {
  depth16.assign(
      static_cast<size_t>(std::max(0, w)) * static_cast<size_t>(std::max(0, h)),
      0xFFFFu);
}

void Surface::AllocateStencil(int w, int h) {
  stencil8.assign(
      static_cast<size_t>(std::max(0, w)) * static_cast<size_t>(std::max(0, h)),
      0u);
}

void Surface::Clear() {
  std::fill(rgba.begin(), rgba.end(), 0u);
  std::fill(depth16.begin(), depth16.end(), 0xFFFFu);
  std::fill(stencil8.begin(), stencil8.end(), 0u);
}

const TextureImage* Texture::image(int level, int face) const {
  if (level < 0 || level >= levels || face < 0 || face >= faces())
    return nullptr;
  const int index = level * faces() + face;
  if (index < 0 || index >= static_cast<int>(images.size()))
    return nullptr;
  return &images[index];
}

TextureImage* Texture::mutable_image(int level, int face) {
  return const_cast<TextureImage*>(image(level, face));
}

void Texture::Resize(int level_count, int face_count) {
  levels = std::max(0, level_count);
  images.resize(static_cast<size_t>(levels) * static_cast<size_t>(face_count));
}

bool Texture::IsMipmapComplete() const {
  for (int level = 0; level < levels; ++level) {
    const TextureImage* image_level = image(level, 0);
    if (!image_level || image_level->width <= 0 || image_level->height <= 0)
      return false;
  }
  return true;
}

bool Texture::IsComplete() const {
  if (width <= 0 || height <= 0)
    return false;
  if (target == GL_TEXTURE_CUBE_MAP) {
    for (int face = 0; face < 6; ++face) {
      const TextureImage* image0 = image(0, face);
      if (!image0 || image0->width <= 0 || image0->height != image0->width)
        return false;
    }
  }
  const bool uses_mipmaps = min_filter == GL_NEAREST_MIPMAP_NEAREST ||
                            min_filter == GL_LINEAR_MIPMAP_NEAREST ||
                            min_filter == GL_NEAREST_MIPMAP_LINEAR ||
                            min_filter == GL_LINEAR_MIPMAP_LINEAR;
  if (uses_mipmaps && !IsMipmapComplete())
    return false;
  return true;
}

Context::Context(int width, int height) {
  surface.AllocateColor(width, height);
  surface.AllocateDepth(width, height);
  surface.AllocateStencil(width, height);
  state.viewport = Rect{0, 0, width, height};
  state.scissor = Rect{0, 0, width, height};
  for (int i = 0; i < kMaxVertexAttribs; ++i) {
    state.current_attribute[i][3] = 1.0f;
    state.current_attribute_integer[i][3] = 1;
  }
  diagnostics = "Glanceasy 0.1.0 RGBA8/16-bit depth/8-bit stencil";
  ++g_live_contexts;
}

Context::~Context() {
  --g_live_contexts;
  if (g_current_context == this)
    g_current_context = nullptr;
}

bool Context::Resize(int width, int height) {
  if (width <= 0 || height <= 0)
    return false;
  surface.AllocateColor(width, height);
  surface.AllocateDepth(width, height);
  surface.AllocateStencil(width, height);
  surface.Clear();
  state.viewport = Rect{0, 0, width, height};
  state.scissor = Rect{0, 0, width, height};
  return true;
}

RenderTarget Context::GetDrawTarget() {
  return ResolveFramebuffer(this, state.framebuffer);
}

RenderTarget Context::GetReadTarget() {
  RenderTarget target = ResolveFramebuffer(this, state.read_framebuffer);
  const Framebuffer* framebuffer = framebuffers.Get(state.read_framebuffer);
  if (framebuffer) {
    if (framebuffer->read_buffer == GL_NONE) {
      target.color = nullptr;
    } else if (framebuffer->read_buffer >= GL_COLOR_ATTACHMENT0 &&
               framebuffer->read_buffer <
                   GL_COLOR_ATTACHMENT0 + kMaxColorAttachments) {
      const int index =
          static_cast<int>(framebuffer->read_buffer - GL_COLOR_ATTACHMENT0);
      target.color = target.colors[index];
      target.read_integer = target.color_is_integer[index];
      target.read_unsigned = target.color_is_unsigned[index];
      target.read_has_alpha = target.color_has_alpha[index];
      target.read_channels = target.color_channels[index];
      target.read_format =
          AttachmentFormat(*this, framebuffer->colors[index]);
      target.read_color_bytes = target.color_bytes[index];
      if (target.multisampled() &&
          target.resolved_integer_colors[index]) {
        target.color = reinterpret_cast<uint8_t*>(
            target.resolved_integer_colors[index]);
      } else if (target.multisampled() && target.resolved_colors[index]) {
        target.color =
            reinterpret_cast<uint8_t*>(target.resolved_colors[index]);
      }
    }
  }
  return target;
}

ShaderObject* Context::FindShader(GLuint name) {
  ShaderObject* shader = shaders.Get(name);
  if (shader)
    return shader;
  for (auto& entry : orphaned_shaders) {
    if (entry.first == name)
      return entry.second.get();
  }
  return nullptr;
}

void Context::CollectOrphanedShaders() {
  for (size_t index = 0; index < orphaned_shaders.size();) {
    bool referenced = false;
    for (int name = 1; name <= programs.size(); ++name) {
      const ProgramObject* program = programs.Get(static_cast<GLuint>(name));
      if (!program)
        continue;
      for (GLuint shader : program->shaders) {
        if (shader == orphaned_shaders[index].first) {
          referenced = true;
          break;
        }
      }
      if (referenced)
        break;
    }
    if (referenced) {
      ++index;
      continue;
    }
    shaders.Reclaim(orphaned_shaders[index].first);
    orphaned_shaders.erase(orphaned_shaders.begin() + index);
  }
}

Context* CurrentContext() {
  return g_current_context;
}

void SetCurrentContext(Context* context) {
  g_current_context = context;
}

int LiveContextCount() {
  return g_live_contexts;
}

void SetError(GLenum error) {
  if (g_current_context)
    g_current_context->SetError(error);
}

}  // namespace glanceasy

// ---------------------------------------------------------------------------
// Context lifetime, the only entry points that are not part of GL ES.

extern "C" {

int glanceasyCreateContext(int width, int height) {
  if (width <= 0 || height <= 0)
    return 0;
  glanceasy::g_default_context.reset(new glanceasy::Context(width, height));
  glanceasy::SetCurrentContext(glanceasy::g_default_context.get());
  return 1;
}

void glanceasyDestroyContext(void) {
  if (glanceasy::g_current_context == glanceasy::g_default_context.get())
    glanceasy::SetCurrentContext(nullptr);
  glanceasy::g_default_context.reset();
}

int glanceasyContextReady(void) {
  return glanceasy::CurrentContext() ? 1 : 0;
}

GlanceasyContext* glanceasyCreateContextHandle(int width, int height) {
  if (width <= 0 || height <= 0)
    return nullptr;
  return reinterpret_cast<GlanceasyContext*>(new glanceasy::Context(width, height));
}

void glanceasyDestroyContextHandle(GlanceasyContext* context) {
  if (!context)
    return;
  glanceasy::Context* ctx = reinterpret_cast<glanceasy::Context*>(context);
  if (glanceasy::CurrentContext() == ctx)
    glanceasy::SetCurrentContext(nullptr);
  delete ctx;
}

void glanceasyMakeCurrent(GlanceasyContext* context) {
  glanceasy::SetCurrentContext(reinterpret_cast<glanceasy::Context*>(context));
}

GlanceasyContext* glanceasyGetCurrentContext(void) {
  return reinterpret_cast<GlanceasyContext*>(glanceasy::CurrentContext());
}

int glanceasyResizeContext(GlanceasyContext* context, int width, int height) {
  glanceasy::Context* ctx = reinterpret_cast<glanceasy::Context*>(context);
  if (!ctx)
    ctx = glanceasy::CurrentContext();
  if (!ctx)
    return 0;
  return ctx->Resize(width, height) ? 1 : 0;
}

int glanceasyGetWidth(void) {
  glanceasy::Context* context = glanceasy::CurrentContext();
  return context ? context->surface.width : 0;
}

int glanceasyGetHeight(void) {
  glanceasy::Context* context = glanceasy::CurrentContext();
  return context ? context->surface.height : 0;
}

const uint8_t* glanceasyGetPixels(int* width, int* height) {
  glanceasy::Context* context = glanceasy::CurrentContext();
  if (!context)
    return nullptr;
  if (width)
    *width = context->surface.width;
  if (height)
    *height = context->surface.height;
  return context->surface.rgba.data();
}

const uint32_t* glanceasyGetPixels32(int* width, int* height) {
  return reinterpret_cast<const uint32_t*>(glanceasyGetPixels(width, height));
}

void glanceasyGetStats(GlanceasyStats* stats) {
  if (!stats)
    return;
  glanceasy::Context* context = glanceasy::CurrentContext();
  *stats = context ? context->stats : GlanceasyStats{};
}

void glanceasyResetStats(void) {
  glanceasy::Context* context = glanceasy::CurrentContext();
  if (context)
    context->stats = GlanceasyStats{};
}

int glanceasyLiveContextCount(void) {
  return glanceasy::LiveContextCount();
}

const char* glanceasyGetDiagnostics(void) {
  glanceasy::Context* context = glanceasy::CurrentContext();
  if (!context)
    return "";
  return context->diagnostics.c_str();
}

}  // extern "C"
