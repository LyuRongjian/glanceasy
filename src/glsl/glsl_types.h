// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZGL_SRC_GLSL_GLSL_TYPES_H_
#define ZGL_SRC_GLSL_GLSL_TYPES_H_

#include <stdint.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "src/zgl_common.h"

namespace zgl {
namespace glsl {

// Scalar and opaque base types of the subset.  Vectors and matrices are
// described by the shape carried in Type.
enum class BaseType {
  kVoid,
  kFloat,
  kInt,
  kUInt,
  kBool,
  kSampler2D,
  kSamplerCube,
  kSampler3D,
  kSampler2DShadow,
  kSampler2DArray,
  kSampler2DArrayShadow,
  kSamplerCubeShadow,
  kISampler2D,
  kISampler3D,
  kISamplerCube,
  kISampler2DArray,
  kUSampler2D,
  kUSampler3D,
  kUSamplerCube,
  kUSampler2DArray,
};

enum class TypeKind {
  kBasic,
  kArray,
  kStruct,
};

struct StructInfo;
struct StructMember;

// GLSL type.  A vector has |columns| == 1; a matrix has |columns| columns of
// |rows| components, matching the constructor order of matCxR.  Arrays and
// structures keep their element / member layout in shared, immutable metadata
// so that copies stay cheap and both stages can compare layouts.
class Type {
 public:
  Type() = default;
  Type(BaseType base, int rows, int columns)
      : base_(base), rows_(rows), columns_(columns) {}

  static Type Void() { return Type(BaseType::kVoid, 0, 0); }
  static Type Float() { return Type(BaseType::kFloat, 1, 1); }
  static Type Int() { return Type(BaseType::kInt, 1, 1); }
  static Type UInt() { return Type(BaseType::kUInt, 1, 1); }
  static Type Bool() { return Type(BaseType::kBool, 1, 1); }
  static Type Vec(int rows) { return Type(BaseType::kFloat, rows, 1); }
  static Type IVec(int rows) { return Type(BaseType::kInt, rows, 1); }
  static Type UVec(int rows) { return Type(BaseType::kUInt, rows, 1); }
  static Type BVec(int rows) { return Type(BaseType::kBool, rows, 1); }
  static Type Mat(int columns, int rows) {
    return Type(BaseType::kFloat, rows, columns);
  }
  static Type Array(const Type& element, int count);
  static Type Struct(const std::shared_ptr<const StructInfo>& info);

  TypeKind kind() const { return kind_; }
  BaseType base() const { return base_; }
  int rows() const { return rows_; }
  int columns() const { return columns_; }

  bool is_array() const { return kind_ == TypeKind::kArray; }
  bool is_struct() const { return kind_ == TypeKind::kStruct; }
  bool is_aggregate() const { return kind_ != TypeKind::kBasic; }
  const Type& array_element() const;
  int array_size() const { return array_size_; }
  const StructInfo* struct_info() const { return struct_.get(); }

  int components() const;
  bool is_void() const { return kind_ == TypeKind::kBasic && base_ == BaseType::kVoid; }
  bool is_scalar() const {
    return kind_ == TypeKind::kBasic &&
           (base_ == BaseType::kFloat || base_ == BaseType::kInt ||
            base_ == BaseType::kUInt || base_ == BaseType::kBool) &&
           rows_ == 1 && columns_ == 1;
  }
  bool is_vector() const {
    return kind_ == TypeKind::kBasic &&
           (base_ == BaseType::kFloat || base_ == BaseType::kInt ||
            base_ == BaseType::kUInt || base_ == BaseType::kBool) &&
           columns_ == 1 && rows_ > 1;
  }
  bool is_matrix() const {
    return kind_ == TypeKind::kBasic && base_ == BaseType::kFloat &&
           columns_ > 1;
  }
  bool is_uint() const {
    return kind_ == TypeKind::kBasic && base_ == BaseType::kUInt;
  }
  bool is_integer() const {
    return kind_ == TypeKind::kBasic &&
           (base_ == BaseType::kInt || base_ == BaseType::kUInt);
  }
  bool is_numeric() const {
    return kind_ == TypeKind::kBasic &&
           (base_ == BaseType::kFloat || base_ == BaseType::kInt ||
            base_ == BaseType::kUInt);
  }
  bool is_sampler() const {
    return kind_ == TypeKind::kBasic &&
           base_ >= BaseType::kSampler2D;
  }

