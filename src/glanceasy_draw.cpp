// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/glanceasy_draw.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <chrono>
#include <time.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "src/glsl/glsl_program.h"
#include "src/glanceasy_context.h"
#include "src/glanceasy_raster.h"
#include "src/glanceasy_texture.h"

namespace glanceasy {

namespace {

// Number of floats before the varying block in a transformed vertex.
constexpr int kFloatPerPosition = 4;

// Clip space is the intersection of six half spaces.  Clipping a convex
// polygon against each plane can add at most one vertex, so a clipped
// triangle never exceeds nine vertices.
constexpr int kMaxClippedVertices = 9;

uint64_t ProfileNowNs() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
         static_cast<uint64_t>(ts.tv_nsec);
}

bool ApproxCullEnabled() {
  static const bool enabled = []() {
    const char* value = std::getenv("GLANCEASY_APPROX_CULL");
    // Approximate sub-pixel culling is on by default for the low-resolution
    // profile; set GLANCEASY_APPROX_CULL=0 to restore exact coverage.
    return value == nullptr || (value[0] != '\0' && value[0] != '0');
  }();
  return enabled;
}

float ApproxCullMinArea(int width, int height) {
  static const float explicit_min_area = []() {
    const char* value = std::getenv("GLANCEASY_APPROX_MIN_AREA");
    return value ? static_cast<float>(std::atof(value)) : -1.0f;
  }();
  if (explicit_min_area >= 0.0f)
    return explicit_min_area;
  // Keep the cull threshold proportional to the target area.  A triangle
  // smaller than roughly 1/9600 of the screen is unlikely to contribute a
  // meaningful fragment once the target is this small.
  return std::max(0.25f, static_cast<float>(width * height) / 9600.0f);
}

void Mat4MulVec4(const float* matrix, const float* vector, float out[4]) {
  for (int r = 0; r < 4; ++r) {
    out[r] = matrix[0 * 4 + r] * vector[0] +
             matrix[1 * 4 + r] * vector[1] +
             matrix[2 * 4 + r] * vector[2] +
             matrix[3 * 4 + r] * vector[3];
  }
}

uint64_t g_profile_interp_ns = 0;
uint64_t g_profile_shader_ns = 0;
uint64_t g_profile_store_ns = 0;
uint64_t g_profile_fragments = 0;

void ResetFragmentProfile() {
  g_profile_interp_ns = 0;
  g_profile_shader_ns = 0;
  g_profile_store_ns = 0;
  g_profile_fragments = 0;
}

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

bool IsPacked2101010(GLenum type) {
  return type == GL_INT_2_10_10_10_REV ||
         type == GL_UNSIGNED_INT_2_10_10_10_REV;
}

int Packed2101010Bits(int component) {
  return component < 3 ? 10 : 2;
}

float ReadPacked2101010(const uint8_t* data, int component, GLenum type,
                        bool normalized) {
  uint32_t word;
  memcpy(&word, data, sizeof(word));
  const int bits = Packed2101010Bits(component);
  int shift = 0;
  for (int i = 0; i < component; ++i)
    shift += Packed2101010Bits(i);
  const uint32_t mask = (1u << bits) - 1u;
  const int raw = static_cast<int>((word >> shift) & mask);
  if (normalized) {
    if (type == GL_INT_2_10_10_10_REV) {
      const int sign_bit = 1 << (bits - 1);
      const int signed_value =
          (raw & sign_bit) ? raw - (1 << bits) : raw;
      const float scale = 1.0f / static_cast<float>(sign_bit - 1);
      return std::max(-1.0f, static_cast<float>(signed_value) * scale);
    }
    return static_cast<float>(raw) /
           static_cast<float>((1u << bits) - 1u);
  }
  if (type == GL_INT_2_10_10_10_REV) {
    const int sign_bit = 1 << (bits - 1);
    return static_cast<float>((raw & sign_bit) ? raw - (1 << bits) : raw);
  }
  return static_cast<float>(raw);
}

bool IsIntegerType(GLenum type) {
  return type == GL_BYTE || type == GL_UNSIGNED_BYTE || type == GL_SHORT ||
         type == GL_UNSIGNED_SHORT || type == GL_INT ||
         type == GL_UNSIGNED_INT || IsPacked2101010(type);
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
  if (IsPacked2101010(type))
    return ReadPacked2101010(data, component, type, false);
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
  if (IsPacked2101010(type))
    return ReadPacked2101010(data, component, type, true);
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
  if (type == GL_UNSIGNED_INT) {
    uint32_t raw32;
    memcpy(&raw32, data + component * 4, sizeof(raw32));
    return static_cast<float>(raw32) * (1.0f / 4294967295.0f);
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

bool DepthTestPasses16(GLenum func, uint16_t incoming, uint16_t stored) {
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

// Blend factors that can be evaluated exactly from 8-bit source/destination
// bytes.  Constant-colour factors are intentionally absent: they need the
// blend colour, which the fixed-point path does not carry.
bool FastBlendFactorSupported(GLenum factor) {
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
    case GL_SRC_ALPHA_SATURATE:
      return true;
    default:
      return false;
  }
}

int FastBlendFactorByte(GLenum factor,
                        const uint8_t src[4],
                        const uint8_t dst[4],
                        int channel) {
  switch (factor) {
    case GL_ZERO:
      return 0;
    case GL_ONE:
      return 255;
    case GL_SRC_COLOR:
      return src[channel];
    case GL_ONE_MINUS_SRC_COLOR:
      return 255 - src[channel];
    case GL_DST_COLOR:
      return dst[channel];
    case GL_ONE_MINUS_DST_COLOR:
      return 255 - dst[channel];
    case GL_SRC_ALPHA:
      return src[3];
    case GL_ONE_MINUS_SRC_ALPHA:
      return 255 - src[3];
    case GL_DST_ALPHA:
      return dst[3];
    case GL_ONE_MINUS_DST_ALPHA:
      return 255 - dst[3];
    case GL_SRC_ALPHA_SATURATE:
      return channel == 3
                 ? 255
                 : std::min(static_cast<int>(src[3]),
                            255 - static_cast<int>(dst[3]));
    default:
      return 0;
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
double PlaneDistance(const float* position, int plane) {
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
  void ShadeBatch(const Fragment* fragments, int count) override;

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
  void Sample2DArray(int unit,
                     float u,
                     float v,
                     float layer,
                     float lod_bias,
                     float out[4]) override;
  void Sample2DShadow(int unit,
                      float u,
                      float v,
                      float ref,
                      float lod_bias,
                      float out[4]) override;
  void Sample2DArrayShadow(int unit,
                           float u,
                           float v,
                           float layer,
                           float ref,
                           float lod_bias,
                           float out[4]) override;
  void SampleCubeShadow(int unit,
                        const float direction[3],
                        float ref,
                        float lod_bias,
                        float out[4]) override;
  void Size(int unit, int out[3]) override;
  bool Sample2DRgba8Fast(int unit,
                         float u,
                         float v,
                         float out[4]) override;
  bool NeedsImplicitLod(int unit) override;
  uint32_t MinFilter(int unit) override;
  void SizeLod(int unit, int lod, int out[3]) override;
  void Fetch2D(int unit, int x, int y, int lod, float out[4]) override;
  void Fetch3D(int unit, int x, int y, int z, int lod, float out[4]) override;
  void Fetch2DArray(int unit,
                    int x,
                    int y,
                    int layer,
                    int lod,
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

  void ShadeFragment(const Fragment& fragment,
                     size_t pixel,
                     int gl_y,
                     const State& state);
  bool Prepare();
  const State& state() const;
  bool varying_derivatives_ = true;
  struct CachedTexture2D {
    bool valid = false;
    const Texture* texture = nullptr;
    GLenum min_filter = GL_NEAREST;
    GLenum mag_filter = GL_NEAREST;
    GLenum wrap_s = GL_REPEAT;
    GLenum wrap_t = GL_REPEAT;
    const TextureImage* image = nullptr;
    bool flip_y = false;
    bool opaque = false;
    bool direct_rgba8 = false;
    bool mask_alpha = true;
    bool needs_lod = false;
  };
  CachedTexture2D cached_textures_2d_[kMaxCombinedTextureUnits];
  std::vector<int> active_texture_units_;
  CachedTexture2D BuildCachedTexture2D(int slot) const;
  bool CacheTextureBindings();
  void FetchAttributes(GLuint index,
                       int instance,
                       float* attributes,
                       uint32_t* attribute_int) const;
  bool FetchPosition(GLuint index,
                     int instance,
                     int location,
                     float out[4]) const;
  TransformedVertex AcquireVertex(GLuint index, int instance);
  void CaptureVertex(GLuint index, int instance);
  void WriteTransformFeedback(const float* varyings,
                              const float* position,
                              float point_size);
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
                          const uint32_t color_int[4],
                          glsl::BaseType base);
  void ApplyStencilOp(GLenum op,
                      size_t pixel,
                      uint8_t reference,
                      uint8_t write_mask);

  Context* context_;
  Rasterizer rasterizer_;

  glsl::Program* program_ = nullptr;
  const glsl::Value* uniforms_ = nullptr;
  const VertexArray* vertex_array_ = nullptr;
  RenderTarget target_;

  int stride_ = kFloatPerPosition;
  int varying_floats_ = 0;
  bool empty_clip_ = false;
  int active_attribute_count_ = 0;
  GLuint active_attributes_[kMaxVertexAttribs] = {};
  uint32_t vertex_attribute_mask_ = 0;
  struct CachedAttribute {
    const uint8_t* base = nullptr;
    size_t available = 0;
    int stride = 0;
    int element_size = 0;
    int components = 0;
    int divisor = 0;
    GLenum type = GL_FLOAT;
    bool normalized = false;
    bool integer = false;
    bool valid = false;
  };
  CachedAttribute cached_attributes_[kMaxVertexAttribs];
  float default_attribute_floats_[kMaxVertexAttribs * 4] = {};
  uint32_t default_attribute_ints_[kMaxVertexAttribs * 4] = {};

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
  mutable std::vector<float> varying_dx_values_;
  mutable std::vector<float> varying_dy_values_;
  std::vector<std::pair<int, int>> flat_varying_ranges_;
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
  if (GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_ERROR"))
    fprintf(stderr,
            "[draw] %dx%d vp=%d,%d,%d,%d scissor=%d empty=%d count=%d "
            "instances=%d prog=%u vf=%d\n",
            target_.width, target_.height, state().viewport.x,
            state().viewport.y, state().viewport.width,
            state().viewport.height, state().scissor_test ? 1 : 0,
            empty_clip_ ? 1 : 0, count, instance_count,
            static_cast<unsigned>(state().program), varying_floats_);
  glanceasy::TransformFeedback* feedback = glanceasy::CurrentTransformFeedback(context_);
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
    // Transform feedback captures only vertices that belong to a complete
    // primitive; an incomplete trailing line or triangle is not recorded.
    size_t capture_count = static_cast<size_t>(count);
    switch (mode) {
      case GL_LINES:
        capture_count -= capture_count % 2;
        break;
      case GL_LINE_STRIP:
      case GL_LINE_LOOP:
        capture_count = capture_count >= 2 ? capture_count : 0;
        break;
      case GL_TRIANGLES:
        capture_count -= capture_count % 3;
        break;
      case GL_TRIANGLE_STRIP:
      case GL_TRIANGLE_FAN:
        capture_count = capture_count >= 3 ? capture_count : 0;
        break;
      default:
        break;
    }
    for (int instance = 0; instance < instance_count; ++instance) {
      cursor = 0;
      GLuint index;
      size_t captured = 0;
      while (captured < capture_count && next_index(&index) == IndexResult::kOk) {
        CaptureVertex(index, instance);
        ++captured;
      }
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
        while (true) {
          const IndexResult result = next_index(&index);
          if (result == IndexResult::kEnd)
            break;
          if (result == IndexResult::kOk)
            EmitPoint(index, instance);
        }
        break;
      }
      case GL_LINES: {
        GLuint pending[2] = {0, 0};
        int have = 0;
        GLuint current;
        while (true) {
          const IndexResult result = next_index(&current);
          if (result == IndexResult::kEnd)
            break;
          if (result == IndexResult::kRestart) {
            have = 0;
            continue;
          }
          pending[have++] = current;
          if (have == 2) {
            EmitLine(pending[0], pending[1], instance);
            have = 0;
          }
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
        GLuint pending[3] = {0, 0, 0};
        int have = 0;
        GLuint current;
        while (true) {
          const IndexResult result = next_index(&current);
          if (result == IndexResult::kEnd)
            break;
          if (result == IndexResult::kRestart) {
            have = 0;
            continue;
          }
          pending[have++] = current;
          if (have == 3) {
            EmitTriangle(pending[0], pending[1], pending[2], instance);
            have = 0;
          }
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
  program_ = object->program.get();  target_ = context_->GetDrawTarget();
  varying_derivatives_ = program_->uses_varying_derivatives();
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
  active_texture_units_.clear();
  for (const glsl::UniformInfo& uniform : program_->uniforms()) {
    if (!uniform.type.is_sampler() || uniform.location < 0)
      continue;
    const int element_count =
        uniform.type.is_array() ? std::max(1, uniform.type.array_size()) : 1;
    for (int element = 0; element < element_count; ++element) {
      const int unit =
          static_cast<int>(uniforms_[uniform.location + element].data[0]);
      if (std::find(active_texture_units_.begin(), active_texture_units_.end(),
                    unit) == active_texture_units_.end())
        active_texture_units_.push_back(unit);
    }
  }
  int simple_unit_slot = -1;
  int simple_literal_unit = 0;
  int simple_offset = 0;
  int simple_output = 0;
  if (program_->simple_texture_sample(&simple_unit_slot, &simple_literal_unit,
                                    &simple_offset, &simple_output)) {
    const int unit = simple_unit_slot >= 0
                         ? static_cast<int>(uniforms_[simple_unit_slot].data[0])
                         : simple_literal_unit;
    if (std::find(active_texture_units_.begin(), active_texture_units_.end(),
                  unit) == active_texture_units_.end())
      active_texture_units_.push_back(unit);
  }
  const glsl::Program::SimpleConvolution& convolution =
      program_->simple_convolution();
  if (convolution.valid) {
    const int unit = convolution.uniform_slot >= 0
                         ? static_cast<int>(
                               uniforms_[convolution.uniform_slot].data[0])
                         : convolution.literal_unit;
    if (std::find(active_texture_units_.begin(), active_texture_units_.end(),
                  unit) == active_texture_units_.end())
      active_texture_units_.push_back(unit);
  }
  CacheTextureBindings();
  if (program_->simple_texture_sample(&simple_unit_slot, &simple_literal_unit,
                                    &simple_offset, &simple_output)) {
    const int unit = simple_unit_slot >= 0
                         ? static_cast<int>(uniforms_[simple_unit_slot].data[0])
                         : simple_literal_unit;
    if (!NeedsImplicitLod(unit))
      varying_derivatives_ = false;
  }
  if (convolution.valid)
    varying_derivatives_ = false;
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
    if (GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_UBO")) {
      fprintf(stderr, "DRAW block=%s bp=%d id=%u off=%zu size=%zu bytes=%zu\n",
              program_->uniform_blocks()[i].name.c_str(), binding_point,
              binding.buffer, static_cast<size_t>(binding.offset),
              static_cast<size_t>(binding.size), buffer->data.size());
      fprintf(stderr, "HEX");
      for (size_t q = 0; q < 32 && q < buffer->data.size(); ++q)
        fprintf(stderr, " %02x", buffer->data[q]);
      fprintf(stderr, "\n");
    }
    program_->UploadUniformBlock(static_cast<int>(i),
                                 buffer->data.data() + binding.offset,
                                 available);
  }
  varying_floats_ = program_->VaryingFloats();
  flat_varying_ranges_.clear();
  for (const glsl::VaryingInfo& info : program_->varyings()) {
    if (info.flat)
      flat_varying_ranges_.push_back(
          {info.offset, info.offset + info.components});
  }
  stride_ = kFloatPerPosition + varying_floats_;
  active_attribute_count_ = 0;
  vertex_attribute_mask_ = 0;
  for (const glsl::AttributeInfo& attribute : program_->attributes()) {
    if (attribute.location >= 0 && attribute.location < kMaxVertexAttribs)
      vertex_attribute_mask_ |= 1u << attribute.location;
  }
  for (int i = 0; i < kMaxVertexAttribs; ++i) {
    if (vertex_array_->attributes[i].enabled)
      active_attributes_[active_attribute_count_++] = static_cast<GLuint>(i);
    for (int c = 0; c < 4; ++c) {
      default_attribute_floats_[i * 4 + c] = state.current_attribute[i][c];
      default_attribute_ints_[i * 4 + c] = static_cast<uint32_t>(
          state.current_attribute_integer[i][c]);
    }
  }
  for (int i = 0; i < kMaxVertexAttribs; ++i) {
    CachedAttribute& cached = cached_attributes_[i];
    cached.valid = false;
    const VertexAttribute& attribute = vertex_array_->attributes[i];
    if (!attribute.enabled)
      continue;
    const int components = ClampInt(attribute.size, 1, 4);
    const int element_size = IsPacked2101010(attribute.type)
                                 ? 4
                                 : TypeSize(attribute.type) * components;
    if (element_size == 0)
      continue;
    const int stride = attribute.stride > 0 ? attribute.stride : element_size;
    if (attribute.buffer != 0) {
      const Buffer* buffer = context_->buffers.Get(attribute.buffer);
      if (!buffer || static_cast<size_t>(attribute.offset) >= buffer->data.size())
        continue;
      cached.base = buffer->data.data() + attribute.offset;
      cached.available = buffer->data.size() -
                         static_cast<size_t>(attribute.offset);
    } else if (attribute.client_pointer) {
      cached.base = attribute.client_pointer + attribute.offset;
      cached.available = static_cast<size_t>(1) << 30;
    } else {
      continue;
    }
    cached.stride = stride;
    cached.element_size = element_size;
    cached.components = components;
    cached.divisor = attribute.divisor;
    cached.type = attribute.type;
    cached.normalized = attribute.normalized;
    cached.integer = attribute.integer;
    cached.valid = true;
  }
  cache_.assign(static_cast<size_t>(kVertexCacheSize) * stride_, 0.0f);
  cache_vertices_.assign(kVertexCacheSize, CachedVertex());
  next_cache_slot_ = 0;
  for (std::vector<float>& vertices : emit_)
    vertices.assign(static_cast<size_t>(stride_), 0.0f);
  clip_.assign(static_cast<size_t>(kMaxClippedVertices) * stride_, 0.0f);
  scratch_.assign(static_cast<size_t>(kMaxClippedVertices + 1) * stride_, 0.0f);
  screen_.assign(kMaxClippedVertices, RasterVertex());
  varying_values_.assign(varying_floats_, 0.0f);
  varying_dx_values_.assign(varying_floats_, 0.0f);
  varying_dy_values_.assign(varying_floats_, 0.0f);
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
  if (GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_VIEWPORT")) {
    for (int i = 0; i < 2; ++i) {
      const VertexAttribute& a = vertex_array_->attributes[i];
      fprintf(stderr, "[attribstate] %d en=%d size=%d stride=%d ptr=%p buf=%u\n",
              i, a.enabled ? 1 : 0, a.size, a.stride,
              static_cast<const void*>(a.client_pointer), a.buffer);
    }
  }
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

uint32_t ReadAttributeInteger(const uint8_t* data, int component,
                            GLenum type) {
  switch (type) {
    case GL_BYTE:
      return static_cast<uint32_t>(
          static_cast<int32_t>(static_cast<int8_t>(data[component])));
    case GL_UNSIGNED_BYTE:
      return data[component];
    case GL_SHORT: {
      int16_t value;
      memcpy(&value, data + component * 2, sizeof(value));
      return static_cast<uint32_t>(static_cast<int32_t>(value));
    }
    case GL_UNSIGNED_SHORT: {
      uint16_t value;
      memcpy(&value, data + component * 2, sizeof(value));
      return value;
    }
    case GL_INT: {
      int32_t value;
      memcpy(&value, data + component * 4, sizeof(value));
      return static_cast<uint32_t>(value);
    }
    case GL_UNSIGNED_INT: {
      uint32_t value;
      memcpy(&value, data + component * 4, sizeof(value));
      return value;
    }
    case GL_FLOAT: {
      float value;
      memcpy(&value, data + component * 4, sizeof(value));
      return static_cast<uint32_t>(value);
    }
    default:
      return 0;
  }
}

void Pipeline::FetchAttributes(GLuint index,
                               int instance,
                               float* attributes,
                               uint32_t* attribute_int) const {
  // Disabled attributes are constant for the whole draw, so copy the cached
  // defaults once and then touch only the enabled slots.  A 16-way branch
  // per vertex was ~7% of the phong scene on Cortex-A7.
  // Only attributes the vertex shader actually declares need their constant
  // defaults copied.  The full 16-slot default block cost roughly half a
  // kilobyte of memcpy per vertex on the Cortex-A7.
  for (int i = 0; i < kMaxVertexAttribs; ++i) {
    if ((vertex_attribute_mask_ & (1u << i)) == 0)
      continue;
    memcpy(attributes + i * 4, default_attribute_floats_ + i * 4,
           sizeof(float) * 4);
    memcpy(attribute_int + i * 4, default_attribute_ints_ + i * 4,
           sizeof(uint32_t) * 4);
  }
  for (int slot = 0; slot < active_attribute_count_; ++slot) {
    const GLuint i = active_attributes_[slot];
    float* destination = attributes + i * 4;
    uint32_t* int_destination = attribute_int + i * 4;
    const CachedAttribute& cached = cached_attributes_[i];
    destination[0] = 0.0f;
    destination[1] = 0.0f;
    destination[2] = 0.0f;
    destination[3] = 1.0f;
    if (!cached.valid)
      continue;
    const int divisor = cached.divisor;
    const size_t element =
        divisor > 0 ? static_cast<size_t>(instance) / divisor : index;
    const size_t offset = element * static_cast<size_t>(cached.stride);
    if (offset + static_cast<size_t>(cached.element_size) > cached.available)
      continue;
    const uint8_t* base = cached.base + offset;
    if (cached.type == GL_FLOAT && !cached.normalized) {
      // The overwhelmingly common case: a plain float attribute.  Copy the
      // stored floats directly instead of going through the generic
      // per-component readers, and leave the integer payload untouched --
      // a float attribute can never be read through an integer builtin.
      memcpy(destination, base,
             sizeof(float) * static_cast<size_t>(cached.components));
      continue;
    }
    for (int component = 0; component < cached.components; ++component) {
      destination[component] =
          cached.normalized && IsIntegerType(cached.type)
              ? ReadNormalizedComponent(base, component, cached.type)
              : ReadAttributeComponent(base, component, cached.type);
      int_destination[component] =
          cached.normalized && IsIntegerType(cached.type)
              ? static_cast<uint32_t>(destination[component])
              : ReadAttributeInteger(base, component, cached.type);
    }
  }
}

bool Pipeline::FetchPosition(GLuint index,
                             int instance,
                             int location,
                             float out[4]) const {
  if (location < 0 || location >= kMaxVertexAttribs)
    return false;
  const CachedAttribute& cached = cached_attributes_[location];
  if (!cached.valid || cached.components < 3)
    return false;
  const size_t element = cached.divisor > 0
                             ? static_cast<size_t>(instance) /
                                   static_cast<size_t>(cached.divisor)
                             : index;
  const size_t offset = element * static_cast<size_t>(cached.stride);
  if (offset + static_cast<size_t>(cached.element_size) > cached.available)
    return false;
  const uint8_t* base = cached.base + offset;
  for (int component = 0; component < cached.components && component < 3;
       ++component) {
    out[component] =
        cached.normalized && IsIntegerType(cached.type)
            ? ReadNormalizedComponent(base, component, cached.type)
            : ReadAttributeComponent(base, component, cached.type);
  }
  if (cached.components >= 4) {
    out[3] = cached.normalized && IsIntegerType(cached.type)
                 ? ReadNormalizedComponent(base, 3, cached.type)
                 : ReadAttributeComponent(base, 3, cached.type);
  } else {
    out[3] = 1.0f;
  }
  for (int component = cached.components; component < 3; ++component)
    out[component] = 0.0f;
  return true;
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
  uint32_t attribute_int[kMaxVertexAttribs * 4];
  FetchAttributes(index, instance, attributes, attribute_int);
  glsl::VertexInput input;
  input.attributes = attributes;
  input.attribute_int = attribute_int;
  input.vertex_id = static_cast<int>(index);
  input.instance_id = instance;
  input.depth_range[0] = state().depth.range_near;
  input.depth_range[1] = state().depth.range_far;
  input.depth_range[2] = state().depth.range_far - state().depth.range_near;
  input.uniforms = uniforms_;
  input.sampler = this;
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
  uint32_t attribute_int[kMaxVertexAttribs * 4];
  FetchAttributes(index, instance, attributes, attribute_int);
  glsl::VertexInput input;
  input.attributes = attributes;
  input.attribute_int = attribute_int;
  input.vertex_id = static_cast<int>(index);
  input.instance_id = instance;
  input.uniforms = uniforms_;
  input.sampler = this;
  float position[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float point_size = 1.0f;
  program_->RunVertex(input, position, &point_size, tf_varyings_.data());
  if (program_->has_runtime_error()) {
    context_->SetError(GL_INVALID_OPERATION);
    context_->diagnostics = program_->runtime_error();
    return;
  }
  WriteTransformFeedback(tf_varyings_.data(), position, point_size);
}

void Pipeline::WriteTransformFeedback(const float* varyings,
                                      const float* position,
                                      float point_size) {
  glanceasy::TransformFeedback* feedback = glanceasy::CurrentTransformFeedback(context_);
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
        buffer_index >= glanceasy::kMaxTransformFeedbackBuffers) {
      continue;
    }
    const glanceasy::IndexedBufferBinding& binding = feedback->buffers[buffer_index];
    if (binding.attachment_deleted)
      continue;
    glanceasy::Buffer* buffer = context_->buffers.Get(binding.buffer);
    if (!buffer)
      continue;
    size_t cursor = feedback->cursors[buffer_index];
    const size_t start = binding.offset + cursor +
                         (separate ? 0 : start_offset);
    size_t end = buffer->data.size();
    if (binding.size > 0)
      end = std::min(end, binding.offset + binding.size);
    if (bytes > 0 && start <= end && bytes <= end - start &&
        !buffer->data.empty()) {
      const float* source = nullptr;
      float point_value = point_size;
      if (output.offset == -3)
        source = position;
      else if (output.offset == -2)
        source = &point_value;
      else if (varyings && output.offset >= 0 &&
               output.offset + output.components <= varying_floats_)
        source = varyings + output.offset;
      if (source)
        memcpy(buffer->data.data() + start, source, bytes);
    }
  }

  if (separate) {
    for (const auto& output : outputs) {
      const int buffer_index = output.buffer_index;
      if (buffer_index >= 0 &&
          buffer_index < glanceasy::kMaxTransformFeedbackBuffers) {
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
  for (int plane = 4; plane < 6; ++plane) {
    if (PlaneDistance(vertex.data, plane) < 0.0f)
      return;
  }
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
  if (GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_POINT"))
    fprintf(stderr, "[pt] center=%.2f,%.2f size=%.2f\n", screen.x, screen.y, size);
  rasterizer_.DrawPoint(screen, 0, size, this);
}

void Pipeline::EmitLine(GLuint index_a, GLuint index_b, int instance) {
  memcpy(emit_[0].data(), AcquireVertex(index_a, instance).data,
         sizeof(float) * stride_);
  memcpy(emit_[1].data(), AcquireVertex(index_b, instance).data,
         sizeof(float) * stride_);
  memcpy(clip_.data(), emit_[0].data(), sizeof(float) * stride_);
  memcpy(clip_.data() + stride_, emit_[1].data(), sizeof(float) * stride_);
  int count = 2;
  float* polygon = clip_.data();
  float* destination = scratch_.data();
  for (int plane = 0; plane < 6 && count >= 2; ++plane) {
    count = ClipPolygon(polygon, count, plane, destination);
    polygon = destination;
    destination =
        destination == scratch_.data() ? clip_.data() : scratch_.data();
  }
  if (count < 2)
    return;
  const State& current = state();
  RasterVertex vertices[2];
  for (int i = 0; i < 2; ++i) {
    const float* position = polygon + static_cast<size_t>(i) * stride_;
    const float w = position[3];
    if (!(w > 0.0f))
      return;
    const float ndc_x = position[0] / w;
    const float ndc_y = position[1] / w;
    const float ndc_z = position[2] / w;
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
  glanceasy::TransformFeedback* feedback =
      glanceasy::CurrentTransformFeedback(context_);
  const bool capturing = feedback && feedback->active && !feedback->paused;
  if (!capturing && ApproxCullEnabled()) {
    const glsl::Program::SimplePosition& simple = program_->simple_position();
    if (simple.valid && simple.uniform_slot >= 0) {
      float local[3][4];
      if (FetchPosition(index_a, instance, simple.attribute_location, local[0]) &&
          FetchPosition(index_b, instance, simple.attribute_location, local[1]) &&
          FetchPosition(index_c, instance, simple.attribute_location, local[2])) {
        const glsl::Value& matrix = uniforms_[simple.uniform_slot];
        float clip[3][4];
        Mat4MulVec4(matrix.data, local[0], clip[0]);
        Mat4MulVec4(matrix.data, local[1], clip[1]);
        Mat4MulVec4(matrix.data, local[2], clip[2]);
        bool positive_w = true;
        for (int i = 0; i < 3; ++i)
          positive_w = positive_w && clip[i][3] > 0.0f;
        if (positive_w) {
          bool outside = false;
          for (int plane = 0; plane < 6 && !outside; ++plane) {
            int all = 0;
            for (int i = 0; i < 3; ++i) {
              const float* p = clip[i];
              float distance = 0.0f;
              switch (plane) {
                case 0: distance = p[0] + p[3]; break;  // left
                case 1: distance = p[3] - p[0]; break;  // right
                case 2: distance = p[1] + p[3]; break;  // bottom
                case 3: distance = p[3] - p[1]; break;  // top
                case 4: distance = p[2] + p[3]; break;  // near
                case 5: distance = p[3] - p[2]; break;  // far
              }
              if (distance < 0.0f)
                ++all;
            }
            outside = all == 3;
          }
          if (outside)
            return;
          const State& current = state();
          const float vx = static_cast<float>(current.viewport.x);
          const float vy = static_cast<float>(target_.height -
                                              current.viewport.bottom());
          const float vw = static_cast<float>(current.viewport.width);
          const float vh = static_cast<float>(current.viewport.height);
          float sx[3];
          float sy[3];
          for (int i = 0; i < 3; ++i) {
            const float ndc_x = clip[i][0] / clip[i][3];
            const float ndc_y = clip[i][1] / clip[i][3];
            sx[i] = vx + (ndc_x * 0.5f + 0.5f) * vw;
            sy[i] = vy + (0.5f - ndc_y * 0.5f) * vh;
          }
          const float min_x = std::min(sx[0], std::min(sx[1], sx[2]));
          const float max_x = std::max(sx[0], std::max(sx[1], sx[2]));
          const float min_y = std::min(sy[0], std::min(sy[1], sy[2]));
          const float max_y = std::max(sy[0], std::max(sy[1], sy[2]));
          if (max_x < vx || min_x > vx + vw || max_y < vy ||
              min_y > vy + vh)
            return;
          const float area =
              std::fabs((sx[1] - sx[0]) * (sy[2] - sy[0]) -
                        (sx[2] - sx[0]) * (sy[1] - sy[0])) * 0.5f;
          if (area < ApproxCullMinArea(static_cast<int>(vw), static_cast<int>(vh)))
            return;
        }
      }
    }
  }
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
    const double distance_current = PlaneDistance(current, plane);
    const double distance_next = PlaneDistance(next, plane);
    if (distance_current >= 0.0) {
      memcpy(output + static_cast<size_t>(out_count) * stride_, current,
             sizeof(float) * stride_);
      ++out_count;
    }
    if ((distance_current >= 0.0) != (distance_next >= 0.0)) {
      const double t = distance_current / (distance_current - distance_next);
      float* destination = output + static_cast<size_t>(out_count) * stride_;
      for (int k = 0; k < stride_; ++k) {
        destination[k] = static_cast<float>(static_cast<double>(current[k]) +
                                            (static_cast<double>(next[k]) -
                                             static_cast<double>(current[k])) *
                                                t);
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
  // Most triangles in a typical scene are completely inside the frustum.
  // Testing three vertices against six planes is much cheaper than running
  // the polygon clipper and duplicate removal for every triangle.
  bool inside = true;
  for (int i = 0; i < 3 && inside; ++i) {
    const float* position = clip_.data() + static_cast<size_t>(i) * stride_;
    if (!(position[3] > 0.0f)) {
      inside = false;
      break;
    }
    for (int plane = 0; plane < 6; ++plane) {
      if (PlaneDistance(position, plane) < 0.0f) {
        inside = false;
        break;
      }
    }
  }
  bool clipped = false;
  if (!inside) {
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
    clipped = true;
  }

  int unique = 0;
  for (int i = 0; clipped && i < count; ++i) {
    const float* candidate = clip_.data() + static_cast<size_t>(i) * stride_;
    bool duplicate = false;
    for (int j = 0; j < unique; ++j) {
      const float* kept = clip_.data() + static_cast<size_t>(j) * stride_;
      float difference = 0.0f;
      for (int k = 0; k < stride_; ++k)
        difference = std::max(difference, std::fabs(kept[k] - candidate[k]));
      if (difference < 1e-5f) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) {
      if (unique != i)
        memcpy(clip_.data() + static_cast<size_t>(unique) * stride_,
               candidate, sizeof(float) * stride_);
      ++unique;
    }
  }
  count = clipped ? unique : count;
  if (count < 3)
    return;

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
  const int provoking_index =
      fragment.provoking >= 0 ? fragment.provoking : fragment.vertex[2];
  const float* provoking =
      clip_.data() + static_cast<size_t>(provoking_index) * stride_ + 4;
  const auto is_flat = [&](int index) {
    for (const auto& range : flat_varying_ranges_)
      if (index >= range.first && index < range.second)
        return true;
    return false;
  };
  if (GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_DER")) {
    static int interp_dbg = 0;
    if (interp_dbg < 4) {
      fprintf(stderr, "[interp] bary=%.3f,%.3f,%.3f db=%.4f,%.4f v0=%.3f v1=%.3f v2=%.3f\n",
              bary0, bary1, bary2, fragment.dbary[0][0], fragment.dbary[0][1],
              vertex0[0], vertex1[0], vertex2[0]);
      ++interp_dbg;
    }
  }
  if (GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_FLAT")) {
    fprintf(stderr, "interp prov=%d ranges=%zu bary=%.3f,%.3f,%.3f verts=%d,%d,%d flat0=%d val=%.2f provval=%.2f\n",
            provoking_index, flat_varying_ranges_.size(), bary0, bary1, bary2,
            fragment.vertex[0], fragment.vertex[1], fragment.vertex[2],
            flat_varying_ranges_.empty() ? -1 : is_flat(0),
            out[0], provoking[0]);
  }
  if (GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_CUBE") && varying_floats_ >= 3) {
    static int cdbg = 0;
    if (cdbg++ < 4)
      fprintf(stderr,
              "[interp3] n=%d bary=%.3f,%.3f,%.3f dbary=%.4f,%.4f verts=%.3f/%.3f/%.3f out0=%.3f\n",
              varying_floats_, bary0, bary1, bary2, fragment.dbary[0][0],
              fragment.dbary[1][0], vertex0[0], vertex1[0], vertex2[0],
              out[0]);
  }
  const bool affine =
      (w0 == w1 && w1 == w2) || w0 == 0.0f || w1 == 0.0f || w2 == 0.0f;
  if (affine) {
    const float b0dx = fragment.dbary[0][0];
    const float b1dx = fragment.dbary[1][0];
    const float b2dx = fragment.dbary[2][0];
    const float b0dy = fragment.dbary[0][1];
    const float b1dy = fragment.dbary[1][1];
    const float b2dy = fragment.dbary[2][1];
    for (int i = 0; i < varying_floats_; ++i) {
      const bool flat = is_flat(i);
      out[i] = flat ? provoking[i]
                    : vertex0[i] * bary0 + vertex1[i] * bary1 +
                          vertex2[i] * bary2;
      if (flat) {
        if (varying_derivatives_) {
          varying_dx_values_[i] = provoking[i];
          varying_dy_values_[i] = provoking[i];
        }
        continue;
      }
      if (!varying_derivatives_)
        continue;
      varying_dx_values_[i] =
          vertex0[i] * (bary0 + b0dx) +
          vertex1[i] * (bary1 + b1dx) +
          vertex2[i] * (1.0f - bary0 - b0dx - bary1 - b1dx);
      varying_dy_values_[i] =
          vertex0[i] * (bary0 - b0dy) +
          vertex1[i] * (bary1 - b1dy) +
          vertex2[i] * (1.0f - bary0 + b0dy - bary1 + b1dy);
    }
    if (GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_VAR")) {
      static int vdbg = 0;
      if (vdbg++ < 2) {
        fprintf(stderr, "[var] n=%d bary=(%.4f,%.4f,%.4f) db=(%.4f,%.4f)\n",
                varying_floats_, bary0, bary1, bary2, b0dx, b0dy);
        for (int i = 0; i < varying_floats_ && i < 12; ++i)
          fprintf(stderr, "[var] i=%d v0=%.4f v1=%.4f v2=%.4f cur=%.4f dx=%.4f dy=%.4f\n",
                  i, vertex0[i], vertex1[i], vertex2[i], out[i],
                  varying_dx_values_[i], varying_dy_values_[i]);
      }
    }
    if (GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_DER")) {
      static int adbg = 0;
      if (adbg < 3) {
        fprintf(stderr, "[interp-aff] out0=%.4f dx0=%.4f dy0=%.4f\n", out[0],
                varying_dx_values_[0], varying_dy_values_[0]);
        ++adbg;
      }
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
  const auto perspective = [&](float b0, float b1, float b2, int i) {
    const float s0 = b0 / w0;
    const float s1 = b1 / w1;
    const float s2 = b2 / w2;
    const float sum = s0 + s1 + s2;
    if (sum == 0.0f)
      return vertex0[i];
    return (vertex0[i] * s0 + vertex1[i] * s1 + vertex2[i] * s2) / sum;
  };
  for (int i = 0; i < varying_floats_; ++i) {
    if (is_flat(i)) {
      out[i] = provoking[i];
      if (varying_derivatives_) {
        varying_dx_values_[i] = 0.0f;
        varying_dy_values_[i] = 0.0f;
      }
      continue;
    }
    out[i] = perspective(bary0, bary1, bary2, i);
    if (!varying_derivatives_)
      continue;
    const float bx0 = bary0 + fragment.dbary[0][0];
    const float bx1 = bary1 + fragment.dbary[1][0];
    const float bx2 = 1.0f - bx0 - bx1;
    const float by0 = bary0 - fragment.dbary[0][1];
    const float by1 = bary1 - fragment.dbary[1][1];
    const float by2 = 1.0f - by0 - by1;
    varying_dx_values_[i] = perspective(bx0, bx1, bx2, i);
    varying_dy_values_[i] = perspective(by0, by1, by2, i);
  }
}

void Pipeline::ApplyStencilOp(GLenum op,
                              size_t pixel,
                              uint8_t reference,
                              uint8_t write_mask) {
  const uint8_t current = target_.stencil[pixel];
  const uint8_t updated = StencilOp(op, current, reference);
  target_.stencil[pixel] = static_cast<uint8_t>(
      (updated & write_mask) | (current & ~write_mask));
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
  ShadeFragment(fragment, pixel, gl_y, state);
}

void Pipeline::ShadeBatch(const Fragment* fragments, int count) {
  if (!target_.valid() || count <= 0)
    return;
  const State& state = this->state();
  for (int i = 0; i < count; ++i) {
    const Fragment& fragment = fragments[i];
    if (fragment.x < 0 || fragment.y < 0 || fragment.x >= target_.width ||
        fragment.y >= target_.height)
      continue;
    const int gl_y = target_.height - 1 - fragment.y;
    if (state.scissor_test && !state.scissor.Contains(fragment.x, gl_y))
      continue;
    const size_t sample_offset =
        static_cast<size_t>(fragment.sample) * target_.sample_texels;
    const size_t pixel =
        static_cast<size_t>(fragment.y) * target_.width + fragment.x +
        sample_offset;
    ShadeFragment(fragment, pixel, gl_y, state);
  }
}

void Pipeline::ShadeFragment(const Fragment& fragment,
                             size_t pixel,
                             int gl_y,
                             const State& state) {
  context_->stats.fragments_shaded++;

  const float interpolated_depth =
      Clamp01(fragment.depth + (polygon_ ? depth_offset_ : 0.0f));

  const StencilFace* face =
      current_front_ ? &state.stencil.front : &state.stencil.back;
  const uint8_t reference = static_cast<uint8_t>(face->reference);
  const uint8_t compare_reference =
      reference & static_cast<uint8_t>(face->compare_mask);
  bool has_stencil = target_.stencil != nullptr;
  if (state.stencil.test && has_stencil) {
    const uint8_t stored =
        static_cast<uint8_t>(target_.stencil[pixel] &
                             face->compare_mask);
    if (!StencilTestPasses(face->func, compare_reference, stored)) {
      ApplyStencilOp(face->op_fail, pixel, reference, static_cast<uint8_t>(face->write_mask));
      return;
    }
  }

  const bool writes_depth = program_->uses_frag_depth();
  const bool has_depth = target_.depth != nullptr;
  const uint16_t interpolated_depth16 = ToDepth16(interpolated_depth);
  if (state.depth.test && has_depth && !writes_depth) {
    if (!DepthTestPasses16(state.depth.func, interpolated_depth16,
                           target_.depth[pixel])) {
      if (state.stencil.test && has_stencil)
        ApplyStencilOp(face->op_depth_fail, pixel, reference, static_cast<uint8_t>(face->write_mask));
      return;
    }
  }

  const bool profile = GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_PROFILE");
  const uint64_t profile_start = profile ? ProfileNowNs() : 0;
  Interpolate(fragment, varying_values_.data());
  const uint64_t profile_after_interp = profile ? ProfileNowNs() : 0;
  if (GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_VIEWPORT") && varying_floats_ >= 2) {
    static int fv_debug[8] = {};
    const unsigned program = static_cast<unsigned>(context_->state.program);
    if (program < 8 && fv_debug[program] < 3) {
      fprintf(stderr, "[fragvary] prog=%u pix=%zu %.4f %.4f bary=%.3f,%.3f,%.3f\n",
              program, pixel, varying_values_[0], varying_values_[1],
              fragment.bary[0], fragment.bary[1], fragment.bary[2]);
      ++fv_debug[program];
    }
  }
  glsl::FragmentInput input;
  input.varyings = varying_values_.data();
  input.varyings_dx = varying_dx_values_.data();
  input.varyings_dy = varying_dy_values_.data();
  input.frag_coord[0] =
      static_cast<float>(fragment.x) + 0.5f + fragment.sample_dx;
  input.frag_coord[1] =
      static_cast<float>(gl_y) + 0.5f + fragment.sample_dy;
  input.frag_coord[2] = interpolated_depth;
  input.frag_coord[3] = 1.0f;
  {
    const float* clip0 = clip_.data() +
                         static_cast<size_t>(fragment.vertex[0]) * stride_ + 3;
    const float* clip1 = clip_.data() +
                         static_cast<size_t>(fragment.vertex[1]) * stride_ + 3;
    const float* clip2 = clip_.data() +
                         static_cast<size_t>(fragment.vertex[2]) * stride_ + 3;
    if (*clip0 != 0.0f && *clip1 != 0.0f && *clip2 != 0.0f) {
      input.frag_coord[3] = fragment.bary[0] / *clip0 +
                            fragment.bary[1] / *clip1 +
                            fragment.bary[2] / *clip2;
    }
  }
  input.depth_range[0] = state.depth.range_near;
  input.depth_range[1] = state.depth.range_far;
  input.depth_range[2] = state.depth.range_far - state.depth.range_near;
  input.front_facing = current_front_;
  input.point_coord[0] = fragment.point_coord[0];
  input.point_coord[1] = fragment.point_coord[1];
  input.sample_id = fragment.sample;
  input.uniforms = uniforms_;
  input.sampler = this;
  glsl::FragmentOutput output;
  int simple_unit_slot = -1;
  int simple_literal_unit = 0;
  int simple_offset = 0;
  int simple_output = 0;
  bool handled = false;
  const glsl::Program::SimpleConvolution& convolution =
      program_->simple_convolution();
  if (convolution.valid) {
    const int unit = convolution.uniform_slot >= 0
                         ? static_cast<int>(
                               uniforms_[convolution.uniform_slot].data[0])
                         : convolution.literal_unit;
    const float u = varying_values_[convolution.varying_offset];
    const float v = varying_values_[convolution.varying_offset + 1];
    float accum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    bool sampled = true;
    for (const glsl::ConvolutionTap& tap : convolution.taps) {
      float color[4];
      if (!Sample2DRgba8Fast(unit, u + tap.dx * convolution.step_x,
                             v + tap.dy * convolution.step_y, color)) {
        sampled = false;
        break;
      }
      accum[0] += color[0] * tap.weight;
      accum[1] += color[1] * tap.weight;
      accum[2] += color[2] * tap.weight;
    }
    if (sampled) {
      const int location = convolution.output_location;
      output.color[location][0] = accum[0] * convolution.scale;
      output.color[location][1] = accum[1] * convolution.scale;
      output.color[location][2] = accum[2] * convolution.scale;
      output.color[location][3] = 1.0f;
      output.color_base[location] = glsl::BaseType::kFloat;
      output.written[location] = true;
      handled = true;
    }
  }
  if (!handled && program_->simple_texture_sample(&simple_unit_slot, &simple_literal_unit,
                                    &simple_offset, &simple_output)) {
    const int unit = simple_unit_slot >= 0
                         ? static_cast<int>(uniforms_[simple_unit_slot].data[0])
                         : simple_literal_unit;
    float color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    const float u = varying_values_[simple_offset];
    const float v = varying_values_[simple_offset + 1];
    if (Sample2DRgba8Fast(unit, u, v, color)) {
      output.color[simple_output][0] = color[0];
      output.color[simple_output][1] = color[1];
      output.color[simple_output][2] = color[2];
      output.color[simple_output][3] = color[3];
      output.color_base[simple_output] = glsl::BaseType::kFloat;
      output.written[simple_output] = true;
      handled = true;
    }
  }
  if (!handled && !program_->RunFragment(input, &output))
    return;
  const uint64_t profile_after_shader = profile ? ProfileNowNs() : 0;
  if (output.discarded)
    return;

  // Resolve the per-sample coverage mask.  Alpha-to-coverage and explicit
  // sample coverage both map a scalar in [0, 1] to a subset of the samples.
  if (target_.samples > 1) {
    const int sample_count = target_.samples >= 4 ? 4 : target_.samples;
    const float threshold =
        (static_cast<float>(fragment.sample) + 0.5f) /
        static_cast<float>(sample_count);
    if (state.sample_coverage) {
      // Inversion complements the same per-sample mask, so drawing with
      // non-inverted and inverted coverage covers every sample exactly once.
      bool covered = Clamp01(state.sample_coverage_value) > threshold;
      if (state.invert_coverage)
        covered = !covered;
      if (!covered)
        return;
    }
    if (state.sample_alpha_to_coverage &&
        Clamp01(output.color[0][3]) <= threshold)
      return;
  }

  uint16_t depth16 = interpolated_depth16;
  if (output.writes_depth) {
    depth16 = ToDepth16(Clamp01(output.depth));
    if (state.depth.test && has_depth &&
        !DepthTestPasses16(state.depth.func, depth16,
                           target_.depth[pixel])) {
      if (state.stencil.test && has_stencil)
        ApplyStencilOp(face->op_depth_fail, pixel, reference, static_cast<uint8_t>(face->write_mask));
      return;
    }
  }

  if (state.stencil.test && has_stencil)
    ApplyStencilOp(face->op_pass, pixel, reference, static_cast<uint8_t>(face->write_mask));

  if (state.depth.test && has_depth && state.depth.mask)
    target_.depth[pixel] = depth16;

  bool wrote_color = false;
  for (int location = 0; location < glsl::kMaxFragmentOutputs; ++location) {
    if (!output.written[location])
      continue;
    StoreFragmentColor(pixel, location, output.color[location],
                       output.color_int[location],
                       output.color_base[location]);
    wrote_color = true;
  }
  if (wrote_color)
    context_->stats.fragments_written++;
  if (profile) {
    const uint64_t profile_end = ProfileNowNs();
    g_profile_interp_ns += profile_after_interp - profile_start;
    g_profile_shader_ns += profile_after_shader - profile_after_interp;
    g_profile_store_ns += profile_end - profile_after_shader;
    ++g_profile_fragments;
  }
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
                                  const uint32_t color_int[4],
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
  if (GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_INT") && !target_.color_is_integer[attachment]) {
    static int fb_debug = 0;
    if (fb_debug < 12 && (fb_debug < 3 || pixel % 4096 == 0)) {
      fprintf(stderr, "[fbstore] pix=%zu base=%d color=%.4f %.4f %.4f %.4f dst=%p\n",
              pixel, static_cast<int>(base), color[0], color[1], color[2],
              color[3], static_cast<void*>(destination));
      ++fb_debug;
    }
  }
  const State& state = this->state();
  if (target_.color_is_integer[attachment]) {
    uint32_t values[4];
    const bool unsigned_output =
        target_.color_is_unsigned[attachment] || base == glsl::BaseType::kUInt;
    const bool exact_integer =
        base == glsl::BaseType::kInt || base == glsl::BaseType::kUInt;
    for (int i = 0; i < 4; ++i) {
      if (exact_integer) {
        values[i] = color_int[i];
      } else if (unsigned_output) {
        values[i] = static_cast<uint32_t>(
            std::clamp(color[i], 0.0f, 4294967295.0f));
      } else if (color[i] >= 2147483520.0f) {
        values[i] = 0x7fffffffU;
      } else {
        const float clamped =
            std::clamp(color[i], -2147483648.0f, 2147483520.0f);
        values[i] = static_cast<uint32_t>(static_cast<int64_t>(clamped));
      }
    }
    uint32_t* const integer_destination =
        target_.integer_colors[attachment];
    if (GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_INT") && integer_destination) {
      static int int_debug = 0;
      if (int_debug < 6) {
        fprintf(stderr, "[intstore] pix=%zu base=%d ints=%u,%u,%u,%u dst=%p\n",
                pixel, static_cast<int>(base), values[0], values[1], values[2],
                values[3], static_cast<void*>(integer_destination));
        ++int_debug;
      }
    }
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
  if (GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_RAST")) {
    static int bdbg = 0;
    if (bdbg < 4) {
      fprintf(stderr, "[blend] enabled=%d src=%x dst=%x\n",
              state.blend.enabled ? 1 : 0, state.blend.src_rgb,
              state.blend.dst_rgb);
      ++bdbg;
    }
  }
  // Fixed-point fast path for the common GL_FUNC_ADD cases.  All source and
  // destination factors are derived from 8-bit values, matching what a
  // byte-oriented GPU does; the generic float path below remains for
  // constant-colour factors and the subtract/min/max equations.
  if (state.blend.enabled && !target_.color_is_srgb[attachment] &&
      state.blend.equation_rgb == GL_FUNC_ADD &&
      state.blend.equation_alpha == GL_FUNC_ADD &&
      state.color_mask[0] && state.color_mask[1] && state.color_mask[2] &&
      state.color_mask[3] &&
      FastBlendFactorSupported(state.blend.src_rgb) &&
      FastBlendFactorSupported(state.blend.dst_rgb) &&
      FastBlendFactorSupported(state.blend.src_alpha) &&
      FastBlendFactorSupported(state.blend.dst_alpha)) {
    if (state.blend.src_rgb == GL_SRC_ALPHA &&
        state.blend.dst_rgb == GL_ONE_MINUS_SRC_ALPHA &&
        state.blend.src_alpha == GL_SRC_ALPHA &&
        state.blend.dst_alpha == GL_ONE_MINUS_SRC_ALPHA) {
      uint32_t existing;
      memcpy(&existing, destination + pixel * 4, sizeof(existing));
      const uint8_t dst[4] = {
          static_cast<uint8_t>(existing & 0xFFu),
          static_cast<uint8_t>((existing >> 8) & 0xFFu),
          static_cast<uint8_t>((existing >> 16) & 0xFFu),
          static_cast<uint8_t>((existing >> 24) & 0xFFu)};
      uint8_t src[4];
      for (int i = 0; i < 4; ++i)
        src[i] = FloatToUnorm8(color[i]);
      const int alpha = src[3];
      const int one_minus_alpha = 255 - alpha;
      uint8_t out[4];
      for (int i = 0; i < 3; ++i) {
        const int sum = static_cast<int>(src[i]) * alpha +
                        static_cast<int>(dst[i]) * one_minus_alpha;
        out[i] = static_cast<uint8_t>((sum + 127) / 255);
      }
      const int alpha_sum = alpha * alpha +
                            static_cast<int>(dst[3]) * one_minus_alpha;
      out[3] = static_cast<uint8_t>((alpha_sum + 127) / 255);
      const uint32_t packed = PackRgba(out[0], out[1], out[2], out[3]);
      memcpy(destination + pixel * 4, &packed, sizeof(packed));
      return;
    }
    uint32_t existing;
    memcpy(&existing, destination + pixel * 4, sizeof(existing));
    const uint8_t dst[4] = {
        static_cast<uint8_t>(existing & 0xFFu),
        static_cast<uint8_t>((existing >> 8) & 0xFFu),
        static_cast<uint8_t>((existing >> 16) & 0xFFu),
        static_cast<uint8_t>((existing >> 24) & 0xFFu)};
    uint8_t src[4];
    for (int i = 0; i < 4; ++i)
      src[i] = FloatToUnorm8(color[i]);
    uint8_t out[4];
    for (int i = 0; i < 3; ++i) {
      const int sf = FastBlendFactorByte(state.blend.src_rgb, src, dst, i);
      const int df = FastBlendFactorByte(state.blend.dst_rgb, src, dst, i);
      const int sum = static_cast<int>(src[i]) * sf +
                      static_cast<int>(dst[i]) * df;
      out[i] = static_cast<uint8_t>(std::min(255, (sum + 127) / 255));
    }
    {
      const int sf = FastBlendFactorByte(state.blend.src_alpha, src, dst, 3);
      const int df = FastBlendFactorByte(state.blend.dst_alpha, src, dst, 3);
      const int sum = static_cast<int>(src[3]) * sf +
                      static_cast<int>(dst[3]) * df;
      out[3] = static_cast<uint8_t>(std::min(255, (sum + 127) / 255));
    }
    const uint32_t packed = PackRgba(out[0], out[1], out[2], out[3]);
    memcpy(destination + pixel * 4, &packed, sizeof(packed));
    return;
  }
  if (state.blend.enabled) {
    float destination_color[4];
    uint32_t existing;
    memcpy(&existing, destination + pixel * 4, sizeof(existing));
    UnpackRgba(existing, destination_color);
    if (target_.color_is_srgb[attachment]) {
      const Rgba stored = UnpackRgba(existing);
      destination_color[0] = SrgbToLinear(stored.r);
      destination_color[1] = SrgbToLinear(stored.g);
      destination_color[2] = SrgbToLinear(stored.b);
      destination_color[3] = static_cast<float>(stored.a) / 255.0f;
    }
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
    const bool srgb = target_.color_is_srgb[attachment];
    for (int i = 0; i < 4; ++i) {
      if (!state.color_mask[i]) {
        const Rgba previous = UnpackRgba(existing);
        bytes[i] = i == 0   ? previous.r
                   : i == 1 ? previous.g
                   : i == 2 ? previous.b
                            : previous.a;
      } else {
        bytes[i] = srgb && i < 3 ? LinearToSrgb8(result[i])
                                 : FloatToUnorm8(result[i]);
      }
    }
    const uint32_t packed = PackRgba(bytes[0], bytes[1], bytes[2], bytes[3]);
    memcpy(destination + pixel * 4, &packed, sizeof(packed));
    return;
  }
  uint8_t bytes[4];
  const bool srgb = target_.color_is_srgb[attachment];
  if (GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_SRGB")) {
    static int dbg = 0;
    if (dbg++ < 4)
      fprintf(stderr, "[store] attach=%d srgb=%d color=%.4f %.4f %.4f\n",
              attachment, (int)srgb, color[0], color[1], color[2]);
  }
  for (int i = 0; i < 4; ++i)
    bytes[i] = srgb && i < 3 ? LinearToSrgb8(color[i])
                             : FloatToUnorm8(color[i]);
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

Pipeline::CachedTexture2D Pipeline::BuildCachedTexture2D(int slot) const {
  CachedTexture2D cached;
  const GLuint name = state().textures_2d[slot];
  const Texture* texture = context_->textures.Get(name);
  if (!texture)
    return cached;
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
  cached.valid = true;
  cached.texture = texture;
  cached.min_filter = min_filter;
  cached.mag_filter = mag_filter;
  cached.wrap_s = wrap_s;
  cached.wrap_t = wrap_t;
  cached.mask_alpha = texture->internal_format != GL_RGBA &&
                      texture->internal_format != GL_RGBA8;
  cached.needs_lod =
      texture->levels > 1 &&
      (min_filter == GL_NEAREST_MIPMAP_NEAREST ||
       min_filter == GL_LINEAR_MIPMAP_NEAREST ||
       min_filter == GL_NEAREST_MIPMAP_LINEAR ||
       min_filter == GL_LINEAR_MIPMAP_LINEAR);
  const TextureImage* image = texture->image(texture->base_level, 0);
  cached.image = image;
  cached.flip_y = texture->render_target_flip_y;
  cached.opaque = FormatColorChannels(texture->internal_format) < 4;
  const bool identity_swizzle =
      (sampler ? (sampler->swizzle_r == GL_RED &&
                  sampler->swizzle_g == GL_GREEN &&
                  sampler->swizzle_b == GL_BLUE &&
                  sampler->swizzle_a == GL_ALPHA)
               : (texture->swizzle_r == GL_RED &&
                  texture->swizzle_g == GL_GREEN &&
                  texture->swizzle_b == GL_BLUE &&
                  texture->swizzle_a == GL_ALPHA));
  cached.direct_rgba8 =
      image && image->width > 0 && image->height > 0 &&
      !image->rgba.empty() && !image->srgb && texture->samples == 0 &&
      identity_swizzle &&
      (texture->internal_format == GL_RGBA8 ||
       texture->internal_format == GL_RGBA ||
       texture->internal_format == GL_RGB8 ||
       texture->internal_format == GL_RGB) &&
      min_filter == mag_filter &&
      (min_filter == GL_NEAREST || min_filter == GL_LINEAR);
  return cached;
}

bool Pipeline::CacheTextureBindings() {
  for (int slot = 0; slot < kMaxCombinedTextureUnits; ++slot)
    cached_textures_2d_[slot].valid = false;
  if (active_texture_units_.empty()) {
    for (int slot = 0; slot < kMaxCombinedTextureUnits; ++slot)
      cached_textures_2d_[slot] = BuildCachedTexture2D(slot);
    return true;
  }
  for (int unit : active_texture_units_) {
    const int slot = ClampInt(unit, 0, kMaxCombinedTextureUnits - 1);
    cached_textures_2d_[slot] = BuildCachedTexture2D(slot);
  }
  return true;
}

bool Pipeline::NeedsImplicitLod(int unit) {
  const int slot = ClampInt(unit, 0, kMaxCombinedTextureUnits - 1);
  CachedTexture2D dynamic_cache;
  const CachedTexture2D* cached_ptr = &cached_textures_2d_[slot];
  if (!cached_ptr->valid) {
    dynamic_cache = BuildCachedTexture2D(slot);
    cached_ptr = &dynamic_cache;
  }
  const CachedTexture2D& cached = *cached_ptr;
  if (cached.texture)
    return cached.needs_lod;
  // Fall back to cube/array/3D units whose textures are not in the 2D table.
  const Texture* texture = context_->textures.Get(state().textures_2d_array[slot]);
  if (!texture)
    texture = context_->textures.Get(state().textures_3d[slot]);
  if (!texture)
    texture = context_->textures.Get(state().textures_cube[slot]);
  if (!texture || texture->levels <= 1)
    return false;
  switch (MinFilter(unit)) {
    case GL_NEAREST_MIPMAP_NEAREST:
    case GL_LINEAR_MIPMAP_NEAREST:
    case GL_NEAREST_MIPMAP_LINEAR:
    case GL_LINEAR_MIPMAP_LINEAR:
      return true;
    default:
      return false;
  }
}

uint32_t Pipeline::MinFilter(int unit) {
  const int slot = ClampInt(unit, 0, kMaxCombinedTextureUnits - 1);
  const Sampler* sampler = context_->samplers.Get(state().samplers[slot]);
  if (sampler)
    return sampler->min_filter;
  const Texture* texture = context_->textures.Get(state().textures_2d[slot]);
  if (!texture)
    texture = context_->textures.Get(state().textures_2d_array[slot]);
  if (!texture)
    texture = context_->textures.Get(state().textures_3d[slot]);
  if (!texture)
    texture = context_->textures.Get(state().textures_cube[slot]);
  return texture ? texture->min_filter : GL_NEAREST;
}

void Pipeline::Size(int unit, int out[3]) {
  out[0] = 0;
  out[1] = 0;
  out[2] = 0;
  const int slot = ClampInt(unit, 0, kMaxCombinedTextureUnits - 1);
  const Texture* texture =
      context_->textures.Get(state().textures_2d_array[slot]);
  if (!texture)
    texture = context_->textures.Get(state().textures_3d[slot]);
  if (!texture)
    texture = context_->textures.Get(state().textures_2d[slot]);
  if (!texture)
    texture = context_->textures.Get(state().textures_cube[slot]);
  if (!texture)
    return;
  const int base_level = std::max(0, texture->base_level);
  out[0] = std::max(1, texture->width >> base_level);
  out[1] = std::max(1, texture->height >> base_level);
  if (texture->target == GL_TEXTURE_3D)
    out[2] = std::max(1, texture->depth >> base_level);
  else if (texture->target == GL_TEXTURE_2D_ARRAY)
    out[2] = texture->depth;  // Layer count does not shrink with mip levels.
  else
    out[2] = 1;
}

void Pipeline::SizeLod(int unit, int lod, int out[3]) {
  Size(unit, out);
  const int slot = ClampInt(unit, 0, kMaxCombinedTextureUnits - 1);
  const Texture* texture =
      context_->textures.Get(state().textures_2d[slot]);
  if (!texture)
    texture = context_->textures.Get(state().textures_cube[slot]);
  if (!texture)
    texture = context_->textures.Get(state().textures_2d_array[slot]);
  if (!texture)
    texture = context_->textures.Get(state().textures_3d[slot]);
  const int base_level = texture ? std::max(0, texture->base_level) : 0;
  const int max_level =
      texture ? std::max(0, texture->levels - 1 - base_level) : 0;
  const int level = ClampInt(lod, 0, max_level);
  out[0] = std::max(1, out[0] >> level);
  out[1] = std::max(1, out[1] >> level);
  // Only a 3D texture has a mipmapped depth; a 2D array keeps its layer count.
  if (texture && texture->target == GL_TEXTURE_3D)
    out[2] = std::max(1, out[2] >> level);
}

namespace {

void MaskSampledAlpha(GLenum internal_format, float out[4]);

// Bound checks shared by the texelFetch entry points.
int ClampFetchCoord(int coord, int size) {
  if (size <= 0)
    return 0;
  return ClampInt(coord, 0, size - 1);
}

}  // namespace

void Pipeline::Fetch2D(int unit, int x, int y, int lod, float out[4]) {
  out[0] = 0.0f;
  out[1] = 0.0f;
  out[2] = 0.0f;
  out[3] = 1.0f;
  const int slot = ClampInt(unit, 0, kMaxCombinedTextureUnits - 1);
  const Texture* texture = context_->textures.Get(state().textures_2d[slot]);
  if (!texture)
    return;
  const TextureImage* image = texture->image(
      ClampInt(lod, 0, 30) + std::max(0, texture->base_level), 0);
  if (!image || image->width <= 0 || image->height <= 0)
    return;
  const int cx = ClampFetchCoord(x, image->width);
  const int cy = ClampFetchCoord(y, image->height);
  FetchTexelImage(*image, static_cast<size_t>(cy) * image->width + cx, out);
  MaskSampledAlpha(texture->internal_format, out);
}

void Pipeline::Fetch3D(int unit, int x, int y, int z, int lod, float out[4]) {
  out[0] = 0.0f;
  out[1] = 0.0f;
  out[2] = 0.0f;
  out[3] = 1.0f;
  const int slot = ClampInt(unit, 0, kMaxCombinedTextureUnits - 1);
  const Texture* texture = context_->textures.Get(state().textures_3d[slot]);
  if (!texture)
    return;
  const TextureImage* image = texture->image(
      ClampInt(lod, 0, 30) + std::max(0, texture->base_level), 0);
  if (!image || image->width <= 0 || image->height <= 0)
    return;
  const int cx = ClampFetchCoord(x, image->width);
  const int cy = ClampFetchCoord(y, image->height);
  const int cz = ClampFetchCoord(z, image->depth);
  FetchTexelImage(*image,
             (static_cast<size_t>(cz) * image->height + cy) *
                     static_cast<size_t>(image->width) +
                 cx,
             out);
  MaskSampledAlpha(texture->internal_format, out);
}

void Pipeline::Fetch2DArray(int unit,
                            int x,
                            int y,
                            int layer,
                            int lod,
                            float out[4]) {
  out[0] = 0.0f;
  out[1] = 0.0f;
  out[2] = 0.0f;
  out[3] = 1.0f;
  const int slot = ClampInt(unit, 0, kMaxCombinedTextureUnits - 1);
  const Texture* texture =
      context_->textures.Get(state().textures_2d_array[slot]);
  if (!texture)
    return;
  const int level = ClampInt(lod, 0, 30);
  const TextureImage* image =
      texture->image(level + std::max(0, texture->base_level), 0);
  if (GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_TEX"))
    fprintf(stderr,
            "[fetch2da] unit=%d x=%d y=%d layer=%d lod=%d depth=%d levels=%d "
            "img=%dx%dx%d base=%d\n",
            unit, x, y, layer, lod, texture->depth, texture->levels,
            image ? image->width : -1, image ? image->height : -1,
            image ? image->depth : -1, texture->base_level);
  if (!image || image->width <= 0 || image->height <= 0)
    return;
  const int face = ClampInt(layer, 0, std::max(0, image->depth - 1));
  const int cx = ClampFetchCoord(x, image->width);
  const int cy = ClampFetchCoord(y, image->height);
  const size_t plane =
      static_cast<size_t>(image->width) * static_cast<size_t>(image->height);
  FetchTexelImage(*image,
                  static_cast<size_t>(face) * plane +
                      static_cast<size_t>(cy) * image->width + cx,
                  out);
  MaskSampledAlpha(texture->internal_format, out);
}

namespace {

void MaskSampledAlpha(GLenum internal_format, float out[4]) {
  if (GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_ALPHA"))
    fprintf(stderr, "[alpha] %x %d %g %g %g %g", internal_format, FormatColorChannels(internal_format), out[0], out[1], out[2], out[3]);
  // Legacy single-channel formats keep their non-color channels explicit.
  if (internal_format == GL_ALPHA) {
    out[0] = 0.0f;
    out[1] = 0.0f;
    out[2] = 0.0f;
    return;
  }
  if (internal_format == GL_LUMINANCE) {
    out[3] = 1.0f;
    return;
  }
  if (internal_format == GL_LUMINANCE_ALPHA)
    return;
  const int channels = FormatColorChannels(internal_format);
  for (int channel = channels; channel < 4; ++channel)
    out[channel] = channel == 3 ? 1.0f : 0.0f;
}

}  // namespace

namespace {
void ApplySamplerSwizzle(const Texture* texture,
                         const Sampler* sampler,
                         float rgba[4]);
}  // namespace

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
  CachedTexture2D dynamic_cache;
  const CachedTexture2D* cached_ptr = &cached_textures_2d_[slot];
  if (!cached_ptr->valid) {
    dynamic_cache = BuildCachedTexture2D(slot);
    cached_ptr = &dynamic_cache;
  }
  const CachedTexture2D& cached = *cached_ptr;
  const Texture* texture = cached.texture;
  if (!texture)
    return;
  const GLenum min_filter = cached.min_filter;
  const GLenum mag_filter = cached.mag_filter;
  const GLenum wrap_s = cached.wrap_s;
  const GLenum wrap_t = cached.wrap_t;
  const Sampler* sampler = context_->samplers.Get(state().samplers[slot]);
  if (GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_VIEWPORT") &&
      (unit != 0 || (u < 0.01f && v > 0.97f))) {
    static int sd = 0;
    if (sd < 200) {
      const Texture* dbg_tex = context_->textures.Get(state().textures_2d[slot]);
      fprintf(stderr, "[s2d] unit=%d tex=%u size=%dx%d lod=%.4f min=0x%x mag=0x%x uv=%.4f,%.4f\n",
              unit, (unsigned)state().textures_2d[slot],
              dbg_tex ? (dbg_tex->image(0, 0) ? dbg_tex->image(0, 0)->width : -1) : -1,
              dbg_tex ? (dbg_tex->image(0, 0) ? dbg_tex->image(0, 0)->height : -1) : -1,
              lod_bias, min_filter, mag_filter, u, v);
      ++sd;
    }
  }
  SampleTexture2D(*texture, u, v, lod_bias, min_filter, mag_filter, wrap_s,
                  wrap_t, out,
                  sampler ? sampler->min_lod : texture->min_lod,
                  sampler ? sampler->max_lod : texture->max_lod);
  if (cached.mask_alpha)
    MaskSampledAlpha(texture->internal_format, out);
  ApplySamplerSwizzle(texture, sampler, out);
}

bool Pipeline::Sample2DRgba8Fast(int unit,
                                 float u,
                                 float v,
                                 float out[4]) {
  const int slot = ClampInt(unit, 0, kMaxCombinedTextureUnits - 1);
  if (!cached_textures_2d_[slot].valid) {
    // The per-draw cache normally covers every used unit.  Only the rare
    // uncached unit needs a constructed temporary; keeping it out of the
    // cached path avoids re-initialising the whole descriptor for every
    // texture tap in a convolution shader.
    CachedTexture2D dynamic = BuildCachedTexture2D(slot);
    if (!dynamic.direct_rgba8 || !dynamic.image)
      return false;
    SampleTexture2DRgba8Fast(*dynamic.image, u, v, dynamic.min_filter,
                             dynamic.wrap_s, dynamic.wrap_t, dynamic.flip_y,
                             dynamic.opaque, out);
    return true;
  }
  const CachedTexture2D& cached = cached_textures_2d_[slot];
  if (!cached.direct_rgba8 || !cached.image)
    return false;
  SampleTexture2DRgba8Fast(*cached.image, u, v, cached.min_filter,
                           cached.wrap_s, cached.wrap_t, cached.flip_y,
                           cached.opaque, out);
  return true;
}

namespace {

float CompareDepth(float reference, float depth, GLenum func) {
  bool pass = false;  switch (func) {
    case GL_NEVER:
      break;
    case GL_LESS:
      pass = reference < depth;
      break;
    case GL_EQUAL:
      pass = reference == depth;
      break;
    case GL_LEQUAL:
      pass = reference <= depth;
      break;
    case GL_GREATER:
      pass = reference > depth;
      break;
    case GL_NOTEQUAL:
      pass = reference != depth;
      break;
    case GL_GEQUAL:
      pass = reference >= depth;
      break;
    case GL_ALWAYS:
    default:
      pass = true;
      break;
  }
  if (GLANCEASY_DEBUG_FLAG("GLANCEASY_CMP_INVERT"))
    pass = !pass;
  return pass ? 1.0f : 0.0f;
}

void ApplySamplerSwizzle(const Texture* texture,
                         const Sampler* sampler,
                         float rgba[4]) {
  if (!texture && !sampler)
    return;
  const float in[4] = {rgba[0], rgba[1], rgba[2], rgba[3]};
  GLenum sel[4];
  if (sampler) {
    sel[0] = sampler->swizzle_r;
    sel[1] = sampler->swizzle_g;
    sel[2] = sampler->swizzle_b;
    sel[3] = sampler->swizzle_a;
  } else {
    sel[0] = texture->swizzle_r;
    sel[1] = texture->swizzle_g;
    sel[2] = texture->swizzle_b;
    sel[3] = texture->swizzle_a;
  }
  for (int i = 0; i < 4; ++i) {
    switch (sel[i]) {
      case GL_RED:
        rgba[i] = in[0];
        break;
      case GL_GREEN:
        rgba[i] = in[1];
        break;
      case GL_BLUE:
        rgba[i] = in[2];
        break;
      case GL_ALPHA:
        rgba[i] = in[3];
        break;
      case GL_ZERO:
        rgba[i] = 0.0f;
        break;
      case GL_ONE:
        rgba[i] = 1.0f;
        break;
      default:
        rgba[i] = in[i];
        break;
    }
  }
}

}  // namespace

void Pipeline::Sample2DShadow(int unit,
                              float u,
                              float v,
                              float ref,
                              float lod_bias,
                              float out[4]) {
  out[0] = 0.0f;
  out[1] = 0.0f;
  out[2] = 0.0f;
  out[3] = 1.0f;
  const int slot = ClampInt(unit, 0, kMaxCombinedTextureUnits - 1);
  const Texture* texture = context_->textures.Get(state().textures_2d[slot]);
  if (!texture)
    return;
  GLenum min_filter = texture->min_filter;
  GLenum mag_filter = texture->mag_filter;
  GLenum wrap_s = texture->wrap_s;
  GLenum wrap_t = texture->wrap_t;
  GLenum compare_func = texture->compare_func;
  const Sampler* sampler = context_->samplers.Get(state().samplers[slot]);
  if (sampler) {
    min_filter = sampler->min_filter;
    mag_filter = sampler->mag_filter;
    wrap_s = sampler->wrap_s;
    wrap_t = sampler->wrap_t;
    compare_func = sampler->compare_func;
  }
  float depth[4];
  SampleTexture2D(*texture, u, v, lod_bias, min_filter, mag_filter, wrap_s,
                  wrap_t, depth,
                  sampler ? sampler->min_lod : texture->min_lod,
                  sampler ? sampler->max_lod : texture->max_lod);
  out[0] = CompareDepth(ref, depth[0], compare_func);
}

void Pipeline::Sample2DArrayShadow(int unit,
                                   float u,
                                   float v,
                                   float layer,
                                   float ref,
                                   float lod_bias,
                                   float out[4]) {
  out[0] = 0.0f;
  out[1] = 0.0f;
  out[2] = 0.0f;
  out[3] = 1.0f;
  const int slot = ClampInt(unit, 0, kMaxCombinedTextureUnits - 1);
  const Texture* texture =
      context_->textures.Get(state().textures_2d_array[slot]);
  if (!texture)
    return;
  GLenum min_filter = texture->min_filter;
  GLenum mag_filter = texture->mag_filter;
  GLenum wrap_s = texture->wrap_s;
  GLenum wrap_t = texture->wrap_t;
  GLenum compare_func = texture->compare_func;
  const Sampler* sampler = context_->samplers.Get(state().samplers[slot]);
  if (sampler) {
    min_filter = sampler->min_filter;
    mag_filter = sampler->mag_filter;
    wrap_s = sampler->wrap_s;
    wrap_t = sampler->wrap_t;
    compare_func = sampler->compare_func;
  }
  float depth[4];
  float layer_arg = layer;
  if (const char* off = GLANCEASY_DEBUG_FLAG("GLANCEASY_LAYER_OFF"))
    layer_arg += static_cast<float>(atof(off));
  const bool swap_uv = GLANCEASY_DEBUG_FLAG("GLANCEASY_SHADOW_SWAP") != nullptr;
  const float su = GLANCEASY_DEBUG_FLAG("GLANCEASY_SHADOW_FLIP_S") ? 1.0f - (swap_uv ? v : u)
                                               : (swap_uv ? v : u);
  const float sv = GLANCEASY_DEBUG_FLAG("GLANCEASY_SHADOW_FLIP_T") ? 1.0f - (swap_uv ? u : v)
                                               : (swap_uv ? u : v);
  const GLenum active_filter =
      lod_bias <= 0.0f ? mag_filter : min_filter;
  const bool pcf = active_filter == GL_LINEAR ||
                   active_filter == GL_LINEAR_MIPMAP_NEAREST ||
                   active_filter == GL_LINEAR_MIPMAP_LINEAR;
  if (pcf && texture->width > 0 && texture->height > 0) {
    // ES 3.0 applies the depth comparison before filtering, so a linear
    // shadow lookup averages four comparisons rather than comparing one
    // bilinearly filtered depth value.
    const float fx = su * static_cast<float>(texture->width) - 0.5f;
    const float fy = sv * static_cast<float>(texture->height) - 0.5f;
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const float ax = fx - static_cast<float>(x0);
    const float ay = fy - static_cast<float>(y0);
    auto probe = [&](int x, int y) {
      float d[4];
      SampleTexture2DArray(
          *texture,
          (static_cast<float>(x) + 0.5f) / static_cast<float>(texture->width),
          (static_cast<float>(y) + 0.5f) / static_cast<float>(texture->height),
          layer_arg, lod_bias, GL_NEAREST, GL_NEAREST, wrap_s, wrap_t, d,
          sampler ? sampler->min_lod : texture->min_lod,
          sampler ? sampler->max_lod : texture->max_lod);
      return CompareDepth(ref, d[0], compare_func);
    };
    out[0] = probe(x0, y0) * (1.0f - ax) * (1.0f - ay) +
             probe(x0 + 1, y0) * ax * (1.0f - ay) +
             probe(x0, y0 + 1) * (1.0f - ax) * ay +
             probe(x0 + 1, y0 + 1) * ax * ay;
  } else {
    float depth[4];
    SampleTexture2DArray(*texture, su, sv, layer_arg, lod_bias, min_filter,
                         mag_filter, wrap_s, wrap_t, depth,
                         sampler ? sampler->min_lod : texture->min_lod,
                         sampler ? sampler->max_lod : texture->max_lod);
    out[0] = CompareDepth(ref, depth[0], compare_func);
  }

  if (GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_TEXARR")) {
    static int cdbg = 0;
    if (cdbg++ < 5)
      fprintf(stderr, "[cmp] func=%x ref=%.4f depth=%.4f out=%.1f\n",
              compare_func, ref, depth[0], out[0]);
  }
}

void Pipeline::SampleCubeShadow(int unit,
                                const float direction[3],
                                float ref,
                                float lod_bias,
                                float out[4]) {
  out[0] = 0.0f;
  out[1] = 0.0f;
  out[2] = 0.0f;
  out[3] = 1.0f;
  const int slot = ClampInt(unit, 0, kMaxCombinedTextureUnits - 1);
  const Texture* texture =
      context_->textures.Get(state().textures_cube[slot]);
  if (!texture)
    return;
  GLenum min_filter = texture->min_filter;
  GLenum mag_filter = texture->mag_filter;
  GLenum compare_func = texture->compare_func;
  const Sampler* sampler = context_->samplers.Get(state().samplers[slot]);
  if (sampler) {
    min_filter = sampler->min_filter;
    mag_filter = sampler->mag_filter;
    compare_func = sampler->compare_func;
  }
  float depth[4];
  SampleTextureCube(*texture, direction, lod_bias, min_filter, mag_filter,
                    depth, compare_func, ref,
                    sampler ? sampler->min_lod : texture->min_lod,
                    sampler ? sampler->max_lod : texture->max_lod);
  out[0] = depth[0];
}

void Pipeline::Sample2DArray(int unit,
                             float u,
                             float v,
                             float layer,
                             float lod_bias,
                             float out[4]) {
  out[0] = 0.0f;
  out[1] = 0.0f;
  out[2] = 0.0f;
  out[3] = 1.0f;
  const int slot = ClampInt(unit, 0, kMaxCombinedTextureUnits - 1);
  const Texture* texture =
      context_->textures.Get(state().textures_2d_array[slot]);
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
  SampleTexture2DArray(*texture, u, v, layer, lod_bias, min_filter,
                       mag_filter, wrap_s, wrap_t, out,
                       sampler ? sampler->min_lod : texture->min_lod,
                       sampler ? sampler->max_lod : texture->max_lod);
  MaskSampledAlpha(texture->internal_format, out);
  ApplySamplerSwizzle(texture, sampler, out);
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
  const Texture* texture =
      context_->textures.Get(state().textures_cube[slot]);
  if (!texture)
    return;
  GLenum min_filter = texture->min_filter;
  GLenum mag_filter = texture->mag_filter;
  const Sampler* sampler = context_->samplers.Get(state().samplers[slot]);
  if (sampler) {
    min_filter = sampler->min_filter;
    mag_filter = sampler->mag_filter;
  }
  SampleTextureCube(*texture, direction, lod_bias, min_filter, mag_filter, out,
                    0, 0.0f,
                    sampler ? sampler->min_lod : texture->min_lod,
                    sampler ? sampler->max_lod : texture->max_lod);
  MaskSampledAlpha(texture->internal_format, out);
  ApplySamplerSwizzle(texture, sampler, out);
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
  const Texture* texture =
      context_->textures.Get(state().textures_3d[slot]);
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
                  wrap_t, wrap_r, out,
                  sampler ? sampler->min_lod : texture->min_lod,
                  sampler ? sampler->max_lod : texture->max_lod);
  MaskSampledAlpha(texture->internal_format, out);
  ApplySamplerSwizzle(texture, sampler, out);
}

}  // namespace

namespace {

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

int MinimumVertices(GLenum mode) {
  switch (mode) {
    case GL_POINTS:
      return 1;
    case GL_LINES:
    case GL_LINE_STRIP:
    case GL_LINE_LOOP:
      return 2;
    case GL_TRIANGLES:
    case GL_TRIANGLE_STRIP:
    case GL_TRIANGLE_FAN:
      return 3;
    default:
      return 1;
  }
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
  if (!ValidPrimitiveMode(mode)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (!context->GetDrawTarget().valid()) {
    context->SetError(GL_INVALID_FRAMEBUFFER_OPERATION);
    return;
  }
  if (count == 0 || instance_count == 0)
    return;
  Pipeline pipeline(context);
  const bool profile = GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_PROFILE");
  if (profile)
    ResetFragmentProfile();
  const auto start = std::chrono::steady_clock::now();
  const uint64_t shaded_before = context->stats.fragments_shaded;
  const uint64_t written_before = context->stats.fragments_written;
  pipeline.Run(mode, first, count, index_type, indices, instance_count);
  const auto after_run = std::chrono::steady_clock::now();
  pipeline.Resolve();
  if (GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_DRAW")) {
    const auto finish = std::chrono::steady_clock::now();
    const double run_us =
        std::chrono::duration<double, std::micro>(after_run - start).count();
    const double resolve_us =
        std::chrono::duration<double, std::micro>(finish - after_run).count();
    fprintf(stderr,
            "[draw] mode=0x%x count=%d inst=%d run=%.1fus resolve=%.1fus "
            "shaded=%llu written=%llu target=%dx%d"
            " interp=%.1fus shader=%.1fus store=%.1fus nfrag=%llu\n",
            static_cast<unsigned>(mode), count, instance_count, run_us,
            resolve_us, context->stats.fragments_shaded - shaded_before,
            context->stats.fragments_written - written_before,
            context->GetDrawTarget().width, context->GetDrawTarget().height,
            g_profile_interp_ns / 1000.0, g_profile_shader_ns / 1000.0,
            g_profile_store_ns / 1000.0,
            static_cast<unsigned long long>(g_profile_fragments));
  }
}

}  // namespace glanceasy
