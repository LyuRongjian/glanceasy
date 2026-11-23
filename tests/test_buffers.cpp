// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include "GLES3/gl32.h"
#include "tests/zgl_test.h"
#include "zgl/zgl.h"

namespace {

const float kTriangle[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};

void DrainErrors() {
  while (glGetError() != GL_NO_ERROR) {
  }
}

}  // namespace

TEST(BufferObjectsStoreAndQueryData) {
  EXPECT_EQ(zglCreateContext(64, 48), 1);

  GLuint buffer = 0;
  glGenBuffers(1, &buffer);
  EXPECT_TRUE(buffer != 0);
  EXPECT_EQ(glIsBuffer(buffer), GL_TRUE);
  glBindBuffer(GL_ARRAY_BUFFER, buffer);

  glBufferData(GL_ARRAY_BUFFER, sizeof(kTriangle), kTriangle, GL_STATIC_DRAW);
  GLint value = 0;
  glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &value);
  EXPECT_EQ(value, static_cast<GLint>(sizeof(kTriangle)));
  glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_USAGE, &value);
  EXPECT_EQ(value, GL_STATIC_DRAW);

  GLint64 size64 = 0;
  glGetBufferParameteri64v(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &size64);
  EXPECT_EQ(size64, static_cast<GLint64>(sizeof(kTriangle)));

  // Partial updates must land in place and leave the rest untouched.
  const float patch[2] = {7.0f, 8.0f};
  glBufferSubData(GL_ARRAY_BUFFER, sizeof(float) * 4, sizeof(patch), patch);

  // Buffers are shared across every binding point, so the copy target has to
  // see the same contents.
  GLuint copy_target = 0;
  glGenBuffers(1, &copy_target);
  glBindBuffer(GL_COPY_WRITE_BUFFER, copy_target);
  glBufferData(GL_COPY_WRITE_BUFFER, sizeof(kTriangle), nullptr,
               GL_STATIC_DRAW);
  glBindBuffer(GL_COPY_READ_BUFFER, buffer);
  glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0,
                      sizeof(kTriangle));
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glBindBuffer(GL_COPY_WRITE_BUFFER, copy_target);
  float* mapped = static_cast<float*>(
      glMapBufferRange(GL_COPY_WRITE_BUFFER, sizeof(float) * 4,
                       sizeof(float) * 2, GL_MAP_READ_BIT));
  EXPECT_TRUE(mapped != nullptr);
  EXPECT_EQ(mapped[0], 7.0f);
  EXPECT_EQ(mapped[1], 8.0f);
  glGetBufferParameteriv(GL_COPY_WRITE_BUFFER, GL_BUFFER_MAPPED, &value);
  EXPECT_EQ(value, GL_TRUE);
  EXPECT_EQ(glUnmapBuffer(GL_COPY_WRITE_BUFFER), GL_TRUE);
  glGetBufferParameteriv(GL_COPY_WRITE_BUFFER, GL_BUFFER_MAPPED, &value);
  EXPECT_EQ(value, GL_FALSE);

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glDeleteBuffers(1, &copy_target);
  glDeleteBuffers(1, &buffer);
  EXPECT_EQ(glIsBuffer(buffer), GL_FALSE);
  zglDestroyContext();
}

TEST(BufferObjectsRejectInvalidArguments) {
  EXPECT_EQ(zglCreateContext(64, 48), 1);

  // No buffer is bound.
  glBufferData(GL_ARRAY_BUFFER, 16, nullptr, GL_STATIC_DRAW);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);

  GLuint buffer = 0;
  glGenBuffers(1, &buffer);
  glBindBuffer(GL_ARRAY_BUFFER, buffer);
  glBufferData(GL_ARRAY_BUFFER, -1, nullptr, GL_STATIC_DRAW);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glBufferData(GL_ARRAY_BUFFER, 16, nullptr, GL_TRIANGLES);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, 16, nullptr, GL_STATIC_DRAW);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  // COPY_READ_BUFFER is a legal target once a buffer is bound to it.
  glBindBuffer(GL_COPY_READ_BUFFER, buffer);
  glBufferData(GL_COPY_READ_BUFFER, 16, nullptr, GL_STATIC_DRAW);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glBindBuffer(GL_ARRAY_BUFFER, buffer);
  glBufferData(GL_ARRAY_BUFFER, 32, nullptr, GL_STATIC_DRAW);
  glBufferSubData(GL_ARRAY_BUFFER, 40, 8, nullptr);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glBufferSubData(GL_ARRAY_BUFFER, -4, 8, nullptr);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, 8, nullptr);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);

  GLint value = 0;
  glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_ACCESS_FLAGS, &value);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);

  EXPECT_TRUE(glMapBufferRange(GL_ARRAY_BUFFER, -1, 4, GL_MAP_READ_BIT) ==
              nullptr);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  EXPECT_TRUE(glMapBufferRange(GL_ARRAY_BUFFER, 40, 4, GL_MAP_READ_BIT) ==
              nullptr);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);

  void* pointer = nullptr;
  glGetBufferPointerv(GL_ARRAY_BUFFER, GL_BUFFER_MAP_POINTER, &pointer);
  EXPECT_TRUE(pointer == nullptr);
  EXPECT_EQ(glUnmapBuffer(GL_ARRAY_BUFFER), GL_FALSE);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glFlushMappedBufferRange(GL_ARRAY_BUFFER, 0, 4);

  glDeleteBuffers(1, &buffer);
  zglDestroyContext();
}

