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
#include "src/zgl_simd.h"

namespace zgl {

bool GetIntegerValue(Context* context, GLenum pname, GLint* first, int* count) {
  const State& state = context->state;
  *count = 1;
  switch (pname) {
    case GL_MAX_TEXTURE_SIZE:
      *first = kMaxTextureSize;
      return true;
    case GL_MAX_CUBE_MAP_TEXTURE_SIZE:
      *first = kMaxCubeMapSize;
      return true;
    case GL_MAX_RENDERBUFFER_SIZE:
      *first = kMaxRenderbufferSize;
      return true;
    case GL_MAX_VIEWPORT_DIMS:
      *first = kMaxViewportSize;
      *count = 2;
      return true;
    case GL_MAX_VERTEX_ATTRIBS:
      *first = kMaxVertexAttribs;
      return true;
    case GL_MAX_TEXTURE_IMAGE_UNITS:
    case GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS:
      *first = kMaxTextureUnits;
      return true;
    case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:
      *first = kMaxCombinedTextureUnits;
      return true;
    case GL_MAX_VERTEX_UNIFORM_VECTORS:
    case GL_MAX_FRAGMENT_UNIFORM_VECTORS:
      *first = kMaxUniformVectors;
      return true;
    case GL_MAX_UNIFORM_BUFFER_BINDINGS:
      *first = kMaxUniformBufferBindings;
      return true;
    case GL_MAX_UNIFORM_BLOCK_SIZE:
      *first = 16384;
      return true;
    case GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS:
    case GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS:
      *first = kMaxTransformFeedbackBuffers;
      return true;
    case GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS:
      *first = 64;
      return true;
    case GL_TRANSFORM_FEEDBACK_BUFFER_BINDING:
      *first = static_cast<GLint>(state.transform_feedback_buffer);
      return true;
    case GL_TRANSFORM_FEEDBACK_ACTIVE: {
      const TransformFeedback* tf = CurrentTransformFeedback(context);
      *first = tf && tf->active ? 1 : 0;
      return true;
    }
    case GL_TRANSFORM_FEEDBACK_PAUSED: {
      const TransformFeedback* tf = CurrentTransformFeedback(context);
      *first = tf && tf->paused ? 1 : 0;
      return true;
    }
    case GL_MAX_VERTEX_UNIFORM_BLOCKS:
    case GL_MAX_FRAGMENT_UNIFORM_BLOCKS:
      *first = kMaxUniformBufferBindings;
      return true;
    case GL_MAX_COMBINED_UNIFORM_BLOCKS:
      *first = kMaxUniformBufferBindings;
      return true;
    case GL_UNIFORM_BUFFER_BINDING:
      *first = static_cast<GLint>(state.uniform_buffer);
      return true;
    case GL_MAX_VARYING_VECTORS:
      *first = kMaxVaryingVectors;
      return true;
    case GL_MAX_3D_TEXTURE_SIZE:
      *first = kMax3DTextureSize;
      return true;
    case GL_MAX_ARRAY_TEXTURE_LAYERS:
      *first = 256;
      return true;
    case GL_MAX_DRAW_BUFFERS:
      *first = kMaxDrawBuffers;
      return true;
    case GL_MAX_COLOR_ATTACHMENTS:
      *first = kMaxColorAttachments;
      return true;
    case GL_MAX_SAMPLES:
      *first = kMaxSamples;
      return true;
    case GL_SAMPLES:
    case GL_SAMPLE_BUFFERS:
    case GL_NUM_COMPRESSED_TEXTURE_FORMATS:
      *first = 10;
      return true;
    case GL_NUM_SHADER_BINARY_FORMATS:
    case GL_NUM_EXTENSIONS:
      *first = 0;
      return true;
    case GL_SUBPIXEL_BITS:
      *first = 8;
      return true;
    case GL_RED_BITS:
    case GL_GREEN_BITS:
    case GL_BLUE_BITS:
    case GL_ALPHA_BITS:
      *first = 8;
      return true;
    case GL_DEPTH_BITS:
      *first = 16;
      return true;
    case GL_STENCIL_BITS:
      *first = 8;
      return true;
    case GL_MAJOR_VERSION:
      *first = 3;
      return true;
    case GL_MINOR_VERSION:
      *first = 0;
      return true;
    case GL_VIEWPORT:
      *first = state.viewport.x;
      *count = 4;
      return true;
    case GL_SCISSOR_BOX:
      *first = state.scissor.x;
      *count = 4;
      return true;
    case GL_CURRENT_PROGRAM:
      *first = static_cast<GLint>(state.program);
      return true;
    case GL_VERTEX_ARRAY_BINDING:
      *first = static_cast<GLint>(state.vertex_array);
      return true;
    case GL_ARRAY_BUFFER_BINDING:
      *first = static_cast<GLint>(state.array_buffer);
      return true;
    case GL_ELEMENT_ARRAY_BUFFER_BINDING:
      *first = static_cast<GLint>(state.element_buffer);
      return true;
    case GL_FRAMEBUFFER_BINDING:
      *first = static_cast<GLint>(state.framebuffer);
      return true;
    case GL_READ_FRAMEBUFFER_BINDING:
      *first = static_cast<GLint>(state.read_framebuffer);
      return true;
    case GL_RENDERBUFFER_BINDING:
      *first = static_cast<GLint>(state.renderbuffer);
      return true;
    case GL_ACTIVE_TEXTURE:
      *first = static_cast<GLint>(state.active_texture);
      return true;
    case GL_TEXTURE_BINDING_2D:
    case GL_TEXTURE_BINDING_CUBE_MAP:
    case GL_TEXTURE_BINDING_3D:
    case GL_TEXTURE_BINDING_2D_ARRAY: {
      const int unit = static_cast<int>(state.active_texture) -
                       static_cast<int>(GL_TEXTURE0);
      *first = static_cast<GLint>(
          state.textures[ClampInt(unit, 0, kMaxCombinedTextureUnits - 1)]);
      return true;
    }
    case GL_SAMPLER_BINDING: {
      const int unit = static_cast<int>(state.active_texture) -
                       static_cast<int>(GL_TEXTURE0);
      *first = static_cast<GLint>(
          state.samplers[ClampInt(unit, 0, kMaxCombinedTextureUnits - 1)]);
      return true;
    }
    case GL_CULL_FACE:
      *first = state.cull.enabled ? 1 : 0;
      return true;
    case GL_DEPTH_TEST:
      *first = state.depth.test ? 1 : 0;
      return true;
    case GL_DITHER:
      *first = state.dither ? 1 : 0;
      return true;
    case GL_POLYGON_OFFSET_FILL:
      *first = state.polygon_offset_fill ? 1 : 0;
      return true;
    case GL_PRIMITIVE_RESTART_FIXED_INDEX:
      *first = state.primitive_restart_fixed_index ? 1 : 0;
      return true;
    case GL_SAMPLE_ALPHA_TO_COVERAGE:
      *first = state.sample_alpha_to_coverage ? 1 : 0;
      return true;
    case GL_SAMPLE_COVERAGE:
      *first = state.sample_coverage ? 1 : 0;
      return true;
    case GL_SCISSOR_TEST:
      *first = state.scissor_test ? 1 : 0;
      return true;
    case GL_STENCIL_TEST:
      *first = state.stencil.test ? 1 : 0;
      return true;
    case GL_SHADER_COMPILER:
      *first = 1;
      return true;
    case GL_IMPLEMENTATION_COLOR_READ_FORMAT:
      *first = GL_RGBA;
      return true;
    case GL_IMPLEMENTATION_COLOR_READ_TYPE:
      *first = GL_UNSIGNED_BYTE;
      return true;
    case GL_DEPTH_FUNC:
      *first = static_cast<GLint>(state.depth.func);
      return true;
    case GL_DEPTH_CLEAR_VALUE:
      *first = static_cast<GLint>(state.clear_depth > 0.0f ? 1 : 0);
      return true;
    case GL_STENCIL_CLEAR_VALUE:
      *first = state.clear_stencil;
      return true;
    case GL_COLOR_CLEAR_VALUE:
      *first = static_cast<GLint>(state.clear_color[0] > 0.5f ? 1 : 0);
      *count = 4;
      return true;
    case GL_COLOR_WRITEMASK:
      *first = state.color_mask[0] ? GL_TRUE : GL_FALSE;
      *count = 4;
      return true;
    case GL_DEPTH_WRITEMASK:
      *first = state.depth.mask ? GL_TRUE : GL_FALSE;
      return true;
    case GL_STENCIL_WRITEMASK:
      *first = static_cast<GLint>(state.stencil.write_mask);
      return true;
    case GL_STENCIL_BACK_WRITEMASK:
      *first = static_cast<GLint>(state.stencil.write_mask);
      return true;
    case GL_STENCIL_FUNC:
      *first = static_cast<GLint>(state.stencil.front.func);
      return true;
    case GL_STENCIL_REF:
      *first = state.stencil.front.reference;
      return true;
    case GL_STENCIL_VALUE_MASK:
      *first = state.stencil.mask;
      return true;
    case GL_STENCIL_FAIL:
      *first = static_cast<GLint>(state.stencil.front.op_fail);
      return true;
    case GL_STENCIL_PASS_DEPTH_FAIL:
      *first = static_cast<GLint>(state.stencil.front.op_depth_fail);
      return true;
    case GL_STENCIL_PASS_DEPTH_PASS:
      *first = static_cast<GLint>(state.stencil.front.op_pass);
      return true;
    case GL_STENCIL_BACK_FUNC:
      *first = static_cast<GLint>(state.stencil.back.func);
      return true;
    case GL_STENCIL_BACK_REF:
      *first = state.stencil.back.reference;
      return true;
    case GL_STENCIL_BACK_VALUE_MASK:
      *first = state.stencil.mask;
      return true;
    case GL_STENCIL_BACK_FAIL:
      *first = static_cast<GLint>(state.stencil.back.op_fail);
      return true;
    case GL_STENCIL_BACK_PASS_DEPTH_FAIL:
      *first = static_cast<GLint>(state.stencil.back.op_depth_fail);
      return true;
    case GL_STENCIL_BACK_PASS_DEPTH_PASS:
      *first = static_cast<GLint>(state.stencil.back.op_pass);
      return true;
    case GL_BLEND:
      *first = state.blend.enabled ? GL_TRUE : GL_FALSE;
      return true;
    case GL_BLEND_SRC_RGB:
      *first = static_cast<GLint>(state.blend.src_rgb);
      return true;
    case GL_BLEND_DST_RGB:
      *first = static_cast<GLint>(state.blend.dst_rgb);
      return true;
    case GL_BLEND_SRC_ALPHA:
      *first = static_cast<GLint>(state.blend.src_alpha);
      return true;
    case GL_BLEND_DST_ALPHA:
      *first = static_cast<GLint>(state.blend.dst_alpha);
      return true;
    case GL_BLEND_EQUATION_RGB:
      *first = static_cast<GLint>(state.blend.equation_rgb);
      return true;
    case GL_BLEND_EQUATION_ALPHA:
      *first = static_cast<GLint>(state.blend.equation_alpha);
      return true;
    case GL_CULL_FACE_MODE:
      *first = static_cast<GLint>(state.cull.mode);
      return true;
    case GL_FRONT_FACE:
      *first = static_cast<GLint>(state.cull.front_face);
      return true;
    case GL_PACK_ALIGNMENT:
      *first = state.pixel_store.pack_alignment;
      return true;
    case GL_UNPACK_ALIGNMENT:
      *first = state.pixel_store.unpack_alignment;
      return true;
    case GL_PACK_ROW_LENGTH:
      *first = state.pixel_store.pack_row_length;
      return true;
    case GL_UNPACK_ROW_LENGTH:
      *first = state.pixel_store.unpack_row_length;
      return true;
    case GL_PACK_SKIP_ROWS:
      *first = state.pixel_store.pack_skip_rows;
      return true;
    case GL_UNPACK_SKIP_ROWS:
      *first = state.pixel_store.unpack_skip_rows;
      return true;
    case GL_PACK_SKIP_PIXELS:
      *first = state.pixel_store.pack_skip_pixels;
      return true;
    case GL_UNPACK_SKIP_PIXELS:
      *first = state.pixel_store.unpack_skip_pixels;
      return true;
    case GL_RASTERIZER_DISCARD:
      *first = state.rasterizer_discard ? GL_TRUE : GL_FALSE;
      return true;
    case GL_LINE_WIDTH:
      *first = static_cast<GLint>(state.line_width + 0.5f);
      return true;
    case GL_SAMPLE_COVERAGE_VALUE:
      *first = state.sample_coverage_value >= 0.5f ? 1 : 0;
      return true;
    case GL_SAMPLE_COVERAGE_INVERT:
      *first = state.invert_coverage ? GL_TRUE : GL_FALSE;
      return true;
    default:
      return false;
  }
}

}  // namespace zgl

