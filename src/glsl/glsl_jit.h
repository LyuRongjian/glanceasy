// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZGL_SRC_GLSL_GLSL_JIT_H_
#define ZGL_SRC_GLSL_GLSL_JIT_H_

#include <memory>
#include <string>

#include "src/glsl/glsl_ast.h"
#include "src/glsl/glsl_program.h"
#include "src/glsl/glsl_types.h"

namespace zgl {
namespace glsl {

// Pointers and scalars that a compiled shader needs while it runs.  Built-in
// variables are already copied into |globals| by Program before this struct
// is handed to ShaderJit::Run().
struct JitRunInputs {
  const Value* uniforms = nullptr;
  const int* uniform_map = nullptr;
  const float* attributes = nullptr;
  const float* varyings_in = nullptr;
  float* varyings_out = nullptr;
  TextureSampler* sampler = nullptr;
  Value* globals = nullptr;
};

// A native-code version of one shader stage.  Compilation is all-or-nothing:
// constructs that the backend does not lower (loops, user functions, integer
// arithmetic, dynamic indexing, ...) make Compile() fail, and Program keeps
// using the AST evaluator for that stage.  This keeps the JIT a strict
// optimisation rather than a second, subtly different GLSL implementation.
class ShaderJit {
 public:
  static std::unique_ptr<ShaderJit> Compile(const ShaderAst& ast,
                                            std::string* reason);
  ~ShaderJit();

  bool Run(const JitRunInputs& inputs, bool* discarded);

  int frame_slots() const { return frame_slots_; }

 private:
  class Impl;
  explicit ShaderJit(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;
  int frame_slots_ = 0;
};

}  // namespace glsl
}  // namespace zgl

#endif  // ZGL_SRC_GLSL_GLSL_JIT_H_
