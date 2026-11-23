// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/glsl/glsl_types.h"

#include <algorithm>
#include <cstring>

namespace zgl {
namespace glsl {

namespace {

bool StructsEqual(const StructInfo* a, const StructInfo* b);

bool TypesEqual(const Type& a, const Type& b) {
  if (a.kind() != b.kind())
    return false;
  if (a.kind() == TypeKind::kBasic) {
    return a.base() == b.base() && a.rows() == b.rows() &&
           a.columns() == b.columns();
  }
  if (a.kind() == TypeKind::kArray) {
    return a.array_size() == b.array_size() &&
           TypesEqual(a.array_element(), b.array_element());
  }
  return StructsEqual(a.struct_info(), b.struct_info());
}

bool StructsEqual(const StructInfo* a, const StructInfo* b) {
  if (a == b)
    return true;
  if (!a || !b || a->name != b->name ||
      a->members.size() != b->members.size()) {
    return false;
  }
  for (size_t i = 0; i < a->members.size(); ++i) {
    if (a->members[i].name != b->members[i].name ||
        a->members[i].component_offset != b->members[i].component_offset ||
        a->members[i].byte_offset != b->members[i].byte_offset ||
        !TypesEqual(a->members[i].type, b->members[i].type)) {
      return false;
    }
  }
  return true;
}

int RoundUp(int value, int alignment) {
  if (alignment <= 0)
    return value;
  return (value + alignment - 1) / alignment * alignment;
}

}  // namespace

Type Type::Array(const Type& element, int count) {
  Type type;
  type.kind_ = TypeKind::kArray;
  type.array_size_ = count > 0 ? count : 0;
  type.element_ = std::make_shared<Type>(element);
  return type;
}

Type Type::Struct(const std::shared_ptr<const StructInfo>& info) {
  Type type;
  type.kind_ = TypeKind::kStruct;
  type.struct_ = info;
  return type;
}

const Type& Type::array_element() const {
  static const Type empty;
  return element_ ? *element_ : empty;
}

int Type::components() const {
  switch (kind_) {
    case TypeKind::kBasic:
      return is_matrix() ? rows_ * columns_ : rows_;
    case TypeKind::kArray:
      return array_size_ * array_element().components();
    case TypeKind::kStruct:
      return struct_ ? struct_->components : 0;
  }
  return 0;
}

std::string Type::Name() const {
  if (kind_ == TypeKind::kArray)
    return array_element().Name() + "[" + std::to_string(array_size_) + "]";
  if (kind_ == TypeKind::kStruct)
    return struct_ ? struct_->name : "struct";
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
  if (is_matrix())
    return "mat" + std::to_string(columns_) + "x" + std::to_string(rows_);
  if (rows_ == 1) {
    switch (base_) {
      case BaseType::kFloat:
        return "float";
      case BaseType::kInt:
        return "int";
      case BaseType::kUInt:
        return "uint";
      default:
        return "bool";
    }
  }
  const char* prefix = base_ == BaseType::kFloat ? ""
                       : base_ == BaseType::kInt ? "i"
                       : base_ == BaseType::kUInt ? "u"
                                                  : "b";
  return std::string(prefix) + "vec" + std::to_string(rows_);
}

bool Type::operator==(const Type& other) const {
  return TypesEqual(*this, other);
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

Value MakeUInt(uint32_t v) {
  Value value(Type::UInt());
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

Value MakeUVec(const uint32_t* values, int count) {
  Value value(Type::UVec(count));
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
  if (index < 0 || index >= value.size())
    return 0.0f;
  return value.Data()[index];
}

void SetComponent(Value* value, int index, float v) {
  if (!value || index < 0 || index >= value->size())
    return;
  value->Data()[index] = v;
}

bool IsTrue(const Value& value) {
  for (int i = 0; i < value.type.components(); ++i) {
    if (!(value.Data()[i] != 0.0f))
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
    case BaseType::kUInt:
      if (from == BaseType::kBool)
        return v != 0.0f ? 1.0f : 0.0f;
      return static_cast<float>(static_cast<uint32_t>(v));
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
  if (target.is_aggregate()) {
    if (value.type == target) {
      std::memcpy(out.Data(), value.Data(),
                  sizeof(float) * static_cast<size_t>(target.components()));
    } else if (target.is_array() && value.type.is_array() &&
               target.array_element() == value.type.array_element()) {
      const int count = std::min(target.array_size(), value.type.array_size());
      const int stride = target.array_element().components();
      for (int i = 0; i < count * stride; ++i)
        out.Data()[i] = value.Data()[i];
    } else if (target.is_struct() && value.type.is_struct() &&
               target.struct_info()->name == value.type.struct_info()->name) {
      const int count = std::min(target.components(), value.size());
      for (int i = 0; i < count; ++i)
        out.Data()[i] = value.Data()[i];
    }
    return out;
  }

  if (value.type.is_aggregate()) {
    out.data[0] = value.size() > 0 ? value.Data()[0] : 0.0f;
    return out;
  }

  const int src_components = value.type.components();
  const int dst_rows = target.rows();
  const int dst_columns = target.columns();

  if (target.is_matrix()) {
    if (value.type.is_matrix()) {
      const int copy_rows = std::min(target.rows(), value.type.rows());
      const int copy_columns = std::min(target.columns(), value.type.columns());
      for (int c = 0; c < copy_columns; ++c) {
        for (int r = 0; r < copy_rows; ++r)
          out.data[c * dst_rows + r] = value.Data()[c * value.type.rows() + r];
      }
      return out;
    }
    // A scalar fills the diagonal; anything else is a compile-time error in
    // real GLSL and is treated as the identity here.
    for (int c = 0; c < dst_columns; ++c)
      for (int r = 0; r < dst_rows; ++r)
        out.data[c * dst_rows + r] =
            (c == r) ? ConvertScalar(value.type.base(), BaseType::kFloat,
                                     value.Data()[0])
                     : 0.0f;
    return out;
  }

  if (value.type.is_matrix()) {
    out.data[0] = value.Data()[0];
    return out;
  }

  if (src_components == 1) {
    const float v =
        ConvertScalar(value.type.base(), target.base(), value.Data()[0]);
    for (int i = 0; i < dst_rows; ++i)
      out.data[i] = v;
    return out;
  }

  for (int i = 0; i < dst_rows; ++i) {
    if (i < src_components) {
      out.data[i] =
          ConvertScalar(value.type.base(), target.base(), value.Data()[i]);
    } else {
      // GLSL fills the tail of a widened vector with (0, 0, 0, 1).
      out.data[i] = (i == 3) ? 1.0f : 0.0f;
    }
  }
  return out;
}

int Std140Alignment(const Type& type) {
  if (type.is_array())
    return Std140Alignment(type.array_element());
  if (type.is_struct()) {
    int alignment = 4;
    const StructInfo* info = type.struct_info();
    if (info) {
      for (const StructMember& member : info->members)
        alignment = std::max(alignment, Std140Alignment(member.type));
    }
    return alignment;
  }
  if (type.is_matrix())
    return 16;
  if (type.is_vector()) {
    if (type.rows() == 2)
      return 8;
    return 16;
  }
  return 4;
}

int Std140Size(const Type& type) {
  if (type.is_array())
    return type.array_size() * Std140ArrayStride(type.array_element());
  if (type.is_struct()) {
    const StructInfo* info = type.struct_info();
    if (!info)
      return 0;
    int end = 0;
    for (const StructMember& member : info->members)
      end = std::max(end, member.byte_offset + Std140Size(member.type));
    return RoundUp(end, Std140Alignment(type));
  }
  if (type.is_matrix())
    return type.columns() * 16;
  if (type.is_vector())
    return type.rows() * 4;
  return 4;
}

int Std140ArrayStride(const Type& element_type) {
  return RoundUp(Std140Size(element_type), 16);
}

}  // namespace glsl
}  // namespace zgl