extern "C" {

GLenum glGetError(void) {
  zgl::Context* context = zgl::ApiContext();
  return context ? context->TakeError() : GL_NO_ERROR;
}

const GLubyte* glGetString(GLenum name) {
  switch (name) {
    case GL_VENDOR:
      return reinterpret_cast<const GLubyte*>("ZCat-GLES");
    case GL_RENDERER:
      return reinterpret_cast<const GLubyte*>("ZCat CPU rasteriser");
    case GL_VERSION:
      return reinterpret_cast<const GLubyte*>("OpenGL ES 3.0 ZCat-GLES 0.1.0");
    case GL_SHADING_LANGUAGE_VERSION:
      return reinterpret_cast<const GLubyte*>(
          "OpenGL ES GLSL ES 3.00 ZCat-GLES");
    case GL_EXTENSIONS:
      return reinterpret_cast<const GLubyte*>("");
    default:
      break;
  }
  zgl::Context* context = zgl::ApiContext();
  if (context)
    context->SetError(GL_INVALID_ENUM);
  return nullptr;
}

const GLubyte* glGetStringi(GLenum name, GLuint index) {
  (void)index;
  if (name != GL_EXTENSIONS) {
    zgl::Context* context = zgl::ApiContext();
    if (context)
      context->SetError(GL_INVALID_ENUM);
  }
  return nullptr;
}

void glGetIntegerv(GLenum pname, GLint* data) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !data)
    return;
  GLint first = 0;
  int count = 1;
  if (!zgl::GetIntegerValue(context, pname, &first, &count)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (pname == GL_VIEWPORT) {
    data[0] = context->state.viewport.x;
    data[1] = context->state.viewport.y;
    data[2] = context->state.viewport.width;
    data[3] = context->state.viewport.height;
    return;
  }
  if (pname == GL_SCISSOR_BOX) {
    data[0] = context->state.scissor.x;
    data[1] = context->state.scissor.y;
    data[2] = context->state.scissor.width;
    data[3] = context->state.scissor.height;
    return;
  }
  if (pname == GL_COLOR_WRITEMASK) {
    for (int i = 0; i < 4; ++i)
      data[i] = context->state.color_mask[i] ? GL_TRUE : GL_FALSE;
    return;
  }
  if (pname == GL_COLOR_CLEAR_VALUE) {
    for (int i = 0; i < 4; ++i)
      data[i] =
          static_cast<GLint>(context->state.clear_color[i] * 255.0f + 0.5f);
    return;
  }
  if (pname == GL_MAX_VIEWPORT_DIMS) {
    data[0] = zgl::kMaxViewportSize;
    data[1] = zgl::kMaxViewportSize;
    return;
  }
  data[0] = first;
}

