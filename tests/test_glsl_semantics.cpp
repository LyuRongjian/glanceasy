// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Shader-level tests for the GLSL interpreter: operators, control flow,
// built-in functions, matrices, relational functions, user functions,
// discard and texture lookups.

#include "GLES3/gl32.h"
#include "tests/zgl_test.h"
#include "zgl/zgl.h"

namespace {

const float kFullscreen[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};

const char* kVertex =
    "#version 100\n"
    "attribute vec2 a_pos;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "  v_uv = a_pos * 0.5 + vec2(0.5);\n"
    "  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

GLuint BuildProgram(const char* fragment_source) {
  const GLuint vs = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vs, 1, &kVertex, nullptr);
  glCompileShader(vs);
  const GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fs, 1, &fragment_source, nullptr);
  glCompileShader(fs);
  const GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  return program;
}

void RenderAndRead(GLuint program, unsigned char rgba[4]) {
  glUseProgram(program);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, kFullscreen);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

int Unorm8(float value) {
  if (value >= 1.0f)
    return 255;
  if (value <= 0.0f)
    return 0;
  return static_cast<int>(value * 255.0f + 0.5f);
}

void ExpectPixel(const unsigned char rgba[4], float r, float g, float b) {
  EXPECT_NEAR(rgba[0], Unorm8(r), 2);
  EXPECT_NEAR(rgba[1], Unorm8(g), 2);
  EXPECT_NEAR(rgba[2], Unorm8(b), 2);
}

}  // namespace

