// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "GLES3/gl32.h"
#include "tests/glanceasy_test.h"
#include "glanceasy/glanceasy.h"

namespace {

enum : int { kUnknownEnumPlaceholder = 0x1234 };

const float kFullscreen[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};

const char* kVertex =
    "#version 100\n"
    "attribute vec2 a_pos;\n"
    "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
const char* kFragment =
    "#version 100\n"
    "precision mediump float;\n"
    "void main() { gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n";

GLuint MakeProgram() {
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
}

void DrawFullscreen() {
  glDrawArrays(GL_TRIANGLES, 0, 3);
}

bool CentreIsRed() {
  unsigned char rgba[4] = {};
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  return rgba[0] > 200 && rgba[2] < 40;
}

GLuint AttachColourTexture(GLuint width, GLuint height) {
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         texture, 0);
  return texture;
}

GLuint AttachDepthStencilRenderbuffer(GLuint width, GLuint height) {
  GLuint renderbuffer = 0;
  glGenRenderbuffers(1, &renderbuffer);
  glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                            GL_RENDERBUFFER, renderbuffer);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                            GL_RENDERBUFFER, renderbuffer);
  return renderbuffer;
}

}  // namespace

TEST(BlitCopiesDepthAndStencilPayloads) {
  EXPECT_EQ(glanceasyCreateContext(64, 64), 1);
  const GLuint program = MakeProgram();
  PrepareDraw(program);

  GLuint source = 0;
  GLuint destination = 0;
  glGenFramebuffers(1, &source);
  glGenFramebuffers(1, &destination);

  glBindFramebuffer(GL_FRAMEBUFFER, source);
  GLuint source_colour = AttachColourTexture(32, 32);
  GLuint source_depth = AttachDepthStencilRenderbuffer(32, 32);
  EXPECT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);
  const GLfloat far_depth[1] = {1.0f};
  glClearBufferfv(GL_DEPTH, 0, far_depth);
  const GLint source_stencil[1] = {3};
  glClearBufferiv(GL_STENCIL, 0, source_stencil);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glBindFramebuffer(GL_FRAMEBUFFER, destination);
  GLuint destination_colour = AttachColourTexture(32, 32);
  GLuint destination_depth = AttachDepthStencilRenderbuffer(32, 32);
  EXPECT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);
  const GLfloat near_depth[1] = {0.0f};
  glClearBufferfv(GL_DEPTH, 0, near_depth);
  const GLint near_stencil[1] = {0};
  glClearBufferiv(GL_STENCIL, 0, near_stencil);
  glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  glBindFramebuffer(GL_READ_FRAMEBUFFER, source);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
  glBlitFramebuffer(0, 0, 32, 32, 0, 0, 32, 32,
                    GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT, GL_NEAREST);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glBindFramebuffer(GL_FRAMEBUFFER, destination);
  glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  DrawFullscreen();
  EXPECT_TRUE(CentreIsRed());

  glDisable(GL_DEPTH_TEST);
  glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_STENCIL_TEST);
  glStencilFunc(GL_EQUAL, 3, 0xFF);
  glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
  DrawFullscreen();
  EXPECT_TRUE(CentreIsRed());

  glClear(GL_COLOR_BUFFER_BIT);
  glStencilFunc(GL_EQUAL, 0, 0xFF);
  DrawFullscreen();
  EXPECT_FALSE(CentreIsRed());
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glDisable(GL_STENCIL_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteRenderbuffers(1, &source_depth);
  glDeleteRenderbuffers(1, &destination_depth);
  glDeleteTextures(1, &source_colour);
  glDeleteTextures(1, &destination_colour);
  glDeleteFramebuffers(1, &destination);
  glDeleteFramebuffers(1, &source);
  glDeleteProgram(program);
  glanceasyDestroyContext();
}

