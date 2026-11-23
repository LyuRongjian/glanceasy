// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <string.h>

#include "GLES3/gl32.h"
#include "tests/zgl_test.h"
#include "zgl/zgl.h"

namespace {

const float kTriangle[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};

const char* kSamplerVertex =
    "#version 100\n"
    "attribute vec2 a_pos;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "  v_uv = a_pos * 0.5 + vec2(0.5);\n"
    "  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

GLuint BuildProgram(const char* fragment) {
  const GLuint vs = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vs, 1, &kSamplerVertex, nullptr);
  glCompileShader(vs);
  const GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fs, 1, &fragment, nullptr);
  glCompileShader(fs);
  const GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  return program;
}

void DrawFullscreen() {
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, kTriangle);
  glDrawArrays(GL_TRIANGLES, 0, 3);
}

}  // namespace

TEST(CopyTexImageCapturesTheFramebuffer) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 8, 8, 0);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "uniform sampler2D u_texture;\n"
      "varying vec2 v_uv;\n"
      "void main() { gl_FragColor = texture2D(u_texture, v_uv); }\n";
  const GLuint program = BuildProgram(fragment);
  glUseProgram(program);
  glUniform1i(glGetUniformLocation(program, "u_texture"), 0);
  glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  DrawFullscreen();
  unsigned char rgba[4] = {};
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  EXPECT_TRUE(rgba[0] > 200);
  EXPECT_TRUE(rgba[2] < 40);
  glDeleteProgram(program);
  glDeleteTextures(1, &texture);
  zglDestroyContext();
}

TEST(CopyTexSubImageUpdatesARegion) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  const unsigned char black[4 * 4 * 4] = {};
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               black);
  glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 1, 1, 0, 0, 2, 2);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  // Read the modified texel back through glGetTexImage style sampling.
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "uniform sampler2D u_texture;\n"
      "varying vec2 v_uv;\n"
      "void main() { gl_FragColor = texture2D(u_texture, v_uv); }\n";
  const GLuint program = BuildProgram(fragment);
  glUseProgram(program);
  glUniform1i(glGetUniformLocation(program, "u_texture"), 0);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  DrawFullscreen();
  unsigned char rgba[4] = {};
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  EXPECT_EQ(rgba[1], 255);
  glDeleteProgram(program);
  glDeleteTextures(1, &texture);
  zglDestroyContext();
}

TEST(ThreeDimensionalStorageAcceptsSubUpdates) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_3D, texture);
  glTexStorage3D(GL_TEXTURE_3D, 1, GL_RGBA8, 4, 4, 2);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  unsigned char red[4 * 4 * 4] = {};
  for (unsigned int i = 0; i < sizeof(red); i += 4) {
    red[i] = 255;
    red[i + 3] = 255;
  }
  glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 1, 4, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                  red);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  GLint depth = 0;
  glGetTexLevelParameteriv(GL_TEXTURE_3D, 0, GL_TEXTURE_DEPTH, &depth);
  EXPECT_EQ(depth, 2);

  const char* fragment =
      "#version 300 es\n"
      "precision mediump float;\n"
      "uniform sampler3D u_texture;\n"
      "out vec4 o_colour;\n"
      "void main() { o_colour = texture(u_texture, vec3(0.5, 0.5, 0.6)); }\n";
  const GLuint program = BuildProgram(fragment);
  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (linked == GL_TRUE) {
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "u_texture"), 0);
    glClear(GL_COLOR_BUFFER_BIT);
    DrawFullscreen();
  }
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glDeleteProgram(program);
  glDeleteTextures(1, &texture);
  zglDestroyContext();
}

TEST(UnpackRowLengthAndSkipOffsetsAreApplied) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  // Two rows of four pixels; only the middle two pixels of the second row
  // should end up in the texture.
  unsigned char pixels[4 * 4 * 2] = {};
  for (unsigned int i = 0; i < sizeof(pixels); i += 4) {
    pixels[i] = 1;
    pixels[i + 3] = 255;
  }
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 4);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 2);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 1);
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               pixels);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glDeleteTextures(1, &texture);
  zglDestroyContext();
}

TEST(MipLevelsCanBeDefinedIndividually) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  unsigned char level0[4 * 4 * 4] = {};
  for (size_t i = 0; i < sizeof(level0); i += 4) {
    level0[i] = 255;
    level0[i + 3] = 255;
  }
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               level0);
  unsigned char level1[2 * 2 * 4] = {};
  for (unsigned int i = 0; i < sizeof(level1); i += 4) {
    level1[i + 1] = 255;
    level1[i + 3] = 255;
  }
  glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               level1);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  // glGenerateMipmap rebuilds the chain from level zero.
  glGenerateMipmap(GL_TEXTURE_2D);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glDeleteTextures(1, &texture);
  zglDestroyContext();
}

TEST(TextureStorageRejectsInvalidSizes) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexStorage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 8, 8);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 8, 8);
  // Updating an immutable texture through glTexImage2D is invalid.
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               nullptr);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  const unsigned char texel[4] = {1, 2, 3, 4};
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                  texel);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 8, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                  texel);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);

  // Compressed formats are not supported by the CPU backend.
  glTexImage2D(GL_TEXTURE_2D, 0, 0x83F0, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               nullptr);
  EXPECT_TRUE(glGetError() != GL_NO_ERROR);
  glDeleteTextures(1, &texture);
  zglDestroyContext();
}

TEST(DepthTextureAttachmentEnablesDepthTesting) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  GLuint colour = 0;
  GLuint depth = 0;
  glGenTextures(1, &colour);
  glGenTextures(1, &depth);
  glBindTexture(GL_TEXTURE_2D, colour);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               nullptr);
  glBindTexture(GL_TEXTURE_2D, depth);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT16, 16, 16, 0,
               GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, nullptr);
  GLuint framebuffer = 0;
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         colour, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                         depth, 0);
  EXPECT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);

  const char* vertex =
      "#version 100\n"
      "attribute vec3 a_pos;\n"
      "void main() { gl_Position = vec4(a_pos, 1.0); }\n";
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "void main() { gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n";
  const GLuint vs = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vs, 1, &vertex, nullptr);
  glCompileShader(vs);
  const GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fs, 1, &fragment, nullptr);
  glCompileShader(fs);
  const GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  glUseProgram(program);

  const float far_quad[9] = {-1.0f, -1.0f, 0.9f, 3.0f, -1.0f,
                             0.9f,  -1.0f, 3.0f, 0.9f};
  const float near_quad[9] = {-0.5f, -0.5f, 0.1f, 0.5f, -0.5f,
                              0.1f,  -0.5f, 0.5f, 0.1f};
  glEnableVertexAttribArray(0);
  glEnable(GL_DEPTH_TEST);
  glClearDepthf(1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, far_quad);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, near_quad);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  unsigned char rgba[4] = {};
  glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  EXPECT_EQ(rgba[0], 255);
  glDeleteProgram(program);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &framebuffer);
  glDeleteTextures(1, &depth);
  glDeleteTextures(1, &colour);
  zglDestroyContext();
}
