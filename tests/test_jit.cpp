// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>

#include "GLES3/gl32.h"
#include "tests/glanceasy_test.h"
#include "glanceasy/glanceasy.h"

#if defined(GLANCEASY_ENABLE_JIT)

namespace {

bool JitDisabled() {
  const char* value = getenv("GLANCEASY_DISABLE_JIT");
  return value && *value && strcmp(value, "0") != 0;
}

const char* kVertex =
    "#version 100\n"
    "attribute vec2 a_pos;\n"
    "void main() {\n"
    "  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

const float kTriangle[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};

GLuint Compile(GLenum type, const char* source) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  return shader;
}

GLuint BuildProgram(const char* vertex, const char* fragment) {
  const GLuint vs = Compile(GL_VERTEX_SHADER, vertex);
  const GLuint fs = Compile(GL_FRAGMENT_SHADER, fragment);
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

}  // namespace

TEST(SljitCompilesAndDispatchesArithmeticShader) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  glanceasyResetJitStats();

  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "void main() {\n"
      "  float a = 0.5;\n"
      "  vec3 v = vec3(0.1, 0.2, 0.3);\n"
      "  vec3 w = v.zyx * 2.0 + vec3(a, 0.0, -a);\n"
      "  gl_FragColor = vec4(w.x, w.y, w.z, 1.0);\n"
      "}\n";
  const GLuint program = BuildProgram(kVertex, fragment);
  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  EXPECT_TRUE(linked == GL_TRUE);

  GlanceasyJitStats stats = {};
  if (!JitDisabled()) {
    glanceasyGetJitStats(&stats);
    EXPECT_TRUE(stats.stages_compiled >= 2);
    EXPECT_EQ(stats.fallback_stages, 0);
  }

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  DrawFullscreen(program);

  unsigned char rgba[4] = {};
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  EXPECT_EQ(rgba[0], 255);
  EXPECT_NEAR(static_cast<float>(rgba[1]) / 255.0f, 0.4f, 0.02f);
  EXPECT_EQ(rgba[2], 0);

  if (!JitDisabled()) {
    glanceasyGetJitStats(&stats);
    EXPECT_TRUE(stats.dispatches > 0);
  }
  glDeleteProgram(program);
  glanceasyDestroyContext();
}

TEST(SljitFallsBackForLoopingShader) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  glanceasyResetJitStats();

  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "void main() {\n"
      "  float total = 0.0;\n"
      "  for (int i = 0; i < 4; ++i) total += 0.25;\n"
      "  gl_FragColor = vec4(total, 0.0, 0.0, 1.0);\n"
      "}\n";
  const GLuint program = BuildProgram(kVertex, fragment);
  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  EXPECT_TRUE(linked == GL_TRUE);

  GlanceasyJitStats stats = {};
  glanceasyGetJitStats(&stats);
  if (JitDisabled()) {
    EXPECT_EQ(stats.stages_compiled, 0);
    EXPECT_TRUE(stats.fallback_stages >= 2);
  } else {
    EXPECT_TRUE(stats.fallback_stages >= 1);
    EXPECT_TRUE(stats.stages_compiled >= 1);
  }

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  DrawFullscreen(program);
  unsigned char rgba[4] = {};
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  EXPECT_NEAR(static_cast<float>(rgba[0]) / 255.0f, 1.0f, 0.02f);
  glDeleteProgram(program);
  glanceasyDestroyContext();
}

#endif  // GLANCEASY_ENABLE_JIT

TEST(SljitCompilesMultipleFragmentOutputs) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  glanceasyResetJitStats();

  const char* vertex =
      "#version 300 es\n"
      "in vec2 a_pos;\n"
      "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
  const char* fragment =
      "#version 300 es\n"
      "precision mediump float;\n"
      "layout(location = 0) out vec4 colour0;\n"
      "layout(location = 1) out vec4 colour1;\n"
      "void main() {\n"
      "  colour0 = vec4(1.0, 0.0, 0.0, 1.0);\n"
      "  colour1 = vec4(0.0, 0.0, 1.0, 1.0);\n"
      "}\n";
  const GLuint program = BuildProgram(vertex, fragment);
  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  EXPECT_EQ(linked, GL_TRUE);
  EXPECT_EQ(glGetFragDataLocation(program, "colour0"), 0);
  EXPECT_EQ(glGetFragDataLocation(program, "colour1"), 1);

  GlanceasyJitStats stats = {};
  if (!JitDisabled()) {
    glanceasyGetJitStats(&stats);
    EXPECT_TRUE(stats.stages_compiled >= 2);
    EXPECT_EQ(stats.fallback_stages, 0);
  }

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  DrawFullscreen(program);
  unsigned char rgba[4] = {};
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  EXPECT_EQ(rgba[0], 255);
  EXPECT_EQ(rgba[1], 0);
  EXPECT_EQ(rgba[2], 0);

  if (!JitDisabled()) {
    glanceasyGetJitStats(&stats);
    EXPECT_TRUE(stats.dispatches > 0);
  }
  glDeleteProgram(program);
  glanceasyDestroyContext();
}
