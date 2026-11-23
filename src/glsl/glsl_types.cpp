// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/glsl/glsl_types.h"

#include <cstring>

namespace zgl {
namespace glsl {

std::string Type::Name() const {
  switch (base_) {
    case BaseType::kVoid:
      return "void";
    case BaseType::kSampler2D:
      return "sampler2D";
    case BaseType::kSamplerCube:
      return "samplerCube";
    case BaseType::kSampler3D:
      return "sampler3D";
    default:
      break;
  }
  const char* prefix = base_ == BaseType::kFloat ? ""
                       : base_ == BaseType::kInt ? "i"
                                                 : "b";
  if (is_matrix())
    return "mat" + std::to_string(columns_) + "x" + std::to_string(rows_);
  if (rows_ == 1)
    return base_ == BaseType::kFloat
               ? "float"
               : (base_ == BaseType::kInt ? "int" : "bool");
  return std::string(prefix) + "vec" + std::to_string(rows_);
}

Value MakeFloat(float v) {
  Value value(Type::Float());
  value.data[0] = v;
  return value;
}

Value MakeInt(int32_t v) {
  Value value(Type::Int());
  value.data[0] = static_cast<float>(v);
  return value;
}

Value MakeBool(bool v) {
  Value value(Type::Bool());
  value.data[0] = v ? 1.0f : 0.0f;
  return value;
}

Value MakeVec(const float* values, int count) {
  Value value(Type::Vec(count));
  std::memcpy(value.data, values, sizeof(float) * count);
  return value;
}

Value MakeIVec(const int32_t* values, int count) {
  Value value(Type::IVec(count));
  for (int i = 0; i < count; ++i)
    value.data[i] = static_cast<float>(values[i]);
  return value;
}

Value MakeBVec(const bool* values, int count) {
  Value value(Type::BVec(count));
  for (int i = 0; i < count; ++i)
    value.data[i] = values[i] ? 1.0f : 0.0f;
  return value;
}

Value MakeMat(int columns, int rows, const float* column_major) {
  Value value(Type::Mat(columns, rows));
  std::memcpy(value.data, column_major, sizeof(float) * columns * rows);
  return value;
}

Value MakeSampler(BaseType base, int unit) {
  Value value(Type(base, 1, 1));
  value.data[0] = static_cast<float>(unit);
  return value;
}

float Component(const Value& value, int index) {
  if (index < 0 || index >= kValueFloats)
    return 0.0f;
  return value.data[index];
}

void SetComponent(Value* value, int index, float v) {
  if (!value || index < 0 || index >= kValueFloats)
    return;
  value->data[index] = v;
}

bool IsTrue(const Value& value) {
  for (int i = 0; i < value.type.components(); ++i) {
    if (!(value.data[i] != 0.0f))
      return false;
  }
  return true;
}

namespace {

float ConvertScalar(BaseType from, BaseType to, float v) {
  if (from == to)
    return v;
  switch (to) {
    case BaseType::kFloat:
      return from == BaseType::kBool ? (v != 0.0f ? 1.0f : 0.0f) : v;
    case BaseType::kInt:
      if (from == BaseType::kBool)
        return v != 0.0f ? 1.0f : 0.0f;
      return static_cast<float>(static_cast<int32_t>(v));
    case BaseType::kBool:
      return v != 0.0f ? 1.0f : 0.0f;
    default:
      return v;
  }
}

}  // namespace

Value Convert(const Value& value, const Type& target) {
  if (value.type == target)
    return value;

  Value out(target);
  const int src_components = value.type.components();
  const int dst_rows = target.rows();
  const int dst_columns = target.columns();

  if (target.is_matrix()) {
    if (value.type.is_matrix()) {
      const int copy_rows = std::min(target.rows(), value.type.rows());
      const int copy_columns = std::min(target.columns(), value.type.columns());
      for (int c = 0; c < copy_columns; ++c) {
        for (int r = 0; r < copy_rows; ++r)
          out.data[c * dst_rows + r] = value.data[c * value.type.rows() + r];
      }
      return out;
    }
    // A scalar fills the diagonal; anything else is a compile-time error in
    // real GLSL and is treated as the identity here.
    for (int c = 0; c < dst_columns; ++c)
      for (int r = 0; r < dst_rows; ++r)
        out.data[c * dst_rows + r] =
            (c == r) ? ConvertScalar(value.type.base(), BaseType::kFloat,
                                     value.data[0])
                     : 0.0f;
    return out;
  }

  if (value.type.is_matrix()) {
    out.data[0] = value.data[0];
    return out;
  }

  if (src_components == 1) {
    const float v =
        ConvertScalar(value.type.base(), target.base(), value.data[0]);
    for (int i = 0; i < dst_rows; ++i)
      out.data[i] = v;
    return out;
  }

  for (int i = 0; i < dst_rows; ++i) {
    if (i < src_components) {
      out.data[i] =
          ConvertScalar(value.type.base(), target.base(), value.data[i]);
    } else {
      // GLSL fills the tail of a widened vector with (0, 0, 0, 1).
      out.data[i] = (i == 3) ? 1.0f : 0.0f;
    }
  }
  return out;
}

}  // namespace glsl
}  // namespace zgl