void glGetFloatv(GLenum pname, GLfloat* data) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !data)
    return;
  switch (pname) {
    case GL_DEPTH_RANGE:
      data[0] = context->state.depth.range_near;
      data[1] = context->state.depth.range_far;
      return;
    case GL_COLOR_CLEAR_VALUE:
      for (int i = 0; i < 4; ++i)
        data[i] = context->state.clear_color[i];
      return;
    case GL_BLEND_COLOR:
      for (int i = 0; i < 4; ++i)
        data[i] = context->state.blend.color[i];
      return;
    case GL_DEPTH_CLEAR_VALUE:
      data[0] = context->state.clear_depth;
      return;
    case GL_LINE_WIDTH:
      data[0] = context->state.line_width;
      return;
    case GL_SAMPLE_COVERAGE_VALUE:
      data[0] = context->state.sample_coverage_value;
      return;
    case GL_ALIASED_LINE_WIDTH_RANGE:
      data[0] = 1.0f;
      data[1] = 1.0f;
      return;
    case GL_ALIASED_POINT_SIZE_RANGE:
      data[0] = 1.0f;
      data[1] = 64.0f;
      return;
    default:
      break;
  }
  GLint value = 0;
  int count = 1;
  if (!zgl::GetIntegerValue(context, pname, &value, &count)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  // Vector state such as GL_COLOR_WRITEMASK and GL_VIEWPORT must fill every
  // component, not just the first one.
  GLint values[16] = {};
  if (count > 1) {
    glGetIntegerv(pname, values);
    for (int i = 0; i < count && i < 16; ++i)
      data[i] = static_cast<GLfloat>(values[i]);
    return;
  }
  data[0] = static_cast<GLfloat>(value);
}

