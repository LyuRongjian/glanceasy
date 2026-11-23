// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include "GLES3/gl32.h"
#include "tests/zgl_test.h"
#include "zgl/zgl.h"

namespace {

GLuint CompileShader(GLenum type, const char* source) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  return shader;
}

GLuint MakeSolidProgram(float r, float g, float b, float a) {
  char fragment[256];
  snprintf(fragment, sizeof(fragment),
           "#version 100\n"
           "precision mediump float;\n"
           "void main() { gl_FragColor = vec4(%.3f, %.3f, %.3f, %.3f); }\n",
           r, g, b, a);
  const char* vertex =
      "#version 100\n"
      "attribute vec2 a_pos;\n"
      "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
  const GLuint vs = CompileShader(GL_VERTEX_SHADER, vertex);
  const GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragment);
  const GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glBindAttribLocation(program, 0, "a_pos");
  glLinkProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  return program;
}

const float kTriangle[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};

void DrawFullscreenTriangle() {
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, kTriangle);
  glDrawArrays(GL_TRIANGLES, 0, 3);
}

void ReadPixel(int x, int y, unsigned char rgba[4]) {
  glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

GLuint MakeColourTexture(int width, int height, GLenum internal_format) {
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  GLenum format = GL_RGBA;
  GLenum type = GL_UNSIGNED_BYTE;
  if (internal_format == GL_DEPTH_COMPONENT16) {
    format = GL_DEPTH_COMPONENT;
    type = GL_UNSIGNED_SHORT;
  }
  glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, format,
               type, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  return texture;
}

}  // namespace

TEST(FramebufferCompletenessRules) {
  EXPECT_EQ(zglCreateContext(64, 48), 1);
  GLuint framebuffer = 0;
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  EXPECT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER),
            GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT);

  const GLuint colour = MakeColourTexture(32, 32, GL_RGBA);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         colour, 0);
  EXPECT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);

  // A depth attachment of a different size breaks the framebuffer.
  const GLuint depth_texture = MakeColourTexture(16, 16, GL_RGBA);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                         depth_texture, 0);
  EXPECT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER),
            GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS);

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0,
                         0);
  EXPECT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);

  GLint type = 0;
  GLint name = 0;
  GLint level = 0;
  glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,
                                        &type);
  glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
                                        &name);
  glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                        GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL,
                                        &level);
  EXPECT_EQ(type, GL_TEXTURE);
  EXPECT_EQ(name, static_cast<GLint>(colour));
  EXPECT_EQ(level, 0);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &framebuffer);
  glDeleteTextures(1, &depth_texture);
  glDeleteTextures(1, &colour);
  zglDestroyContext();
}

TEST(RenderbuffersBackDepthAndStencilAttachments) {
  EXPECT_EQ(zglCreateContext(64, 48), 1);
  GLuint renderbuffer = 0;
  glGenRenderbuffers(1, &renderbuffer);
  glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 32, 32);
  GLint value = 0;
  glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &value);
  EXPECT_EQ(value, 32);
  glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &value);
  EXPECT_EQ(value, 32);
  glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_INTERNAL_FORMAT,
                               &value);
  EXPECT_EQ(value, GL_DEPTH24_STENCIL8);
  glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES,
                               &value);
  EXPECT_EQ(value, 0);

  // Multisample storage is backed by four per-sample planes.
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, 16, 16);
  glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES,
                               &value);
  EXPECT_EQ(value, 4);
  glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &value);
  EXPECT_EQ(value, 16);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glBindRenderbuffer(GL_RENDERBUFFER, 0);
  glDeleteRenderbuffers(1, &renderbuffer);
  EXPECT_EQ(glIsRenderbuffer(renderbuffer), GL_FALSE);
  zglDestroyContext();
}

