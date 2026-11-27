// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include "GLES3/gl32.h"
#include "tests/glanceasy_test.h"
#include "glanceasy/glanceasy.h"

namespace {

GLuint MakeShader(GLenum type, const char* source) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  return shader;
}

GLuint MakeProgram(const char* vertex_source, const char* fragment_source) {
  const GLuint vertex = MakeShader(GL_VERTEX_SHADER, vertex_source);
  const GLuint fragment = MakeShader(GL_FRAGMENT_SHADER, fragment_source);
  const GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  GLint linked = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (!linked) {
    char log[512] = {};
    glGetProgramInfoLog(program, sizeof(log), nullptr, log);
    fprintf(stderr, "link failed: %s\n", log);
  }
  return program;
}

// Builds a vertex array object holding one triangle with the given horizontal
// extents and the given program's aPos attribute.
GLuint MakeTriangleVao(GLuint program, float x0, float x1) {
  GLuint vao = 0;
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  GLuint buffer = 0;
  glGenBuffers(1, &buffer);
  glBindBuffer(GL_ARRAY_BUFFER, buffer);
  const float data[9] = {x0,   -0.7f, 0.0f, x1, -0.7f, 0.0f, (x0 + x1) * 0.5f,
                         0.7f, 0.0f};
  glBufferData(GL_ARRAY_BUFFER, sizeof(data), data, GL_STATIC_DRAW);
  const GLint location = glGetAttribLocation(program, "aPos");
  glEnableVertexAttribArray(static_cast<GLuint>(location));
  glVertexAttribPointer(static_cast<GLuint>(location), 3, GL_FLOAT, GL_FALSE, 0,
                        nullptr);
  return vao;
}

void ReadPixel(int x, int y, unsigned char rgba[4]) {
  glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

}  // namespace

