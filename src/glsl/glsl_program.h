// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZGL_SRC_GLSL_GLSL_PROGRAM_H_
#define ZGL_SRC_GLSL_GLSL_PROGRAM_H_

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
};

struct VaryingInfo {
  std::string name;
  Type type;
  int offset = 0;  // Float offset inside the varying arrays.
  int components = 0;
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
};

struct VertexInput {
  // 4 floats per attribute slot, indexed by attribute location.
  const float* attributes = nullptr;
  int vertex_id = 0;
  int instance_id = 0;
  const Value* uniforms = nullptr;
  const int* uniform_map = nullptr;  // Stage uniform index -> program index.
  TextureSampler* sampler = nullptr;
};

struct FragmentInput {
  const float* varyings = nullptr;  // Interpolated fragment-stage varyings.
  float frag_coord[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  bool front_facing = true;
  float point_coord[2] = {0.0f, 0.0f};
  const Value* uniforms = nullptr;
  const int* uniform_map = nullptr;
  TextureSampler* sampler = nullptr;
};

struct FragmentOutput {
  float color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
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
            std::string* log);
  bool linked() const { return linked_; }

  const std::vector<AttributeInfo>& attributes() const { return attributes_; }
  const std::vector<UniformInfo>& uniforms() const { return uniforms_; }
  const std::vector<VaryingInfo>& varyings() const { return varyings_; }

  int AttributeLocation(const std::string& name) const;
  int UniformLocation(const std::string& name) const;
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
  std::vector<VaryingInfo> varyings_;
  std::vector<Value> uniform_values_;
  std::vector<int> vertex_uniform_map_;
  std::vector<int> fragment_uniform_map_;
  int varying_floats_ = 0;

  // Per-stage global variables, including built-ins.
  std::vector<Value> vertex_globals_;
  std::vector<Value> fragment_globals_;

  bool uses_frag_depth_ = false;
  bool uses_discard_ = false;
  bool uses_point_size_ = false;

  // Native versions of the stages.  Null means "use the AST evaluator", either
  // because the shader uses a construct the JIT does not cover or because the
  // library was built with ZGL_ENABLE_JIT=0.
  std::unique_ptr<ShaderJit> vertex_jit_;
  std::unique_ptr<ShaderJit> fragment_jit_;

  std::string runtime_error_;
  friend class Evaluator;
};

}  // namespace glsl
}  // namespace zgl

#endif  // ZGL_SRC_GLSL_GLSL_PROGRAM_H_
