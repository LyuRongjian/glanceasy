// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include "GLES3/gl32.h"
#include "src/zgl_api_common.h"
#include "src/zgl_draw.h"

namespace zgl {
namespace {

bool ValidIndexType(GLenum type) {
  return type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_SHORT ||
         type == GL_UNSIGNED_INT;
}

bool ValidPrimitiveMode(GLenum mode) {
  switch (mode) {
    case GL_POINTS:
    case GL_LINES:
    case GL_LINE_STRIP:
    case GL_LINE_LOOP:
    case GL_TRIANGLES:
    case GL_TRIANGLE_STRIP:
    case GL_TRIANGLE_FAN:
      return true;
    default:
      return false;
  }
}

bool CheckDrawPreamble(Context* context, GLenum mode) {
  if (!ValidPrimitiveMode(mode)) {
    context->SetError(GL_INVALID_ENUM);
    return false;
  }
  if (!context->GetDrawTarget().valid()) {
    context->SetError(GL_INVALID_FRAMEBUFFER_OPERATION);
    return false;
  }
  return true;
}

GLuint MaxIndexValue(GLenum type) {
  return type == GL_UNSIGNED_BYTE
             ? 0xffu
             : (type == GL_UNSIGNED_SHORT ? 0xffffu : 0xffffffffu);
}

// Resolves the element pointer.  A bound element buffer makes |indices| a byte
// offset into the buffer (null means offset zero); otherwise the caller
// supplied client memory is used directly.
const void* ElementPointer(Context* context, const void* indices) {
  if (context->state.element_buffer == 0) {
    zgl::TransformFeedback* feedback = zgl::CurrentTransformFeedback(context);
    if (feedback && feedback->active && !feedback->paused)
      return nullptr;
    return indices;
  }
  const Buffer* buffer = context->buffers.Get(context->state.element_buffer);
  if (!buffer)
    return nullptr;
  const size_t offset = reinterpret_cast<size_t>(indices);
  if (offset > buffer->data.size())
    return nullptr;
  return buffer->data.data() + offset;
}

}  // namespace
}  // namespace zgl

extern "C" {

void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
  if (getenv("ZGL_DEBUG_ERROR"))
    fprintf(stderr, "[gldrawarrays] first=%d count=%d\n", first, count);
  if (first < 0 || count < 0) {
    zgl::Context* context = zgl::ApiContext();
    if (context)
      context->SetError(GL_INVALID_VALUE);
    return;
  }
  zgl::Draw(mode, first, count, GL_NONE, nullptr, 1);
}

void glDrawElements(GLenum mode,
                    GLsizei count,
                    GLenum type,
                    const void* indices) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (!zgl::CheckDrawPreamble(context, mode))
    return;
  if (count < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!zgl::ValidIndexType(type)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  const void* pointer = zgl::ElementPointer(context, indices);
  if (!pointer) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  zgl::Draw(mode, 0, count, type, pointer, 1);
}

void glDrawArraysInstanced(GLenum mode,
                           GLint first,
                           GLsizei count,
                           GLsizei instance_count) {
  if (first < 0 || count < 0 || instance_count < 0) {
    zgl::Context* context = zgl::ApiContext();
    if (context)
      context->SetError(GL_INVALID_VALUE);
    return;
  }
  zgl::Draw(mode, first, count, GL_NONE, nullptr, instance_count);
}

void glDrawElementsInstanced(GLenum mode,
                             GLsizei count,
                             GLenum type,
                             const void* indices,
                             GLsizei instance_count) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (!zgl::CheckDrawPreamble(context, mode))
    return;
  if (count < 0 || instance_count < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!zgl::ValidIndexType(type)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  const void* pointer = zgl::ElementPointer(context, indices);
  if (!pointer) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  zgl::Draw(mode, 0, count, type, pointer, instance_count);
}

void glDrawRangeElements(GLenum mode,
                         GLuint start,
                         GLuint end,
                         GLsizei count,
                         GLenum type,
                         const void* indices) {
  zgl::Context* context = zgl::ApiContext();
  if (context) {
    if (start > end || end > zgl::MaxIndexValue(type)) {
      context->SetError(GL_INVALID_VALUE);
      return;
    }
  }
  glDrawElements(mode, count, type, indices);
}

}  // extern "C"