void glGetBooleanv(GLenum pname, GLboolean* data) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !data)
    return;
  GLint value = 0;
  int count = 1;
  if (!zgl::GetIntegerValue(context, pname, &value, &count)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  GLint values[16] = {};
  if (count > 1) {
    glGetIntegerv(pname, values);
    for (int i = 0; i < count && i < 16; ++i)
      data[i] = values[i] ? GL_TRUE : GL_FALSE;
    return;
  }
  data[0] = value ? GL_TRUE : GL_FALSE;
}

void glEnable(GLenum cap) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  zgl::State& state = context->state;
  switch (cap) {
    case GL_BLEND:
      state.blend.enabled = true;
      return;
    case GL_CULL_FACE:
      state.cull.enabled = true;
      return;
    case GL_DEPTH_TEST:
      state.depth.test = true;
      return;
    case GL_DITHER:
      state.dither = true;
      return;
    case GL_POLYGON_OFFSET_FILL:
      state.polygon_offset_fill = true;
      return;
    case GL_PRIMITIVE_RESTART_FIXED_INDEX:
      state.primitive_restart = true;
      state.primitive_restart_fixed_index = true;
      return;
    case GL_RASTERIZER_DISCARD:
      state.rasterizer_discard = true;
      return;
    case GL_SAMPLE_ALPHA_TO_COVERAGE:
      state.sample_alpha_to_coverage = true;
      return;
    case GL_SAMPLE_COVERAGE:
      state.sample_coverage = true;
      return;
    case GL_SCISSOR_TEST:
      state.scissor_test = true;
      return;
    case GL_STENCIL_TEST:
      state.stencil.test = true;
      return;
    default:
      context->SetError(GL_INVALID_ENUM);
      return;
  }
}

void glDisable(GLenum cap) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  zgl::State& state = context->state;
  switch (cap) {
    case GL_BLEND:
      state.blend.enabled = false;
      return;
    case GL_CULL_FACE:
      state.cull.enabled = false;
      return;
    case GL_DEPTH_TEST:
      state.depth.test = false;
      return;
    case GL_DITHER:
      state.dither = false;
      return;
    case GL_POLYGON_OFFSET_FILL:
      state.polygon_offset_fill = false;
      return;
    case GL_PRIMITIVE_RESTART_FIXED_INDEX:
      state.primitive_restart = false;
      state.primitive_restart_fixed_index = false;
      return;
    case GL_RASTERIZER_DISCARD:
      state.rasterizer_discard = false;
      return;
    case GL_SAMPLE_ALPHA_TO_COVERAGE:
      state.sample_alpha_to_coverage = false;
      return;
    case GL_SAMPLE_COVERAGE:
      state.sample_coverage = false;
      return;
    case GL_SCISSOR_TEST:
      state.scissor_test = false;
      return;
    case GL_STENCIL_TEST:
      state.stencil.test = false;
      return;
    default:
      context->SetError(GL_INVALID_ENUM);
      return;
  }
}

GLboolean glIsEnabled(GLenum cap) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return GL_FALSE;
  const zgl::State& state = context->state;
  switch (cap) {
    case GL_BLEND:
      return state.blend.enabled ? GL_TRUE : GL_FALSE;
    case GL_CULL_FACE:
      return state.cull.enabled ? GL_TRUE : GL_FALSE;
    case GL_DEPTH_TEST:
      return state.depth.test ? GL_TRUE : GL_FALSE;
    case GL_DITHER:
      return state.dither ? GL_TRUE : GL_FALSE;
    case GL_POLYGON_OFFSET_FILL:
      return state.polygon_offset_fill ? GL_TRUE : GL_FALSE;
    case GL_PRIMITIVE_RESTART_FIXED_INDEX:
      return state.primitive_restart ? GL_TRUE : GL_FALSE;
    case GL_RASTERIZER_DISCARD:
      return state.rasterizer_discard ? GL_TRUE : GL_FALSE;
    case GL_SAMPLE_ALPHA_TO_COVERAGE:
      return state.sample_alpha_to_coverage ? GL_TRUE : GL_FALSE;
    case GL_SAMPLE_COVERAGE:
      return state.sample_coverage ? GL_TRUE : GL_FALSE;
    case GL_SCISSOR_TEST:
      return state.scissor_test ? GL_TRUE : GL_FALSE;
    case GL_STENCIL_TEST:
      return state.stencil.test ? GL_TRUE : GL_FALSE;
    default:
      context->SetError(GL_INVALID_ENUM);
      return GL_FALSE;
  }
}

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (width < 0 || height < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (width > zgl::kMaxViewportSize || height > zgl::kMaxViewportSize) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  context->state.viewport = zgl::Rect{x, y, width, height};
}

void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (width < 0 || height < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  context->state.scissor = zgl::Rect{x, y, width, height};
}

void glDepthRange(GLfloat near_value, GLfloat far_value) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  context->state.depth.range_near = near_value;
  context->state.depth.range_far = far_value;
}

void glDepthRangef(GLfloat near_value, GLfloat far_value) {
  glDepthRange(near_value, far_value);
}

void glDepthFunc(GLenum func) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  switch (func) {
    case GL_NEVER:
    case GL_LESS:
    case GL_EQUAL:
    case GL_LEQUAL:
    case GL_GREATER:
    case GL_NOTEQUAL:
    case GL_GEQUAL:
    case GL_ALWAYS:
      context->state.depth.func = func;
      return;
    default:
      context->SetError(GL_INVALID_ENUM);
      return;
  }
}

