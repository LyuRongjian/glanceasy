// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZGL_SRC_GLSL_GLSL_TYPES_H_
#define ZGL_SRC_GLSL_GLSL_TYPES_H_

#include <stdint.h>

#include <string>

#include "src/zgl_common.h"

namespace zgl {
namespace glsl {

// Scalar and opaque base types of the subset.  Vectors and matrices are
// described by the shape carried in Type.
enum class BaseType {
  kVoid,
  kFloat,
  kInt,
  kBool,
  kSampler2D,
  kSamplerCube,
  kSampler3D,
};

// GLSL type.  A vector has |columns| == 1; a matrix has |columns| columns of
// |rows| components, matching the constructor order of matCxR.
class Type {
 public:
  Type() = default;
  Type(BaseType base, int rows, int columns)
      : base_(base), rows_(rows), columns_(columns) {}

  static Type Void() { return Type(BaseType::kVoid, 0, 0); }
  static Type Float() { return Type(BaseType::kFloat, 1, 1); }
  static Type Int() { return Type(BaseType::kInt, 1, 1); }
  static Type Bool() { return Type(BaseType::kBool, 1, 1); }
  static Type Vec(int rows) { return Type(BaseType::kFloat, rows, 1); }
  static Type IVec(int rows) { return Type(BaseType::kInt, rows, 1); }
  static Type BVec(int rows) { return Type(BaseType::kBool, rows, 1); }
  static Type Mat(int columns, int rows) {
    return Type(BaseType::kFloat, rows, columns);
  }

  BaseType base() const { return base_; }
  int rows() const { return rows_; }
  int columns() const { return columns_; }

  int components() const {
    if (is_matrix())
      return rows_ * columns_;
    return rows_;
  }

  bool is_void() const { return base_ == BaseType::kVoid; }
  bool is_scalar() const {
    return (base_ == BaseType::kFloat || base_ == BaseType::kInt ||
            base_ == BaseType::kBool) &&
           rows_ == 1 && columns_ == 1;
  }
  bool is_vector() const {
    return (base_ == BaseType::kFloat || base_ == BaseType::kInt ||
            base_ == BaseType::kBool) &&
           columns_ == 1 && rows_ > 1;
  }
  bool is_matrix() const { return base_ == BaseType::kFloat && columns_ > 1; }
  bool is_sampler() const {
    return base_ == BaseType::kSampler2D || base_ == BaseType::kSamplerCube ||
           base_ == BaseType::kSampler3D;
  }

  // Element type of a vector / matrix (scalar).
  Type Element() const { return Type(base_, 1, 1); }

  // Column type of a matrix (vector of |rows| components).
  Type Column() const { return Type(base_, rows_, 1); }

  std::string Name() const;

  bool operator==(const Type& other) const {
    return base_ == other.base_ && rows_ == other.rows_ &&
           columns_ == other.columns_;
  }
  bool operator!=(const Type& other) const { return !(*this == other); }

 private:
  BaseType base_ = BaseType::kVoid;
  int rows_ = 0;
  int columns_ = 0;
};

// A runtime value.  Integer and boolean scalars are stored as floats; the
// interpreter truncates on read, which is exact for the integer range that
// shaders normally use (|i| < 2^24) and avoids a second register file.
struct Value {
  Value() = default;
  explicit Value(const Type& t) : type(t) {}

  Type type;
  float data[kValueFloats] = {};

  int size() const { return type.components(); }
};

Value MakeFloat(float v);
Value MakeInt(int32_t v);
Value MakeBool(bool v);
Value MakeVec(const float* values, int count);
Value MakeIVec(const int32_t* values, int count);
Value MakeBVec(const bool* values, int count);
Value MakeMat(int columns, int rows, const float* column_major);
Value MakeSampler(BaseType base, int unit);

// Converts |value| to |target| using the GLSL conversion rules.  Scalars
// broadcast, vectors truncate, int/bool conversions round-trip through zero.
Value Convert(const Value& value, const Type& target);

// Reads the scalar component at |index| as a float (vectors/matrices are
// column major, matching m[column][row] indexing).
float Component(const Value& value, int index);
void SetComponent(Value* value, int index, float v);

// Truthiness of a boolean scalar or vector (all components must be true for
// a vector, as in an if() condition).
bool IsTrue(const Value& value);

}  // namespace glsl
}  // namespace zgl

#endif  // ZGL_SRC_GLSL_GLSL_TYPES_H_
