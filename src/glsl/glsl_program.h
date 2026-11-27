// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZGL_SRC_GLSL_GLSL_PROGRAM_H_
#define ZGL_SRC_GLSL_GLSL_PROGRAM_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/glsl/glsl_ast.h"
#include "src/glsl/glsl_types.h"

namespace zgl {
namespace glsl {

class ShaderJit;

struct AttributeInfo {
  std::string name;
  Type type;
  int location = -1;
};

struct UniformInfo {
  std::string name;
  Type type;
  int location = -1;  // Equals the index into the program's value array.
  int block_index = -1;  // Uniform block owning this member, or -1.
  int block_offset = 0;
};

struct ActiveUniformInfo {
  std::string name;
  Type type;  // Element type; arrays are collapsed to a size + element.
  int size = 1;
  int uniform_index = -1;  // Parent entry in Program::uniforms().
  int block_index = -1;
  int block_offset = -1;
  int array_stride = 0;
};

struct UniformFieldInfo {
  std::string name;
  Type type;
  int parent = -1;         // Index into Program::uniforms().
  int component_offset = 0;
  int location = -1;       // Location returned by glGetUniformLocation().
};

struct UniformBlockMemberInfo {
  std::string name;
  Type type;
  int offset = 0;
  int matrix_layout = 0;
  int uniform_index = -1;  // Index into Program::uniforms().
};

struct UniformBlockInfo {
  std::string name;
  int size = 0;
  int matrix_layout = 0;  // 0 = column_major/default, 1 = row_major.
  bool std140 = false;
  bool packed = false;
  bool instance_array = false;
  std::string instance_name;
  int binding = 0;
  bool vertex_stage = false;
  bool fragment_stage = false;
  bool used = false;
  bool has_instance = false;
  std::vector<UniformBlockMemberInfo> members;
  std::vector<int> active_uniform_indices;
};

struct VaryingInfo {
  std::string name;
  Type type;
  int offset = 0;  // Float offset inside the varying arrays.
  int components = 0;
  bool flat = false;
};

struct TransformFeedbackVaryingInfo {
  std::string name;
  Type type;
  int offset = 0;  // Float offset inside the vertex varying array.
  int components = 0;
  int buffer_index = 0;  // Separate-attrib buffer, or 0 for interleaved.
};

// Sampling backend used by texture*() built-ins.  Lod bias is already
// clamped by the caller; implementations may ignore it.
class TextureSampler {
 public:
  virtual ~TextureSampler() = default;
  virtual void Sample2D(int unit,
                        float u,
                        float v,
                        float lod_bias,
                        float out[4]) = 0;
  virtual void SampleCube(int unit,
                          const float direction[3],
                          float lod_bias,
                          float out[4]) = 0;
  virtual void Sample3D(int unit,
                        const float position[3],
                        float lod_bias,
                        float out[4]) = 0;
  virtual void Sample2DArray(int unit,
                             float u,
                             float v,
                             float layer,
                             float lod_bias,
                             float out[4]) = 0;
  virtual void Sample2DShadow(int unit,
                              float u,
                              float v,
                              float ref,
                              float lod_bias,
                              float out[4]) = 0;
  virtual void Sample2DArrayShadow(int unit,
                                   float u,
                                   float v,
                                   float layer,
                                   float ref,
                                   float lod_bias,
                                   float out[4]) = 0;
  virtual void SampleCubeShadow(int unit,
                                const float direction[3],
                                float ref,
                                float lod_bias,
                                float out[4]) = 0;
  virtual void Size(int unit, int out[3]) = 0;
  // Active minification filter (sampler object overrides texture state).
  virtual uint32_t MinFilter(int unit) {
    (void)unit;
    return 0x2600u;  // GL_NEAREST
  }
  // Dimensions of mip level |lod| (clamped inside the implementation).
  // Falls back to the base level when the backend has only one level.
  virtual void SizeLod(int unit, int lod, int out[3]) {
    (void)lod;
    Size(unit, out);
  }
  // Unfiltered single-texel fetch backing texelFetch()/texelFetchOffset().
  virtual void Fetch2D(int unit, int x, int y, int lod, float out[4]) = 0;
  virtual void Fetch3D(int unit, int x, int y, int z, int lod, float out[4]) = 0;
  virtual void Fetch2DArray(int unit,
                            int x,
                            int y,
                            int layer,
                            int lod,
                            float out[4]) = 0;
};

struct VertexInput {
  // 4 floats per attribute slot, indexed by attribute location.  Integer
  // attributes also carry their exact bit patterns in |attribute_int|.
  const float* attributes = nullptr;
  const uint32_t* attribute_int = nullptr;
  int vertex_id = 0;
  int instance_id = 0;
  float depth_range[3] = {0.0f, 1.0f, 1.0f};
  const Value* uniforms = nullptr;
  const int* uniform_map = nullptr;  // Stage uniform index -> program index.
  TextureSampler* sampler = nullptr;
};

struct FragmentInput {
  const float* varyings = nullptr;  // Interpolated fragment-stage varyings.
  const float* varyings_dx = nullptr;  // d(varying)/dx in framebuffer pixels.
  const float* varyings_dy = nullptr;  // d(varying)/dy in framebuffer pixels.
  float frag_coord[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float depth_range[3] = {0.0f, 1.0f, 1.0f};
  bool front_facing = true;
  float point_coord[2] = {0.0f, 0.0f};
  int sample_id = 0;
  uint32_t sample_mask = 0xFFFFFFFFu;
  const Value* uniforms = nullptr;
  const int* uniform_map = nullptr;
  TextureSampler* sampler = nullptr;
};

struct FragmentOutputInfo {
  std::string name;
  int location = -1;
  int slot = -1;
  int element = 0;
  int components = 0;
  BaseType base = BaseType::kFloat;
};

struct FragmentOutput {
  // Indexed by fragment output location.  |color[i]| is only meaningful when
  // |written[i]| is true; outputs the shader does not declare stay untouched.
  float color[kMaxFragmentOutputs][4] = {};
  uint32_t color_int[kMaxFragmentOutputs][4] = {};
  BaseType color_base[kMaxFragmentOutputs] = {BaseType::kFloat, BaseType::kFloat,
                                             BaseType::kFloat,
                                             BaseType::kFloat};
  bool written[kMaxFragmentOutputs] = {};
  float depth = 0.0f;
  bool writes_depth = false;
  bool discarded = false;
};

// A linked vertex + fragment pair.  The Program borrows the two ASTs owned by
// the shader objects; they must outlive it.
class Program {
 public:
  Program();
  ~Program();