void glDepthMask(GLboolean flag) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  context->state.depth.mask = flag != GL_FALSE;
}

void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  context->state.clear_color[0] = red;
  context->state.clear_color[1] = green;
  context->state.clear_color[2] = blue;
  context->state.clear_color[3] = alpha;
}

void glClearDepthf(GLfloat depth) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  context->state.clear_depth = depth;
}

void glClearStencil(GLint value) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  context->state.clear_stencil = value;
}

void glClear(GLbitfield mask) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  zgl::RenderTarget target = context->GetDrawTarget();
  if (!target.valid()) {
    context->SetError(GL_INVALID_FRAMEBUFFER_OPERATION);
    return;
  }
  const zgl::State& state = context->state;
  zgl::Rect area{0, 0, target.width, target.height};
  if (state.scissor_test) {
    const zgl::Rect scissor{state.scissor.x,
                            target.height - state.scissor.bottom(),
                            state.scissor.width, state.scissor.height};
    area = area.Intersect(scissor);
  }
  if (area.IsEmpty())
    return;

  if (mask & GL_COLOR_BUFFER_BIT) {
    const zgl::Rgba color = zgl::Rgba{zgl::FloatToUnorm8(state.clear_color[0]),
                                      zgl::FloatToUnorm8(state.clear_color[1]),
                                      zgl::FloatToUnorm8(state.clear_color[2]),
                                      zgl::FloatToUnorm8(state.clear_color[3])};
    const bool writes_all_channels =
        state.color_mask[0] && state.color_mask[1] && state.color_mask[2] &&
        state.color_mask[3];
    const uint32_t packed =
        zgl::PackRgba(color.r, color.g, color.b, color.a);
    for (int buffer_index = 0; buffer_index < target.draw_buffer_count;
         ++buffer_index) {
      const GLenum buffer = target.draw_buffers[buffer_index];
      if (buffer < GL_COLOR_ATTACHMENT0 ||
          buffer >= GL_COLOR_ATTACHMENT0 + zgl::kMaxColorAttachments)
        continue;
      uint8_t* const color_buffer =
          target.colors[buffer - GL_COLOR_ATTACHMENT0];
      if (!color_buffer)
        continue;
      const int sample_planes = target.multisampled() ? target.samples : 1;
      const size_t plane_bytes =
          target.multisampled() ? target.sample_texels * 4 : 0;
      for (int sample = 0; sample < sample_planes; ++sample) {
        uint8_t* const plane =
            color_buffer + static_cast<size_t>(sample) * plane_bytes;
      if (writes_all_channels) {
        for (int y = area.y; y < area.bottom(); ++y) {
          uint8_t* row =
              plane + static_cast<size_t>(y) * target.width * 4;
          zgl::simd::FillRgba(
              row + static_cast<size_t>(area.x) * 4, area.width, packed);
        }
        continue;
      }
      for (int y = area.y; y < area.bottom(); ++y) {
        uint8_t* row =
            plane + static_cast<size_t>(y) * target.width * 4;
        for (int x = area.x; x < area.right(); ++x) {
          uint32_t previous;
          memcpy(&previous, row + static_cast<size_t>(x) * 4,
                 sizeof(previous));
          const zgl::Rgba stored = zgl::UnpackRgba(previous);
          const uint32_t value =
              zgl::PackRgba(state.color_mask[0] ? color.r : stored.r,
                            state.color_mask[1] ? color.g : stored.g,
                            state.color_mask[2] ? color.b : stored.b,
                            state.color_mask[3] ? color.a : stored.a);
          memcpy(row + static_cast<size_t>(x) * 4, &value, sizeof(value));
        }
      }
      }
      if (target.multisampled() &&
          target.resolved_colors[buffer - GL_COLOR_ATTACHMENT0]) {
        uint32_t* const resolved =
            target.resolved_colors[buffer - GL_COLOR_ATTACHMENT0];
        for (int y = area.y; y < area.bottom(); ++y) {
          for (int x = area.x; x < area.right(); ++x)
            resolved[static_cast<size_t>(y) * target.width + x] = packed;
        }
      }
    }
  }
  if ((mask & GL_DEPTH_BUFFER_BIT) && target.depth && state.depth.mask) {
    const uint16_t depth = static_cast<uint16_t>(
        zgl::Clamp01(state.clear_depth) * 65535.0f + 0.5f);
    const int sample_planes = target.multisampled() ? target.samples : 1;
    const size_t plane_texels =
        target.multisampled() ? target.sample_texels : 0;
    for (int sample = 0; sample < sample_planes; ++sample) {
      uint16_t* const plane =
          target.depth + static_cast<size_t>(sample) * plane_texels;
      for (int y = area.y; y < area.bottom(); ++y) {
        uint16_t* row = plane + static_cast<size_t>(y) * target.width;
        zgl::simd::FillU16(row + area.x, area.width, depth);
      }
    }
    if (target.resolved_depth) {
      for (int y = area.y; y < area.bottom(); ++y) {
        uint16_t* row =
            target.resolved_depth + static_cast<size_t>(y) * target.width;
        zgl::simd::FillU16(row + area.x, area.width, depth);
      }
    }
  }
  if ((mask & GL_STENCIL_BUFFER_BIT) && target.stencil) {
    const uint8_t value = static_cast<uint8_t>(state.clear_stencil);
    const uint8_t write_mask = static_cast<uint8_t>(state.stencil.write_mask);
    const int sample_planes = target.multisampled() ? target.samples : 1;
    const size_t plane_texels =
        target.multisampled() ? target.sample_texels : 0;
    for (int sample = 0; sample < sample_planes; ++sample) {
      uint8_t* const plane =
          target.stencil + static_cast<size_t>(sample) * plane_texels;
      for (int y = area.y; y < area.bottom(); ++y) {
        uint8_t* row = plane + static_cast<size_t>(y) * target.width;
        if (write_mask == 0xFF) {
          zgl::simd::FillU8(row + area.x, area.width, value);
          continue;
        }
        for (int x = area.x; x < area.right(); ++x) {
          row[x] = static_cast<uint8_t>((value & write_mask) |
                                        (row[x] & ~write_mask));
        }
      }
    }
    if (target.resolved_stencil) {
      for (int y = area.y; y < area.bottom(); ++y) {
        uint8_t* row =
            target.resolved_stencil + static_cast<size_t>(y) * target.width;
        for (int x = area.x; x < area.right(); ++x)
          row[x] = value;
      }
    }
  }
}

