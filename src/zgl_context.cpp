// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/zgl_context.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <memory>
#include <string>

#include "zgl/zgl.h"

namespace zgl {

namespace {

thread_local Context* g_current_context = nullptr;
std::unique_ptr<Context> g_default_context;
int g_live_contexts = 0;

const TextureImage* AttachmentImage(const Context& context,
                                    const FramebufferAttachment& attachment) {
  if (attachment.texture) {
    const Texture* texture = context.textures.Get(attachment.texture);
    if (!texture)
      return nullptr;
    return texture->image(attachment.level, 0);
  }
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
    target.color = context->surface.rgba.data();
    target.depth = context->surface.depth16.data();
    target.stencil = context->surface.stencil8.data();
    return target;
  }
  const Framebuffer* framebuffer = context->framebuffers.Get(framebuffer_name);
  if (!framebuffer)
    return target;
  const TextureImage* color = AttachmentImage(*context, framebuffer->colors[0]);
  const TextureImage* depth = AttachmentImage(*context, framebuffer->depth);
  const TextureImage* stencil = AttachmentImage(*context, framebuffer->stencil);
  if (!color || !color->width || !color->height)
    return target;
  target.width = color->width;
  target.height = color->height;
  target.color =
      reinterpret_cast<uint8_t*>(const_cast<uint32_t*>(color->rgba.data()));
  if (depth && depth->width == target.width && depth->height == target.height)
    target.depth = const_cast<uint16_t*>(depth->depth16.data());
  if (stencil && stencil->width == target.width &&
      stencil->height == target.height) {
    target.stencil = const_cast<uint8_t*>(stencil->stencil8.data());
  }
  return target;
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
  for (int i = 0; i < kMaxVertexAttribs; ++i)
    state.current_attribute[i][3] = 1.0f;
  diagnostics = "ZCat-GLES 0.1.0 RGBA8/16-bit depth/8-bit stencil";
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
  return ResolveFramebuffer(this, state.read_framebuffer);
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

}  // namespace zgl

// ---------------------------------------------------------------------------
// Context lifetime, the only entry points that are not part of GL ES.

extern "C" {

int zglCreateContext(int width, int height) {
  if (width <= 0 || height <= 0)
    return 0;
  zgl::g_default_context.reset(new zgl::Context(width, height));
  zgl::SetCurrentContext(zgl::g_default_context.get());
  return 1;
}

void zglDestroyContext(void) {
  if (zgl::g_current_context == zgl::g_default_context.get())
    zgl::SetCurrentContext(nullptr);
  zgl::g_default_context.reset();
}

int zglContextReady(void) {
  return zgl::CurrentContext() ? 1 : 0;
}

ZglContext* zglCreateContextHandle(int width, int height) {
  if (width <= 0 || height <= 0)
    return nullptr;
  return reinterpret_cast<ZglContext*>(new zgl::Context(width, height));
}

void zglDestroyContextHandle(ZglContext* context) {
  if (!context)
    return;
  zgl::Context* ctx = reinterpret_cast<zgl::Context*>(context);
  if (zgl::CurrentContext() == ctx)
    zgl::SetCurrentContext(nullptr);
  delete ctx;
}

void zglMakeCurrent(ZglContext* context) {
  zgl::SetCurrentContext(reinterpret_cast<zgl::Context*>(context));
}

ZglContext* zglGetCurrentContext(void) {
  return reinterpret_cast<ZglContext*>(zgl::CurrentContext());
}

int zglResizeContext(ZglContext* context, int width, int height) {
  zgl::Context* ctx = reinterpret_cast<zgl::Context*>(context);
  if (!ctx)
    ctx = zgl::CurrentContext();
  if (!ctx)
    return 0;
  return ctx->Resize(width, height) ? 1 : 0;
}

int zglGetWidth(void) {
  zgl::Context* context = zgl::CurrentContext();
  return context ? context->surface.width : 0;
}

int zglGetHeight(void) {
  zgl::Context* context = zgl::CurrentContext();
  return context ? context->surface.height : 0;
}

const uint8_t* zglGetPixels(int* width, int* height) {
  zgl::Context* context = zgl::CurrentContext();
  if (!context)
    return nullptr;
  if (width)
    *width = context->surface.width;
  if (height)
    *height = context->surface.height;
  return context->surface.rgba.data();
}

const uint32_t* zglGetPixels32(int* width, int* height) {
  return reinterpret_cast<const uint32_t*>(zglGetPixels(width, height));
}

void zglGetStats(ZglStats* stats) {
  if (!stats)
    return;
  zgl::Context* context = zgl::CurrentContext();
  *stats = context ? context->stats : ZglStats{};
}

void zglResetStats(void) {
  zgl::Context* context = zgl::CurrentContext();
  if (context)
    context->stats = ZglStats{};
}

int zglLiveContextCount(void) {
  return zgl::LiveContextCount();
}

const char* zglGetDiagnostics(void) {
  zgl::Context* context = zgl::CurrentContext();
  if (!context)
    return "";
  return context->diagnostics.c_str();
}

}  // extern "C"