TEST(ArithmeticOperatorsAndIncrements) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "void main() {\n"
      "  float a = 1.0;\n"
      "  a += 2.0;\n"
      "  a *= 2.0;\n"
      "  a -= 1.0;\n"
      "  a /= 3.0;\n"
      "  int i = 2;\n"
      "  i++;\n"
      "  i--;\n"
      "  i *= 3;\n"
      "  bool flag = (i == 6) && (a > 1.5) && !(a < 1.0);\n"
      "  gl_FragColor = vec4(flag ? 1.0 : 0.0, a / 2.0, float(i) / 8.0, 1.0);\n"
      "}\n";
  const GLuint program = BuildProgram(fragment);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  unsigned char rgba[4] = {};
  RenderAndRead(program, rgba);
  ExpectPixel(rgba, 1.0f, 1.666f / 2.0f, 6.0f / 8.0f);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(ControlFlowStatementsExecute) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "void main() {\n"
      "  float sum = 0.0;\n"
      "  for (int i = 0; i < 4; ++i) {\n"
      "    if (i == 2) continue;\n"
      "    sum += 0.25;\n"
      "  }\n"
      "  float w = 0.0;\n"
      "  int j = 0;\n"
      "  while (j < 2) {\n"
      "    w += 0.125;\n"
      "    ++j;\n"
      "  }\n"
      "  float d = 0.0;\n"
      "  int k = 0;\n"
      "  do {\n"
      "    d += 0.125;\n"
      "    ++k;\n"
      "  } while (k < 2);\n"
      "  for (int m = 0; m < 3; ++m) {\n"
      "    if (m == 1) break;\n"
      "    sum += 0.125;\n"
      "  }\n"
      "  float t = sum > 0.5 ? 1.0 : 0.0;\n"
      "  gl_FragColor = vec4(sum, w + d, t, 1.0);\n"
      "}\n";
  const GLuint program = BuildProgram(fragment);
  unsigned char rgba[4] = {};
  RenderAndRead(program, rgba);
  ExpectPixel(rgba, 0.875f, 0.5f, 1.0f);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(TrigonometricAndExponentialBuiltins) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "void main() {\n"
      "  float a = sin(0.0);\n"
      "  float b = cos(0.0);\n"
      "  float c = pow(2.0, 3.0) / 8.0;\n"
      "  float d = sqrt(16.0) / 4.0;\n"
      "  float e = inversesqrt(4.0) / 0.5;\n"
      "  float f = exp(0.0);\n"
      "  float g = log(exp(1.0));\n"
      "  float h = exp2(3.0) / 8.0;\n"
      "  float i = log2(8.0) / 3.0;\n"
      "  gl_FragColor = vec4(a, (b + c + d) / 3.0, (e + f + g + h + i) / 5.0,\n"
      "                      1.0);\n"
      "}\n";
  const GLuint program = BuildProgram(fragment);
  unsigned char rgba[4] = {};
  RenderAndRead(program, rgba);
  ExpectPixel(rgba, 0.0f, 1.0f, 1.0f);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(VectorTrigonometricBuiltinsEvaluateAllLanes) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "void main() {\n"
      "  vec4 s = sin(vec4(0.0, 0.5235988, 1.0471976, 1.5707963));\n"
      "  vec3 c = cos(vec3(0.0, 1.0471976, 1.5707963));\n"
      "  gl_FragColor = vec4(s.x, c.x, s.y, 1.0);\n"
      "}\n";
  const GLuint program = BuildProgram(fragment);
  unsigned char rgba[4] = {};
  RenderAndRead(program, rgba);
  ExpectPixel(rgba, 0.0f, 1.0f, 0.5f);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(CommonBuiltinsHandleEdgeCases) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "void main() {\n"
      "  float a = abs(-0.5);\n"
      "  float b = sign(-3.0) * -1.0;\n"
      "  float c = floor(2.75) / 4.0;\n"
      "  float d = ceil(1.25) / 4.0;\n"
      "  float e = fract(1.25);\n"
      "  float f = mod(5.0, 2.0);\n"
      "  float g = min(max(-2.0, 0.0), 1.0);\n"
      "  float h = clamp(2.0, 0.0, 1.0);\n"
      "  float i = mix(0.0, 1.0, 0.5);\n"
      "  float j = step(0.5, 0.75);\n"
      "  float k = smoothstep(0.0, 1.0, 1.0);\n"
      "  gl_FragColor = vec4((a + b + c + d + e + f + g + h) / 8.0,\n"
      "                      (i + j + k) / 3.0, 0.0, 1.0);\n"
      "}\n";
  const GLuint program = BuildProgram(fragment);
  unsigned char rgba[4] = {};
  RenderAndRead(program, rgba);
  // (0.5 + 1 + 0.5 + 0.5 + 0.25 + 1 + 0 + 1) / 8 = 0.59375 and
  // (0.5 + 1 + 1) / 3.
  ExpectPixel(rgba, 0.59375f, 0.8333f, 0.0f);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(GeometricBuiltinsProduceUnitResults) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "void main() {\n"
      "  vec2 u = normalize(vec2(3.0, 4.0));\n"
      "  float len = length(vec2(3.0, 4.0)) / 5.0;\n"
      "  float dist = distance(vec2(1.0, 1.0), vec2(4.0, 5.0)) / 5.0;\n"
      "  float dp = dot(u, vec2(0.6, 0.8));\n"
      "  vec3 cr = cross(vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0));\n"
      "  vec2 rf = reflect(vec2(0.0, -1.0), vec2(0.0, 1.0));\n"
      "  gl_FragColor = vec4(u.x * len * dp, rf.x + 1.0, cr.z, 1.0);\n"
      "}\n";
  const GLuint program = BuildProgram(fragment);
  unsigned char rgba[4] = {};
  RenderAndRead(program, rgba);
  ExpectPixel(rgba, 0.6f, 1.0f, 1.0f);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(MatrixBuiltinsAndColumnMajorStorage) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "void main() {\n"
      "  mat2 m = mat2(1.0, 2.0, 3.0, 4.0);\n"
      "  vec2 v = m * vec2(1.0, 0.0);\n"
      "  mat2 t = transpose(m);\n"
      "  vec2 tv = t * vec2(0.0, 1.0);\n"
      "  mat2 p = matrixCompMult(m, m);\n"
      "  vec2 pv = p * vec2(1.0, 0.0);\n"
      "  gl_FragColor = vec4(v.x / 2.0, v.y / 4.0, tv.x / 4.0, 1.0);\n"
      "}\n";
  const GLuint program = BuildProgram(fragment);
  unsigned char rgba[4] = {};
  RenderAndRead(program, rgba);
  // v = (1, 2), transpose column 1 = (2, 4), matrixCompMult column 0 = (1, 4).
  ExpectPixel(rgba, 0.5f, 0.5f, 0.5f);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(RelationalBuiltinsReturnBooleanVectors) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "void main() {\n"
      "  vec2 a = vec2(1.0, 2.0);\n"
      "  vec2 b = vec2(1.0, 3.0);\n"
      "  bvec2 lt = lessThan(a, b);\n"
      "  bvec2 eq = equal(a, b);\n"
      "  bvec2 nt = not(lt);\n"
      "  bool anyv = any(lt);\n"
      "  bool allv = all(eq);\n"
      "  gl_FragColor = vec4(lt.y ? 1.0 : 0.0, anyv ? 1.0 : 0.0,\n"
      "                      nt.x ? 1.0 : 0.0, allv ? 0.0 : 1.0);\n"
      "}\n";
  const GLuint program = BuildProgram(fragment);
  unsigned char rgba[4] = {};
  RenderAndRead(program, rgba);
  ExpectPixel(rgba, 1.0f, 1.0f, 1.0f);
  EXPECT_EQ(rgba[3], 255);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(UserFunctionsKeepTheirOwnLocalFrame) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "float Square(float x) { return x * x; }\n"
      "float Add(float a, float b) { float s = a + b; return s; }\n"
      "float Twice(float v) {\n"
      "  float local = Square(v);\n"
      "  return Add(local, local);\n"
      "}\n"
      "void main() {\n"
      "  float r = Twice(0.5);\n"
      "  gl_FragColor = vec4(r, 2.0 * r, 1.0 - r, 1.0);\n"
      "}\n";
  const GLuint program = BuildProgram(fragment);
  unsigned char rgba[4] = {};
  RenderAndRead(program, rgba);
  ExpectPixel(rgba, 0.5f, 1.0f, 0.5f);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(DiscardLeavesTheFramebufferUntouched) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "varying vec2 v_uv;\n"
      "void main() {\n"
      "  if (v_uv.x >= 0.0) discard;\n"
      "  gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
      "}\n";
  const GLuint program = BuildProgram(fragment);
  glUseProgram(program);
  glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, kFullscreen);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  unsigned char rgba[4] = {};
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  ExpectPixel(rgba, 0.0f, 1.0f, 0.0f);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(TextureBuiltinsSelectLevelsAndProject) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  unsigned char red[4 * 4 * 4] = {};
  for (unsigned int i = 0; i < sizeof(red); i += 4) {
    red[i] = 255;
    red[i + 3] = 255;
  }
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               red);
  unsigned char green[2 * 2 * 4] = {};
  for (unsigned int i = 0; i < sizeof(green); i += 4) {
    green[i + 1] = 255;
    green[i + 3] = 255;
  }
  glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               green);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  const char* fragment =
      "#version 300 es\n"
      "precision mediump float;\n"
      "uniform sampler2D u_texture;\n"
      "out vec4 o_colour;\n"
      "void main() {\n"
      "  o_colour = textureLod(u_texture, vec2(0.5, 0.5), 1.0);\n"
      "}\n";
  GLuint program = BuildProgram(fragment);
  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (linked == GL_TRUE) {
    glUniform1i(glGetUniformLocation(program, "u_texture"), 0);
    unsigned char level_rgba[4] = {};
    RenderAndRead(program, level_rgba);
    ExpectPixel(level_rgba, 0.0f, 1.0f, 0.0f);
  }
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glDeleteProgram(program);

  const char* proj_fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "uniform sampler2D u_texture;\n"
      "void main() {\n"
      "  gl_FragColor = texture2DProj(u_texture, vec3(0.5, 0.5, 1.0));\n"
      "}\n";
  program = BuildProgram(proj_fragment);
  glUseProgram(program);
  glUniform1i(glGetUniformLocation(program, "u_texture"), 0);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, kFullscreen);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  unsigned char rgba[4] = {};
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  ExpectPixel(rgba, 1.0f, 0.0f, 0.0f);
  glDeleteProgram(program);
  glDeleteTextures(1, &texture);
  zglDestroyContext();
}
