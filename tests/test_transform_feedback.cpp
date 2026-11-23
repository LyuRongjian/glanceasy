// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <string.h>

#include <string>

#include "GLES3/gl32.h"
#include "tests/zgl_test.h"
#include "zgl/zgl.h"

namespace {

extern "C" void glDrawTransformFeedback(GLenum mode, GLuint id);

const char* kVertex =
    "#version 300 es\n"
    "out float v_id;\n"
    "out vec2 v_pair;\n"
    "void main() {\n"
    "  v_id = float(gl_VertexID);\n"
    "  v_pair = vec2(v_id, v_id * 2.0);\n"
    "  gl_Position = vec4(0.0, 0.0, 0.0, 1.0);\n"
    "  gl_PointSize = 8.0;\n"
    "}\n";

const char* kFragment =
    "#version 300 es\n"
    "precision mediump float;\n"
    "out vec4 frag_color;\n"
    "void main() { frag_color = vec4(1.0); }\n";

GLuint LinkProgram(const char* vertex_source,
                   const char* fragment_source,
                   GLsizei varying_count,
                   const char* const* varyings,
                   GLenum buffer_mode) {
  const GLuint vs = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vs, 1, &vertex_source, nullptr);
  glCompileShader(vs);
  const GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fs, 1, &fragment_source, nullptr);
  glCompileShader(fs);
  const GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  if (varying_count > 0)
    glTransformFeedbackVaryings(program, varying_count, varyings, buffer_mode);
  glLinkProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  return program;
}

bool Linked(GLuint program) {
  GLint status = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &status);
  return status == GL_TRUE;
}

GLuint MakeBuffer(GLenum target, size_t size) {
  GLuint buffer = 0;
  glGenBuffers(1, &buffer);
  glBindBuffer(target, buffer);
  glBufferData(target, static_cast<GLsizeiptr>(size), nullptr, GL_STREAM_DRAW);
  return buffer;
}

void ReadBuffer(GLenum target, GLuint buffer, size_t size, void* out) {
  glBindBuffer(target, buffer);
  void* mapped = glMapBufferRange(target, 0, static_cast<GLsizeiptr>(size),
                                  GL_MAP_READ_BIT);
  EXPECT_TRUE(mapped != nullptr);
  if (mapped)
    memcpy(out, mapped, size);
  glUnmapBuffer(target);
}

}  // namespace

