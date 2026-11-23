// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/zgl_draw.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "src/glsl/glsl_program.h"
#include "src/zgl_context.h"
#include "src/zgl_raster.h"
#include "src/zgl_texture.h"

namespace zgl {

namespace {

// Number of floats before the varying block in a transformed vertex.
constexpr int kFloatPerPosition = 4;

// Clip space is the intersection of six half spaces.  Clipping a convex
// polygon against each plane can add at most one vertex, so a clipped
// triangle never exceeds nine vertices.
constexpr int kMaxClippedVertices = 9;

// Vertices kept in the transform cache.  Four entries let indexed meshes
// reuse most of a triangle without thrashing.
constexpr int kVertexCacheSize = 4;

int TypeSize(GLenum type) {
  switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
      return 1;
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
    case GL_HALF_FLOAT:
      return 2;
    case GL_INT:
    case GL_UNSIGNED_INT:
    case GL_FLOAT:
    case GL_FIXED:
      return 4;
    default:
      return 0;
  }
}

bool IsIntegerType(GLenum type) {
  return type == GL_BYTE || type == GL_UNSIGNED_BYTE || type == GL_SHORT ||
         type == GL_UNSIGNED_SHORT || type == GL_INT || type == GL_UNSIGNED_INT;
}

float NormalizeComponent(GLenum type, int raw) {
  switch (type) {
    case GL_BYTE:
      return std::max(-1.0f, static_cast<float>(raw) / 127.0f);
    case GL_UNSIGNED_BYTE:
      return static_cast<float>(raw) / 255.0f;
    case GL_SHORT:
      return std::max(-1.0f, static_cast<float>(raw) / 32767.0f);
    case GL_UNSIGNED_SHORT:
      return static_cast<float>(raw) / 65535.0f;
    case GL_INT:
      return static_cast<float>(raw) * (1.0f / 2147483647.0f);
    case GL_UNSIGNED_INT:
      return static_cast<float>(raw) * (1.0f / 4294967295.0f);
    default:
      return static_cast<float>(raw);
  }
}

float ReadHalfFloat(const uint8_t* data) {
  uint16_t bits;
  memcpy(&bits, data, sizeof(bits));
  const uint32_t sign = (bits & 0x8000u) << 16;
  const uint32_t exponent = (bits >> 10) & 0x1fu;
  const uint32_t mantissa = bits & 0x3ffu;
  uint32_t out;
  if (exponent == 0) {
    out = sign;
  } else if (exponent == 31) {
    out = sign | 0x7f800000u | (mantissa << 13);
  } else {
    out = sign | ((exponent + 112u) << 23) | (mantissa << 13);
  }
  float value;
  memcpy(&value, &out, sizeof(value));
  return value;
}

// Reads one attribute component as a float.  |data| points at the first
// component of the element.
float ReadAttributeComponent(const uint8_t* data, int component, GLenum type) {
  switch (type) {
    case GL_FLOAT: {
      float value;
      memcpy(&value, data + component * 4, sizeof(value));
      return value;
    }
    case GL_HALF_FLOAT:
      return ReadHalfFloat(data + component * 2);
    case GL_FIXED: {
      int32_t value;
      memcpy(&value, data + component * 4, sizeof(value));
      return static_cast<float>(value) / 65536.0f;
    }
    case GL_BYTE: {
      const int8_t value = static_cast<int8_t>(data[component]);
      return static_cast<float>(value);
    }
    case GL_UNSIGNED_BYTE:
      return static_cast<float>(data[component]);
    case GL_SHORT: {
      int16_t value;
      memcpy(&value, data + component * 2, sizeof(value));
      return static_cast<float>(value);
    }
    case GL_UNSIGNED_SHORT: {
      uint16_t value;
      memcpy(&value, data + component * 2, sizeof(value));
      return static_cast<float>(value);
    }
    case GL_INT: {
      int32_t value;
      memcpy(&value, data + component * 4, sizeof(value));
      return static_cast<float>(value);
    }
    case GL_UNSIGNED_INT: {
      uint32_t value;
      memcpy(&value, data + component * 4, sizeof(value));
      return static_cast<float>(value);
    }
    default:
      return 0.0f;
  }
}

float ReadNormalizedComponent(const uint8_t* data, int component, GLenum type) {
  if (TypeSize(type) == 1) {
    const uint8_t raw = data[component];
    return NormalizeComponent(type, type == GL_BYTE ? static_cast<int8_t>(raw)
                                                    : static_cast<int>(raw));
  }
  if (TypeSize(type) == 2) {
    uint16_t raw16;
    memcpy(&raw16, data + component * 2, sizeof(raw16));
    const int raw = type == GL_SHORT ? static_cast<int16_t>(raw16)
                                     : static_cast<int>(raw16);
    return NormalizeComponent(type, raw);
  }
  int32_t raw32;
  memcpy(&raw32, data + component * 4, sizeof(raw32));
  return NormalizeComponent(type, raw32);
}

uint16_t ToDepth16(float depth) {
  return static_cast<uint16_t>(Clamp01(depth) * 65535.0f + 0.5f);
}

bool DepthTestPasses(GLenum func, float incoming, float stored) {
  switch (func) {
    case GL_NEVER:
      return false;
    case GL_LESS:
      return incoming < stored;
    case GL_EQUAL:
      return incoming == stored;
    case GL_LEQUAL:
      return incoming <= stored;
    case GL_GREATER:
      return incoming > stored;
    case GL_NOTEQUAL:
      return incoming != stored;
    case GL_GEQUAL:
      return incoming >= stored;
    case GL_ALWAYS:
      return true;
    default:
      return false;
  }
}

bool StencilTestPasses(GLenum func, uint8_t reference, uint8_t stored) {
  switch (func) {
    case GL_NEVER:
      return false;
    case GL_LESS:
      return reference < stored;
    case GL_EQUAL:
      return reference == stored;
    case GL_LEQUAL:
      return reference <= stored;
    case GL_GREATER:
      return reference > stored;
    case GL_NOTEQUAL:
      return reference != stored;
    case GL_GEQUAL:
      return reference >= stored;
    case GL_ALWAYS:
      return true;
    default:
      return false;
  }
}

uint8_t StencilOp(GLenum op, uint8_t current, uint8_t reference) {
  switch (op) {
    case GL_KEEP:
      return current;
    case GL_ZERO:
      return 0;
    case GL_REPLACE:
      return reference;
    case GL_INCR:
      return current == 255 ? 255 : static_cast<uint8_t>(current + 1);
    case GL_DECR:
      return current == 0 ? 0 : static_cast<uint8_t>(current - 1);
    case GL_INVERT:
      return static_cast<uint8_t>(~current);
    case GL_INCR_WRAP:
      return static_cast<uint8_t>(current + 1);
    case GL_DECR_WRAP:
      return static_cast<uint8_t>(current - 1);
    default:
      return current;
  }
}

float BlendFactorValue(GLenum factor,
                       const float src[4],
                       const float dst[4],
                       const float constant[4],
                       int channel) {
  switch (factor) {
    case GL_ZERO:
      return 0.0f;
    case GL_ONE:
      return 1.0f;
    case GL_SRC_COLOR:
      return src[channel];
    case GL_ONE_MINUS_SRC_COLOR:
      return 1.0f - src[channel];
    case GL_DST_COLOR:
      return dst[channel];
    case GL_ONE_MINUS_DST_COLOR:
      return 1.0f - dst[channel];
    case GL_SRC_ALPHA:
      return src[3];
    case GL_ONE_MINUS_SRC_ALPHA:
      return 1.0f - src[3];
    case GL_DST_ALPHA:
      return dst[3];
    case GL_ONE_MINUS_DST_ALPHA:
      return 1.0f - dst[3];
    case GL_CONSTANT_COLOR:
      return constant[channel];
    case GL_ONE_MINUS_CONSTANT_COLOR:
      return 1.0f - constant[channel];
    case GL_CONSTANT_ALPHA:
      return constant[3];
    case GL_ONE_MINUS_CONSTANT_ALPHA:
      return 1.0f - constant[3];
    case GL_SRC_ALPHA_SATURATE:
      return channel == 3 ? 1.0f : std::min(src[3], 1.0f - dst[3]);
    default:
      return 0.0f;
  }
}

float BlendCombine(GLenum equation, float src, float dst, float sf, float df) {
  switch (equation) {
    case GL_FUNC_SUBTRACT:
      return src * sf - dst * df;
    case GL_FUNC_REVERSE_SUBTRACT:
      return dst * df - src * sf;
    case GL_MIN:
      return std::min(src, dst);
    case GL_MAX:
      return std::max(src, dst);
    case GL_FUNC_ADD:
    default:
      return src * sf + dst * df;
  }
}

GLuint ReadIndex(const void* indices, GLenum type, size_t index) {
  switch (type) {
    case GL_UNSIGNED_BYTE:
      return static_cast<const uint8_t*>(indices)[index];
    case GL_UNSIGNED_SHORT:
      return static_cast<const uint16_t*>(indices)[index];
    case GL_UNSIGNED_INT:
    default:
      return static_cast<const uint32_t*>(indices)[index];
  }
}

// GLES 3.0 defines GL_PRIMITIVE_RESTART_FIXED_INDEX in terms of the largest
// value representable by the index type rather than a separate state value.
GLuint MaxIndexValue(GLenum type) {
  switch (type) {
    case GL_UNSIGNED_BYTE:
      return 0xffu;
    case GL_UNSIGNED_SHORT:
      return 0xffffu;
    default:
      return 0xffffffffu;
  }
}

// Distance of a clip space vertex from a plane.  The polygon interior is
// where the distance is non-negative.
float PlaneDistance(const float* position, int plane) {
  const float w = position[3];
  switch (plane) {
    case 0:
      return w - position[0];
    case 1:
      return w + position[0];
    case 2:
      return w - position[1];
    case 3:
      return w + position[1];
    case 4:
      return w - position[2];
    case 5:
    default:
      return w + position[2];
  }
}

bool PrimitiveModeCompatible(GLenum draw_mode, GLenum capture_mode) {
  switch (capture_mode) {
    case GL_POINTS:
      return draw_mode == GL_POINTS;
    case GL_LINES:
      return draw_mode == GL_LINES || draw_mode == GL_LINE_STRIP ||
             draw_mode == GL_LINE_LOOP;
    case GL_TRIANGLES:
      return draw_mode == GL_TRIANGLES || draw_mode == GL_TRIANGLE_STRIP ||
             draw_mode == GL_TRIANGLE_FAN;
    default:
      return false;
  }
}

uint32_t PrimitiveCount(GLenum mode, size_t vertices) {
  switch (mode) {
    case GL_POINTS:
      return static_cast<uint32_t>(vertices);
    case GL_LINES:
      return static_cast<uint32_t>(vertices / 2);
    case GL_LINE_STRIP:
      return vertices > 1 ? static_cast<uint32_t>(vertices - 1) : 0u;
    case GL_LINE_LOOP:
      return vertices > 1 ? static_cast<uint32_t>(vertices) : 0u;
    case GL_TRIANGLES:
      return static_cast<uint32_t>(vertices / 3);
    case GL_TRIANGLE_STRIP:
    case GL_TRIANGLE_FAN:
      return vertices > 2 ? static_cast<uint32_t>(vertices - 2) : 0u;
    default:
      return 0u;
  }
}

class Pipeline : public FragmentSink, public glsl::TextureSampler {
 public:
  explicit Pipeline(Context* context) : context_(context) {}