  bool Link(ShaderAst* vertex,
            ShaderAst* fragment,
            const std::vector<std::pair<std::string, int>>& bindings,
            const std::vector<std::pair<std::string, int>>& frag_bindings,
            const std::vector<std::string>& transform_feedback_varyings,
            bool transform_feedback_separate,
            std::string* log);
  bool linked() const { return linked_; }

  const std::vector<AttributeInfo>& attributes() const { return attributes_; }
  const std::vector<UniformInfo>& uniforms() const { return uniforms_; }
  const std::vector<ActiveUniformInfo>& active_uniforms() const {
    return active_uniforms_;
  }
  const std::vector<UniformBlockInfo>& uniform_blocks() const {
    return uniform_blocks_;
  }
  int UniformBlockIndex(const std::string& name) const;
  bool SetUniformBlockBinding(int block_index, int binding);
  // Copies std140 data from |bytes| into the block's uniform values.  Reads
  // beyond |bytes| leave the corresponding value untouched.
  void UploadUniformBlock(int block_index,
                          const uint8_t* bytes,
                          size_t byte_count);
  const std::vector<VaryingInfo>& varyings() const { return varyings_; }
  const std::vector<FragmentOutputInfo>& fragment_outputs() const {
    return fragment_outputs_;
  }
  const std::vector<TransformFeedbackVaryingInfo>&
  transform_feedback_varyings() const {
    return transform_feedback_varyings_;
  }
  bool transform_feedback_separate() const {
    return transform_feedback_separate_;
  }