void glColorMask(GLboolean red,
                 GLboolean green,
                 GLboolean blue,
                 GLboolean alpha) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  context->state.color_mask[0] = red != GL_FALSE;
  context->state.color_mask[1] = green != GL_FALSE;
  context->state.color_mask[2] = blue != GL_FALSE;
  context->state.color_mask[3] = alpha != GL_FALSE;
}

void glStencilMask(GLuint mask) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  context->state.stencil.write_mask = static_cast<int>(mask);
  context->state.stencil_write_mask = mask;
}

void glStencilMaskSeparate(GLenum face, GLuint mask) {
  if (face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) {
    zgl::Context* context = zgl::ApiContext();
    if (context)
      context->SetError(GL_INVALID_ENUM);
    return;
  }
  glStencilMask(mask);
}

static void SetStencilFunc(zgl::StencilFace* face,
                           GLenum func,
                           GLint reference,
                           GLuint mask) {
  face->func = func;
  face->reference = reference;
  (void)mask;
}

void glStencilFunc(GLenum func, GLint reference, GLuint mask) {
  glStencilFuncSeparate(GL_FRONT_AND_BACK, func, reference, mask);
}

void glStencilFuncSeparate(GLenum face,
                           GLenum func,
                           GLint reference,
                           GLuint mask) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  switch (func) {
    case GL_NEVER:
    case GL_LESS:
    case GL_EQUAL:
    case GL_LEQUAL:
    case GL_GREATER:
    case GL_NOTEQUAL:
    case GL_GEQUAL:
    case GL_ALWAYS:
      break;
    default:
      context->SetError(GL_INVALID_ENUM);
      return;
  }
  context->state.stencil.mask = static_cast<int>(mask);
  context->state.stencil_mask = mask;
  if (face == GL_FRONT || face == GL_FRONT_AND_BACK)
    SetStencilFunc(&context->state.stencil.front, func, reference, mask);
  if (face == GL_BACK || face == GL_FRONT_AND_BACK)
    SetStencilFunc(&context->state.stencil.back, func, reference, mask);
  if (face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK)
    context->SetError(GL_INVALID_ENUM);
}

static bool ValidStencilOp(GLenum op) {
  switch (op) {
    case GL_KEEP:
    case GL_ZERO:
    case GL_REPLACE:
    case GL_INCR:
    case GL_DECR:
    case GL_INVERT:
    case GL_INCR_WRAP:
    case GL_DECR_WRAP:
      return true;
    default:
      return false;
  }
}

void glStencilOp(GLenum fail, GLenum pass_depth_fail, GLenum pass_depth_pass) {
  glStencilOpSeparate(GL_FRONT_AND_BACK, fail, pass_depth_fail,
                      pass_depth_pass);
}

void glStencilOpSeparate(GLenum face,
                         GLenum fail,
                         GLenum depth_fail,
                         GLenum pass) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (!ValidStencilOp(fail) || !ValidStencilOp(depth_fail) ||
      !ValidStencilOp(pass)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  const auto apply = [&](zgl::StencilFace* target) {
    target->op_fail = fail;
    target->op_depth_fail = depth_fail;
    target->op_pass = pass;
  };
  if (face == GL_FRONT || face == GL_FRONT_AND_BACK)
    apply(&context->state.stencil.front);
  if (face == GL_BACK || face == GL_FRONT_AND_BACK)
    apply(&context->state.stencil.back);
  if (face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK)
    context->SetError(GL_INVALID_ENUM);
}

void glBlendFunc(GLenum source, GLenum destination) {
  glBlendFuncSeparate(source, destination, source, destination);
}

static bool ValidBlendFactor(GLenum factor) {
  switch (factor) {
    case GL_ZERO:
    case GL_ONE:
    case GL_SRC_COLOR:
    case GL_ONE_MINUS_SRC_COLOR:
    case GL_DST_COLOR:
    case GL_ONE_MINUS_DST_COLOR:
    case GL_SRC_ALPHA:
    case GL_ONE_MINUS_SRC_ALPHA:
    case GL_DST_ALPHA:
    case GL_ONE_MINUS_DST_ALPHA:
    case GL_CONSTANT_COLOR:
    case GL_ONE_MINUS_CONSTANT_COLOR:
    case GL_CONSTANT_ALPHA:
    case GL_ONE_MINUS_CONSTANT_ALPHA:
    case GL_SRC_ALPHA_SATURATE:
      return true;
    default:
      return false;
  }
}

static bool ValidBlendEquation(GLenum equation) {
  return equation == GL_FUNC_ADD || equation == GL_FUNC_SUBTRACT ||
         equation == GL_FUNC_REVERSE_SUBTRACT;
}

void glBlendFuncSeparate(GLenum source_rgb,
                         GLenum destination_rgb,
                         GLenum source_alpha,
                         GLenum destination_alpha) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (!ValidBlendFactor(source_rgb) || !ValidBlendFactor(destination_rgb) ||
      !ValidBlendFactor(source_alpha) || !ValidBlendFactor(destination_alpha)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  zgl::BlendState& blend = context->state.blend;
  blend.src_rgb = source_rgb;
  blend.dst_rgb = destination_rgb;
  blend.src_alpha = source_alpha;
  blend.dst_alpha = destination_alpha;
}

