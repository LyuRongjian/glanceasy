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
                      int components,
                      int element_count,
                      bool integer) {
  (void)integer;
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
  if (components < 1 || element_count < 1 || location < 0)
    return;
  glsl::Value* value = object->program->uniform_value(location);
  if (!value)
    return;
  if (value->type.is_array()) {
    const glsl::Type& element = value->type.array_element();
    if (element.components() != components ||
        element_count > value->type.array_size()) {
      context->SetError(GL_INVALID_OPERATION);
      return;
    }
    const int total = components * element_count;
    for (int i = 0; i < total; ++i)
      value->Data()[i] = values[i];
    return;
  }
  if (element_count != 1 || value->type.components() != components) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  for (int i = 0; i < components; ++i)
    value->Data()[i] = values[i];
}

void SetUniformMatrix(GLint location,
                      const float* values,
                      int rows,
                      int columns,
                      int element_count,
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
  if (rows < 1 || columns < 1 || element_count < 1 || location < 0)
    return;
  glsl::Value* value = object->program->uniform_value(location);
  if (!value)
    return;
  const glsl::Type* matrix_type = &value->type;
  if (value->type.is_array()) {
    if (element_count > value->type.array_size())
      return;
    matrix_type = &value->type.array_element();
  } else if (element_count != 1) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (!matrix_type->is_matrix() || matrix_type->rows() != rows ||
      matrix_type->columns() != columns) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  const int elements = matrix_type->components();
  for (int element = 0; element < element_count; ++element) {
    for (int column = 0; column < columns; ++column) {
      for (int row = 0; row < rows; ++row) {
        // GL passes matrices in column major order, so the element at row
        // |row| of column |column| lives at values[column * rows + row].
        const int source = transpose ? row * columns + column
                                     : column * rows + row;
        value->Data()[element * elements + column * rows + row] =
            values[element * elements + source];
      }
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
