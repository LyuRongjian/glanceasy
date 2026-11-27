// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include "GLES3/gl32.h"
#include "tests/glanceasy_test.h"
#include "glanceasy/glanceasy.h"

TEST(BufferObjectsStoreAndCopyData) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  GLuint buffers[2] = {};
  glGenBuffers(2, buffers);
  EXPECT_TRUE(glIsBuffer(buffers[0]) == GL_TRUE);

  const float values[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  glBindBuffer(GL_ARRAY_BUFFER, buffers[0]);
  glBufferData(GL_ARRAY_BUFFER, sizeof(values), values, GL_STATIC_DRAW);
  GLint size = 0;
  GLint usage = 0;
  glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &size);
  EXPECT_EQ(size, 32);
  glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_USAGE, &usage);
  EXPECT_EQ(usage, GL_STATIC_DRAW);

  const float patch[2] = {9, 10};
  glBufferSubData(GL_ARRAY_BUFFER, 8, sizeof(patch), patch);
  const float* mapped = static_cast<const float*>(
      glMapBufferRange(GL_ARRAY_BUFFER, 0, 16, GL_MAP_READ_BIT));
  EXPECT_TRUE(mapped != nullptr);
  EXPECT_NEAR(mapped[0], 1.0f, 0.001f);
  EXPECT_NEAR(mapped[2], 9.0f, 0.001f);
  EXPECT_EQ(static_cast<GLenum>(glUnmapBuffer(GL_ARRAY_BUFFER)), GL_TRUE);

  // Copy between two buffers and read the result back through a mapping.
  glBindBuffer(GL_COPY_READ_BUFFER, buffers[0]);
  glBindBuffer(GL_COPY_WRITE_BUFFER, buffers[1]);
  glBufferData(GL_COPY_WRITE_BUFFER, 32, nullptr, GL_DYNAMIC_COPY);
  glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, 16);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  const float* copied = static_cast<const float*>(
      glMapBufferRange(GL_COPY_WRITE_BUFFER, 0, 16, GL_MAP_READ_BIT));
  EXPECT_TRUE(copied != nullptr);
  // The source was patched to {1, 2, 9, 10} before the copy.
  EXPECT_NEAR(copied[3], 10.0f, 0.001f);
  glUnmapBuffer(GL_COPY_WRITE_BUFFER);

  glDeleteBuffers(2, buffers);
  EXPECT_EQ(glIsBuffer(buffers[0]), GL_FALSE);
  glanceasyDestroyContext();
}

TEST(BufferRangesBehaveLikeTheSpecification) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  GLuint buffer = 0;
  glGenBuffers(1, &buffer);
  glBindBuffer(GL_ARRAY_BUFFER, buffer);
  const unsigned char data[16] = {};
  glBufferData(GL_ARRAY_BUFFER, 16, data, GL_DYNAMIC_DRAW);
  unsigned char* mapped = static_cast<unsigned char*>(glMapBufferRange(
      GL_ARRAY_BUFFER, 4, 8, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT));
  EXPECT_TRUE(mapped != nullptr);
  mapped[0] = 0x7F;
  // Mapping twice without unmapping is an invalid operation.
  EXPECT_TRUE(glMapBufferRange(GL_ARRAY_BUFFER, 0, 4, GL_MAP_READ_BIT) ==
              nullptr);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  EXPECT_EQ(static_cast<GLenum>(glUnmapBuffer(GL_ARRAY_BUFFER)), GL_TRUE);
  glUnmapBuffer(GL_ARRAY_BUFFER);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);

  glBufferSubData(GL_ARRAY_BUFFER, 64, 4, data);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glBufferSubData(0x1234, 0, 4, data);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glBindBuffer(0x1234, 0);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  GLint64 size64 = 0;
  glGetBufferParameteri64v(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &size64);
  EXPECT_EQ(size64, 16);
  glDeleteBuffers(1, &buffer);
  glanceasyDestroyContext();
}

TEST(ObjectLifecycleForTexturesAndFramebuffers) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  GLuint textures[2] = {};
  glGenTextures(2, textures);
  EXPECT_EQ(glIsTexture(textures[0]), GL_TRUE);
  glBindTexture(GL_TEXTURE_2D, textures[0]);
  glBindTexture(GL_TEXTURE_2D, textures[1]);
  glDeleteTextures(1, textures);
  EXPECT_EQ(glIsTexture(textures[0]), GL_FALSE);
  glBindTexture(GL_TEXTURE_2D, textures[1]);
  glDeleteTextures(1, textures + 1);
  // Deleting the bound texture unbinds it.
  GLint binding = -1;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
  EXPECT_EQ(binding, 0);

  GLuint framebuffers[2] = {};
  glGenFramebuffers(2, framebuffers);
  EXPECT_EQ(glIsFramebuffer(framebuffers[0]), GL_TRUE);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[0]);
  glDeleteFramebuffers(2, framebuffers);
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &binding);
  EXPECT_EQ(binding, 0);

  GLuint renderbuffer = 0;
  glGenRenderbuffers(1, &renderbuffer);
  EXPECT_EQ(glIsRenderbuffer(renderbuffer), GL_TRUE);
  glDeleteRenderbuffers(1, &renderbuffer);
  EXPECT_EQ(glIsRenderbuffer(renderbuffer), GL_FALSE);

  GLsizei negative = -1;
  glGenTextures(negative, textures);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glGenBuffers(negative, nullptr);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glanceasyDestroyContext();
}

