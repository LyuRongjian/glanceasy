// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <math.h>
#include <string.h>

#include "GLES3/gl32.h"
#include "tests/zgl_test.h"
#include "zgl/zgl.h"

namespace {

const char* kFullscreenVertex =
    "#version 100\n"
    "attribute vec2 a_pos;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "  v_uv = a_pos * 0.5 + vec2(0.5);\n"
    "  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

const float kTriangle[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};

GLuint Compile(GLenum type, const char* source) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  return shader;
}

GLuint BuildProgram(const char* vertex_source, const char* fragment_source) {
  const GLuint vs = Compile(GL_VERTEX_SHADER, vertex_source);
  const GLuint fs = Compile(GL_FRAGMENT_SHADER, fragment_source);
  const GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  return program;
}

void DrawFullscreen(GLuint program) {
  glUseProgram(program);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, kTriangle);
  glDrawArrays(GL_TRIANGLES, 0, 3);
}

void ReadCentre(unsigned char rgba[4]) {
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

bool LinkSucceeded(GLuint program) {
  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  return linked == GL_TRUE;
}

}  // namespace

TEST(ArithmeticAndSwizzlesDriveColour) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "void main() {\n"
      "  float a = 0.5;\n"
      "  vec3 v = vec3(0.1, 0.2, 0.3);\n"
      "  vec3 w = v.zyx * 2.0 + vec3(a, 0.0, -a);\n"
      "  gl_FragColor = vec4(w.x, w.y, w.z, 1.0);\n"
      "}\n";
  const GLuint program = BuildProgram(kFullscreenVertex, fragment);
  EXPECT_TRUE(LinkSucceeded(program));
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  DrawFullscreen(program);
  unsigned char rgba[4] = {};
  ReadCentre(rgba);
  // v.zyx * 2 + (0.5, 0.0, -0.5) saturates the red channel and clips the
  // blue one at zero.
  EXPECT_EQ(rgba[0], 255);
  EXPECT_NEAR(static_cast<float>(rgba[1]) / 255.0f, 0.4f, 0.02f);
  EXPECT_EQ(rgba[2], 0);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(LoopsAndBranchesExecute) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "void main() {\n"
      "  float total = 0.0;\n"
      "  for (int i = 0; i < 4; ++i) {\n"
      "    int j = i;\n"
      "    while (j > 0) { total += 1.0; --j; }\n"
      "  }\n"
      "  if (total > 5.0 && total < 7.0) {\n"
      "    gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
      "  } else {\n"
      "    gl_FragColor = vec4(0.0, 0.0, 1.0, 1.0);\n"
      "  }\n"
      "}\n";
  const GLuint program = BuildProgram(kFullscreenVertex, fragment);
  EXPECT_TRUE(LinkSucceeded(program));
  glClear(GL_COLOR_BUFFER_BIT);
  DrawFullscreen(program);
  unsigned char rgba[4] = {};
  ReadCentre(rgba);
  EXPECT_EQ(rgba[0], 255);
  EXPECT_EQ(rgba[2], 0);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(DoWhileBreakAndContinueWork) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "void main() {\n"
      "  float value = 0.0;\n"
      "  int i = 0;\n"
      "  do {\n"
      "    ++i;\n"
      "    if (i == 2) continue;\n"
      "    if (i > 3) break;\n"
      "    value += 0.25;\n"
      "  } while (i < 10);\n"
      "  gl_FragColor = vec4(0.0, value, 0.0, 1.0);\n"
      "}\n";
  const GLuint program = BuildProgram(kFullscreenVertex, fragment);
  EXPECT_TRUE(LinkSucceeded(program));
  glClear(GL_COLOR_BUFFER_BIT);
  DrawFullscreen(program);
  unsigned char rgba[4] = {};
  ReadCentre(rgba);
  // The loop adds 0.25 for i equal to 1 and 3, then breaks at i == 4.
  EXPECT_EQ(rgba[1], 128);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(UserFunctionsAreCallableButNotRecursive) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "float Square(float x) { return x * x; }\n"
      "vec3 Mix3(vec3 a, vec3 b, float t) { return a + (b - a) * t; }\n"
      "void main() {\n"
      "  float s = Square(0.5);\n"
      "  gl_FragColor = vec4(Mix3(vec3(0.0), vec3(1.0), s), 1.0);\n"
      "}\n";
  const GLuint program = BuildProgram(kFullscreenVertex, fragment);
  EXPECT_TRUE(LinkSucceeded(program));
  glClear(GL_COLOR_BUFFER_BIT);
  DrawFullscreen(program);
  unsigned char rgba[4] = {};
  ReadCentre(rgba);
  EXPECT_EQ(rgba[0], 64);

  const char* recursive =
      "#version 100\n"
      "precision mediump float;\n"
      "float Down(float x) { return x > 0.0 ? Down(x - 1.0) : 0.0; }\n"
      "void main() { gl_FragColor = vec4(Down(2.0)); }\n";
  const GLuint broken = BuildProgram(kFullscreenVertex, recursive);
  EXPECT_FALSE(LinkSucceeded(broken));
  GLint log_length = 0;
  glGetProgramiv(broken, GL_INFO_LOG_LENGTH, &log_length);
  EXPECT_TRUE(log_length > 0);
  glDeleteProgram(program);
  glDeleteProgram(broken);
  zglDestroyContext();
}