TEST(ClearBufferCommandsValidateAttachmentNames) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  GLuint framebuffer = 0;
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  const GLuint colour = AttachColourTexture(16, 16);
  EXPECT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);

  const GLint stencil[1] = {1};
  glClearBufferiv(GL_STENCIL, 1, stencil);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glClearBufferiv(static_cast<GLenum>(0x1234), 0, stencil);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  const GLuint unsigned_value[4] = {0, 0, 0, 0};
  glClearBufferuiv(GL_DEPTH, 0, unsigned_value);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  const GLfloat float_value[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  glClearBufferfv(static_cast<GLenum>(0x1234), 0, float_value);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glClearBufferfi(GL_COLOR, 0, 1.0f, 0);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glClearBufferfi(GL_DEPTH_STENCIL, 1, 1.0f, 0);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);

  // A depth/stencil attachment that has no storage is rejected before the
  // clear can touch it.
  glClearBufferfi(GL_DEPTH_STENCIL, 0, 0.5f, 2);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glClearBufferfv(GL_DEPTH, 0, float_value);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteTextures(1, &colour);
  glDeleteFramebuffers(1, &framebuffer);
  glanceasyDestroyContext();
}

TEST(InvalidateValidatesAttachments) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  GLuint framebuffer = 0;
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  const GLuint colour = AttachColourTexture(16, 16);
  EXPECT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);

  const GLenum colour_attachment[1] = {GL_COLOR_ATTACHMENT0};
  glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, colour_attachment);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glInvalidateFramebuffer(kUnknownEnumPlaceholder, 1, colour_attachment);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glInvalidateFramebuffer(GL_FRAMEBUFFER, -1, colour_attachment);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  const GLenum unknown_attachment[1] = {static_cast<GLenum>(0x1234)};
  glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, unknown_attachment);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  const GLenum depth_attachment[1] = {GL_DEPTH_ATTACHMENT};
  glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, depth_attachment);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  const GLenum depth_stencil_attachment[1] = {GL_DEPTH_STENCIL_ATTACHMENT};
  glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, depth_stencil_attachment);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);

  glInvalidateSubFramebuffer(GL_FRAMEBUFFER, 1, colour_attachment, 0, 0, -1, 4);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glInvalidateSubFramebuffer(GL_FRAMEBUFFER, 1, colour_attachment, 0, 0, 4, 4);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteTextures(1, &colour);
  glDeleteFramebuffers(1, &framebuffer);
  glanceasyDestroyContext();
}

TEST(RenderbufferStorageValidation) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  GLuint renderbuffer = 0;
  glGenRenderbuffers(1, &renderbuffer);
  glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, 16, 16);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  GLint width = 0;
  glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &width);
  EXPECT_EQ(width, 16);
  glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &width);
  EXPECT_EQ(width, 16);
  GLint format = 0;
  glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_INTERNAL_FORMAT,
                               &format);
  EXPECT_EQ(format, GL_DEPTH_COMPONENT16);
  glRenderbufferStorage(GL_RENDERBUFFER, kUnknownEnumPlaceholder, 16, 16);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, -1, 16);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);

  GLint bound = 0;
  glGetIntegerv(GL_RENDERBUFFER_BINDING, &bound);
  EXPECT_EQ(bound, static_cast<GLint>(renderbuffer));
  glBindRenderbuffer(GL_RENDERBUFFER, 0);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glDeleteRenderbuffers(1, &renderbuffer);
  glanceasyDestroyContext();
}

