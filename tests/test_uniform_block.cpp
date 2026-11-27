// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include "GLES3/gl32.h"
#include "tests/glanceasy_test.h"
#include "glanceasy/glanceasy.h"

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
    "#version 300 es\n"
    "in vec2 a_pos;\n"
    "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }\n";

void DrawAndRead(unsigned char* rgba) {
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, kFullscreen);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

GLuint MakeUniformBuffer(const void* data, size_t size) {
  GLuint buffer = 0;
  glGenBuffers(1, &buffer);
  glBindBuffer(GL_UNIFORM_BUFFER, buffer);
  glBufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(size), data,
               GL_STATIC_DRAW);
  return buffer;
}

bool Linked(GLuint program) {
  GLint status = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &status);
  return status == GL_TRUE;
}

}  // namespace

TEST(UniformBlockWithoutInstanceExposesMemberNames) {
  EXPECT_EQ(glanceasyCreateContext(64, 64), 1);
  const char* fragment =
      "#version 300 es\n"
      "precision mediump float;\n"
      "layout(std140) uniform Tint { vec4 color; };\n"
      "out vec4 frag_color;\n"
      "void main() { frag_color = color; }\n";
  const GLuint program = LinkProgram(kVertex, fragment);
  EXPECT_TRUE(Linked(program));
  glUseProgram(program);

  const float color[4] = {1.0f, 0.5f, 0.0f, 1.0f};
  const GLuint buffer = MakeUniformBuffer(color, sizeof(color));
  glBindBufferBase(GL_UNIFORM_BUFFER, 0, buffer);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  unsigned char rgba[4] = {};
  DrawAndRead(rgba);
  EXPECT_EQ(rgba[0], 255);
  EXPECT_EQ(rgba[1], 128);

  glDeleteBuffers(1, &buffer);
  glDeleteProgram(program);
  glanceasyDestroyContext();
}

TEST(UniformBlockFeedsAnInstanceMember) {
  EXPECT_EQ(glanceasyCreateContext(64, 64), 1);
  const char* fragment =
      "#version 300 es\n"
      "precision mediump float;\n"
      "layout(std140) uniform Tint { vec4 color; } tint;\n"
      "out vec4 frag_color;\n"
      "void main() { frag_color = tint.color; }\n";
  const GLuint program = LinkProgram(kVertex, fragment);
  EXPECT_TRUE(Linked(program));
  glUseProgram(program);

  const float color[4] = {0.25f, 0.5f, 0.75f, 1.0f};
  const GLuint buffer = MakeUniformBuffer(color, sizeof(color));
  glBindBufferBase(GL_UNIFORM_BUFFER, 0, buffer);

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  unsigned char rgba[4] = {};
  DrawAndRead(rgba);
  EXPECT_EQ(rgba[0], 64);
  EXPECT_EQ(rgba[1], 128);
  EXPECT_EQ(rgba[2], 191);

  EXPECT_EQ(glGetUniformLocation(program, "color"), -1);
  glDeleteBuffers(1, &buffer);
  glDeleteProgram(program);
  glanceasyDestroyContext();
}

TEST(UniformBlockReflectionUsesStd140Offsets) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  const char* fragment =
      "#version 300 es\n"
      "precision mediump float;\n"
      "layout(std140) uniform Layout {\n"
      "  float a;\n"
      "  vec3 b;\n"
      "  vec2 c;\n"
      "  mat2 d;\n"
      "} layout;\n"
      "out vec4 frag_color;\n"
      "void main() { frag_color = vec4(1.0); }\n";
  const GLuint program = LinkProgram(kVertex, fragment);
  EXPECT_TRUE(Linked(program));

  EXPECT_EQ(glGetUniformBlockIndex(program, "Layout"), 0);
  EXPECT_EQ(glGetUniformBlockIndex(program, "Missing"), GL_INVALID_INDEX);
  GLchar name[16] = {};
  GLsizei length = 0;
  glGetActiveUniformBlockName(program, 0, sizeof(name), &length, name);
  EXPECT_EQ(length, 6);
  EXPECT_TRUE(name[0] == 'L' && name[5] == 't');
  GLint value = 0;
  glGetActiveUniformBlockiv(program, 0, GL_UNIFORM_BLOCK_NAME_LENGTH, &value);
  EXPECT_EQ(value, 7);
  glGetActiveUniformBlockiv(program, 0, GL_UNIFORM_BLOCK_DATA_SIZE, &value);
  EXPECT_EQ(value, 80);
  glGetActiveUniformBlockiv(program, 0, GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS,
                            &value);
  EXPECT_EQ(value, 4);
  glGetActiveUniformBlockiv(
      program, 0, GL_UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER, &value);
  EXPECT_EQ(value, GL_FALSE);
  glGetActiveUniformBlockiv(program, 0,
                            GL_UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER,
                            &value);
  EXPECT_EQ(value, GL_TRUE);

  const char* names[4] = {"a", "b", "c", "d"};
  GLuint indices[4] = {};
  glGetUniformIndices(program, 4, names, indices);
  EXPECT_EQ(indices[0], 0);
  EXPECT_EQ(indices[1], 1);
  EXPECT_EQ(indices[2], 2);
  EXPECT_EQ(indices[3], 3);
  GLint offsets[4] = {};
  glGetActiveUniformsiv(program, 4, indices, GL_UNIFORM_OFFSET, offsets);
  EXPECT_EQ(offsets[0], 0);
  EXPECT_EQ(offsets[1], 16);
  EXPECT_EQ(offsets[2], 32);
  EXPECT_EQ(offsets[3], 48);
  GLint blocks[4] = {};
  glGetActiveUniformsiv(program, 4, indices, GL_UNIFORM_BLOCK_INDEX, blocks);
  EXPECT_EQ(blocks[0], 0);
  EXPECT_EQ(blocks[3], 0);
  GLint strides[4] = {};
  glGetActiveUniformsiv(program, 4, indices, GL_UNIFORM_MATRIX_STRIDE, strides);
  EXPECT_EQ(strides[0], 0);
  EXPECT_EQ(strides[3], 16);

  glDeleteProgram(program);
  glanceasyDestroyContext();
}