TEST(BuiltinMathFunctionsComputeExpectedValues) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "void main() {\n"
      "  vec3 a = normalize(vec3(3.0, 0.0, 4.0));\n"
      "  float d = dot(vec2(0.5, 0.5), vec2(1.0, 1.0));\n"
      "  float c = clamp(1.5, 0.0, 1.0);\n"
      "  float m = mix(0.0, 1.0, 0.5);\n"
      "  float st = step(0.5, 0.75);\n"
      "  float ss = smoothstep(0.0, 1.0, 0.5);\n"
      "  gl_FragColor = vec4(a.x + d - c, m, st, 1.0) * vec4(1.0, 1.0, 1.0, "
      "ss);\n"
      "}\n";
  const GLuint program = BuildProgram(kFullscreenVertex, fragment);
  EXPECT_TRUE(LinkSucceeded(program));
  glClear(GL_COLOR_BUFFER_BIT);
  DrawFullscreen(program);
  unsigned char rgba[4] = {};
  ReadCentre(rgba);
  EXPECT_EQ(rgba[0], 153);
  EXPECT_EQ(rgba[1], 128);
  EXPECT_EQ(rgba[2], 255);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(DiscardLeavesExistingPixels) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "void main() {\n"
      "  if (gl_FragCoord.x > 16.0 && gl_FragCoord.x < 24.0 &&\n"
      "      gl_FragCoord.y > 8.0 && gl_FragCoord.y < 24.0)\n"
      "    discard;\n"
      "  gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
      "}\n";
  const GLuint program = BuildProgram(kFullscreenVertex, fragment);
  EXPECT_TRUE(LinkSucceeded(program));
  glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  DrawFullscreen(program);
  unsigned char discarded[4] = {};
  unsigned char kept[4] = {};
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, discarded);
  glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, kept);
  EXPECT_EQ(discarded[2], 255);
  EXPECT_EQ(kept[0], 255);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(MatrixUniformMovesTheTriangle) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* vertex =
      "#version 100\n"
      "attribute vec2 a_pos;\n"
      "uniform mat4 u_mvp;\n"
      "void main() { gl_Position = u_mvp * vec4(a_pos, 0.0, 1.0); }\n";
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "void main() { gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n";
  const GLuint program = BuildProgram(vertex, fragment);
  EXPECT_TRUE(LinkSucceeded(program));
  glUseProgram(program);
  const GLint location = glGetUniformLocation(program, "u_mvp");
  EXPECT_TRUE(location >= 0);
  // The quad is centred first and then translated half a viewport to the
  // right, which is the identity path plus a column major translation.
  const GLfloat identity[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  const GLfloat translated[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                  0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                  0.5f, 0.0f, 0.0f, 1.0f};
  const float quad[12] = {-0.2f, -0.2f, 0.2f, -0.2f, 0.2f,  0.2f,
                          -0.2f, -0.2f, 0.2f, 0.2f,  -0.2f, 0.2f};
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
  glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glUniformMatrix4fv(location, 1, GL_FALSE, identity);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  unsigned char centred[4] = {};
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, centred);
  EXPECT_EQ(centred[0], 255);

  glClear(GL_COLOR_BUFFER_BIT);
  glUniformMatrix4fv(location, 1, GL_FALSE, translated);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  unsigned char left[4] = {};
  unsigned char right[4] = {};
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, left);
  glReadPixels(24, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, right);
  EXPECT_EQ(left[2], 255);
  EXPECT_EQ(right[0], 255);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(TextureSamplingWorksInsideShader) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const unsigned char texels[16] = {
      255, 0, 0,   255, 0,   255, 0, 255,  // Row 0: red, green.
      0,   0, 255, 255, 255, 255, 0, 255   // Row 1: blue, yellow.
  };
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               texels);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "uniform sampler2D u_texture;\n"
      "varying vec2 v_uv;\n"
      "void main() { gl_FragColor = texture2D(u_texture, v_uv); }\n";
  const GLuint program = BuildProgram(kFullscreenVertex, fragment);
  EXPECT_TRUE(LinkSucceeded(program));
  glUseProgram(program);
  glUniform1i(glGetUniformLocation(program, "u_texture"), 0);
  glClear(GL_COLOR_BUFFER_BIT);
  DrawFullscreen(program);

  unsigned char top_left[4] = {};
  unsigned char top_right[4] = {};
  glReadPixels(4, 28, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, top_left);
  glReadPixels(28, 28, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, top_right);
  EXPECT_EQ(top_left[0], 255);
  EXPECT_EQ(top_left[1], 0);
  EXPECT_EQ(top_right[0], 0);
  EXPECT_EQ(top_right[1], 255);
  glDeleteProgram(program);
  glDeleteTextures(1, &texture);
  zglDestroyContext();
}

