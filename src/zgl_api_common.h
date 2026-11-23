// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZGL_SRC_ZGL_API_COMMON_H_
#define ZGL_SRC_ZGL_API_COMMON_H_

#include "GLES3/gl32.h"
#include "src/zgl_context.h"

namespace zgl {

// The context the entry point should operate on.  Entry points are no-ops
// when no context is current, matching the behaviour of a null EGL context.
Context* ApiContext();

// The program object bound with glUseProgram(), or null.
ProgramObject* CurrentProgramObject();

// Binds |location| of the current program to |count| floats.  |integer| is
// true for the glUniform*i* entry points; the interpreter keeps integers in
// float registers.  Unknown locations are ignored, as the specification
// requires.
void SetUniformFloats(GLint location,
                      const float* values,
                      int count,
                      bool integer);

// Binds a matrix uniform.  |dimension| is 2, 3 or 4 and |transpose| selects
// GL_FALSE row-major input; the interpreter stores matrices column major.
void SetUniformMatrix(GLint location,
                      const float* values,
                      int dimension,
                      bool transpose);

// Number of mip levels a texture of |width| x |height| needs.
int MipLevelCount(int width, int height);

}  // namespace zgl

#endif  // ZGL_SRC_ZGL_API_COMMON_H_
