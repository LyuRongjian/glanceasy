// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GLANCEASY_SRC_GLSL_GLSL_BUILTINS_H_
#define GLANCEASY_SRC_GLSL_GLSL_BUILTINS_H_

#include <vector>

#include "src/glsl/glsl_ast.h"
#include "src/glsl/glsl_types.h"

namespace glanceasy {
namespace glsl {

// Pure implementation of the non-texture built-in functions.  Both the AST
// evaluator and the SLJIT backend use this entry point so that their
// floating-point behaviour cannot drift apart.
Value EvaluateBuiltinMath(BuiltinFunc function, const std::vector<Value>& args);

}  // namespace glsl
}  // namespace glanceasy

#endif  // GLANCEASY_SRC_GLSL_GLSL_BUILTINS_H_