TEST(Version300ShadersAcceptInAndOutDeclarations) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* vertex =
      "#version 300 es\n"
      "in vec2 a_pos;\n"
      "out vec2 v_uv;\n"
      "void main() { v_uv = a_pos; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
  const char* fragment =
      "#version 300 es\n"
      "precision mediump float;\n"
      "in vec2 v_uv;\n"
      "out vec4 o_colour;\n"
      "void main() { o_colour = vec4(v_uv * 0.5 + vec2(0.5), 0.0, 1.0); }\n";
  const GLuint program = BuildProgram(vertex, fragment);
  EXPECT_TRUE(LinkSucceeded(program));
  glClear(GL_COLOR_BUFFER_BIT);
  DrawFullscreen(program);
  unsigned char rgba[4] = {};
  ReadCentre(rgba);
  EXPECT_NEAR(static_cast<float>(rgba[0]) / 255.0f, 0.49f, 0.05f);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(VectorRelationalFunctionsProduceBooleans) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "void main() {\n"
      "  bvec2 a = lessThan(vec2(0.1, 0.9), vec2(0.5));\n"
      "  bool any_set = any(a);\n"
      "  bool all_set = all(a);\n"
      "  gl_FragColor = vec4(any_set ? 0.5 : 0.0, all_set ? 0.5 : 0.0, "
      "not(a).x ? 0.5 : 0.0, 1.0);\n"
      "}\n";
  const GLuint program = BuildProgram(kFullscreenVertex, fragment);
  EXPECT_TRUE(LinkSucceeded(program));
  glClear(GL_COLOR_BUFFER_BIT);
  DrawFullscreen(program);
  unsigned char rgba[4] = {};
  ReadCentre(rgba);
  // any() is true for the two mixed comparisons, all() is false because
  // 0.9 is not below 0.5, and not(a).x is false.
  EXPECT_EQ(rgba[0], 128);
  EXPECT_EQ(rgba[1], 0);
  EXPECT_EQ(rgba[2], 0);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(MatrixHelpersAndTransposeAreSupported) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "void main() {\n"
      "  mat2 m = mat2(1.0, 2.0, 3.0, 4.0);\n"
      "  mat2 t = transpose(m);\n"
      "  mat2 p = matrixCompMult(m, t);\n"
      "  float d = determinant(m);\n"
      "  vec2 column = m[0];\n"
      "  gl_FragColor = vec4(p[0][0] * 0.5, column.x * 0.5, d * 0.1, 1.0);\n"
      "}\n";
  const GLuint program = BuildProgram(kFullscreenVertex, fragment);
  EXPECT_TRUE(LinkSucceeded(program));
  glClear(GL_COLOR_BUFFER_BIT);
  DrawFullscreen(program);
  unsigned char rgba[4] = {};
  ReadCentre(rgba);
  // m[0] is column zero (1, 2), so the first element is one; the product of
  // the transposed matrix is one as well and the determinant is -2.
  EXPECT_EQ(rgba[0], 128);
  EXPECT_EQ(rgba[1], 128);
  EXPECT_EQ(rgba[2], 0);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(GeometryFunctionsHandleDegenerateInput) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "void main() {\n"
      "  vec3 zero = vec3(0.0);\n"
      "  vec3 normal = normalize(zero);\n"
      "  float len = length(zero);\n"
      "  vec3 cross_value = cross(vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0));\n"
      "  gl_FragColor = vec4(normal.x, len, cross_value.z, 1.0);\n"
      "}\n";
  const GLuint program = BuildProgram(kFullscreenVertex, fragment);
  EXPECT_TRUE(LinkSucceeded(program));
  glClear(GL_COLOR_BUFFER_BIT);
  DrawFullscreen(program);
  unsigned char rgba[4] = {};
  ReadCentre(rgba);
  EXPECT_EQ(rgba[2], 255);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(UnsupportedConstructsFailToCompile) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* constructs[] = {
      // Arrays are outside the supported subset.
      "#version 100\nprecision mediump float;\n"
      "void main() { float values[2]; values[0] = 1.0; "
      "gl_FragColor = vec4(values[0]); }\n",
      // Structures are not implemented.
      "#version 100\nprecision mediump float;\n"
      "struct Light { vec3 colour; };\n"
      "void main() { Light l; l.colour = vec3(1.0); "
      "gl_FragColor = vec4(l.colour, 1.0); }\n",
      // Unknown builtins must be rejected.
      "#version 100\nprecision mediump float;\n"
      "void main() { gl_FragColor = vec4(no_such_function(1.0)); }\n",
      // Unsupported version directives are rejected.
      "#version 400\nprecision mediump float;\n"
      "void main() { gl_FragColor = vec4(1.0); }\n",
  };
  for (const char* source : constructs) {
    const GLuint shader = Compile(GL_FRAGMENT_SHADER, source);
    GLint compiled = GL_TRUE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    EXPECT_EQ(compiled, GL_FALSE);
    glDeleteShader(shader);
  }
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  zglDestroyContext();
}