TEST(ClearBufferCommandsTargetAttachments) {
  EXPECT_EQ(zglCreateContext(64, 48), 1);
  const GLuint colour = MakeColourTexture(32, 32, GL_RGBA);
  GLuint framebuffer = 0;
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         colour, 0);
  EXPECT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);

  const GLfloat green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
  glClearBufferfv(GL_COLOR, 0, green);
  unsigned char rgba[4] = {};
  ReadPixel(16, 16, rgba);
  EXPECT_EQ(rgba[1], 255);

  const GLint red[4] = {255, 0, 0, 255};
  glClearBufferiv(GL_COLOR, 0, red);
  ReadPixel(16, 16, rgba);
  EXPECT_EQ(rgba[0], 255);
  EXPECT_EQ(rgba[1], 0);

  const GLuint blue[4] = {0u, 0u, 255u, 255u};
  glClearBufferuiv(GL_COLOR, 0, blue);
  ReadPixel(16, 16, rgba);
  EXPECT_EQ(rgba[2], 255);

  glClearBufferfi(GL_DEPTH_STENCIL, 0, 1.0f, 5);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  // Clearing an attachment that has no storage is an invalid operation.
  glClearBufferfv(GL_COLOR, 3, green);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &framebuffer);
  glDeleteTextures(1, &colour);
  zglDestroyContext();
}

TEST(FramebufferBlitCopiesColourAndDepth) {
  EXPECT_EQ(zglCreateContext(64, 48), 1);
  const float triangle[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
  const GLuint program = MakeSolidProgram(1.0f, 0.5f, 0.0f, 1.0f);
  glUseProgram(program);

  GLuint source = 0;
  GLuint destination = 0;
  glGenFramebuffers(1, &source);
  glGenFramebuffers(1, &destination);
  const GLuint source_colour = MakeColourTexture(32, 32, GL_RGBA);
  glBindFramebuffer(GL_FRAMEBUFFER, source);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         source_colour, 0);
  EXPECT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);
  glViewport(0, 0, 32, 32);
  glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  DrawFullscreenTriangle();

  // Destination framebuffer with its own colour texture.
  const GLuint destination_colour = MakeColourTexture(32, 32, GL_RGBA);
  glBindFramebuffer(GL_FRAMEBUFFER, destination);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         destination_colour, 0);
  EXPECT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  glBindFramebuffer(GL_READ_FRAMEBUFFER, source);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
  glBlitFramebuffer(0, 0, 32, 32, 0, 0, 32, 32, GL_COLOR_BUFFER_BIT,
                    GL_NEAREST);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glBindFramebuffer(GL_FRAMEBUFFER, destination);
  unsigned char rgba[4] = {};
  ReadPixel(16, 16, rgba);
  EXPECT_TRUE(rgba[0] > 240);
  EXPECT_TRUE(rgba[1] > 100);
  EXPECT_TRUE(rgba[2] < 20);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &destination);
  glDeleteFramebuffers(1, &source);
  glDeleteTextures(1, &destination_colour);
  glDeleteTextures(1, &source_colour);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(ReadPixelsSupportsEveryFormat) {
  EXPECT_EQ(zglCreateContext(64, 48), 1);
  glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  unsigned char rgba[4] = {};
  glReadPixels(10, 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  EXPECT_TRUE(rgba[0] > 50 && rgba[0] < 80);
  EXPECT_TRUE(rgba[1] > 110 && rgba[1] < 140);
  EXPECT_TRUE(rgba[2] > 180 && rgba[2] < 210);

  unsigned char rgb[3] = {};
  glReadPixels(10, 10, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, rgb);
  EXPECT_TRUE(rgb[2] > 180 && rgb[2] < 210);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  unsigned char alpha[1] = {};
  glReadPixels(10, 10, 1, 1, GL_ALPHA, GL_UNSIGNED_BYTE, alpha);
  EXPECT_EQ(alpha[0], 255);

  // A two by two readback returns the rows in bottom-up order.
  unsigned char block[16] = {};
  glReadPixels(0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, block);
  EXPECT_EQ(block[0], rgba[0]);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  // Floating point readback returns sixteen bytes per pixel.
  float floats[4] = {};
  glReadPixels(10, 10, 1, 1, GL_RGBA, GL_FLOAT, floats);
  EXPECT_TRUE(floats[0] > 0.2f && floats[0] < 0.3f);
  EXPECT_TRUE(floats[1] > 0.45f && floats[1] < 0.55f);

  // Invalid format and type combinations are rejected.
  glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_INT, rgba);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glReadPixels(0, 0, -1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  zglDestroyContext();
}

TEST(InvalidateAndDiscardAffectNoState) {
  EXPECT_EQ(zglCreateContext(64, 48), 1);
  const GLuint colour = MakeColourTexture(32, 32, GL_RGBA);
  GLuint framebuffer = 0;
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         colour, 0);
  const GLenum attachments[1] = {GL_COLOR_ATTACHMENT0};
  glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, attachments);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glInvalidateSubFramebuffer(GL_FRAMEBUFFER, 1, attachments, 0, 0, 32, 32);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  // Discarding a non-existent attachment is invalid.
  const GLenum missing[1] = {GL_COLOR_ATTACHMENT1};
  glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, missing);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &framebuffer);
  glDeleteTextures(1, &colour);
  zglDestroyContext();
}

