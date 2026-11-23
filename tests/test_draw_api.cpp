// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "GLES3/gl32.h"
#include "tests/zgl_test.h"
#include "zgl/zgl.h"

namespace {

const float kFullscreen[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};

const char* kVertex =
    "#version 100\n"
    "attribute vec2 a_pos;\n"
    "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
const char* kFragment =
    "#version 100\n"
    "precision mediump float;\n"
    "void main() { gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n";

GLuint BuildProgram() {
  const GLuint vs = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vs, 1, &kVertex, nullptr);
  glCompileShader(vs);
  const GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fs, 1, &kFragment, nullptr);
  glCompileShader(fs);
  const GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  return program;
}

void PrepareDraw(GLuint program) {
  glUseProgram(program);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, kFullscreen);
  glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
}

bool CentreIsRed() {
  unsigned char rgba[4] = {};
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  return rgba[0] > 200 && rgba[2] < 40;
}

}  // namespace

TEST(DrawArraysRejectsNegativeArguments) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  glDrawArrays(GL_TRIANGLES, -1, 3);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glDrawArrays(GL_TRIANGLES, 0, -1);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glDrawArraysInstanced(GL_TRIANGLES, -1, 3, 1);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glDrawArraysInstanced(GL_TRIANGLES, 0, 3, -1);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  zglDestroyContext();
}

TEST(DrawElementsValidatesTypesAndPointers) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const GLuint program = BuildProgram();
  PrepareDraw(program);
  const unsigned short indices[3] = {0, 1, 2};
  const unsigned int indices32[3] = {0, 1, 2};
  const unsigned char indices8[3] = {0, 1, 2};

  glDrawElements(GL_TRIANGLES, -1, GL_UNSIGNED_SHORT, indices);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glDrawElements(GL_TRIANGLES, 3, GL_FLOAT, indices);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, indices32);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  EXPECT_TRUE(CentreIsRed());

  glClear(GL_COLOR_BUFFER_BIT);
  glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_BYTE, indices8);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  EXPECT_TRUE(CentreIsRed());

  GLuint element_buffer = 0;
  glGenBuffers(1, &element_buffer);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, element_buffer);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
               GL_STATIC_DRAW);
  glClear(GL_COLOR_BUFFER_BIT);
  glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, nullptr);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  EXPECT_TRUE(CentreIsRed());

  glClear(GL_COLOR_BUFFER_BIT);
  glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT,
                 reinterpret_cast<const void*>(sizeof(indices) + 4));
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);

  glDrawElementsInstanced(GL_TRIANGLES, -1, GL_UNSIGNED_SHORT, nullptr, 1);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glDrawElementsInstanced(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, nullptr, -1);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glDrawElementsInstanced(GL_TRIANGLES, 3, GL_SHORT, nullptr, 1);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glDrawElementsInstanced(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT,
                          reinterpret_cast<const void*>(1024), 1);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);

  glClear(GL_COLOR_BUFFER_BIT);
  glDrawElementsInstanced(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, nullptr, 2);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  EXPECT_TRUE(CentreIsRed());

  glClear(GL_COLOR_BUFFER_BIT);
  glDrawRangeElements(GL_TRIANGLES, 0, 2, 3, GL_UNSIGNED_SHORT, nullptr);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  EXPECT_TRUE(CentreIsRed());

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glDeleteBuffers(1, &element_buffer);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(InstancedArraysMultiplyGeometry) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const GLuint program = BuildProgram();
  PrepareDraw(program);
  glClear(GL_COLOR_BUFFER_BIT);
  glDrawArraysInstanced(GL_TRIANGLES, 0, 3, 3);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  EXPECT_TRUE(CentreIsRed());
  glDeleteProgram(program);
  zglDestroyContext();
}