TEST(UniformBlockBindingPointsAndSharedStages) {
  EXPECT_EQ(glanceasyCreateContext(64, 64), 1);
  const char* vertex =
      "#version 300 es\n"
      "in vec2 a_pos;\n"
      "layout(std140) uniform Transform { vec4 color; vec4 offset; } xf;\n"
      "void main() { gl_Position = vec4(a_pos + xf.offset.xy, 0.0, 1.0); }\n";
  const char* fragment =
      "#version 300 es\n"
      "precision mediump float;\n"
      "layout(std140) uniform Transform { vec4 color; vec4 offset; } xf;\n"
      "out vec4 frag_color;\n"
      "void main() { frag_color = xf.color; }\n";
  const GLuint program = LinkProgram(vertex, fragment);
  EXPECT_TRUE(Linked(program));
  glUseProgram(program);

  EXPECT_EQ(glGetUniformBlockIndex(program, "Transform"), 0);
  glUniformBlockBinding(program, 0, 2);
  GLint binding = 0;
  glGetActiveUniformBlockiv(program, 0, GL_UNIFORM_BLOCK_BINDING, &binding);
  EXPECT_EQ(binding, 2);
  GLint value = 0;
  glGetActiveUniformBlockiv(program, 0, GL_UNIFORM_BLOCK_DATA_SIZE, &value);
  EXPECT_EQ(value, 32);

  const float data[8] = {0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  const GLuint buffer = MakeUniformBuffer(data, sizeof(data));
  glBindBufferBase(GL_UNIFORM_BUFFER, 2, buffer);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  unsigned char rgba[4] = {};
  DrawAndRead(rgba);
  EXPECT_EQ(rgba[0], 0);
  EXPECT_EQ(rgba[1], 255);

  glDeleteBuffers(1, &buffer);
  glDeleteProgram(program);
  glanceasyDestroyContext();
}

TEST(UniformBlockRangeBindingSkipsThePrefix) {
  EXPECT_EQ(glanceasyCreateContext(64, 64), 1);
  const char* fragment =
      "#version 300 es\n"
      "precision mediump float;\n"
      "layout(std140) uniform Tint { vec4 color; } tint;\n"
      "out vec4 frag_color;\n"
      "void main() { frag_color = tint.color; }\n";
  const GLuint program = LinkProgram(kVertex, fragment);
  EXPECT_TRUE(Linked(program));
  glUseProgram(program);

  const float data[8] = {0.0f, 0.0f, 0.0f, 1.0f,
                         1.0f, 0.0f, 0.0f, 1.0f};
  const GLuint buffer = MakeUniformBuffer(data, sizeof(data));
  glBindBufferRange(GL_UNIFORM_BUFFER, 0, buffer, 16, 16);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  unsigned char rgba[4] = {};
  DrawAndRead(rgba);
  EXPECT_EQ(rgba[0], 255);
  EXPECT_EQ(rgba[1], 0);

  glDeleteBuffers(1, &buffer);
  glDeleteProgram(program);
  glanceasyDestroyContext();
}

TEST(UniformBlockLayoutMismatchFailsToLink) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  const char* vertex =
      "#version 300 es\n"
      "in vec2 a_pos;\n"
      "layout(std140) uniform Transform { vec4 color; } xf;\n"
      "void main() { gl_Position = vec4(a_pos + xf.color.xy, 0.0, 1.0); }\n";
  const char* fragment =
      "#version 300 es\n"
      "precision mediump float;\n"
      "layout(std140) uniform Transform { vec2 color; } xf;\n"
      "out vec4 frag_color;\n"
      "void main() { frag_color = vec4(xf.color, 0.0, 1.0); }\n";
  const GLuint program = LinkProgram(vertex, fragment);
  EXPECT_FALSE(Linked(program));
  glDeleteProgram(program);
  glanceasyDestroyContext();
}
