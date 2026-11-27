// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "src/glsl/glsl_parser.h"
#include "src/glsl/glsl_program.h"
#include "src/glsl/glsl_types.h"
#include "tests/zgl_test.h"

#include "GLES3/gl32.h"
#include "zgl/zgl.h"

#define ASSERT_TRUE(condition)     \
  do {                             \
    if (!(condition)) {            \
      EXPECT_TRUE(false);          \
      return;                      \
    }                              \
  } while (0)

namespace {

const char* kVertexSource =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "in highp vec4 a_position;\n"
    "in highp vec4 a_in0;\n"
    "in highp vec4 a_in1;\n"
    "out highp vec4 v_in0;\n"
    "out highp vec4 v_in1;\n"
    "void main() {\n"
    "  gl_Position = a_position;\n"
    "  v_in0 = a_in0;\n"
    "  v_in1 = a_in1;\n"
    "}\n";

const char* kAddFragmentSource =
    "#version 300 es\n"
    "precision highp int;\n"
    "in highp vec4 v_in0;\n"
    "in highp vec4 v_in1;\n"
    "out ivec4 o_color;\n"
    "void main() {\n"
    "  highp int in0 = int(v_in0.z);\n"
    "  highp int in1 = int(v_in1.x);\n"
    "  highp int res = in0 + in1;\n"
    "  o_color = ivec4(in0, in1, res, 0);\n"
    "}\n";

const char* kFloatOutputFragmentSource =
    "#version 300 es\n"
    "precision highp int;\n"
    "precision mediump float;\n"
    "in highp vec4 v_in0;\n"
    "in highp vec4 v_in1;\n"
    "out mediump vec4 o_color;\n"
    "void main() {\n"
    "  highp int in0 = int(v_in0.z);\n"
    "  highp int in1 = int(v_in1.x);\n"
    "  highp int res = in0 + in1;\n"
    "  highp vec4 color = vec4(0.0, 0.0, 0.0, 1.0);\n"
    "  color.x = float(res);\n"
    "  color = color * vec4(4e-10, 1.0, 1.0, 1.0) + vec4(0.50, 0.0, 0.0, 0.0);\n"
    "  o_color = color;\n"
    "}\n";

const char* kArrayFragmentSource =
    "#version 300 es\n"
    "precision highp int;\n"
    "in highp vec4 v_in0;\n"
    "out ivec4 o_color;\n"
    "void main() {\n"
    "  ivec3 in0 = ivec3(v_in0.xyz * 1.0025);\n"
    "  int[3] x;\n"
    "  x = int[3](in0.z, in0.x, in0.y);\n"
    "  ivec3 out0 = ivec3(x[0], x[1], x[2]);\n"
    "  o_color = ivec4(out0, 0);\n"
    "}\n";

zgl::glsl::Program* LinkProgram(const char* vertex_source,
                                const char* fragment_source,
                                std::unique_ptr<zgl::glsl::ShaderAst>* vertex,
                                std::unique_ptr<zgl::glsl::ShaderAst>* fragment,
                                std::unique_ptr<zgl::glsl::Program>* program) {
  using namespace zgl::glsl;
  Parser vertex_parser(Stage::kVertex, vertex_source);
  *vertex = vertex_parser.Parse();
  if (!*vertex)
    return nullptr;
  Parser fragment_parser(Stage::kFragment, fragment_source);
  *fragment = fragment_parser.Parse();
  if (!*fragment)
    return nullptr;
  *program = std::make_unique<Program>();
  std::string log;
  if (!program->get()->Link(vertex->get(), fragment->get(), {}, {}, {}, false,
                            &log))
    return nullptr;
  return program->get();
}

bool RunFragmentWithAttributes(zgl::glsl::Program* program,
                               uint32_t in0_x,
                               uint32_t in0_y,
                               uint32_t in0_z,
                               uint32_t in1,
                               zgl::glsl::FragmentOutput* output) {
  using namespace zgl::glsl;
  float attributes[12] = {};
  uint32_t attribute_int[12] = {};
  const int position_location = program->AttributeLocation("a_position");
  const int in0_location = program->AttributeLocation("a_in0");
  const int in1_location = program->AttributeLocation("a_in1");
  if (position_location < 0 || in0_location < 0 || in1_location < 0)
    return false;
  attributes[position_location * 4 + 0] = 0.0f;
  attributes[position_location * 4 + 1] = 0.0f;
  attributes[position_location * 4 + 2] = 0.0f;
  attributes[position_location * 4 + 3] = 1.0f;
  attributes[in0_location * 4 + 0] =
      static_cast<float>(static_cast<int32_t>(in0_x));
  attributes[in0_location * 4 + 1] =
      static_cast<float>(static_cast<int32_t>(in0_y));
  attributes[in0_location * 4 + 2] =
      static_cast<float>(static_cast<int32_t>(in0_z));
  attribute_int[in0_location * 4 + 0] = in0_x;
  attribute_int[in0_location * 4 + 1] = in0_y;
  attribute_int[in0_location * 4 + 2] = in0_z;
  attributes[in1_location * 4 + 0] =
      static_cast<float>(static_cast<int32_t>(in1));
  attribute_int[in1_location * 4 + 0] = in1;

  VertexInput vertex_input;
  vertex_input.attributes = attributes;
  vertex_input.attribute_int = attribute_int;
  float position[4] = {};
  float point_size = 1.0f;
  std::vector<float> varyings(program->VaryingFloats(), 0.0f);
  program->RunVertex(vertex_input, position, &point_size, varyings.data());
  if (program->has_runtime_error())
    return false;

  FragmentInput fragment_input;
  fragment_input.varyings = varyings.data();
  return program->RunFragment(fragment_input, output);
}

}  // namespace

