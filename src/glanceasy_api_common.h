// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GLANCEASY_SRC_API_COMMON_H_
#define GLANCEASY_SRC_API_COMMON_H_

#include "GLES3/gl32.h"
#include "src/glanceasy_context.h"

namespace glanceasy {

// The context the entry point should operate on.  Entry points are no-ops
// when no context is current, matching the behaviour of a null EGL context.
Context* ApiContext();

// The program object bound with glUseProgram(), or null.
ProgramObject* CurrentProgramObject();

// Binds |location| of the current program to |element_count| elements of
// |components| floats each.  |numeric_kind| is 0 for glUniform*f*, 1 for
// signed integer entry points and 2 for unsigned integer entry points.  The
// interpreter keeps all numeric values in float registers.
void SetUniformFloats(GLint location,
                      const float* values,
                      int components,
                      int element_count,
                      int numeric_kind);

// Binds a matrix uniform (or an array of matrices).  |transpose| selects
// GL_FALSE row-major input; the interpreter stores matrices column major.
void SetUniformMatrix(GLint location,
                      const float* values,
                      int rows,
                      int columns,
                      int element_count,
                      bool transpose);

// Number of mip levels a texture of |width| x |height| needs.
int MipLevelCount(int width, int height);

// Reads a scalar or vector integer state value.  |count| receives the number
// of components; only |first| is filled here, callers that need the whole
// vector use glGetIntegerv().
bool GetIntegerValue(Context* context, GLenum pname, GLint* first, int* count);

}  // namespace glanceasy

#endif  // GLANCEASY_SRC_API_COMMON_H_
