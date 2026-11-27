// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/glanceasy_api_common.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace glanceasy {

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
                      int numeric_kind) {
  Context* context = ApiContext();
  if (!context || !values)
    return;
  ProgramObject* object = CurrentProgramObject();
  if (!object || !object->linked || !object->program) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (location == -1)
    return;
  if (components < 1 || element_count < 1 || location < 0) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  glsl::Value* value = nullptr;
  int base_offset = 0;
  const glsl::Type* uniform_type = nullptr;
  if (!object->program->UniformStorage(location, &value, &base_offset,
                                       &uniform_type)) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (getenv("GLANCEASY_DEBUG_UNI") && location >= 0 &&
      location < static_cast<int>(object->program->uniforms().size())) {
    static int uni_debug = 0;
    if (uni_debug < 60) {
      const auto& info = object->program->uniforms()[location];
      fprintf(stderr, "[uni] %s comps=%d count=%d vals=%.3f %.3f %.3f %.3f\n",
              info.name.c_str(), components, element_count,
              values[0], components > 1 ? values[1] : 0.0f,
              components > 2 ? values[2] : 0.0f,
              components > 3 ? values[3] : 0.0f);
      ++uni_debug;
    }
  }
  if (!value) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (getenv("GLANCEASY_DEBUG_UNI")) {
    fprintf(stderr,
            "[uniset] loc=%d type=%s array=%d size=%d comps=%d count=%d\n",
            location, uniform_type->Name().c_str(),
            uniform_type->is_array() ? 1 : 0,
            uniform_type->is_array() ? uniform_type->array_size() : 1,
            components, element_count);
  }
  const glsl::Type& scalar_type =
      uniform_type->is_array() ? uniform_type->array_element() : *uniform_type;
  const glsl::BaseType base = scalar_type.base();
  const bool is_sampler = scalar_type.is_sampler();
  const bool signed_integer =
      base == glsl::BaseType::kInt || base == glsl::BaseType::kBool;
  const bool unsigned_integer =
      base == glsl::BaseType::kUInt || base == glsl::BaseType::kBool;
  bool compatible = false;
  if (numeric_kind == 0) {
    // Floating-point uploads are also accepted for bool uniforms; any
    // non-zero value becomes true when the value is queried as an integer.
    compatible = !is_sampler && base != glsl::BaseType::kInt &&
                 base != glsl::BaseType::kUInt;
  } else if (numeric_kind == 1) {
    compatible = signed_integer || (is_sampler && components == 1);
  } else {
    compatible = unsigned_integer;
  }
  if (!compatible || scalar_type.is_matrix()) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (uniform_type->is_array()) {
    const glsl::Type& element = uniform_type->array_element();
    const int element_components = element.components();
    const int total = components * element_count;
    // glUniform{1,2,3,4}fv may fill a whole array with a flat count; count
    // times components must cover an integral number of array elements.
    if (element_components <= 0 || total % element_components != 0 ||
        total / element_components > uniform_type->array_size()) {
      context->SetError(GL_INVALID_OPERATION);
      return;
    }
    for (int i = 0; i < total; ++i) {
      if (scalar_type.base() == glsl::BaseType::kBool) {
        const bool boolean = values[i] != 0.0f;
        value->Data()[base_offset + i] = boolean ? 1.0f : 0.0f;
        value->IntData()[base_offset + i] = boolean ? 1u : 0u;
      } else {
        value->Data()[base_offset + i] = values[i];
        if (numeric_kind == 1)
          value->IntData()[base_offset + i] =
              static_cast<uint32_t>(static_cast<int32_t>(values[i]));
        else if (numeric_kind == 2)
          value->IntData()[base_offset + i] = static_cast<uint32_t>(values[i]);
      }
    }
    return;
  }
  if (element_count != 1 || uniform_type->components() != components) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  for (int i = 0; i < components; ++i) {
    if (scalar_type.base() == glsl::BaseType::kBool) {
      const bool boolean = values[i] != 0.0f;
      value->Data()[base_offset + i] = boolean ? 1.0f : 0.0f;
      value->IntData()[base_offset + i] = boolean ? 1u : 0u;
    } else {
      value->Data()[base_offset + i] = values[i];
      if (numeric_kind == 1)
        value->IntData()[base_offset + i] =
            static_cast<uint32_t>(static_cast<int32_t>(values[i]));
      else if (numeric_kind == 2)
        value->IntData()[base_offset + i] = static_cast<uint32_t>(values[i]);
    }
  }
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
  if (!object || !object->linked || !object->program) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (location == -1)
    return;
  if (rows < 1 || columns < 1 || element_count < 1 || location < 0) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  glsl::Value* value = nullptr;
  int base_offset = 0;
  const glsl::Type* matrix_type = nullptr;
  if (!object->program->UniformStorage(location, &value, &base_offset,
                                       &matrix_type)) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (matrix_type->is_array()) {
    if (element_count > matrix_type->array_size()) {
      context->SetError(GL_INVALID_OPERATION);
      return;
    }
    matrix_type = &matrix_type->array_element();
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
        value->Data()[base_offset + element * elements + column * rows + row] =
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

}  // namespace glanceasy
