// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "GLES3/gl32.h"
#include "src/glsl/glsl_types.h"
#include "tests/zgl_test.h"

using zgl::glsl::BaseType;
using zgl::glsl::Component;
using zgl::glsl::Convert;
using zgl::glsl::IsTrue;
using zgl::glsl::MakeBool;
using zgl::glsl::MakeBVec;
using zgl::glsl::MakeFloat;
using zgl::glsl::MakeInt;
using zgl::glsl::MakeIVec;
using zgl::glsl::MakeMat;
using zgl::glsl::MakeSampler;
using zgl::glsl::MakeVec;
using zgl::glsl::SetComponent;
using zgl::glsl::Type;
using zgl::glsl::Value;

TEST(TypeNamesCoverEveryShape) {
  EXPECT_TRUE(Type::Void().Name() == "void");
  EXPECT_TRUE(Type::Float().Name() == "float");
  EXPECT_TRUE(Type::Int().Name() == "int");
  EXPECT_TRUE(Type::Bool().Name() == "bool");
  EXPECT_TRUE(Type::Vec(3).Name() == "vec3");
  EXPECT_TRUE(Type::IVec(2).Name() == "ivec2");
  EXPECT_TRUE(Type::BVec(4).Name() == "bvec4");
  EXPECT_TRUE(Type::Mat(2, 3).Name() == "mat2x3");
  EXPECT_TRUE(Type(BaseType::kSampler2D, 1, 1).Name() == "sampler2D");
  EXPECT_TRUE(Type(BaseType::kSamplerCube, 1, 1).Name() == "samplerCube");
  EXPECT_TRUE(Type(BaseType::kSampler3D, 1, 1).Name() == "sampler3D");
}

TEST(TypePredicatesDescribeShape) {
  EXPECT_TRUE(Type::Float().is_scalar());
  EXPECT_TRUE(Type::Int().is_scalar());
  EXPECT_TRUE(Type::Bool().is_scalar());
  EXPECT_FALSE(Type::Vec(2).is_scalar());
  EXPECT_TRUE(Type::Vec(3).is_vector());
  EXPECT_FALSE(Type::Vec(1).is_vector());
  EXPECT_TRUE(Type::Mat(4, 4).is_matrix());
  EXPECT_FALSE(Type::Vec(4).is_matrix());
  EXPECT_TRUE(Type::Void().is_void());
  EXPECT_TRUE(Type(BaseType::kSampler2D, 1, 1).is_sampler());
  EXPECT_FALSE(Type::Float().is_sampler());
  EXPECT_EQ(Type::Mat(3, 2).components(), 6);
  EXPECT_EQ(Type::Vec(4).components(), 4);
  EXPECT_EQ(Type::Mat(3, 2).Element().rows(), 1);
  EXPECT_EQ(Type::Mat(3, 2).Column().rows(), 2);
  EXPECT_FALSE(Type::Float() == Type::Int());
  EXPECT_TRUE(Type::Vec(3) != Type::Vec(4));
  EXPECT_TRUE(Type::Vec(3) == Type::Vec(3));
}

TEST(ValueFactoriesRoundTrip) {
  const float vec[2] = {1.5f, -2.5f};
  const Value v = MakeVec(vec, 2);
  EXPECT_EQ(v.type.rows(), 2);
  EXPECT_NEAR(Component(v, 0), 1.5f, 0.001f);
  EXPECT_NEAR(Component(v, 1), -2.5f, 0.001f);
  EXPECT_NEAR(Component(v, -1), 0.0f, 0.001f);
  EXPECT_NEAR(Component(v, 99), 0.0f, 0.001f);

  const int32_t ivec[3] = {1, -2, 7};
  const Value iv = MakeIVec(ivec, 3);
  EXPECT_TRUE(iv.type.base() == BaseType::kInt);
  EXPECT_NEAR(Component(iv, 1), -2.0f, 0.001f);

  const bool bvec[2] = {true, false};
  const Value bv = MakeBVec(bvec, 2);
  EXPECT_TRUE(bv.type.base() == BaseType::kBool);
  EXPECT_NEAR(Component(bv, 0), 1.0f, 0.001f);

  const float column_major[4] = {1, 2, 3, 4};
  const Value m = MakeMat(2, 2, column_major);
  EXPECT_EQ(m.type.components(), 4);
  EXPECT_NEAR(Component(m, 3), 4.0f, 0.001f);

  const Value sampler = MakeSampler(BaseType::kSamplerCube, 3);
  EXPECT_TRUE(sampler.type.is_sampler());
  EXPECT_NEAR(Component(sampler, 0), 3.0f, 0.001f);

  EXPECT_NEAR(Component(MakeFloat(2.5f), 0), 2.5f, 0.001f);
  EXPECT_NEAR(Component(MakeInt(9), 0), 9.0f, 0.001f);
  EXPECT_NEAR(Component(MakeBool(true), 0), 1.0f, 0.001f);
  EXPECT_NEAR(Component(MakeBool(false), 0), 0.0f, 0.001f);
}