TEST(TransformFeedbackCapturesInterleavedVaryings) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* varyings[] = {"v_id", "v_pair"};
  const GLuint program =
      LinkProgram(kVertex, kFragment, 2, varyings, GL_INTERLEAVED_ATTRIBS);
  EXPECT_TRUE(Linked(program));

  GLuint tf = 0;
  glGenTransformFeedbacks(1, &tf);
  EXPECT_EQ(glIsTransformFeedback(tf), GL_TRUE);
  glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, tf);

  const size_t kRecordFloats = 3;
  const GLuint buffer = MakeBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,
                                   kRecordFloats * 3 * sizeof(float));
  glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, buffer);
  GLint bound = 0;
  glGetIntegeri_v(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING, 0, &bound);
  EXPECT_EQ(bound, static_cast<GLint>(buffer));

  glUseProgram(program);
  glEnable(GL_RASTERIZER_DISCARD);
  glBeginTransformFeedback(GL_POINTS);
  glDrawArrays(GL_POINTS, 0, 3);
  glEndTransformFeedback();
  glDisable(GL_RASTERIZER_DISCARD);

  float records[9] = {};
  ReadBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, buffer, sizeof(records), records);
  EXPECT_NEAR(records[0], 0.0f, 1e-6);
  EXPECT_NEAR(records[1], 0.0f, 1e-6);
  EXPECT_NEAR(records[2], 0.0f, 1e-6);
  EXPECT_NEAR(records[3], 1.0f, 1e-6);
  EXPECT_NEAR(records[4], 1.0f, 1e-6);
  EXPECT_NEAR(records[5], 2.0f, 1e-6);
  EXPECT_NEAR(records[6], 2.0f, 1e-6);
  EXPECT_NEAR(records[7], 2.0f, 1e-6);
  EXPECT_NEAR(records[8], 4.0f, 1e-6);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glDeleteBuffers(1, &buffer);
  glDeleteTransformFeedbacks(1, &tf);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(TransformFeedbackSeparateAttribsPauseAndResume) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* varyings[] = {"v_id", "v_pair"};
  const GLuint program =
      LinkProgram(kVertex, kFragment, 2, varyings, GL_SEPARATE_ATTRIBS);
  EXPECT_TRUE(Linked(program));

  GLuint tf = 0;
  glGenTransformFeedbacks(1, &tf);
  glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, tf);
  const GLuint id_buffer =
      MakeBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, 3 * sizeof(float));
  const GLuint pair_buffer =
      MakeBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, 6 * sizeof(float));
  glBindBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, 0, id_buffer, 0,
                    static_cast<GLsizeiptr>(3 * sizeof(float)));
  glBindBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, 1, pair_buffer, 0,
                    static_cast<GLsizeiptr>(6 * sizeof(float)));
  GLint start = 0;
  GLint size = 0;
  glGetIntegeri_v(GL_TRANSFORM_FEEDBACK_BUFFER_START, 1, &start);
  glGetIntegeri_v(GL_TRANSFORM_FEEDBACK_BUFFER_SIZE, 1, &size);
  EXPECT_EQ(start, 0);
  EXPECT_EQ(size, static_cast<GLint>(6 * sizeof(float)));

  glUseProgram(program);
  glEnable(GL_RASTERIZER_DISCARD);
  GLuint query = 0;
  glGenQueries(1, &query);
  glBeginTransformFeedback(GL_POINTS);
  glBeginQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, query);
  glDrawArrays(GL_POINTS, 0, 2);
  glPauseTransformFeedback();
  glDrawArrays(GL_POINTS, 2, 1);
  glResumeTransformFeedback();
  glDrawArrays(GL_POINTS, 3, 1);
  glEndQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN);
  glEndTransformFeedback();
  glDisable(GL_RASTERIZER_DISCARD);

  GLuint written = 0;
  glGetQueryObjectuiv(query, GL_QUERY_RESULT, &written);
  EXPECT_EQ(written, 3u);

  float ids[3] = {};
  float pairs[6] = {};
  ReadBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, id_buffer, sizeof(ids), ids);
  ReadBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, pair_buffer, sizeof(pairs), pairs);
  EXPECT_NEAR(ids[0], 0.0f, 1e-6);
  EXPECT_NEAR(ids[1], 1.0f, 1e-6);
  EXPECT_NEAR(ids[2], 3.0f, 1e-6);
  EXPECT_NEAR(pairs[0], 0.0f, 1e-6);
  EXPECT_NEAR(pairs[1], 0.0f, 1e-6);
  EXPECT_NEAR(pairs[2], 1.0f, 1e-6);
  EXPECT_NEAR(pairs[3], 2.0f, 1e-6);
  EXPECT_NEAR(pairs[4], 3.0f, 1e-6);
  EXPECT_NEAR(pairs[5], 6.0f, 1e-6);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glDeleteQueries(1, &query);
  glDeleteBuffers(1, &id_buffer);
  glDeleteBuffers(1, &pair_buffer);
  glDeleteTransformFeedbacks(1, &tf);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(TransformFeedbackDrawReplaysCapturedVertexCount) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* varyings[] = {"v_id", "v_pair"};
  const GLuint program =
      LinkProgram(kVertex, kFragment, 2, varyings, GL_INTERLEAVED_ATTRIBS);
  EXPECT_TRUE(Linked(program));

  GLuint tf = 0;
  glGenTransformFeedbacks(1, &tf);
  glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, tf);
  const GLuint buffer =
      MakeBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, 9 * sizeof(float));
  glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, buffer);

  glUseProgram(program);
  glEnable(GL_RASTERIZER_DISCARD);
  glBeginTransformFeedback(GL_POINTS);
  glDrawArrays(GL_POINTS, 0, 3);
  glEndTransformFeedback();
  glDisable(GL_RASTERIZER_DISCARD);

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glDrawTransformFeedback(GL_POINTS, tf);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  unsigned char rgba[4] = {};
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  EXPECT_EQ(rgba[0], 255);
  EXPECT_EQ(rgba[1], 255);
  EXPECT_EQ(rgba[2], 255);

  glDeleteBuffers(1, &buffer);
  glDeleteTransformFeedbacks(1, &tf);
  glDeleteProgram(program);
  zglDestroyContext();
}

TEST(TransformFeedbackVaryingReflectionAndLinkFailure) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* varyings[] = {"v_id", "v_pair"};
  const GLuint program =
      LinkProgram(kVertex, kFragment, 2, varyings, GL_INTERLEAVED_ATTRIBS);
  EXPECT_TRUE(Linked(program));

  GLsizei length = 0;
  GLsizei size = 0;
  GLenum type = 0;
  GLchar name[64] = {};
  glGetTransformFeedbackVarying(program, 0, sizeof(name), &length, &size,
                                &type, name);
  EXPECT_EQ(length, 4);
  EXPECT_EQ(size, 1);
  EXPECT_EQ(type, GL_FLOAT);
  EXPECT_EQ(strcmp(name, "v_id"), 0);
  glGetTransformFeedbackVarying(program, 1, sizeof(name), &length, &size,
                                &type, name);
  EXPECT_EQ(length, 6);
  EXPECT_EQ(size, 1);
  EXPECT_EQ(type, GL_FLOAT);
  EXPECT_EQ(strcmp(name, "v_pair"), 0);

  const char* unknown[] = {"missing"};
  const GLuint bad =
      LinkProgram(kVertex, kFragment, 1, unknown, GL_INTERLEAVED_ATTRIBS);
  EXPECT_FALSE(Linked(bad));
  glDeleteProgram(bad);

  glBeginTransformFeedback(GL_POINTS);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glDeleteProgram(program);
  zglDestroyContext();
}