TEST(MappedBuffersExposeSingleMapping) {
  EXPECT_EQ(zglCreateContext(64, 48), 1);
  GLuint buffer = 0;
  glGenBuffers(1, &buffer);
  glBindBuffer(GL_ARRAY_BUFFER, buffer);
  glBufferData(GL_ARRAY_BUFFER, 64, nullptr, GL_DYNAMIC_DRAW);

  unsigned char* data = static_cast<unsigned char*>(glMapBufferRange(
      GL_ARRAY_BUFFER, 8, 16, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT));
  EXPECT_TRUE(data != nullptr);
  memset(data, 0x5a, 16);

  // A second mapping while one is live is an error.
  EXPECT_TRUE(glMapBufferRange(GL_ARRAY_BUFFER, 0, 8, GL_MAP_READ_BIT) ==
              nullptr);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);

  GLint value = 0;
  glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_MAP_OFFSET, &value);
  EXPECT_EQ(value, 8);
  glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_MAP_LENGTH, &value);
  EXPECT_EQ(value, 16);
  glFlushMappedBufferRange(GL_ARRAY_BUFFER, 0, 16);
  EXPECT_EQ(glUnmapBuffer(GL_ARRAY_BUFFER), GL_TRUE);

  data = static_cast<unsigned char*>(
      glMapBufferRange(GL_ARRAY_BUFFER, 0, 64, GL_MAP_READ_BIT));
  EXPECT_TRUE(data != nullptr);
  EXPECT_EQ(data[8], 0x5a);
  EXPECT_EQ(data[23], 0x5a);
  EXPECT_EQ(data[7], 0x00);
  EXPECT_EQ(glUnmapBuffer(GL_ARRAY_BUFFER), GL_TRUE);

  glDeleteBuffers(1, &buffer);
  zglDestroyContext();
}

TEST(VertexArrayObjectsCaptureAttributeState) {
  EXPECT_EQ(zglCreateContext(64, 48), 1);

  GLuint vertex_array = 0;
  glGenVertexArrays(1, &vertex_array);
  EXPECT_EQ(glIsVertexArray(vertex_array), GL_TRUE);
  glBindVertexArray(vertex_array);

  GLuint buffer = 0;
  glGenBuffers(1, &buffer);
  glBindBuffer(GL_ARRAY_BUFFER, buffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof(kTriangle), kTriangle, GL_STATIC_DRAW);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  GLint enabled = 0;
  glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
  EXPECT_EQ(enabled, GL_TRUE);
  GLint size = 0;
  glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_SIZE, &size);
  EXPECT_EQ(size, 2);
  GLint stride = 0;
  glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &stride);
  EXPECT_EQ(stride, static_cast<GLint>(sizeof(float) * 2));
  GLint divisor = 0;
  glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_DIVISOR, &divisor);
  EXPECT_EQ(divisor, 0);
  void* pointer = nullptr;
  glGetVertexAttribPointerv(0, GL_VERTEX_ATTRIB_ARRAY_POINTER, &pointer);
  EXPECT_TRUE(pointer == nullptr);

  glVertexAttribDivisor(0, 3);
  glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_DIVISOR, &divisor);
  EXPECT_EQ(divisor, 3);

  // Attribute 1 keeps a constant value that can be queried back.
  glVertexAttrib4f(1, 1.0f, 2.0f, 3.0f, 4.0f);
  GLfloat constant[4] = {};
  glGetVertexAttribfv(1, GL_CURRENT_VERTEX_ATTRIB, constant);
  EXPECT_EQ(constant[0], 1.0f);
  EXPECT_EQ(constant[3], 4.0f);

  // Switching to the default array must hide the captured state.
  glBindVertexArray(0);
  glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
  EXPECT_EQ(enabled, GL_FALSE);

  glBindVertexArray(vertex_array);
  glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
  EXPECT_EQ(enabled, GL_TRUE);
  glDisableVertexAttribArray(0);

  glDeleteVertexArrays(1, &vertex_array);
  EXPECT_EQ(glIsVertexArray(vertex_array), GL_FALSE);
  glDeleteBuffers(1, &buffer);
  zglDestroyContext();
}

TEST(VertexArrayObjectsRejectInvalidNames) {
  EXPECT_EQ(zglCreateContext(64, 48), 1);
  glBindVertexArray(1234);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glVertexAttribPointer(16, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glVertexAttribPointer(0, 5, GL_FLOAT, GL_FALSE, 0, nullptr);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, -4, nullptr);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glEnableVertexAttribArray(16);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  GLuint missing = 0;
  glDeleteVertexArrays(1, &missing);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  zglDestroyContext();
}

TEST(ElementArrayBufferFollowsVertexArray) {
  EXPECT_EQ(zglCreateContext(64, 48), 1);

  GLuint vertex_array = 0;
  glGenVertexArrays(1, &vertex_array);
  glBindVertexArray(vertex_array);
  GLuint elements = 0;
  glGenBuffers(1, &elements);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elements);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, 6 * sizeof(unsigned short), nullptr,
               GL_STATIC_DRAW);

  GLint binding = 0;
  glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &binding);
  EXPECT_EQ(binding, static_cast<GLint>(elements));

  glBindVertexArray(0);
  glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &binding);
  EXPECT_EQ(binding, 0);

  glBindVertexArray(vertex_array);
  glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &binding);
  EXPECT_EQ(binding, static_cast<GLint>(elements));

  glDeleteBuffers(1, &elements);
  glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &binding);
  EXPECT_EQ(binding, 0);
  glDeleteVertexArrays(1, &vertex_array);
  zglDestroyContext();
}
