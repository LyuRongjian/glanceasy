// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <vector>

#include "GLES3/gl32.h"
#include "tests/glanceasy_test.h"
#include "glanceasy/glanceasy.h"

namespace {

const char* kPositionVertex =
    "#version 100\n"
    "attribute vec2 a_pos;\n"
    "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }\n";

const char* kSolidFragment =
    "#version 100\n"
    "precision mediump float;\n"
    "void main() { gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n";

const char* kInstancedVertex =
    "#version 100\n"
    "attribute vec2 a_pos;\n"
    "attribute float a_offset;\n"
    "attribute vec4 a_color;\n"
    "varying vec4 v_color;\n"
    "void main() {\n"
    "  v_color = a_color;\n"
    "  gl_Position = vec4(a_pos.x + a_offset, a_pos.y, 0.0, 1.0);\n"
    "}\n";

const char* kVaryingFragment =
    "#version 100\n"
    "precision mediump float;\n"
    "varying vec4 v_color;\n"
    "void main() { gl_FragColor = v_color; }\n";

GLuint CompileShader(GLenum type, const char* source) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint status = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (!status) {
    char log[512] = {};
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    fprintf(stderr, "shader compile failed: %s\n", log);
  }
  return shader;
}

GLuint LinkProgram(const char* vertex_source,
                   const char* fragment_source,
                   int color_location) {
  const GLuint vertex = CompileShader(GL_VERTEX_SHADER, vertex_source);
  const GLuint fragment = CompileShader(GL_FRAGMENT_SHADER, fragment_source);
  const GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glBindAttribLocation(program, 0, "a_pos");
  if (color_location >= 0)
    glBindAttribLocation(program, color_location, "a_color");
  if (color_location >= 0)
    glBindAttribLocation(program, 1, "a_offset");
  glLinkProgram(program);
  GLint status = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &status);
  if (!status) {
    char log[512] = {};
    glGetProgramInfoLog(program, sizeof(log), nullptr, log);
    fprintf(stderr, "link failed: %s\n", log);
  }
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  return program;
}

void ReadPixel(int x, int y, unsigned char rgba[4]) {
  glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

// Counts pixels whose red channel is above |threshold|.  Counting rather than
// probing individual rows keeps the line tests independent of the exact
// pixel-centre convention used by the rasteriser.
int CountRedPixels(int width, int height, int threshold) {
  std::vector<unsigned char> pixels(static_cast<size_t>(width * height * 4));
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  int count = 0;
  for (int i = 0; i < width * height; ++i) {
    if (static_cast<int>(pixels[static_cast<size_t>(i * 4)]) > threshold)
      ++count;
  }
  return count;
}

}  // namespace

TEST(TriangleStripCoversQuad) {
  EXPECT_EQ(glanceasyCreateContext(64, 48), 1);
  const GLuint program = LinkProgram(kPositionVertex, kSolidFragment, -1);
  glUseProgram(program);
  glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  const float strip[8] = {-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, strip);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  EXPECT_TRUE(CountRedPixels(64, 48, 200) > 3000);
  unsigned char rgba[4] = {};
  ReadPixel(32, 24, rgba);
  EXPECT_TRUE(rgba[0] > 200);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glDeleteProgram(program);
  glanceasyDestroyContext();
}

TEST(TriangleFanCoversQuad) {
  EXPECT_EQ(glanceasyCreateContext(64, 48), 1);
  const GLuint program = LinkProgram(kPositionVertex, kSolidFragment, -1);
  glUseProgram(program);
  glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glViewport(0, 0, 64, 48);

  // A convex quad fanned from its first corner.
  const float fan[8] = {-0.9f, -0.9f, 0.9f, -0.9f, 0.9f, 0.9f, -0.9f, 0.9f};
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, fan);
  glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

  EXPECT_TRUE(CountRedPixels(64, 48, 200) > 2000);
  glDeleteProgram(program);
  glanceasyDestroyContext();
}

TEST(LineTopologiesDrawPixels) {
  EXPECT_EQ(glanceasyCreateContext(64, 48), 1);
  const GLuint program = LinkProgram(kPositionVertex, kSolidFragment, -1);
  glUseProgram(program);
  glClearColor(0.0f, 0.0f, 1.0f, 1.0f);

  const float segments[8] = {-1.0f, 0.0f,  1.0f, 0.0f,   // Horizontal.
                             0.0f,  -1.0f, 0.0f, 1.0f};  // Vertical.
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, segments);

  glClear(GL_COLOR_BUFFER_BIT);
  glDrawArrays(GL_LINES, 0, 4);
  // The two segments cross in the middle; together they cover at least the
  // viewport width plus its height.
  const int lines_count = CountRedPixels(64, 48, 200);
  EXPECT_TRUE(lines_count > 90);

  glClear(GL_COLOR_BUFFER_BIT);
  const float strip[6] = {-0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f};
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, strip);
  glDrawArrays(GL_LINE_STRIP, 0, 3);
  const int strip_count = CountRedPixels(64, 48, 200);
  EXPECT_TRUE(strip_count > 40);

  glClear(GL_COLOR_BUFFER_BIT);
  glDrawArrays(GL_LINE_LOOP, 0, 3);
  const int loop_count = CountRedPixels(64, 48, 200);
  EXPECT_TRUE(loop_count > 60);

  glDeleteProgram(program);
  glanceasyDestroyContext();
}