TEST(FragmentShaderWritesTwoDrawBuffers) {
  EXPECT_EQ(glanceasyCreateContext(64, 64), 1);
  GLuint textures[2] = {};
  glGenTextures(2, textures);
  for (int i = 0; i < 2; ++i) {
    glBindTexture(GL_TEXTURE_2D, textures[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  }
  GLuint framebuffer = 0;
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         textures[0], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D,
                         textures[1], 0);
  EXPECT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);
  const GLenum buffers[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
  glDrawBuffers(2, buffers);
  glViewport(0, 0, 32, 32);

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
  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  EXPECT_EQ(linked, GL_TRUE);
  EXPECT_EQ(glGetFragDataLocation(program, "colour0"), 0);
  EXPECT_EQ(glGetFragDataLocation(program, "colour1"), 1);
  EXPECT_EQ(glGetFragDataLocation(program, "missing"), -1);

  glUseProgram(program);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, kFullscreen);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  unsigned char rgba[4] = {};
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  EXPECT_EQ(rgba[0], 255);
  EXPECT_EQ(rgba[1], 0);
  EXPECT_EQ(rgba[2], 0);
  glReadBuffer(GL_COLOR_ATTACHMENT1);
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  EXPECT_EQ(rgba[0], 0);
  EXPECT_EQ(rgba[1], 0);
  EXPECT_EQ(rgba[2], 255);

  glDeleteProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &framebuffer);
  glDeleteTextures(2, textures);
  glanceasyDestroyContext();
}


TEST(LegacyFragDataWritesTwoDrawBuffers) {
  EXPECT_EQ(glanceasyCreateContext(64, 64), 1);
  GLuint textures[2] = {};
  glGenTextures(2, textures);
  for (int i = 0; i < 2; ++i) {
    glBindTexture(GL_TEXTURE_2D, textures[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  }
  GLuint framebuffer = 0;
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         textures[0], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D,
                         textures[1], 0);
  const GLenum buffers[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
  glDrawBuffers(2, buffers);
  EXPECT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);
  glViewport(0, 0, 32, 32);

  const char* vertex =
      "#version 100\n"
      "attribute vec2 a_pos;\n"
      "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "void main() {\n"
      "  gl_FragData[0] = vec4(1.0, 0.0, 0.0, 1.0);\n"
      "  gl_FragData[1] = vec4(0.0, 1.0, 0.0, 1.0);\n"
      "}\n";
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
  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  EXPECT_EQ(linked, GL_TRUE);

  glUseProgram(program);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, kFullscreen);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  unsigned char rgba[4] = {};
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  EXPECT_EQ(rgba[0], 255);
  EXPECT_EQ(rgba[1], 0);
  glReadBuffer(GL_COLOR_ATTACHMENT1);
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  EXPECT_EQ(rgba[0], 0);
  EXPECT_EQ(rgba[1], 255);

  glDeleteProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &framebuffer);
  glDeleteTextures(2, textures);
  glanceasyDestroyContext();
}

TEST(DrawBufferTableMapsOutputOne) {
  EXPECT_EQ(glanceasyCreateContext(64, 64), 1);
  GLuint textures[2] = {};
  glGenTextures(2, textures);
  for (int i = 0; i < 2; ++i) {
    glBindTexture(GL_TEXTURE_2D, textures[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  }
  GLuint framebuffer = 0;
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         textures[0], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D,
                         textures[1], 0);
  const GLenum buffers[2] = {GL_NONE, GL_COLOR_ATTACHMENT1};
  glDrawBuffers(2, buffers);
  EXPECT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);
  glViewport(0, 0, 32, 32);

  const char* vertex =
      "#version 300 es\n"
      "in vec2 a_pos;\n"
      "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
  const char* fragment =
      "#version 300 es\n"
      "precision mediump float;\n"
      "layout(location = 1) out vec4 colour;\n"
      "void main() { colour = vec4(0.0, 1.0, 0.0, 1.0); }\n";
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
  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  EXPECT_EQ(linked, GL_TRUE);
  EXPECT_EQ(glGetFragDataLocation(program, "colour"), 1);

  glUseProgram(program);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, kFullscreen);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  unsigned char rgba[4] = {};
  glReadBuffer(GL_COLOR_ATTACHMENT1);
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  EXPECT_EQ(rgba[1], 255);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  EXPECT_EQ(rgba[0], 0);
  EXPECT_EQ(rgba[1], 0);

  glDeleteProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &framebuffer);
  glDeleteTextures(2, textures);
  glanceasyDestroyContext();
}