  // Element type of a vector / matrix (scalar).
  Type Element() const { return Type(base_, 1, 1); }

  // Column type of a matrix (vector of |rows| components).
  Type Column() const { return Type(base_, rows_, 1); }

  std::string Name() const;

  bool operator==(const Type& other) const;
  bool operator!=(const Type& other) const { return !(*this == other); }

 private:
  TypeKind kind_ = TypeKind::kBasic;
  BaseType base_ = BaseType::kVoid;
  int rows_ = 0;
  int columns_ = 0;
  int array_size_ = 0;
  std::shared_ptr<const Type> element_;
  std::shared_ptr<const StructInfo> struct_;
};

// One field of a structure.  |component_offset| is the offset inside the
// flattened value; |byte_offset| follows the std140 rules.
struct StructMember {
  std::string name;
  Type type;
  int component_offset = 0;
  int byte_offset = 0;
};

struct StructInfo {
  std::string name;
  std::vector<StructMember> members;
  int components = 0;
  int byte_size = 0;
};

// A runtime value.  Basic values stay in the fixed-size |data| array so the
// common path allocates nothing.  Arrays and structures live in |aggregate|,
// which is sized from the type's flattened component count.
struct Value {
  Value() = default;
  explicit Value(const Type& t) { ResetType(t); }

  Type type;
  float data[kValueFloats] = {};
  uint32_t int_data[kValueFloats] = {};
  std::vector<float> aggregate;
  std::vector<uint32_t> int_aggregate;

  int size() const { return type.components(); }

  const float* Data() const {
    return aggregate.empty() ? data : aggregate.data();
  }
  float* Data() { return aggregate.empty() ? data : aggregate.data(); }
  const uint32_t* IntData() const {
    return int_aggregate.empty() ? int_data : int_aggregate.data();
  }
  uint32_t* IntData() {
    return int_aggregate.empty() ? int_data : int_aggregate.data();
  }

  void ResetType(const Type& t) {
    type = t;
    if (t.is_aggregate()) {
      aggregate.assign(static_cast<size_t>(std::max(0, t.components())), 0.0f);
      int_aggregate.assign(static_cast<size_t>(std::max(0, t.components())), 0u);
    } else {
      aggregate.clear();
      int_aggregate.clear();
    }
  }
};

Value MakeFloat(float v);
Value MakeInt(int32_t v);
Value MakeUInt(uint32_t v);
Value MakeBool(bool v);
Value MakeVec(const float* values, int count);
Value MakeIVec(const int32_t* values, int count);
Value MakeUVec(const uint32_t* values, int count);
Value MakeBVec(const bool* values, int count);
Value MakeMat(int columns, int rows, const float* column_major);
Value MakeSampler(BaseType base, int unit);

// Converts |value| to |target| using the GLSL conversion rules.  Scalars
// broadcast, vectors truncate, int/bool conversions round-trip through zero.
Value Convert(const Value& value, const Type& target);

// Reads the scalar component at |index| as a float (vectors/matrices are
// column major, matching m[column][row] indexing).  Aggregate values use the
// flattened component order.
float Component(const Value& value, int index);
void SetComponent(Value* value, int index, float v);

// Truthiness of a boolean scalar or vector (all components must be true for
// a vector, as in an if() condition).
bool IsTrue(const Value& value);

// std140 layout rules for the basic member types the subset understands.
// Vectors are not padded to their alignment when stored alone; the next
// member's offset is rounded up instead, matching the std140 rules.
int Std140Alignment(const Type& type);
int Std140Size(const Type& type, int matrix_layout = 0);
int Std140ArrayStride(const Type& element_type, int matrix_layout = 0);
int Std140StructMemberOffset(const StructInfo& info, int member_index,
                             int matrix_layout = 0);

}  // namespace glsl
}  // namespace zgl

#endif  // ZGL_SRC_GLSL_GLSL_TYPES_H_