void glBlendEquation(GLenum mode) {
  glBlendEquationSeparate(mode, mode);
}

void glBlendEquationSeparate(GLenum mode_rgb, GLenum mode_alpha) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (!ValidBlendEquation(mode_rgb) || !ValidBlendEquation(mode_alpha)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  context->state.blend.equation_rgb = mode_rgb;
  context->state.blend.equation_alpha = mode_alpha;
}

void glBlendColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  context->state.blend.color[0] = red;
  context->state.blend.color[1] = green;
  context->state.blend.color[2] = blue;
  context->state.blend.color[3] = alpha;
}

void glCullFace(GLenum mode) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (mode != GL_FRONT && mode != GL_BACK && mode != GL_FRONT_AND_BACK) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  context->state.cull.mode = mode;
}

void glFrontFace(GLenum mode) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (mode != GL_CW && mode != GL_CCW) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  context->state.cull.front_face = mode;
}

void glPolygonOffset(GLfloat factor, GLfloat units) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  context->state.polygon_offset_factor = factor;
  context->state.polygon_offset_units = units;
}

void glLineWidth(GLfloat width) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (!(width > 0.0f)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  context->state.line_width = width;
}

void glSampleCoverage(GLfloat value, GLboolean invert) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  context->state.sample_coverage_value = value;
  context->state.invert_coverage = invert != GL_FALSE;
}

void glHint(GLenum target, GLenum mode) {
  (void)mode;
  switch (target) {
    case GL_GENERATE_MIPMAP_HINT:
    case GL_FRAGMENT_SHADER_DERIVATIVE_HINT:
      return;
    default: {
      zgl::Context* context = zgl::ApiContext();
      if (context)
        context->SetError(GL_INVALID_ENUM);
      return;
    }
  }
}

void glPixelStorei(GLenum pname, GLint param) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  zgl::PixelStoreState& store = context->state.pixel_store;
  switch (pname) {
    case GL_PACK_ROW_LENGTH:
    case GL_UNPACK_ROW_LENGTH:
    case GL_PACK_SKIP_ROWS:
    case GL_UNPACK_SKIP_ROWS:
    case GL_PACK_SKIP_PIXELS:
    case GL_UNPACK_SKIP_PIXELS:
    case GL_UNPACK_IMAGE_HEIGHT:
    case GL_UNPACK_SKIP_IMAGES:
      if (param < 0) {
        context->SetError(GL_INVALID_VALUE);
        return;
      }
      break;
    default:
      break;
  }
  switch (pname) {
    case GL_PACK_ALIGNMENT:
      if (param != 1 && param != 2 && param != 4 && param != 8) {
        context->SetError(GL_INVALID_VALUE);
        return;
      }
      store.pack_alignment = param;
      return;
    case GL_UNPACK_ALIGNMENT:
      if (param != 1 && param != 2 && param != 4 && param != 8) {
        context->SetError(GL_INVALID_VALUE);
        return;
      }
      store.unpack_alignment = param;
      return;
    case GL_PACK_ROW_LENGTH:
      store.pack_row_length = param < 0 ? 0 : param;
      return;
    case GL_UNPACK_ROW_LENGTH:
      store.unpack_row_length = param < 0 ? 0 : param;
      return;
    case GL_PACK_SKIP_ROWS:
      store.pack_skip_rows = param < 0 ? 0 : param;
      return;
    case GL_UNPACK_SKIP_ROWS:
      store.unpack_skip_rows = param < 0 ? 0 : param;
      return;
    case GL_PACK_SKIP_PIXELS:
      store.pack_skip_pixels = param < 0 ? 0 : param;
      return;
    case GL_UNPACK_SKIP_PIXELS:
      store.unpack_skip_pixels = param < 0 ? 0 : param;
      return;
    case GL_UNPACK_IMAGE_HEIGHT:
      store.unpack_image_height = param < 0 ? 0 : param;
      return;
    case GL_UNPACK_SKIP_IMAGES:
      store.unpack_skip_images = param < 0 ? 0 : param;
      return;
    default:
      context->SetError(GL_INVALID_ENUM);
      return;
  }
}

void glGetShaderPrecisionFormat(GLenum shader_type,
                                GLenum precision_type,
                                GLint* range,
                                GLint* precision) {
  (void)shader_type;
  if (precision_type != GL_LOW_FLOAT && precision_type != GL_MEDIUM_FLOAT &&
      precision_type != GL_HIGH_FLOAT && precision_type != GL_LOW_INT &&
      precision_type != GL_MEDIUM_INT && precision_type != GL_HIGH_INT) {
    zgl::Context* context = zgl::ApiContext();
    if (context)
      context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (range) {
    range[0] = 127;
    range[1] = 127;
  }
  if (precision)
    precision[0] = 23;
}

void glFinish(void) {}

void glFlush(void) {}

// ---- Queries --------------------------------------------------------------

void glGenQueries(GLsizei n, GLuint* ids) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !ids)
    return;
  if (n < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  for (GLsizei i = 0; i < n; ++i)
    ids[i] = context->queries.Create();
}

void glDeleteQueries(GLsizei n, const GLuint* ids) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !ids)
    return;
  if (n < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  for (GLsizei i = 0; i < n; ++i)
    context->queries.Delete(ids[i]);
}

GLboolean glIsQuery(GLuint id) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || id == 0)
    return GL_FALSE;
  return context->queries.Get(id) ? GL_TRUE : GL_FALSE;
}

