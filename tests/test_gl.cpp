// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include "GLES3/gl32.h"
#include "src/glsl/glsl_parser.h"
#include "src/glsl/glsl_program.h"
#include "src/zgl_common.h"
#include "src/zgl_raster.h"
#include "tests/zgl_test.h"
#include "zgl/zgl.h"

namespace {

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

GLuint LinkProgram(const char* vertex_source, const char* fragment_source) {
  const GLuint vertex = CompileShader(GL_VERTEX_SHADER, vertex_source);
  const GLuint fragment = CompileShader(GL_FRAGMENT_SHADER, fragment_source);
  const GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
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

const char* kFullscreenVertex =
    "#version 100\n"
    "attribute vec2 a_position;\n"
    "attribute vec2 a_uv;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "  v_uv = a_uv;\n"
    "  gl_Position = vec4(a_position, 0.0, 1.0);\n"
    "}\n";

const char* kSolidFragment =
    "#version 100\n"
    "void main() { gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n";

// Two triangles that cover the whole clip volume.
const float kFullscreenQuad[] = {
    -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f, 1.0f,
    1.0f,  -1.0f, 1.0f, 0.0f, 1.0f, 1.0f,  1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f,
};

}  // namespace

TEST(RasterizerCoversTriangle) {
  class CountingSink : public zgl::FragmentSink {
   public:
    void Shade(const zgl::Fragment& fragment) override {
      (void)fragment;
      count++;
    }
    int count = 0;
  } sink;

  zgl::Rasterizer rasterizer;
  rasterizer.SetClip(zgl::Rect{0, 0, 64, 48});
  zgl::RasterVertex a{0.0f, 48.0f, 0.0f, 1.0f};
  zgl::RasterVertex b{128.0f, 48.0f, 0.0f, 1.0f};
  zgl::RasterVertex c{0.0f, -48.0f, 0.0f, 1.0f};
  rasterizer.DrawTriangle(a, b, c, 0, 1, 2, &sink);
  EXPECT_TRUE(sink.count > 3000);
}

TEST(ContextAndClear) {
  EXPECT_EQ(zglCreateContext(64, 48), 1);
  EXPECT_EQ(zglGetWidth(), 64);
  EXPECT_EQ(zglGetHeight(), 48);
  glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  unsigned char rgba[4] = {};
  ReadPixel(32, 24, rgba);
  EXPECT_EQ(rgba[0], 0);
  EXPECT_EQ(rgba[1], 0);
  EXPECT_EQ(rgba[2], 255);
  EXPECT_EQ(rgba[3], 255);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  zglDestroyContext();
  EXPECT_EQ(zglContextReady(), 0);
}

TEST(DrawFullscreenTriangle) {
  EXPECT_EQ(zglCreateContext(64, 48), 1);
  glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  const GLuint program = LinkProgram(kFullscreenVertex, kSolidFragment);
  glUseProgram(program);
  const GLint position = glGetAttribLocation(program, "a_position");
  EXPECT_EQ(position, 0);

  const float vertices[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
  glVertexAttribPointer(position, 2, GL_FLOAT, GL_FALSE, 0, vertices);
  glEnableVertexAttribArray(position);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  unsigned char rgba[4] = {};
  ReadPixel(1, 1, rgba);
  EXPECT_EQ(rgba[0], 255);
  EXPECT_EQ(rgba[1], 0);
  EXPECT_EQ(rgba[2], 0);

  ReadPixel(62, 46, rgba);
  EXPECT_EQ(rgba[0], 255);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  ZglStats stats = {};
  zglGetStats(&stats);
  EXPECT_EQ(stats.draw_calls, 1);

  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(FragmentDepthDecidesVisibility) {
  EXPECT_EQ(zglCreateContext(64, 48), 1);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClearDepthf(1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);

  const char* vertex_source =
      "#version 100\n"
      "attribute vec2 a_position;\n"
      "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";
  const char* fragment_source =
      "#version 100\n"
      "uniform float u_depth;\n"
      "void main() {\n"
      "  gl_FragDepth = u_depth;\n"
      "  gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);\n"
      "}\n";
  const GLuint program = LinkProgram(vertex_source, fragment_source);
  glUseProgram(program);
  const GLint position = glGetAttribLocation(program, "a_position");
  const GLint depth_location = glGetUniformLocation(program, "u_depth");

  const float far_triangle[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
  const float near_triangle[] = {-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f};

  glVertexAttribPointer(position, 2, GL_FLOAT, GL_FALSE, 0, far_triangle);
  glEnableVertexAttribArray(position);
  glUniform1f(depth_location, 0.9f);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  glVertexAttribPointer(position, 2, GL_FLOAT, GL_FALSE, 0, near_triangle);
  glUniform1f(depth_location, 0.1f);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  unsigned char rgba[4] = {};
  ReadPixel(10, 10, rgba);
  EXPECT_EQ(rgba[1], 255);

  // The far geometry must not overwrite the near fragment.
  glUniform1f(depth_location, 0.95f);
  const float other_triangle[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
  glVertexAttribPointer(position, 2, GL_FLOAT, GL_FALSE, 0, other_triangle);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  ReadPixel(10, 10, rgba);
  EXPECT_EQ(rgba[1], 255);

  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(PointSpriteUsesPointSizeAndCoord) {
  EXPECT_EQ(zglCreateContext(64, 48), 1);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  const char* vertex_source =
      "#version 100\n"
      "attribute vec2 a_position;\n"
      "void main() {\n"
      "  gl_Position = vec4(a_position, 0.0, 1.0);\n"
      "  gl_PointSize = 16.0;\n"
      "}\n";
  const char* fragment_source =
      "#version 100\n"
      "void main() {\n"
      "  if (gl_PointCoord.x < 0.25) discard;\n"
      "  gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);\n"
      "}\n";
  const GLuint program = LinkProgram(vertex_source, fragment_source);
  glUseProgram(program);
  const GLint position = glGetAttribLocation(program, "a_position");
  const float point[] = {0.0f, 0.0f};
  glVertexAttribPointer(position, 2, GL_FLOAT, GL_FALSE, 0, point);
  glEnableVertexAttribArray(position);
  glDrawArrays(GL_POINTS, 0, 1);

  unsigned char rgba[4] = {};
  ReadPixel(32, 24, rgba);  // Centre of the 16x16 sprite.
  EXPECT_EQ(rgba[1], 255);
  EXPECT_EQ(rgba[0], 0);
  // gl_PointCoord.x < 0.25 covers the left quarter of the sprite, which the
  // fragment shader discards.
  ReadPixel(25, 24, rgba);
  EXPECT_EQ(rgba[1], 0);
  ReadPixel(38, 24, rgba);
  EXPECT_EQ(rgba[1], 255);
  ReadPixel(10, 10, rgba);  // Outside the sprite.
  EXPECT_EQ(rgba[1], 0);

  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(FramebufferObjectWithRenderbuffer) {
  EXPECT_EQ(zglCreateContext(64, 48), 1);
  GLuint framebuffer = 0;
  GLuint renderbuffer = 0;
  glGenFramebuffers(1, &framebuffer);
  glGenRenderbuffers(1, &renderbuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 32, 32);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_RENDERBUFFER, renderbuffer);
  EXPECT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);

  GLint binding = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &binding);
  EXPECT_EQ(binding, framebuffer);

  glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  unsigned char rgba[4] = {};
  ReadPixel(16, 16, rgba);
  EXPECT_EQ(rgba[0], 255);
  EXPECT_EQ(rgba[2], 255);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &framebuffer);
  glDeleteRenderbuffers(1, &renderbuffer);
  zglDestroyContext();
}

TEST(TextureSamplingUsesNearestNeighbour) {
  EXPECT_EQ(zglCreateContext(64, 48), 1);
  const unsigned char texels[16] = {255, 0, 0,   255, 0,   255, 0,   255,
                                    0,   0, 255, 255, 255, 255, 255, 255};
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               texels);

  const char* fragment_source =
      "#version 100\n"
      "precision mediump float;\n"
      "varying vec2 v_uv;\n"
      "uniform sampler2D u_texture;\n"
      "void main() { gl_FragColor = texture2D(u_texture, v_uv); }\n";
  const GLuint program = LinkProgram(kFullscreenVertex, fragment_source);
  glUseProgram(program);
  glUniform1i(glGetUniformLocation(program, "u_texture"), 0);
  const GLint position = glGetAttribLocation(program, "a_position");
  const GLint uv = glGetAttribLocation(program, "a_uv");
  glVertexAttribPointer(position, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        kFullscreenQuad);
  glVertexAttribPointer(uv, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        kFullscreenQuad + 2);
  glEnableVertexAttribArray(position);
  glEnableVertexAttribArray(uv);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glDrawArrays(GL_TRIANGLES, 0, 6);

  unsigned char rgba[4] = {};
  ReadPixel(16, 36, rgba);  // Lower left: first texel (red).
  EXPECT_EQ(rgba[0], 255);
  EXPECT_EQ(rgba[1], 0);
  ReadPixel(48, 12, rgba);  // Upper right: last texel (white).
  EXPECT_EQ(rgba[0], 255);
  EXPECT_EQ(rgba[1], 255);
  EXPECT_EQ(rgba[2], 255);

  glDeleteTextures(1, &texture);
  glDeleteProgram(program);
  zglDestroyContext();
}

// Regression test for a type predicate bug: every vector type used to report
// is_scalar() == true, so constructors such as vec4(x, y, 0.0, 1.0) only
// produced their first component and nothing was rasterised.
TEST(InterpreterWritesAllPositionComponents) {
  using namespace zgl::glsl;
  const char* vertex_source =
      "attribute vec2 a_position;\n"
      "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";
  const char* fragment_source = "void main() { gl_FragColor = vec4(1.0); }\n";

  Parser vertex_parser(Stage::kVertex, std::string(vertex_source));
  std::unique_ptr<ShaderAst> vertex_ast = vertex_parser.Parse();
  EXPECT_TRUE(vertex_ast != nullptr);
  Parser fragment_parser(Stage::kFragment, std::string(fragment_source));
  std::unique_ptr<ShaderAst> fragment_ast = fragment_parser.Parse();
  EXPECT_TRUE(fragment_ast != nullptr);

  Program program;
  std::string log;
  EXPECT_TRUE(program.Link(vertex_ast.get(), fragment_ast.get(), {}, {}, &log));

  float attributes[zgl::kMaxVertexAttribs * 4] = {-1.0f, -1.0f};
  VertexInput input;
  input.attributes = attributes;
  float position[4] = {};
  program.RunVertex(input, position, nullptr, nullptr);
  EXPECT_NEAR(position[0], -1.0f, 1e-6);
  EXPECT_NEAR(position[1], -1.0f, 1e-6);
  EXPECT_NEAR(position[2], 0.0f, 1e-6);
  EXPECT_NEAR(position[3], 1.0f, 1e-6);
  EXPECT_FALSE(program.has_runtime_error());

  const char* literal_source =
      "void main() { gl_Position = vec4(1.0, 2.0, 3.0, 4.0); }\n";
  Parser literal_parser(Stage::kVertex, std::string(literal_source));
  std::unique_ptr<ShaderAst> literal_ast = literal_parser.Parse();
  EXPECT_TRUE(literal_ast != nullptr);
  Program literal_program;
  EXPECT_TRUE(
      literal_program.Link(literal_ast.get(), fragment_ast.get(), {}, {}, &log));
  float literal_position[4] = {};
  literal_program.RunVertex(input, literal_position, nullptr, nullptr);
  EXPECT_NEAR(literal_position[0], 1.0f, 1e-6);
  EXPECT_NEAR(literal_position[1], 2.0f, 1e-6);
  EXPECT_NEAR(literal_position[2], 3.0f, 1e-6);
  EXPECT_NEAR(literal_position[3], 4.0f, 1e-6);
}