  ~Pipeline() override = default;

  void Run(GLenum mode,
           GLint first,
           GLsizei count,
           GLenum index_type,
           const void* indices,
           GLsizei instance_count);

  // Averages multisample attachments into their resolved planes.
  void Resolve();

  // FragmentSink implementation.
  void Shade(const Fragment& fragment) override;

  // glsl::TextureSampler implementation.
  void Sample2D(int unit,
                float u,
                float v,
                float lod_bias,
                float out[4]) override;
  void SampleCube(int unit,
                  const float direction[3],
                  float lod_bias,
                  float out[4]) override;
  void Sample3D(int unit,
                const float position[3],
                float lod_bias,
                float out[4]) override;

 private:
  struct TransformedVertex {
    const float* data = nullptr;
    float point_size = 1.0f;
  };

  enum class IndexResult { kOk, kRestart, kEnd };

  struct CachedVertex {
    GLuint index = 0;
    int instance = -1;
    bool valid = false;
  };

  bool Prepare();
  const State& state() const;
  void FetchAttributes(GLuint index, int instance, float* attributes) const;
  TransformedVertex AcquireVertex(GLuint index, int instance);
  void CaptureVertex(GLuint index, int instance);
  void WriteTransformFeedback(const float* varyings);
  void EmitPoint(GLuint index, int instance);
  void EmitLine(GLuint index_a, GLuint index_b, int instance);
  void EmitTriangle(GLuint index_a,
                    GLuint index_b,
                    GLuint index_c,
                    int instance);
  void ProcessTriangle(const float* a, const float* b, const float* c);
  int ClipPolygon(const float* input,
                  int count,
                  int plane,
                  float* output) const;
  void ProjectVertices(const float* polygon, int count);
  void Interpolate(const Fragment& fragment, float* out) const;
  void StoreFragmentColor(size_t pixel,
                          int location,
                          const float color[4],
                          glsl::BaseType base);
  void ApplyStencilOp(GLenum op, size_t pixel, uint8_t reference);