TEST(DefaultFramebufferPixelsAreReadable) {
  EXPECT_EQ(zglCreateContext(64, 48), 1);
  const GLuint program = MakeSolidProgram(0.0f, 1.0f, 0.0f, 1.0f);
  glUseProgram(program);
  glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  DrawFullscreenTriangle();

  unsigned char rgba[4] = {};
  ReadPixel(32, 24, rgba);
  EXPECT_EQ(rgba[1], 255);
  EXPECT_EQ(rgba[2], 0);

  // The colour buffer is also exposed through the ZCat host API.
  int width = 0;
  int height = 0;
  const unsigned int* pixels = zglGetPixels32(&width, &height);
  EXPECT_TRUE(pixels != nullptr);
  EXPECT_EQ(width, 64);
  EXPECT_EQ(height, 48);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(MultisampleFramebufferResolvesCoverage) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  GLuint color_buffer = 0;
  GLuint depth_buffer = 0;
  GLuint framebuffer = 0;
  glGenRenderbuffers(1, &color_buffer);
  glBindRenderbuffer(GL_RENDERBUFFER, color_buffer);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, 32, 32);
  glGenRenderbuffers(1, &depth_buffer);
  glBindRenderbuffer(GL_RENDERBUFFER, depth_buffer);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH24_STENCIL8, 32,
                                    32);
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_RENDERBUFFER, color_buffer);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                            GL_RENDERBUFFER, depth_buffer);
  EXPECT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);

  GLint samples = 0;
  glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES,
                               &samples);
  EXPECT_EQ(samples, 4);

  glViewport(0, 0, 32, 32);
  glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // A right triangle covers the lower-left half of the target.  Pixels straddle
  // the diagonal, so the resolved image contains intermediate coverage values.
  const GLuint program = MakeSolidProgram(0.0f, 1.0f, 0.0f, 1.0f);
  glUseProgram(program);
  const float triangle[6] = {-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f};
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, triangle);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  unsigned char inside[4] = {};
  unsigned char outside[4] = {};
  unsigned char edge[4] = {};
  glReadPixels(4, 28, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, inside);
  glReadPixels(28, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, outside);
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, edge);
  EXPECT_EQ(inside[0], 0);
  EXPECT_EQ(inside[1], 255);
  EXPECT_EQ(outside[0], 255);
  EXPECT_EQ(outside[1], 0);
  // The centre pixel is split by the diagonal; resolving four samples leaves
  // both red and green present.
  EXPECT_TRUE(edge[0] > 0 && edge[0] < 255);
  EXPECT_TRUE(edge[1] > 0 && edge[1] < 255);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &framebuffer);
  glDeleteRenderbuffers(1, &color_buffer);
  glDeleteRenderbuffers(1, &depth_buffer);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(MultisampleShaderReadsSampleId) {
  EXPECT_EQ(zglCreateContext(8, 8), 1);
  GLuint color_buffer = 0;
  GLuint framebuffer = 0;
  glGenRenderbuffers(1, &color_buffer);
  glBindRenderbuffer(GL_RENDERBUFFER, color_buffer);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, 8, 8);
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_RENDERBUFFER, color_buffer);
  EXPECT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);
  glViewport(0, 0, 8, 8);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  const char* vertex =
      "#version 100\n"
      "attribute vec2 a_pos;\n"
      "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "void main() {\n"
      "  gl_FragColor = vec4(float(gl_SampleID) * 0.25, 0.0, 0.0, 1.0);\n"
      "}\n";
  const GLuint vs = CompileShader(GL_VERTEX_SHADER, vertex);
  const GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragment);
  const GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glBindAttribLocation(program, 0, "a_pos");
  glLinkProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  glUseProgram(program);
  const float triangle[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, triangle);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  unsigned char rgba[4] = {};
  glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  // Samples 0..3 average to 0.375 red.
  EXPECT_TRUE(rgba[0] > 88 && rgba[0] < 104);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &framebuffer);
  glDeleteRenderbuffers(1, &color_buffer);
  glDeleteProgram(program);
  zglDestroyContext();
}