void glBeginQuery(GLenum target, GLuint id) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (target != GL_ANY_SAMPLES_PASSED &&
      target != GL_ANY_SAMPLES_PASSED_CONSERVATIVE &&
      target != GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  zgl::Query* query = context->queries.Get(id);
  if (!query) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (query->active) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (target == GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN) {
    zgl::TransformFeedback* feedback = zgl::CurrentTransformFeedback(context);
    if (!feedback || !feedback->active || feedback->paused) {
      context->SetError(GL_INVALID_OPERATION);
      return;
    }
    query->start_primitives = feedback->primitives_written;
  }
  query->target = target;
  query->active = true;
  query->start_fragments = context->stats.fragments_written;
  query->result = 0;
}

void glEndQuery(GLenum target) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (target == GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN) {
    zgl::TransformFeedback* feedback = zgl::CurrentTransformFeedback(context);
    if (!feedback || !feedback->active) {
      context->SetError(GL_INVALID_OPERATION);
      return;
    }
    for (int name = 1; name <= context->queries.size(); ++name) {
      zgl::Query* query = context->queries.Get(static_cast<GLuint>(name));
      if (query && query->active && query->target == target) {
        query->result = feedback->primitives_written - query->start_primitives;
        query->active = false;
        return;
      }
    }
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  for (int name = 1; name <= context->queries.size(); ++name) {
    zgl::Query* query = context->queries.Get(static_cast<GLuint>(name));
    if (query && query->active && query->target == target) {
      query->result =
          context->stats.fragments_written > query->start_fragments ? 1u : 0u;
      query->active = false;
      return;
    }
  }
  context->SetError(GL_INVALID_OPERATION);
}

void glGetQueryiv(GLenum target, GLenum pname, GLint* params) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !params)
    return;
  if (pname == GL_CURRENT_QUERY) {
    params[0] = 0;
    for (int name = 1; name <= context->queries.size(); ++name) {
      const zgl::Query* query = context->queries.Get(static_cast<GLuint>(name));
      if (query && query->active && query->target == target) {
        params[0] = name;
        break;
      }
    }
    return;
  }
  context->SetError(GL_INVALID_ENUM);
}

void glGetQueryObjectuiv(GLuint id, GLenum pname, GLuint* params) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !params)
    return;
  const zgl::Query* query = context->queries.Get(id);
  if (!query) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  switch (pname) {
    case GL_QUERY_RESULT:
      params[0] = query->result;
      return;
    case GL_QUERY_RESULT_AVAILABLE:
      params[0] = query->active ? 0u : 1u;
      return;
    default:
      context->SetError(GL_INVALID_ENUM);
      return;
  }
}

// ---- Sync objects ---------------------------------------------------------

namespace {
std::vector<zgl::SyncObject*> g_sync_objects;
}  // namespace

GLsync glFenceSync(GLenum condition, GLbitfield flags) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return nullptr;
  if (condition != GL_SYNC_GPU_COMMANDS_COMPLETE) {
    context->SetError(GL_INVALID_ENUM);
    return nullptr;
  }
  if (flags != 0) {
    context->SetError(GL_INVALID_VALUE);
    return nullptr;
  }
  zgl::SyncObject* sync = new zgl::SyncObject();
  g_sync_objects.push_back(sync);
  return reinterpret_cast<GLsync>(sync);
}

GLboolean glIsSync(GLsync sync) {
  for (const zgl::SyncObject* object : g_sync_objects) {
    if (reinterpret_cast<GLsync>(const_cast<zgl::SyncObject*>(object)) == sync)
      return GL_TRUE;
  }
  return GL_FALSE;
}

void glDeleteSync(GLsync sync) {
  for (size_t i = 0; i < g_sync_objects.size(); ++i) {
    if (reinterpret_cast<GLsync>(g_sync_objects[i]) == sync) {
      delete g_sync_objects[i];
      g_sync_objects.erase(g_sync_objects.begin() + i);
      return;
    }
  }
}

GLenum glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
  (void)timeout;
  zgl::Context* context = zgl::ApiContext();
  if (!glIsSync(sync)) {
    if (context)
      context->SetError(GL_INVALID_VALUE);
    return GL_WAIT_FAILED;
  }
  if ((flags & ~GL_SYNC_FLUSH_COMMANDS_BIT) != 0) {
    if (context)
      context->SetError(GL_INVALID_VALUE);
    return GL_WAIT_FAILED;
  }
  // Every command that the CPU backend issues has already retired, so a fence
  // is signalled as soon as it is created.
  return GL_ALREADY_SIGNALED;
}

void glWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
  (void)timeout;
  zgl::Context* context = zgl::ApiContext();
  if (!glIsSync(sync)) {
    if (context)
      context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (flags != 0 || timeout != GL_TIMEOUT_IGNORED) {
    if (context)
      context->SetError(GL_INVALID_VALUE);
  }
}

void glGetSynciv(GLsync sync,
                 GLenum pname,
                 GLsizei buf_size,
                 GLsizei* length,
                 GLint* values) {
  (void)buf_size;
  if (!glIsSync(sync)) {
    zgl::Context* context = zgl::ApiContext();
    if (context)
      context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (length)
    *length = 1;
  if (!values)
    return;
  switch (pname) {
    case GL_OBJECT_TYPE:
      values[0] = GL_SYNC_FENCE;
      return;
    case GL_SYNC_STATUS:
      values[0] = GL_SIGNALED;
      return;
    case GL_SYNC_CONDITION:
      values[0] = GL_SYNC_GPU_COMMANDS_COMPLETE;
      return;
    case GL_SYNC_FLAGS:
      values[0] = 0;
      return;
    default: {
      zgl::Context* context = zgl::ApiContext();
      if (context)
        context->SetError(GL_INVALID_ENUM);
      return;
    }
  }
}

}  // extern "C"