  Context* context_;
  Rasterizer rasterizer_;

  glsl::Program* program_ = nullptr;
  const glsl::Value* uniforms_ = nullptr;
  const VertexArray* vertex_array_ = nullptr;
  RenderTarget target_;

  int stride_ = kFloatPerPosition;
  int varying_floats_ = 0;
  bool empty_clip_ = false;

  std::vector<float> cache_;
  std::vector<CachedVertex> cache_vertices_;
  int next_cache_slot_ = 0;
  float cached_point_sizes_[kVertexCacheSize] = {};

  // Copies of the vertices of the primitive being emitted.  The transformed
  // vertex cache rotates, so a pointer handed out for the first vertex of a
  // triangle may be recycled while later vertices are transformed.
  std::vector<float> emit_[3];

  std::vector<float> clip_;
  std::vector<float> scratch_;
  std::vector<RasterVertex> screen_;
  std::vector<float> varying_values_;
  std::vector<float> tf_varyings_;
  float ndc_x_[kMaxClippedVertices] = {};
  float ndc_y_[kMaxClippedVertices] = {};

  bool current_front_ = true;
  bool polygon_ = false;
  float depth_offset_ = 0.0f;
};

void Pipeline::Run(GLenum mode,
                   GLint first,
                   GLsizei count,
                   GLenum index_type,
                   const void* indices,
                   GLsizei instance_count) {
  if (!Prepare())
    return;
  zgl::TransformFeedback* feedback = zgl::CurrentTransformFeedback(context_);
  const bool capture =
      feedback && feedback->active && !feedback->paused;
  if (capture && !PrimitiveModeCompatible(mode, feedback->primitive_mode)) {
    context_->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (empty_clip_ && !capture)
    return;
  if (instance_count <= 0)
    return;
  if (state().rasterizer_discard && !capture)
    return;

  context_->stats.draw_calls++;
  context_->stats.vertices_submitted +=
      static_cast<uint32_t>(count) * static_cast<uint32_t>(instance_count);

  const bool indexed = indices != nullptr;
  const bool restart_enabled = indexed && state().primitive_restart;
  const GLuint restart_index = restart_enabled
                                   ? (state().primitive_restart_fixed_index
                                          ? MaxIndexValue(index_type)
                                          : state().primitive_restart_index)
                                   : 0;
  size_t cursor = 0;

  const auto next_index = [&](GLuint* out) -> IndexResult {
    if (cursor >= static_cast<size_t>(count))
      return IndexResult::kEnd;
    GLuint value;
    if (indexed) {
      value = ReadIndex(indices, index_type, cursor);
    } else {
      value = static_cast<GLuint>(first + static_cast<GLint>(cursor));
    }
    ++cursor;
    if (restart_enabled && value == restart_index)
      return IndexResult::kRestart;
    *out = value;
    return IndexResult::kOk;
  };

  if (capture) {
    for (int instance = 0; instance < instance_count; ++instance) {
      cursor = 0;
      GLuint index;
      while (next_index(&index) == IndexResult::kOk)
        CaptureVertex(index, instance);
    }
    feedback->primitives_written +=
        PrimitiveCount(mode, static_cast<size_t>(count)) *
        static_cast<uint32_t>(instance_count);
    cursor = 0;
    if (state().rasterizer_discard || empty_clip_)
      return;
  }

  for (int instance = 0; instance < instance_count; ++instance) {
    cursor = 0;
    switch (mode) {
      case GL_POINTS: {
        GLuint index;
        while (next_index(&index) == IndexResult::kOk)
          EmitPoint(index, instance);
        break;
      }
      case GL_LINES: {
        GLuint a;
        GLuint b;
        while (next_index(&a) == IndexResult::kOk) {
          if (next_index(&b) != IndexResult::kOk)
            break;
          EmitLine(a, b, instance);
        }
        break;
      }
      case GL_LINE_STRIP: {
        GLuint previous;
        GLuint current;
        bool have_previous = false;
        while (true) {
          const IndexResult result = next_index(&current);
          if (result == IndexResult::kEnd)
            break;
          if (result == IndexResult::kRestart) {
            have_previous = false;
            continue;
          }
          if (have_previous)
            EmitLine(previous, current, instance);
          previous = current;
          have_previous = true;
        }
        break;
      }
      case GL_LINE_LOOP: {
        std::vector<GLuint> loop;
        GLuint current;
        while (true) {
          const IndexResult result = next_index(&current);
          if (result == IndexResult::kEnd || result == IndexResult::kRestart) {
            if (loop.size() >= 2) {
              for (size_t i = 0; i + 1 < loop.size(); ++i)
                EmitLine(loop[i], loop[i + 1], instance);
              EmitLine(loop.back(), loop.front(), instance);
            }
            loop.clear();
            if (result == IndexResult::kEnd)
              break;
            continue;
          }
          loop.push_back(current);
        }
        break;
      }
      case GL_TRIANGLES: {
        GLuint a;
        GLuint b;
        GLuint c;
        while (next_index(&a) == IndexResult::kOk) {
          if (next_index(&b) != IndexResult::kOk)
            break;
          if (next_index(&c) != IndexResult::kOk)
            break;
          EmitTriangle(a, b, c, instance);
        }
        break;
      }
      case GL_TRIANGLE_STRIP: {
        GLuint pending[2] = {0, 0};
        int have = 0;
        int parity = 0;
        GLuint current;
        while (true) {
          const IndexResult result = next_index(&current);
          if (result == IndexResult::kEnd)
            break;
          if (result == IndexResult::kRestart) {
            have = 0;
            parity = 0;
            continue;
          }
          if (have < 2) {
            pending[have++] = current;
            continue;
          }
          if (parity == 0)
            EmitTriangle(pending[0], pending[1], current, instance);
          else
            EmitTriangle(pending[1], pending[0], current, instance);
          pending[0] = pending[1];
          pending[1] = current;
          parity ^= 1;
        }
        break;
      }
      case GL_TRIANGLE_FAN: {
        GLuint origin;
        GLuint pending;
        bool have_origin = false;
        bool have_pending = false;
        GLuint current;
        while (true) {
          const IndexResult result = next_index(&current);
          if (result == IndexResult::kEnd)
            break;
          if (result == IndexResult::kRestart) {
            have_origin = false;
            have_pending = false;
            continue;
          }
          if (!have_origin) {
            origin = current;
            have_origin = true;
            continue;
          }
          if (!have_pending) {
            pending = current;
            have_pending = true;
            continue;
          }
          EmitTriangle(origin, pending, current, instance);
          pending = current;
        }
        break;
      }
      default:
        context_->SetError(GL_INVALID_ENUM);
        return;
    }
  }
}

const State& Pipeline::state() const {
  return context_->state;
}

bool Pipeline::Prepare() {
  State& state = context_->state;
  ProgramObject* object = context_->programs.Get(state.program);
  if (!object || !object->linked || !object->program) {
    context_->SetError(GL_INVALID_OPERATION);
    return false;
  }
  program_ = object->program.get();
  target_ = context_->GetDrawTarget();
  if (!target_.valid()) {
    context_->SetError(GL_INVALID_FRAMEBUFFER_OPERATION);
    return false;
  }
  vertex_array_ = state.vertex_array
                      ? context_->vertex_arrays.Get(state.vertex_array)
                      : &context_->default_vertex_array;
  if (!vertex_array_) {
    context_->SetError(GL_INVALID_OPERATION);
    return false;
  }
  uniforms_ = program_->uniform_value(0);
  for (size_t i = 0; i < program_->uniform_blocks().size(); ++i) {
    const int binding_point = program_->uniform_blocks()[i].binding;
    if (binding_point < 0 || binding_point >= kMaxUniformBufferBindings)
      continue;
    const IndexedBufferBinding& binding =
        context_->state.uniform_buffers[binding_point];
    if (binding.buffer == 0)
      continue;
    const Buffer* buffer = context_->buffers.Get(binding.buffer);
    if (!buffer || binding.offset >= buffer->data.size())
      continue;
    size_t available = buffer->data.size() - binding.offset;
    if (binding.size < available)
      available = binding.size;
    program_->UploadUniformBlock(static_cast<int>(i),
                                 buffer->data.data() + binding.offset,
                                 available);
  }
  varying_floats_ = program_->VaryingFloats();
  stride_ = kFloatPerPosition + varying_floats_;
  cache_.assign(static_cast<size_t>(kVertexCacheSize) * stride_, 0.0f);
  cache_vertices_.assign(kVertexCacheSize, CachedVertex());
  next_cache_slot_ = 0;
  for (std::vector<float>& vertices : emit_)
    vertices.assign(static_cast<size_t>(stride_), 0.0f);
  clip_.assign(static_cast<size_t>(kMaxClippedVertices) * stride_, 0.0f);
  scratch_.assign(static_cast<size_t>(kMaxClippedVertices + 1) * stride_, 0.0f);
  screen_.assign(kMaxClippedVertices, RasterVertex());
  varying_values_.assign(varying_floats_, 0.0f);
  tf_varyings_.assign(varying_floats_, 0.0f);

  Rect viewport = state.viewport;
  Rect clip{viewport.x, target_.height - viewport.bottom(), viewport.width,
            viewport.height};
  if (state.scissor_test) {
    const Rect scissor{state.scissor.x, target_.height - state.scissor.bottom(),
                       state.scissor.width, state.scissor.height};
    clip = clip.Intersect(scissor);
  }
  empty_clip_ = clip.IsEmpty();
  rasterizer_.SetClip(clip);
  static const float kSamplePositions4[4][2] = {
      {-0.375f, -0.125f}, {0.125f, -0.375f},
      {0.375f, 0.125f},   {-0.125f, 0.375f}};
  static const float kSamplePositions2[2][2] = {
      {-0.375f, -0.125f}, {0.375f, 0.125f}};
  if (target_.samples >= 4)
    rasterizer_.SetSamples(4, kSamplePositions4);
  else if (target_.samples == 2)
    rasterizer_.SetSamples(2, kSamplePositions2);
  else
    rasterizer_.SetSamples(1, nullptr);
  return true;
}

void Pipeline::FetchAttributes(GLuint index,
                               int instance,
                               float* attributes) const {
  for (int i = 0; i < kMaxVertexAttribs; ++i) {
    float* destination = attributes + i * 4;
    const VertexAttribute& attribute = vertex_array_->attributes[i];
    if (!attribute.enabled) {
      for (int c = 0; c < 4; ++c)
        destination[c] = context_->state.current_attribute[i][c];
      continue;
    }
    destination[0] = 0.0f;
    destination[1] = 0.0f;
    destination[2] = 0.0f;
    destination[3] = 1.0f;
    const int divisor = attribute.divisor;
    const size_t element =
        divisor > 0 ? static_cast<size_t>(instance) / divisor : index;
    const int components = ClampInt(attribute.size, 1, 4);
    const int element_size = TypeSize(attribute.type) * components;
    if (element_size == 0)
      continue;
    const int stride = attribute.stride > 0 ? attribute.stride : element_size;
    const uint8_t* base = nullptr;
    if (attribute.buffer != 0) {
      const Buffer* buffer = context_->buffers.Get(attribute.buffer);
      if (!buffer)
        continue;
      const size_t offset = static_cast<size_t>(attribute.offset) +
                            element * static_cast<size_t>(stride);
      if (offset + static_cast<size_t>(element_size) > buffer->data.size())
        continue;
      base = buffer->data.data() + offset;
    } else if (attribute.client_pointer) {
      base = attribute.client_pointer + attribute.offset +
             element * static_cast<size_t>(stride);
    } else {
      continue;
    }
    for (int component = 0; component < components; ++component) {
      destination[component] =
          attribute.normalized && IsIntegerType(attribute.type)
              ? ReadNormalizedComponent(base, component, attribute.type)
              : ReadAttributeComponent(base, component, attribute.type);
    }
  }
}

Pipeline::TransformedVertex Pipeline::AcquireVertex(GLuint index,
                                                    int instance) {
  for (int slot = 0; slot < kVertexCacheSize; ++slot) {
    if (cache_vertices_[slot].valid && cache_vertices_[slot].index == index &&
        cache_vertices_[slot].instance == instance) {
      TransformedVertex vertex;
      vertex.data = cache_.data() + static_cast<size_t>(slot) * stride_;
      vertex.point_size = cached_point_sizes_[slot];
      return vertex;
    }
  }
  const int slot = next_cache_slot_;
  next_cache_slot_ = (next_cache_slot_ + 1) % kVertexCacheSize;
  float* destination = cache_.data() + static_cast<size_t>(slot) * stride_;
  float attributes[kMaxVertexAttribs * 4];
  FetchAttributes(index, instance, attributes);
  glsl::VertexInput input;
  input.attributes = attributes;
  input.vertex_id = static_cast<int>(index);
  input.instance_id = instance;
  input.uniforms = uniforms_;
  float position[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float point_size = 1.0f;
  program_->RunVertex(input, position, &point_size, destination + 4);
  memcpy(destination, position, sizeof(position));
  if (program_->has_runtime_error()) {
    context_->SetError(GL_INVALID_OPERATION);
    context_->diagnostics = program_->runtime_error();
  }
  cached_point_sizes_[slot] = point_size;
  cache_vertices_[slot].valid = true;
  cache_vertices_[slot].index = index;
  cache_vertices_[slot].instance = instance;
  TransformedVertex vertex;
  vertex.data = destination;
  vertex.point_size = point_size;
  return vertex;
}

void Pipeline::CaptureVertex(GLuint index, int instance) {
  float attributes[kMaxVertexAttribs * 4];
  FetchAttributes(index, instance, attributes);
  glsl::VertexInput input;
  input.attributes = attributes;
  input.vertex_id = static_cast<int>(index);
  input.instance_id = instance;
  input.uniforms = uniforms_;
  float position[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float point_size = 1.0f;
  program_->RunVertex(input, position, &point_size, tf_varyings_.data());
  if (program_->has_runtime_error()) {
    context_->SetError(GL_INVALID_OPERATION);
    context_->diagnostics = program_->runtime_error();
    return;
  }
  WriteTransformFeedback(tf_varyings_.data());
}

void Pipeline::WriteTransformFeedback(const float* varyings) {
  zgl::TransformFeedback* feedback = zgl::CurrentTransformFeedback(context_);
  if (!feedback || !feedback->active || feedback->paused)
    return;
  const auto& outputs = program_->transform_feedback_varyings();
  if (outputs.empty())
    return;
  const bool separate = program_->transform_feedback_separate();
  size_t interleaved_stride = 0;
  for (const auto& output : outputs) {
    interleaved_stride +=
        static_cast<size_t>(output.components) * sizeof(float);
  }

  size_t record_offset = 0;
  for (const auto& output : outputs) {
    const int buffer_index = separate ? output.buffer_index : 0;
    const size_t bytes = static_cast<size_t>(output.components) * sizeof(float);
    const size_t start_offset = record_offset;
    if (!separate)
      record_offset += bytes;
    if (buffer_index < 0 ||
        buffer_index >= zgl::kMaxTransformFeedbackBuffers) {
      continue;
    }
    const zgl::IndexedBufferBinding& binding = feedback->buffers[buffer_index];
    zgl::Buffer* buffer = context_->buffers.Get(binding.buffer);
    if (!buffer)
      continue;
    size_t cursor = feedback->cursors[buffer_index];
    const size_t start = binding.offset + cursor +
                         (separate ? 0 : start_offset);
    size_t end = buffer->data.size();
    if (binding.size > 0)
      end = std::min(end, binding.offset + binding.size);
    if (start <= end && bytes <= end - start)
      memcpy(buffer->data.data() + start, varyings + output.offset, bytes);
  }

  if (separate) {
    for (const auto& output : outputs) {
      const int buffer_index = output.buffer_index;
      if (buffer_index >= 0 &&
          buffer_index < zgl::kMaxTransformFeedbackBuffers) {
        feedback->cursors[buffer_index] +=
            static_cast<size_t>(output.components) * sizeof(float);
      }
    }
  } else {
    feedback->cursors[0] += interleaved_stride;
  }
  ++feedback->vertex_count;
}

void Pipeline::EmitPoint(GLuint index, int instance) {
  TransformedVertex vertex = AcquireVertex(index, instance);
  const float w = vertex.data[3];
  if (!(w > 0.0f))
    return;
  const State& current = state();
  const float ndc_x = vertex.data[0] / w;
  const float ndc_y = vertex.data[1] / w;
  const float ndc_z = vertex.data[2] / w;
  float size = vertex.point_size;
  if (!(size >= 1.0f))
    size = 1.0f;
  RasterVertex screen;
  screen.x = static_cast<float>(current.viewport.x) +
             (ndc_x * 0.5f + 0.5f) * static_cast<float>(current.viewport.width);
  screen.y =
      static_cast<float>(target_.height - current.viewport.bottom()) +
      (0.5f - ndc_y * 0.5f) * static_cast<float>(current.viewport.height);
  screen.z = current.depth.range_near +
             (ndc_z * 0.5f + 0.5f) *
                 (current.depth.range_far - current.depth.range_near);
  screen.w = w;
  memcpy(clip_.data(), vertex.data, sizeof(float) * stride_);
  current_front_ = true;
  polygon_ = false;
  depth_offset_ = 0.0f;
  rasterizer_.DrawPoint(screen, 0, size, this);
}

void Pipeline::EmitLine(GLuint index_a, GLuint index_b, int instance) {
  memcpy(emit_[0].data(), AcquireVertex(index_a, instance).data,
         sizeof(float) * stride_);
  memcpy(emit_[1].data(), AcquireVertex(index_b, instance).data,
         sizeof(float) * stride_);
  const float wa = emit_[0][3];
  const float wb = emit_[1][3];
  if (!(wa > 0.0f) || !(wb > 0.0f))
    return;
  const State& current = state();
  float* slot_a = clip_.data();
  float* slot_b = clip_.data() + stride_;
  memcpy(slot_a, emit_[0].data(), sizeof(float) * stride_);
  memcpy(slot_b, emit_[1].data(), sizeof(float) * stride_);
  RasterVertex vertices[2];
  const float* positions[2] = {emit_[0].data(), emit_[1].data()};
  for (int i = 0; i < 2; ++i) {
    const float w = positions[i][3];
    const float ndc_x = positions[i][0] / w;
    const float ndc_y = positions[i][1] / w;
    const float ndc_z = positions[i][2] / w;
    vertices[i].x =
        static_cast<float>(current.viewport.x) +
        (ndc_x * 0.5f + 0.5f) * static_cast<float>(current.viewport.width);
    vertices[i].y =
        static_cast<float>(target_.height - current.viewport.bottom()) +
        (0.5f - ndc_y * 0.5f) * static_cast<float>(current.viewport.height);
    vertices[i].z = current.depth.range_near +
                    (ndc_z * 0.5f + 0.5f) *
                        (current.depth.range_far - current.depth.range_near);
    vertices[i].w = w;
  }
  current_front_ = true;
  polygon_ = false;
  depth_offset_ = 0.0f;
  rasterizer_.DrawLine(vertices[0], vertices[1], 0, 1, this);
}

void Pipeline::EmitTriangle(GLuint index_a,
                            GLuint index_b,
                            GLuint index_c,
                            int instance) {
  memcpy(emit_[0].data(), AcquireVertex(index_a, instance).data,
         sizeof(float) * stride_);
  memcpy(emit_[1].data(), AcquireVertex(index_b, instance).data,
         sizeof(float) * stride_);
  memcpy(emit_[2].data(), AcquireVertex(index_c, instance).data,
         sizeof(float) * stride_);
  ProcessTriangle(emit_[0].data(), emit_[1].data(), emit_[2].data());
}

int Pipeline::ClipPolygon(const float* input,
                          int count,
                          int plane,
                          float* output) const {
  int out_count = 0;
  for (int i = 0; i < count; ++i) {
    const float* current = input + static_cast<size_t>(i) * stride_;
    const float* next = input + static_cast<size_t>((i + 1) % count) * stride_;
    const float distance_current = PlaneDistance(current, plane);
    const float distance_next = PlaneDistance(next, plane);
    if (distance_current >= 0.0f) {
      memcpy(output + static_cast<size_t>(out_count) * stride_, current,
             sizeof(float) * stride_);
      ++out_count;
    }
    if ((distance_current >= 0.0f) != (distance_next >= 0.0f)) {
      const float t = distance_current / (distance_current - distance_next);
      float* destination = output + static_cast<size_t>(out_count) * stride_;
      for (int k = 0; k < stride_; ++k) {
        destination[k] = current[k] + (next[k] - current[k]) * t;
      }
      ++out_count;
    }
  }
  return out_count;
}

void Pipeline::ProjectVertices(const float* polygon, int count) {
  const State& current = state();
  const float viewport_x = static_cast<float>(current.viewport.x);
  const float viewport_height = static_cast<float>(current.viewport.height);
  const float viewport_top =
      static_cast<float>(target_.height - current.viewport.bottom());
  const float depth_near = current.depth.range_near;
  const float depth_span = current.depth.range_far - depth_near;
  for (int i = 0; i < count; ++i) {
    const float* position = polygon + static_cast<size_t>(i) * stride_;
    const float w = position[3];
    if (!(w > 0.0f)) {
      screen_[i].x = 0.0f;
      screen_[i].y = 0.0f;
      continue;
    }
    const float ndc_x = position[0] / w;
    const float ndc_y = position[1] / w;
    const float ndc_z = position[2] / w;
    ndc_x_[i] = ndc_x;
    ndc_y_[i] = ndc_y;
    screen_[i].x = viewport_x + (ndc_x * 0.5f + 0.5f) *
                                    static_cast<float>(current.viewport.width);
    screen_[i].y = viewport_top + (0.5f - ndc_y * 0.5f) * viewport_height;
    screen_[i].z = depth_near + (ndc_z * 0.5f + 0.5f) * depth_span;
    screen_[i].w = w;
  }
}

void Pipeline::ProcessTriangle(const float* a, const float* b, const float* c) {
  const float* sources[3] = {a, b, c};
  for (int i = 0; i < 3; ++i) {
    memcpy(clip_.data() + static_cast<size_t>(i) * stride_, sources[i],
           sizeof(float) * stride_);
  }
  int count = 3;
  const float* polygon = clip_.data();
  float* destination = scratch_.data();
  for (int plane = 0; plane < 6 && count >= 3; ++plane) {
    count = ClipPolygon(polygon, count, plane, destination);
    polygon = destination;
    destination =
        destination == scratch_.data() ? clip_.data() : scratch_.data();
  }
  if (count < 3)
    return;
  if (polygon != clip_.data())
    memcpy(clip_.data(), polygon, sizeof(float) * stride_ * count);

  ProjectVertices(clip_.data(), count);

  const float area = (ndc_x_[1] - ndc_x_[0]) * (ndc_y_[2] - ndc_y_[0]) -
                     (ndc_x_[2] - ndc_x_[0]) * (ndc_y_[1] - ndc_y_[0]);
  const State& current = state();
  const bool ccw = area > 0.0f;
  const bool front = current.cull.front_face == GL_CCW ? ccw : !ccw;
  if (current.cull.enabled) {
    const bool cull_front =
        current.cull.mode == GL_FRONT || current.cull.mode == GL_FRONT_AND_BACK;
    const bool cull_back = current.cull.mode != GL_FRONT;
    if ((cull_front && front) || (cull_back && !front))
      return;
  }
  if (area == 0.0f)
    return;

  polygon_ = true;
  for (int i = 1; i + 1 < count; ++i) {
    const bool fan_ccw =
        ((ndc_x_[i] - ndc_x_[0]) * (ndc_y_[i + 1] - ndc_y_[0]) -
         (ndc_x_[i + 1] - ndc_x_[0]) * (ndc_y_[i] - ndc_y_[0])) > 0.0f;
    current_front_ = current.cull.front_face == GL_CCW ? fan_ccw : !fan_ccw;
    if (current.polygon_offset_fill) {
      const float dx = std::fabs(screen_[i].x - screen_[0].x) + 0.5f;
      const float dy = std::fabs(screen_[i].y - screen_[0].y) + 0.5f;
      const float slope = std::max(std::fabs(screen_[i].z - screen_[0].z) / dx,
                                   std::fabs(screen_[i].z - screen_[0].z) / dy);
      depth_offset_ = (current.polygon_offset_factor * slope * 65535.0f +
                       current.polygon_offset_units) /
                      65535.0f;
    } else {
      depth_offset_ = 0.0f;
    }
    context_->stats.triangles_rasterised++;
    rasterizer_.DrawTriangle(screen_[0], screen_[i], screen_[i + 1], 0, i,
                             i + 1, this);
  }
}

void Pipeline::Interpolate(const Fragment& fragment, float* out) const {
  const float bary0 = fragment.bary[0];
  const float bary1 = fragment.bary[1];
  const float bary2 = fragment.bary[2];
  const float* vertex0 =
      clip_.data() + static_cast<size_t>(fragment.vertex[0]) * stride_ + 4;
  const float* vertex1 =
      clip_.data() + static_cast<size_t>(fragment.vertex[1]) * stride_ + 4;
  const float* vertex2 =
      clip_.data() + static_cast<size_t>(fragment.vertex[2]) * stride_ + 4;
  const float w0 = clip_[static_cast<size_t>(fragment.vertex[0]) * stride_ + 3];
  const float w1 = clip_[static_cast<size_t>(fragment.vertex[1]) * stride_ + 3];
  const float w2 = clip_[static_cast<size_t>(fragment.vertex[2]) * stride_ + 3];
  const bool affine =
      (w0 == w1 && w1 == w2) || w0 == 0.0f || w1 == 0.0f || w2 == 0.0f;
  if (affine) {
    for (int i = 0; i < varying_floats_; ++i) {
      out[i] = vertex0[i] * bary0 + vertex1[i] * bary1 + vertex2[i] * bary2;
    }
    return;
  }
  const float inverse0 = bary0 / w0;
  const float inverse1 = bary1 / w1;
  const float inverse2 = bary2 / w2;
  const float inverse_sum = inverse0 + inverse1 + inverse2;
  if (inverse_sum == 0.0f) {
    for (int i = 0; i < varying_floats_; ++i)
      out[i] = vertex0[i];
    return;
  }
  for (int i = 0; i < varying_floats_; ++i) {
    out[i] = (vertex0[i] * inverse0 + vertex1[i] * inverse1 +
              vertex2[i] * inverse2) /
             inverse_sum;
  }
}

void Pipeline::ApplyStencilOp(GLenum op, size_t pixel, uint8_t reference) {
  const StencilState& stencil = state().stencil;
  const uint8_t current = target_.stencil[pixel];
  const uint8_t updated = StencilOp(op, current, reference);
  target_.stencil[pixel] = static_cast<uint8_t>(
      (updated & stencil.write_mask) | (current & ~stencil.write_mask));
}

void Pipeline::Shade(const Fragment& fragment) {
  if (!target_.valid())
    return;
  const State& state = this->state();
  if (fragment.x < 0 || fragment.y < 0 || fragment.x >= target_.width ||
      fragment.y >= target_.height) {
    return;
  }
  const int gl_y = target_.height - 1 - fragment.y;
  if (state.scissor_test && !state.scissor.Contains(fragment.x, gl_y))
    return;
  const size_t sample_offset =
      static_cast<size_t>(fragment.sample) * target_.sample_texels;
  const size_t pixel =
      static_cast<size_t>(fragment.y) * target_.width + fragment.x +
      sample_offset;
  context_->stats.fragments_shaded++;

  const float interpolated_depth =
      Clamp01(fragment.depth + (polygon_ ? depth_offset_ : 0.0f));

  const StencilFace* face =
      current_front_ ? &state.stencil.front : &state.stencil.back;
  const uint8_t reference = static_cast<uint8_t>(face->reference) &
                            static_cast<uint8_t>(state.stencil.mask);
  bool has_stencil = target_.stencil != nullptr;
  if (state.stencil.test && has_stencil) {
    const uint8_t stored =
        static_cast<uint8_t>(target_.stencil[pixel] & state.stencil.mask);
    if (!StencilTestPasses(face->func, reference, stored)) {
      ApplyStencilOp(face->op_fail, pixel, reference);
      return;
    }
  }

  const bool writes_depth = program_->uses_frag_depth();
  const bool has_depth = target_.depth != nullptr;
  if (state.depth.test && has_depth && !writes_depth) {
    const float stored = Unorm16ToFloat(target_.depth[pixel]);
    if (!DepthTestPasses(state.depth.func, interpolated_depth, stored)) {
      if (state.stencil.test && has_stencil)
        ApplyStencilOp(face->op_depth_fail, pixel, reference);
      return;
    }
  }

  Interpolate(fragment, varying_values_.data());
  glsl::FragmentInput input;
  input.varyings = varying_values_.data();
  input.frag_coord[0] =
      static_cast<float>(fragment.x) + 0.5f + fragment.sample_dx;
  input.frag_coord[1] =
      static_cast<float>(gl_y) + 0.5f + fragment.sample_dy;
  input.frag_coord[2] = interpolated_depth;
  input.frag_coord[3] = 1.0f;
  input.front_facing = current_front_;
  input.point_coord[0] = fragment.point_coord[0];
  input.point_coord[1] = fragment.point_coord[1];
  input.sample_id = fragment.sample;
  input.uniforms = uniforms_;
  input.sampler = this;
  glsl::FragmentOutput output;
  if (!program_->RunFragment(input, &output))
    return;
  if (output.discarded)
    return;

  float depth = interpolated_depth;
  if (output.writes_depth) {
    depth = Clamp01(output.depth);
    if (state.depth.test && has_depth &&
        !DepthTestPasses(state.depth.func, depth,
                         Unorm16ToFloat(target_.depth[pixel]))) {
      if (state.stencil.test && has_stencil)
        ApplyStencilOp(face->op_depth_fail, pixel, reference);
      return;
    }
  }

  if (state.stencil.test && has_stencil)
    ApplyStencilOp(face->op_pass, pixel, reference);

  if (state.depth.test && has_depth && state.depth.mask)
    target_.depth[pixel] = ToDepth16(depth);

  bool wrote_color = false;
  for (int location = 0; location < glsl::kMaxFragmentOutputs; ++location) {
    if (!output.written[location])
      continue;
    StoreFragmentColor(pixel, location, output.color[location],
                       output.color_base[location]);
    wrote_color = true;
  }
  if (wrote_color)
    context_->stats.fragments_written++;
}

void Pipeline::Resolve() {
  if (target_.samples <= 1 || target_.sample_texels == 0)
    return;
  const size_t texels = target_.sample_texels;
  for (int attachment = 0; attachment < kMaxColorAttachments; ++attachment) {
    uint32_t* const destination = target_.resolved_colors[attachment];
    if (!destination || !target_.colors[attachment])
      continue;
    const uint32_t* const samples =
        reinterpret_cast<const uint32_t*>(target_.colors[attachment]);
    for (size_t texel = 0; texel < texels; ++texel) {
      uint32_t r = 0;
      uint32_t g = 0;
      uint32_t b = 0;
      uint32_t a = 0;
      for (int sample = 0; sample < target_.samples; ++sample) {
        const Rgba rgba = UnpackRgba(samples[sample * texels + texel]);
        r += rgba.r;
        g += rgba.g;
        b += rgba.b;
        a += rgba.a;
      }
      const int count = target_.samples;
      destination[texel] = PackRgba(static_cast<uint8_t>(r / count),
                                    static_cast<uint8_t>(g / count),
                                    static_cast<uint8_t>(b / count),
                                    static_cast<uint8_t>(a / count));
    }
  }
  if (target_.resolved_depth && target_.depth) {
    memcpy(target_.resolved_depth, target_.depth,
           sizeof(uint16_t) * texels);
  }
  if (target_.resolved_stencil && target_.stencil) {
    memcpy(target_.resolved_stencil, target_.stencil, texels);
  }
}

void Pipeline::StoreFragmentColor(size_t pixel,
                                  int location,
                                  const float color[4],
                                  glsl::BaseType base) {
  if (location < 0 || location >= target_.draw_buffer_count)
    return;
  const GLenum buffer = target_.draw_buffers[location];
  if (buffer < GL_COLOR_ATTACHMENT0 ||
      buffer >= GL_COLOR_ATTACHMENT0 + kMaxColorAttachments)
    return;
  const int attachment = buffer - GL_COLOR_ATTACHMENT0;
  uint8_t* const destination = target_.colors[attachment];
  if (!destination)
    return;
  const State& state = this->state();
  if (target_.color_is_integer[attachment]) {
    uint32_t values[4];
    const bool unsigned_output =
        target_.color_is_unsigned[attachment] || base == glsl::BaseType::kUInt;
    for (int i = 0; i < 4; ++i) {
      if (unsigned_output)
        values[i] = static_cast<uint32_t>(color[i]);
      else
        values[i] = static_cast<uint32_t>(static_cast<int32_t>(color[i]));
    }
    uint32_t* const integer_destination =
        target_.integer_colors[attachment];
    if (!integer_destination)
      return;
    if (!(state.color_mask[0] && state.color_mask[1] &&
          state.color_mask[2] && state.color_mask[3])) {
      uint32_t existing[4];
      memcpy(existing, integer_destination + pixel * 4, sizeof(existing));
      for (int i = 0; i < 4; ++i)
        if (!state.color_mask[i])
          values[i] = existing[i];
    }
    memcpy(integer_destination + pixel * 4, values, sizeof(values));
    return;
  }
  if (state.blend.enabled) {
    float destination_color[4];
    uint32_t existing;
    memcpy(&existing, destination + pixel * 4, sizeof(existing));
    UnpackRgba(existing, destination_color);
    float source[4];
    for (int i = 0; i < 4; ++i)
      source[i] = Clamp01(color[i]);
    float result[4];
    for (int i = 0; i < 3; ++i) {
      const float sf = BlendFactorValue(state.blend.src_rgb, source,
                                        destination_color, state.blend.color, i);
      const float df = BlendFactorValue(state.blend.dst_rgb, source,
                                        destination_color, state.blend.color, i);
      result[i] = BlendCombine(state.blend.equation_rgb, source[i],
                               destination_color[i], sf, df);
    }
    const float sf = BlendFactorValue(state.blend.src_alpha, source,
                                      destination_color, state.blend.color, 3);
    const float df = BlendFactorValue(state.blend.dst_alpha, source,
                                      destination_color, state.blend.color, 3);
    result[3] = BlendCombine(state.blend.equation_alpha, source[3],
                             destination_color[3], sf, df);
    uint8_t bytes[4];
    for (int i = 0; i < 4; ++i)
      bytes[i] = state.color_mask[i] ? FloatToUnorm8(result[i])
                                     : (i == 0   ? UnpackRgba(existing).r
                                        : i == 1 ? UnpackRgba(existing).g
                                        : i == 2 ? UnpackRgba(existing).b
                                                 : UnpackRgba(existing).a);
    const uint32_t packed = PackRgba(bytes[0], bytes[1], bytes[2], bytes[3]);
    memcpy(destination + pixel * 4, &packed, sizeof(packed));
    return;
  }
  uint8_t bytes[4];
  for (int i = 0; i < 4; ++i)
    bytes[i] = FloatToUnorm8(color[i]);
  if (!(state.color_mask[0] && state.color_mask[1] && state.color_mask[2] &&
        state.color_mask[3])) {
    uint32_t existing;
    memcpy(&existing, destination + pixel * 4, sizeof(existing));
    const Rgba previous = UnpackRgba(existing);
    const uint8_t values[4] = {bytes[0], bytes[1], bytes[2], bytes[3]};
    const uint8_t stored[4] = {previous.r, previous.g, previous.b, previous.a};
    uint8_t merged[4];
    for (int i = 0; i < 4; ++i)
      merged[i] = state.color_mask[i] ? values[i] : stored[i];
    const uint32_t packed =
        PackRgba(merged[0], merged[1], merged[2], merged[3]);
    memcpy(destination + pixel * 4, &packed, sizeof(packed));
    return;
  }
  const uint32_t packed = PackRgba(bytes[0], bytes[1], bytes[2], bytes[3]);
  memcpy(destination + pixel * 4, &packed, sizeof(packed));
}

void Pipeline::Sample2D(int unit,
                        float u,
                        float v,
                        float lod_bias,
                        float out[4]) {
  out[0] = 0.0f;
  out[1] = 0.0f;
  out[2] = 0.0f;
  out[3] = 1.0f;
  const int slot = ClampInt(unit, 0, kMaxCombinedTextureUnits - 1);
  const Texture* texture = context_->textures.Get(state().textures[slot]);
  if (!texture)
    return;
  GLenum min_filter = texture->min_filter;
  GLenum mag_filter = texture->mag_filter;
  GLenum wrap_s = texture->wrap_s;
  GLenum wrap_t = texture->wrap_t;
  const Sampler* sampler = context_->samplers.Get(state().samplers[slot]);
  if (sampler) {
    min_filter = sampler->min_filter;
    mag_filter = sampler->mag_filter;
    wrap_s = sampler->wrap_s;
    wrap_t = sampler->wrap_t;
  }
  SampleTexture2D(*texture, u, v, lod_bias, min_filter, mag_filter, wrap_s,
                  wrap_t, out);
}

void Pipeline::SampleCube(int unit,
                          const float direction[3],
                          float lod_bias,
                          float out[4]) {
  out[0] = 0.0f;
  out[1] = 0.0f;
  out[2] = 0.0f;
  out[3] = 1.0f;
  const int slot = ClampInt(unit, 0, kMaxCombinedTextureUnits - 1);
  const Texture* texture = context_->textures.Get(state().textures[slot]);
  if (!texture)
    return;
  GLenum min_filter = texture->min_filter;
  GLenum mag_filter = texture->mag_filter;
  const Sampler* sampler = context_->samplers.Get(state().samplers[slot]);
  if (sampler) {
    min_filter = sampler->min_filter;
    mag_filter = sampler->mag_filter;
  }
  SampleTextureCube(*texture, direction, lod_bias, min_filter, mag_filter, out);
}

void Pipeline::Sample3D(int unit,
                        const float position[3],
                        float lod_bias,
                        float out[4]) {
  out[0] = 0.0f;
  out[1] = 0.0f;
  out[2] = 0.0f;
  out[3] = 1.0f;
  const int slot = ClampInt(unit, 0, kMaxCombinedTextureUnits - 1);
  const Texture* texture = context_->textures.Get(state().textures[slot]);
  if (!texture)
    return;
  GLenum min_filter = texture->min_filter;
  GLenum mag_filter = texture->mag_filter;
  GLenum wrap_s = texture->wrap_s;
  GLenum wrap_t = texture->wrap_t;
  GLenum wrap_r = texture->wrap_r;
  const Sampler* sampler = context_->samplers.Get(state().samplers[slot]);
  if (sampler) {
    min_filter = sampler->min_filter;
    mag_filter = sampler->mag_filter;
    wrap_s = sampler->wrap_s;
    wrap_t = sampler->wrap_t;
    wrap_r = sampler->wrap_r;
  }
  SampleTexture3D(*texture, position, lod_bias, min_filter, mag_filter, wrap_s,
                  wrap_t, wrap_r, out);
}

}  // namespace

void Draw(GLenum mode,
          GLint first,
          GLsizei count,
          GLenum index_type,
          const void* indices,
          GLsizei instance_count) {
  Context* context = CurrentContext();
  if (!context)
    return;
  if (count < 0 || instance_count < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (count == 0 || instance_count == 0)
    return;
  Pipeline pipeline(context);
  pipeline.Run(mode, first, count, index_type, indices, instance_count);
  pipeline.Resolve();
}

}  // namespace zgl