TEST(PrimitiveRestartSplitsLineStrip) {
  EXPECT_EQ(glanceasyCreateContext(64, 48), 1);
  const GLuint program = LinkProgram(kPositionVertex, kSolidFragment, -1);
  glUseProgram(program);
  glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  const unsigned short indices[6] = {0, 1, 0xffff, 2, 3, 0xffff};
  const float positions[8] = {-0.5f, -0.5f, 0.5f, -0.5f,
                              -0.5f, 0.5f,  0.5f, 0.5f};
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, positions);
  glEnable(GL_PRIMITIVE_RESTART_FIXED_INDEX);
  glDrawElements(GL_LINE_STRIP, 6, GL_UNSIGNED_SHORT, indices);

  // Both horizontal segments are drawn, but the restart index must break the
  // strip so no diagonal connects them.
  unsigned char rgba[4] = {};
  ReadPixel(32, 12, rgba);
  EXPECT_TRUE(rgba[0] > 200);
  ReadPixel(32, 36, rgba);
  EXPECT_TRUE(rgba[0] > 200);
  ReadPixel(32, 24, rgba);
  EXPECT_EQ(rgba[0], 0);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glDisable(GL_PRIMITIVE_RESTART_FIXED_INDEX);
  glDeleteProgram(program);
  glanceasyDestroyContext();
}

TEST(InstancedDrawAdvancesDivisor) {
  EXPECT_EQ(glanceasyCreateContext(64, 48), 1);
  const GLuint program = LinkProgram(kInstancedVertex, kVaryingFragment, 2);
  glUseProgram(program);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  // A left-half quad shifted per instance: the first instance lands in the
  // left half, the second in the right half.
  const float positions[8] = {-1.0f, -1.0f, 0.0f, -1.0f,
                              -1.0f, 1.0f,  0.0f, 1.0f};
  const float offsets[2] = {-0.5f, 0.5f};
  const float colors[8] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f};

  GLuint buffers[3] = {};
  glGenBuffers(3, buffers);
  glEnableVertexAttribArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, buffers[0]);
  glBufferData(GL_ARRAY_BUFFER, sizeof(positions), positions, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

  glEnableVertexAttribArray(1);
  glBindBuffer(GL_ARRAY_BUFFER, buffers[1]);
  glBufferData(GL_ARRAY_BUFFER, sizeof(offsets), offsets, GL_STATIC_DRAW);
  glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 0, nullptr);
  glVertexAttribDivisor(1, 1);

  glEnableVertexAttribArray(2);
  glBindBuffer(GL_ARRAY_BUFFER, buffers[2]);
  glBufferData(GL_ARRAY_BUFFER, sizeof(colors), colors, GL_STATIC_DRAW);
  glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
  glVertexAttribDivisor(2, 1);

  GLint divisor = 0;
  glGetVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_DIVISOR, &divisor);
  EXPECT_EQ(divisor, 1);

  glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, 2);

  unsigned char left[4] = {};
  unsigned char right[4] = {};
  ReadPixel(8, 24, left);    // Only the first instance reaches this column.
  ReadPixel(40, 24, right);  // Only the second instance reaches this column.
  EXPECT_TRUE(left[0] > 200);
  EXPECT_TRUE(left[1] < 40);
  EXPECT_TRUE(right[1] > 200);
  EXPECT_TRUE(right[0] < 40);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glDeleteBuffers(3, buffers);
  glDeleteProgram(program);
  glanceasyDestroyContext();
}

TEST(IndexedDrawsAcceptEveryIndexType) {
  EXPECT_EQ(glanceasyCreateContext(64, 48), 1);
  const GLuint program = LinkProgram(kPositionVertex, kSolidFragment, -1);
  glUseProgram(program);
  glClearColor(0.0f, 0.0f, 1.0f, 1.0f);

  // A quad built from two triangles, slightly inset so that no edge lands
  // exactly on the viewport boundary.
  const float positions[8] = {-0.9f, -0.9f, 0.9f,  -0.9f,
                              0.9f,  0.9f,  -0.9f, 0.9f};
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, positions);

  GLuint element_buffer = 0;
  glGenBuffers(1, &element_buffer);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, element_buffer);

  const unsigned char ubyte_indices[6] = {0, 1, 2, 0, 2, 3};
  glClear(GL_COLOR_BUFFER_BIT);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(ubyte_indices), ubyte_indices,
               GL_STATIC_DRAW);
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, nullptr);
  EXPECT_TRUE(CountRedPixels(64, 48, 200) > 2000);

  const unsigned int uint_indices[6] = {0, 1, 2, 0, 2, 3};
  glClear(GL_COLOR_BUFFER_BIT);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint_indices), uint_indices,
               GL_STATIC_DRAW);
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
  EXPECT_TRUE(CountRedPixels(64, 48, 200) > 2000);

  const unsigned short short_indices[6] = {0, 1, 2, 0, 2, 3};
  glClear(GL_COLOR_BUFFER_BIT);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(short_indices), short_indices,
               GL_STATIC_DRAW);
  glDrawRangeElements(GL_TRIANGLES, 0, 3, 6, GL_UNSIGNED_SHORT, nullptr);
  EXPECT_TRUE(CountRedPixels(64, 48, 200) > 2000);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glDeleteBuffers(1, &element_buffer);
  glDeleteProgram(program);
  glanceasyDestroyContext();
}

TEST(ClientSideArraysDrawWithoutBuffer) {
  EXPECT_EQ(glanceasyCreateContext(64, 48), 1);
  const GLuint program = LinkProgram(kPositionVertex, kSolidFragment, -1);
  glUseProgram(program);
  glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  const float triangle[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, triangle);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  EXPECT_TRUE(CountRedPixels(64, 48, 200) > 3000);
  glDeleteProgram(program);
  glanceasyDestroyContext();
}