TEST(SetComponentIgnoresInvalidInput) {
  Value v(Type::Vec(3));
  SetComponent(&v, 1, 4.0f);
  EXPECT_NEAR(Component(v, 1), 4.0f, 0.001f);
  SetComponent(nullptr, 0, 1.0f);
  SetComponent(&v, -1, 1.0f);
  SetComponent(&v, 99, 1.0f);
  EXPECT_NEAR(Component(v, 1), 4.0f, 0.001f);
}

TEST(IsTrueRequiresAllComponents) {
  Value v(Type::BVec(3));
  v.data[0] = 1.0f;
  v.data[1] = 1.0f;
  v.data[2] = 1.0f;
  EXPECT_TRUE(IsTrue(v));
  v.data[1] = 0.0f;
  EXPECT_FALSE(IsTrue(v));
  EXPECT_FALSE(IsTrue(MakeBool(false)));
  EXPECT_TRUE(IsTrue(MakeBool(true)));
}

TEST(ConvertHandlesEveryShapeCombination) {
  EXPECT_NEAR(Component(Convert(MakeFloat(2.0f), Type::Vec(3)), 2), 2.0f,
              0.001f);

  const float vec2[2] = {1.0f, 2.0f};
  Value widen = Convert(MakeVec(vec2, 2), Type::Vec(4));
  EXPECT_NEAR(Component(widen, 0), 1.0f, 0.001f);
  EXPECT_NEAR(Component(widen, 2), 0.0f, 0.001f);
  EXPECT_NEAR(Component(widen, 3), 1.0f, 0.001f);

  EXPECT_NEAR(Component(Convert(MakeFloat(3.7f), Type::Int()), 0), 3.0f,
              0.001f);
  EXPECT_NEAR(Component(Convert(MakeBool(true), Type::Int()), 0), 1.0f, 0.001f);
  EXPECT_NEAR(Component(Convert(MakeFloat(0.0f), Type::Bool()), 0), 0.0f,
              0.001f);
  EXPECT_NEAR(Component(Convert(MakeFloat(2.0f), Type::Bool()), 0), 1.0f,
              0.001f);
  EXPECT_NEAR(Component(Convert(MakeInt(5), Type::Float()), 0), 5.0f, 0.001f);

  const float mat4[16] = {1, 2,  3,  4,  5,  6,  7,  8,
                          9, 10, 11, 12, 13, 14, 15, 16};
  Value shrink = Convert(MakeMat(4, 4, mat4), Type::Mat(2, 2));
  EXPECT_NEAR(Component(shrink, 0), 1.0f, 0.001f);
  EXPECT_NEAR(Component(shrink, 3), 6.0f, 0.001f);

  Value diagonal = Convert(MakeFloat(4.0f), Type::Mat(2, 2));
  EXPECT_NEAR(Component(diagonal, 0), 4.0f, 0.001f);
  EXPECT_NEAR(Component(diagonal, 1), 0.0f, 0.001f);
  EXPECT_NEAR(Component(diagonal, 3), 4.0f, 0.001f);

  Value from_matrix = Convert(MakeMat(2, 2, mat4), Type::Float());
  EXPECT_NEAR(Component(from_matrix, 0), 1.0f, 0.001f);

  Value same = Convert(MakeFloat(1.0f), Type::Float());
  EXPECT_NEAR(Component(same, 0), 1.0f, 0.001f);
}