TEST(HighpIntegerAddKeepsThirtyTwoBitValues) {
  std::unique_ptr<zgl::glsl::ShaderAst> vertex;
  std::unique_ptr<zgl::glsl::ShaderAst> fragment;
  std::unique_ptr<zgl::glsl::Program> program;
  zgl::glsl::Program* linked = LinkProgram(kVertexSource, kAddFragmentSource,
                                          &vertex, &fragment, &program);
  ASSERT_TRUE(linked != nullptr);

  const uint32_t values[][2] = {
      {0x00000007u, 0x00000002u},
      {0xFFFFFFFfu, 0x00000001u},
      {0x80000000u, 0x00000000u},
      {0xC4653600u, 0x3B9ACA00u},
  };
  for (const auto& value : values) {
    zgl::glsl::FragmentOutput output;
    ASSERT_TRUE(RunFragmentWithAttributes(linked, 0, 0, value[0], value[1],
                                         &output));
    const uint32_t expected_sum =
        static_cast<uint32_t>(static_cast<int32_t>(value[0]) +
                              static_cast<int32_t>(value[1]));
    EXPECT_EQ(output.color_base[0], zgl::glsl::BaseType::kInt);
    EXPECT_EQ(output.color_int[0][0], value[0]);
    EXPECT_EQ(output.color_int[0][1], value[1]);
    EXPECT_EQ(output.color_int[0][2], expected_sum);
  }
}

TEST(IntegerAddFloatOutputMatchesReferenceFormula) {
  std::unique_ptr<zgl::glsl::ShaderAst> vertex;
  std::unique_ptr<zgl::glsl::ShaderAst> fragment;
  std::unique_ptr<zgl::glsl::Program> program;
  zgl::glsl::Program* linked = LinkProgram(
      kVertexSource, kFloatOutputFragmentSource, &vertex, &fragment, &program);
  ASSERT_TRUE(linked != nullptr);

  const int32_t values[][2] = {
      {-1000000000, 0}, {0, 0}, {1000000000, 0}, {-1073741824, 0},
      {1073741824, 0}, {123456789, -123456789},
  };
  for (const auto& value : values) {
    zgl::glsl::FragmentOutput output;
    ASSERT_TRUE(RunFragmentWithAttributes(
        linked, 0, 0, static_cast<uint32_t>(value[0]),
        static_cast<uint32_t>(value[1]), &output));
    const int32_t sum =
        static_cast<int32_t>(static_cast<uint32_t>(value[0]) +
                             static_cast<uint32_t>(value[1]));
    const float expected = static_cast<float>(sum) * 4e-10f + 0.5f;
    EXPECT_TRUE(output.color_base[0] == zgl::glsl::BaseType::kFloat);
    EXPECT_NEAR(output.color[0][0], expected, 1e-6);
  }
}