TEST(QueriesReportSampleCounts) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  GLuint query = 0;
  glGenQueries(1, &query);
  EXPECT_EQ(glIsQuery(query), GL_TRUE);
  const char* vertex =
      "#version 100\nattribute vec2 a_pos;\n"
      "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
  const char* fragment =
      "#version 100\nprecision mediump float;\n"
      "void main() { gl_FragColor = vec4(1.0); }\n";
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
  const float triangle[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, triangle);

  glBeginQuery(GL_ANY_SAMPLES_PASSED, query);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glEndQuery(GL_ANY_SAMPLES_PASSED);
  GLuint available = 0;
  GLuint passed = 0;
  glGetQueryObjectuiv(query, GL_QUERY_RESULT_AVAILABLE, &available);
  glGetQueryObjectuiv(query, GL_QUERY_RESULT, &passed);
  EXPECT_EQ(available, GL_TRUE);
  EXPECT_EQ(passed, GL_TRUE);

  glBeginQuery(GL_ANY_SAMPLES_PASSED, query);
  glBeginQuery(GL_ANY_SAMPLES_PASSED, query);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glEndQuery(GL_ANY_SAMPLES_PASSED);

  // 0x88BF is GL_TIME_ELAPSED, which this backend does not implement.
  glBeginQuery(static_cast<GLenum>(0x88BF), query);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glDeleteQueries(1, &query);
  glDeleteProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  glanceasyDestroyContext();
}

TEST(FencesStartSignalledAndCanBeWaited) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  GLsync sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  EXPECT_TRUE(sync != nullptr);
  EXPECT_EQ(glIsSync(sync), GL_TRUE);
  GLenum wait = glClientWaitSync(sync, 0, 0);
  EXPECT_TRUE(wait == GL_ALREADY_SIGNALED || wait == GL_CONDITION_SATISFIED);
  glWaitSync(sync, 0, GL_TIMEOUT_IGNORED);
  GLint status = 0;
  glGetSynciv(sync, GL_SYNC_STATUS, 1, nullptr, &status);
  EXPECT_EQ(status, GL_SIGNALED);
  glDeleteSync(sync);
  EXPECT_EQ(glIsSync(sync), GL_FALSE);
  glClientWaitSync(sync, 0, 0);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glanceasyDestroyContext();
}

TEST(MultipleColourAttachmentsResolveForReading) {
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
  const GLenum draw_buffers[1] = {GL_COLOR_ATTACHMENT1};
  glDrawBuffers(1, draw_buffers);
  glReadBuffer(GL_COLOR_ATTACHMENT1);
  glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  unsigned char rgba[4] = {};
  glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  EXPECT_EQ(rgba[1], 255);

  const GLenum bad_buffers[1] = {GL_COLOR_ATTACHMENT3};
  glDrawBuffers(1, bad_buffers);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glReadBuffer(0x1234);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &framebuffer);
  glDeleteTextures(2, textures);
  glanceasyDestroyContext();
}

TEST(ReadPixelsHonoursPackAlignmentAndRowLength) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  // A three pixel row with alignment one has no padding.
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glPixelStorei(GL_PACK_ROW_LENGTH, 4);
  // A row length of four reserves padding after the three read pixels.
  unsigned char rows[2][4 * 4] = {};
  glReadPixels(0, 0, 3, 2, GL_RGBA, GL_UNSIGNED_BYTE, rows);
  EXPECT_EQ(rows[0][0], 255);
  EXPECT_EQ(rows[1][8], 255);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glPixelStorei(GL_PACK_ROW_LENGTH, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glanceasyDestroyContext();
}

TEST(BlitCopiesBetweenFramebuffersWithScaling) {
  EXPECT_EQ(glanceasyCreateContext(64, 64), 1);
  GLuint textures[2] = {};
  glGenTextures(2, textures);
  for (int i = 0; i < 2; ++i) {
    glBindTexture(GL_TEXTURE_2D, textures[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
  }
  GLuint source = 0;
  GLuint destination = 0;
  glGenFramebuffers(1, &source);
  glGenFramebuffers(1, &destination);
  glBindFramebuffer(GL_FRAMEBUFFER, source);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         textures[0], 0);
  glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, destination);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         textures[1], 0);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  glBindFramebuffer(GL_READ_FRAMEBUFFER, source);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
  // Copy the whole source into the left half of the destination; the x axis
  // is unambiguous because it is not affected by the framebuffer orientation.
  glBlitFramebuffer(0, 0, 32, 32, 0, 0, 16, 32, GL_COLOR_BUFFER_BIT, GL_LINEAR);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glBindFramebuffer(GL_FRAMEBUFFER, destination);
  unsigned char rgba[4] = {};
  glReadPixels(8, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  EXPECT_EQ(rgba[2], 255);
  glReadPixels(24, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  EXPECT_EQ(rgba[2], 0);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &source);
  glDeleteFramebuffers(1, &destination);
  glDeleteTextures(2, textures);
  glanceasyDestroyContext();
}

TEST(UnsupportedFeaturesReportInvalidOperation) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  GLuint ids[2] = {7, 8};
  glGenTransformFeedbacks(2, ids);
  EXPECT_TRUE(ids[0] != 0);
  EXPECT_TRUE(ids[1] != 0);
  EXPECT_EQ(glIsTransformFeedback(11), GL_FALSE);
  glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, 5);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glBeginTransformFeedback(GL_TRIANGLES);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glEndTransformFeedback();
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glPauseTransformFeedback();
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glResumeTransformFeedback();
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glGetError();

  glDispatchCompute(1, 1, 1);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glMemoryBarrier(GL_ALL_BARRIER_BITS);
  glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);

  GLint64 value = 0;
  glGetInteger64v(GL_MAX_TEXTURE_SIZE, &value);
  EXPECT_EQ(value, 4096);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glanceasyDestroyContext();
}