  int AttributeLocation(const std::string& name) const;
  int UniformLocation(const std::string& name) const;
  int FragDataLocation(const std::string& name) const;
  int VaryingFloats() const { return varying_floats_; }

  bool uses_frag_depth() const { return uses_frag_depth_; }
  bool uses_discard() const { return uses_discard_; }
  bool uses_point_size() const { return uses_point_size_; }

  // True when at least one stage was compiled to native code at link time.
  // The counterpart, whether a particular draw took the JIT path, is exposed
  // through the process-wide ZglJitStats counters.
  bool has_native_stage() const { return vertex_jit_ || fragment_jit_; }

  Value* uniform_value(int index) {
    if (index < 0 || index >= static_cast<int>(uniform_values_.size()))
      return nullptr;
    return &uniform_values_[index];
  }
  const Value* uniform_value(int index) const {
    if (index < 0 || index >= static_cast<int>(uniform_values_.size()))
      return nullptr;
    return &uniform_values_[index];
  }

  // Resolves a location returned by UniformLocation() to the owning storage
  // value, the flattened component offset inside it, and the type of the
  // selected uniform or struct field.  Plain uniforms return offset 0.
  bool UniformStorage(int location,
                      Value** value,
                      int* component_offset,
                      const Type** type);

  const std::vector<UniformFieldInfo>& uniform_fields() const {
    return uniform_fields_;
  }

  void RunVertex(const VertexInput& input,
                 float out_position[4],
                 float* out_point_size,
                 float* out_varyings);
  bool RunFragment(const FragmentInput& input, FragmentOutput* output);

  const std::string& runtime_error() const { return runtime_error_; }
  bool has_runtime_error() const { return !runtime_error_.empty(); }

 private:
  bool linked_ = false;
  ShaderAst* vertex_ = nullptr;
  ShaderAst* fragment_ = nullptr;

  std::vector<AttributeInfo> attributes_;
  std::vector<UniformInfo> uniforms_;
  std::vector<ActiveUniformInfo> active_uniforms_;
  std::vector<UniformFieldInfo> uniform_fields_;
  std::vector<UniformBlockInfo> uniform_blocks_;
  std::vector<VaryingInfo> varyings_;
  std::vector<FragmentOutputInfo> fragment_outputs_;
  std::vector<TransformFeedbackVaryingInfo> transform_feedback_varyings_;
  bool transform_feedback_separate_ = false;
  std::vector<Value> uniform_values_;
  std::vector<int> vertex_uniform_map_;
  std::vector<int> fragment_uniform_map_;
  int varying_floats_ = 0;

  // Per-stage global variables, including built-ins.
  std::vector<Value> vertex_globals_;
  std::vector<Value> fragment_globals_;
  // Values produced by the link-time global initialisers.  Every shader
  // invocation starts from this snapshot: mutable globals are per-invocation
  // state, not state shared by all vertices or fragments.
  std::vector<Value> vertex_global_initials_;
  std::vector<Value> fragment_global_initials_;

  bool uses_frag_depth_ = false;
  bool uses_discard_ = false;
  bool uses_point_size_ = false;

  // Native versions of the stages.  Null means "use the AST evaluator", either
  // because the shader uses a construct the JIT does not cover or because the
  // library was built with ZGL_ENABLE_JIT=0.
  std::unique_ptr<ShaderJit> vertex_jit_;
  std::unique_ptr<ShaderJit> fragment_jit_;

  std::string runtime_error_;
  void BuildActiveUniforms();
  friend class Evaluator;
};

}  // namespace glsl
}  // namespace zgl

#endif  // ZGL_SRC_GLSL_GLSL_PROGRAM_H_
