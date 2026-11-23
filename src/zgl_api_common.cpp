// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/zgl_api_common.h"

#include <algorithm>
#include <cmath>

namespace zgl {

Context* ApiContext() {
  return CurrentContext();
}

ProgramObject* CurrentProgramObject() {
  Context* context = ApiContext();
  if (!context)
    return nullptr;
  return context->programs.Get(context->state.program);
}

void SetUniformFloats(GLint location,
                      const float* values,
                      int count,
                      bool integer) {
  Context* context = ApiContext();
  if (!context || !values)
    return;
  ProgramObject* object = CurrentProgramObject();
  // The specification allows uniform updates with no program bound; they are
  // silently ignored, so only a bound but unlinked program reports an error.
  if (!object)
    return;
  if (!object->linked || !object->program) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  glsl::Value* value = object->program->uniform_value(location);
  if (!value)
    return;
  if (value->type.components() != count) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  for (int i = 0; i < count; ++i) {
    value->data[i] =
        integer ? static_cast<float>(static_cast<int>(values[i])) : values[i];
  }
}

void SetUniformMatrix(GLint location,
                      const float* values,
                      int dimension,
                      bool transpose) {
  Context* context = ApiContext();
  if (!context || !values)
    return;
  ProgramObject* object = CurrentProgramObject();
  if (!object)
    return;
  if (!object->linked || !object->program) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  glsl::Value* value = object->program->uniform_value(location);
  if (!value)
    return;
  if (!value->type.is_matrix() || value->type.rows() != dimension ||
      value->type.columns() != dimension) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  for (int column = 0; column < dimension; ++column) {
    for (int row = 0; row < dimension; ++row) {
      // GL passes matrices in column major order, so the element at row |row|
      // of column |column| lives at values[column * dimension + row].
      const int source =
          transpose ? row * dimension + column : column * dimension + row;
      value->data[column * dimension + row] = values[source];
    }
  }
}

int MipLevelCount(int width, int height) {
  const int extent = std::max(width, height);
  if (extent <= 0)
    return 0;
  int levels = 1;
  int size = extent;
  while (size > 1) {
    size >>= 1;
    ++levels;
  }
  return levels;
}

}  // namespace zgl