TEST(IntegerAddSurvivesRasterizationAndVaryings) {
  ASSERT_TRUE(zglCreateContext(32, 32) == 1);
  const char* vertex_source =
      "#version 300 es\n"
      "precision highp float;\n"
      "precision highp int;\n"
      "in highp vec4 a_position;\n"
      "in highp vec4 a_in0;\n"
      "in highp vec4 a_in1;\n"
      "out highp vec4 v_in0;\n"
      "out highp vec4 v_in1;\n"
      "void main() {\n"
      "  gl_Position = a_position;\n"
      "  v_in0 = a_in0;\n"
      "  v_in1 = a_in1;\n"
      "}\n";
  const GLuint vs = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vs, 1, &vertex_source, nullptr);
  glCompileShader(vs);
  const GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fs, 1, &kFloatOutputFragmentSource, nullptr);
  glCompileShader(fs);
  const GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  ASSERT_TRUE(linked == GL_TRUE);

  const int position_location = glGetAttribLocation(program, "a_position");
  const int in0_location = glGetAttribLocation(program, "a_in0");
  const int in1_location = glGetAttribLocation(program, "a_in1");
  ASSERT_TRUE(position_location >= 0 && in0_location >= 0 &&
              in1_location >= 0);
  const float positions[12] = {-1.0f, -1.0f, 0.0f, 1.0f, 3.0f, -1.0f,
                               0.0f,  1.0f,  0.0f, 3.0f, 0.0f, 1.0f};
  const float in0[12] = {0.0f, 0.0f, -1000000000.0f, 0.0f,
                         0.0f, 0.0f, -1000000000.0f, 0.0f,
                         0.0f, 0.0f, -1000000000.0f, 0.0f};
  const float in1[12] = {};
  glUseProgram(program);
  glEnableVertexAttribArray(position_location);
  glVertexAttribPointer(position_location, 4, GL_FLOAT, GL_FALSE, 0,
                        positions);
  glEnableVertexAttribArray(in0_location);
  glVertexAttribPointer(in0_location, 4, GL_FLOAT, GL_FALSE, 0, in0);
  glEnableVertexAttribArray(in1_location);
  glVertexAttribPointer(in1_location, 4, GL_FLOAT, GL_FALSE, 0, in1);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  unsigned char rgba[4] = {};
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  EXPECT_EQ(rgba[0], 26);
  EXPECT_EQ(rgba[1], 0);
  EXPECT_EQ(rgba[2], 0);
  EXPECT_EQ(rgba[3], 255);
}

TEST(IntegerArrayConstructorPreservesOrder) {
  std::unique_ptr<zgl::glsl::ShaderAst> vertex;
  std::unique_ptr<zgl::glsl::ShaderAst> fragment;
  std::unique_ptr<zgl::glsl::Program> program;
  zgl::glsl::Program* linked = LinkProgram(kVertexSource, kArrayFragmentSource,
                                          &vertex, &fragment, &program);
  ASSERT_TRUE(linked != nullptr);

  zgl::glsl::FragmentOutput output;
  ASSERT_TRUE(RunFragmentWithAttributes(linked, 7u, 0xFFFFFFFFu, 2u, 0u,
                                       &output));
  EXPECT_EQ(output.color_base[0], zgl::glsl::BaseType::kInt);
  // The input vector is [7, -1, 2]; the array rotates it to [2, 7, -1].
  EXPECT_EQ(output.color_int[0][0], 2u);
  EXPECT_EQ(output.color_int[0][1], 7u);
  EXPECT_EQ(output.color_int[0][2], 0xFFFFFFFFu);
}
