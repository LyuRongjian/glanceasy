// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "GLES3/gl32.h"
#include "tests/zgl_test.h"
#include "zgl/zgl.h"

namespace {

const float kFullscreen[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};

GLuint LinkProgram(const char* vertex_source, const char* fragment_source) {
  const GLuint vs = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vs, 1, &vertex_source, nullptr);
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

const char* kVertex =
    "#version 100\n"
    "attribute vec2 a_pos;\n"
    "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }\n";

void DrawAndRead(unsigned char* rgba) {
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, kFullscreen);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

}  // namespace

TEST(MatrixUniformsHonourTheTransposeFlag) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "uniform mat2 u_matrix;\n"
      "void main() {\n"
      "  gl_FragColor = vec4((u_matrix * vec2(0.0, 1.0)).x, 0.0, 0.0, 1.0);\n"
      "}\n";
  const GLuint program = LinkProgram(kVertex, fragment);
  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  EXPECT_EQ(linked, GL_TRUE);
  glUseProgram(program);
  const GLint location = glGetUniformLocation(program, "u_matrix");
  EXPECT_EQ(location, 0);
  const float values[4] = {0.1f, 0.2f, 0.3f, 0.4f};
  unsigned char rgba[4] = {};

  glUniformMatrix2fv(location, 1, GL_FALSE, values);
  glClear(GL_COLOR_BUFFER_BIT);
  DrawAndRead(rgba);
  EXPECT_EQ(rgba[0], 77);  // 0.3 * 255

  glUniformMatrix2fv(location, 1, GL_TRUE, values);
  glClear(GL_COLOR_BUFFER_BIT);
  DrawAndRead(rgba);
  EXPECT_EQ(rgba[0], 51);  // 0.2 * 255

  glUniformMatrix3fv(location, 1, GL_FALSE, values);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(FloatUniformsRejectMismatchedComponentCounts) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "uniform float u_value;\n"
      "uniform int u_count;\n"
      "void main() {\n"
      "  gl_FragColor = vec4(u_value, float(u_count) * 0.25, 0.0, 1.0);\n"
      "}\n";
  const GLuint program = LinkProgram(kVertex, fragment);
  glUseProgram(program);
  const GLint value_location = glGetUniformLocation(program, "u_value");
  const GLint count_location = glGetUniformLocation(program, "u_count");
  glUniform1f(value_location, 0.5f);
  glUniform1i(count_location, 2);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glClear(GL_COLOR_BUFFER_BIT);
  unsigned char rgba[4] = {};
  DrawAndRead(rgba);
  EXPECT_EQ(rgba[0], 128);
  EXPECT_EQ(rgba[1], 128);

  // A scalar uniform cannot be written through the vector entry points.
  glUniform3f(value_location, 1.0f, 1.0f, 1.0f);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glUniform4fv(value_location, 1, kFullscreen);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);

  // Unknown locations are silently ignored.
  glUniform1f(-1, 1.0f);
  glUniform1f(99, 1.0f);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(UniformUpdatesWithoutAProgramAreIgnored) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  glUniform1f(0, 1.0f);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  glUniformMatrix4fv(0, 1, GL_FALSE, identity);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  zglDestroyContext();
}