// End-to-end scenario inherited from the original Glanceasy smoke test: vertex
// array object switching, matrix uniforms, a texture-backed framebuffer with a
// renderbuffer depth attachment, gl_FragDepth late depth testing and point
// sprites.
TEST(SmokeVertexArraysFramebuffersAndDepth) {
  EXPECT_EQ(glanceasyCreateContext(160, 120), 1);

  const char* vertex_source =
      "attribute vec3 aPos;\n"
      "uniform mat4 uMVP;\n"
      "void main() { gl_Position = uMVP * vec4(aPos, 1.0); }\n";
  const char* fragment_source =
      "precision mediump float;\n"
      "uniform vec4 uColor;\n"
      "void main() { gl_FragColor = uColor; }\n";
  const GLuint program = MakeProgram(vertex_source, fragment_source);
  glUseProgram(program);
  const GLint color = glGetUniformLocation(program, "uColor");
  const GLint mvp = glGetUniformLocation(program, "uMVP");
  EXPECT_TRUE(color >= 0);
  EXPECT_TRUE(mvp >= 0);
  const float identity[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                              0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  glUniformMatrix4fv(mvp, 1, GL_FALSE, identity);

  const GLuint left = MakeTriangleVao(program, -0.85f, -0.2f);
  const GLuint right = MakeTriangleVao(program, 0.2f, 0.85f);

  glViewport(0, 0, 160, 120);
  glClearColor(0.05f, 0.06f, 0.09f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  glBindVertexArray(left);
  glUniform4f(color, 1.0f, 0.15f, 0.15f, 1.0f);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  glBindVertexArray(right);
  glUniform4f(color, 0.15f, 1.0f, 0.3f, 1.0f);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  glBindVertexArray(left);
  glUniform4f(color, 0.2f, 0.4f, 1.0f, 1.0f);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  unsigned char left_pixel[4] = {};
  unsigned char right_pixel[4] = {};
  ReadPixel(40, 60, left_pixel);
  ReadPixel(120, 60, right_pixel);
  // The last left-triangle draw wins, so the left region is blue and the right
  // region keeps its green fill.
  EXPECT_TRUE(left_pixel[2] > 150);
  EXPECT_TRUE(left_pixel[0] < 90);
  EXPECT_TRUE(right_pixel[1] > 150);
  EXPECT_TRUE(right_pixel[0] < 90);

  // A colour texture plus a depth renderbuffer form a complete framebuffer.
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               nullptr);
  GLuint framebuffer = 0;
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         texture, 0);
  GLuint renderbuffer = 0;
  glGenRenderbuffers(1, &renderbuffer);
  glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, 64, 64);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                            GL_RENDERBUFFER, renderbuffer);
  EXPECT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);

  GLint framebuffer_binding = 0;
  GLint renderbuffer_binding = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer_binding);
  glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer_binding);
  EXPECT_EQ(framebuffer_binding, static_cast<GLint>(framebuffer));
  EXPECT_EQ(renderbuffer_binding, static_cast<GLint>(renderbuffer));

  glViewport(0, 0, 64, 64);
  glClearColor(0.1f, 0.12f, 0.5f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glBindVertexArray(left);
  glUniform4f(color, 1.0f, 0.9f, 0.15f, 1.0f);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  unsigned char fbo_pixel[4] = {};
  ReadPixel(16, 32, fbo_pixel);
  EXPECT_TRUE(fbo_pixel[0] > 180);
  EXPECT_TRUE(fbo_pixel[1] > 160);
  EXPECT_TRUE(fbo_pixel[2] < 100);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, 160, 120);
  unsigned char default_pixel[4] = {};
  ReadPixel(40, 60, default_pixel);
  EXPECT_TRUE(default_pixel[2] > 150);

  // gl_FragDepth: the shader-written depth must drive the depth test even when
  // the interpolated z is the opposite order.
  const char* depth_vertex =
      "attribute vec3 aPos;\n"
      "uniform float uZ;\n"
      "void main() { gl_Position = vec4(aPos.xy, uZ, 1.0); }\n";
  const char* depth_fragment =
      "precision mediump float;\n"
      "uniform float uFD;\n"
      "uniform vec4 uColor;\n"
      "void main() { gl_FragDepth = uFD; gl_FragColor = uColor; }\n";
  const GLuint depth_program = MakeProgram(depth_vertex, depth_fragment);
  glUseProgram(depth_program);
  const GLint depth_z = glGetUniformLocation(depth_program, "uZ");
  const GLint depth_fd = glGetUniformLocation(depth_program, "uFD");
  const GLint depth_color = glGetUniformLocation(depth_program, "uColor");

  GLuint full = 0;
  glGenVertexArrays(1, &full);
  glBindVertexArray(full);
  {
    GLuint buffer = 0;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    const float triangle[9] = {-1.0f, -1.0f, 0.0f, 3.0f, -1.0f,
                               0.0f,  -1.0f, 3.0f, 0.0f};
    glBufferData(GL_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STATIC_DRAW);
    const GLint location = glGetAttribLocation(depth_program, "aPos");
    glEnableVertexAttribArray(static_cast<GLuint>(location));
    glVertexAttribPointer(static_cast<GLuint>(location), 3, GL_FLOAT, GL_FALSE,
                          0, nullptr);
  }

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glDepthMask(GL_TRUE);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // Pass 1 is geometrically far but writes a near depth value, so it must be
  // stored.
  glBindVertexArray(full);
  glUniform1f(depth_z, 0.9f);
  glUniform1f(depth_fd, 0.1f);
  glUniform4f(depth_color, 1.0f, 0.0f, 1.0f, 1.0f);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  // Pass 2 is geometrically near but writes a far depth value and must be
  // rejected.
  glUniform1f(depth_z, -0.9f);
  glUniform1f(depth_fd, 0.9f);
  glUniform4f(depth_color, 0.0f, 1.0f, 1.0f, 1.0f);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  unsigned char depth_pixel[4] = {};
  ReadPixel(80, 60, depth_pixel);
  EXPECT_TRUE(depth_pixel[0] > 150);
  EXPECT_TRUE(depth_pixel[1] < 90);
  EXPECT_TRUE(depth_pixel[2] > 150);
  glDisable(GL_DEPTH_TEST);

  // gl_PointSize and gl_PointCoord: the sprite is 16 pixels wide, red tracks
  // the horizontal coordinate and blue the vertical one.
  const char* point_vertex =
      "attribute vec3 aPos;\n"
      "uniform float uSize;\n"
      "void main() { gl_Position = vec4(aPos, 1.0); gl_PointSize = uSize; }\n";
  const char* point_fragment =
      "precision mediump float;\n"
      "void main() {\n"
      "  gl_FragColor = vec4(gl_PointCoord.x, 0.0, gl_PointCoord.y, 1.0);\n"
      "}\n";
  const GLuint point_program = MakeProgram(point_vertex, point_fragment);
  glUseProgram(point_program);
  const GLint point_size = glGetUniformLocation(point_program, "uSize");

  GLuint point_vao = 0;
  glGenVertexArrays(1, &point_vao);
  glBindVertexArray(point_vao);
  {
    GLuint buffer = 0;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    const float centre[3] = {0.0f, 0.0f, 0.0f};
    glBufferData(GL_ARRAY_BUFFER, sizeof(centre), centre, GL_STATIC_DRAW);
    const GLint location = glGetAttribLocation(point_program, "aPos");
    glEnableVertexAttribArray(static_cast<GLuint>(location));
    glVertexAttribPointer(static_cast<GLuint>(location), 3, GL_FLOAT, GL_FALSE,
                          0, nullptr);
  }

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glBindVertexArray(point_vao);
  glUniform1f(point_size, 16.0f);
  glDrawArrays(GL_POINTS, 0, 1);

  unsigned char centre_pixel[4] = {};
  unsigned char left_edge[4] = {};
  unsigned char outside_pixel[4] = {};
  unsigned char top_pixel[4] = {};
  unsigned char bottom_pixel[4] = {};
  ReadPixel(80, 60, centre_pixel);
  ReadPixel(73, 60, left_edge);
  ReadPixel(90, 60, outside_pixel);
  ReadPixel(80, 55, top_pixel);
  ReadPixel(80, 65, bottom_pixel);

  EXPECT_TRUE(centre_pixel[0] != 0 || centre_pixel[2] != 0);
  EXPECT_TRUE(left_edge[0] != 0 || left_edge[2] != 0);
  EXPECT_EQ(outside_pixel[0], 0);
  EXPECT_EQ(outside_pixel[2], 0);
  EXPECT_TRUE(top_pixel[2] != bottom_pixel[2]);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glDeleteProgram(point_program);
  glDeleteProgram(depth_program);
  glDeleteProgram(program);
  glDeleteVertexArrays(1, &point_vao);
  glDeleteVertexArrays(1, &full);
  glDeleteVertexArrays(1, &left);
  glDeleteVertexArrays(1, &right);
  glDeleteRenderbuffers(1, &renderbuffer);
  glDeleteFramebuffers(1, &framebuffer);
  glDeleteTextures(1, &texture);
  glanceasyDestroyContext();
}
