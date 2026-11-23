// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Exercises the API-compatibility stubs: every entry point that the CPU
// backend accepts but does not implement must either report a sticky
// GL_INVALID_OPERATION or stay silent as a documented no-op.

#include "GLES3/gl32.h"
#include "tests/zgl_test.h"
#include "zgl/zgl.h"

// ES 3.1 entry points that the Khronos GLES3 headers used by this repository do
// not declare but the library still exports as stubs.
extern "C" {
void glDrawTransformFeedback(GLenum mode, GLuint id);
void glShaderStorageBlockBinding(GLuint program,
                                 GLuint storage_block_index,
                                 GLuint storage_block_binding);
void glGetActiveAtomicCounterBufferiv(GLuint program,
                                      GLuint buffer_index,
                                      GLenum pname,
                                      GLint* params);
}

namespace {

enum : int {
  kUnknownCapability = 0x0BEE,
  kCompressedRgbaAstc4x4 = 0x93B0,
};

}  // namespace

TEST(TransformFeedbackEntryPointsAreRejected) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  GLuint ids[2] = {7, 7};
  glGenTransformFeedbacks(2, ids);
  EXPECT_EQ(ids[0], 0);
  EXPECT_EQ(ids[1], 0);
  glDeleteTransformFeedbacks(2, ids);
  glDeleteTransformFeedbacks(0, nullptr);
  EXPECT_EQ(glIsTransformFeedback(1), GL_FALSE);

  glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, 0);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, 3);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);

  glBeginTransformFeedback(GL_TRIANGLES);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glEndTransformFeedback();
  glPauseTransformFeedback();
  glResumeTransformFeedback();
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glTransformFeedbackVaryings(0, 0, nullptr, GL_SEPARATE_ATTRIBS);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  GLsizei length = 12;
  GLsizei size = 12;
  GLenum type = GL_FLOAT;
  GLchar name[8] = "garbage";
  glGetTransformFeedbackVarying(0, 0, 8, &length, &size, &type, name);
  EXPECT_EQ(length, 0);
  EXPECT_EQ(name[0], '\0');
  glGetTransformFeedbackVarying(0, 0, 0, nullptr, nullptr, nullptr, nullptr);

  glDrawTransformFeedback(GL_TRIANGLES, 1);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  zglDestroyContext();
}

TEST(IndexedBufferBindingsAndWideQueries) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  GLuint buffer = 0;
  glGenBuffers(1, &buffer);
  glBindBuffer(GL_ARRAY_BUFFER, buffer);
  glBindBufferBase(GL_ARRAY_BUFFER, 0, buffer);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, buffer);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glBindBufferBase(GL_UNIFORM_BUFFER, 0, buffer);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffer);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);

  glBindBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, 0, buffer, 0, 16);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glBindBufferRange(GL_UNIFORM_BUFFER, 0, buffer, 0, 16);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 0, buffer, 0, 16);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);

  const unsigned char bytes[16] = {};
  glBufferData(GL_ARRAY_BUFFER, 16, bytes, GL_STATIC_DRAW);
  GLint64 size64 = 0;
  glGetBufferParameteri64v(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &size64);
  EXPECT_EQ(size64, 16);
  glGetBufferParameteri64v(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, nullptr);
  GLint64 integer64 = 0;
  glGetInteger64v(GL_MAX_TEXTURE_SIZE, &integer64);
  EXPECT_EQ(integer64, 4096);
  glGetInteger64v(GL_MAX_TEXTURE_SIZE, nullptr);
  GLint indexed = 0;
  glGetIntegeri_v(GL_MAX_VERTEX_ATTRIBS, 0, &indexed);
  EXPECT_EQ(indexed, 16);
  glGetIntegeri_v(GL_MAX_VERTEX_ATTRIBS, 0, nullptr);
  GLint64 indexed64 = 0;
  glGetInteger64i_v(GL_MAX_VERTEX_ATTRIBS, 0, &indexed64);
  EXPECT_EQ(indexed64, 16);
  glGetInteger64i_v(GL_MAX_VERTEX_ATTRIBS, 0, nullptr);
  GLboolean boolean_value = GL_FALSE;
  glGetBooleani_v(GL_BLEND, 0, &boolean_value);
  EXPECT_EQ(boolean_value, GL_FALSE);
  glGetBooleani_v(GL_BLEND, 0, nullptr);

  glEnablei(GL_BLEND, 0);
  EXPECT_EQ(glIsEnabledi(GL_BLEND, 0), GL_TRUE);
  glDisablei(GL_BLEND, 0);
  EXPECT_EQ(glIsEnabledi(GL_BLEND, 0), GL_FALSE);
  EXPECT_EQ(glIsEnabledi(kUnknownCapability, 0), GL_FALSE);

  GLint attrib = 0;
  glGetVertexAttribIiv(0, GL_VERTEX_ATTRIB_ARRAY_SIZE, &attrib);
  EXPECT_EQ(attrib, 4);
  GLuint attrib_unsigned = 0;
  glGetVertexAttribIuiv(0, GL_VERTEX_ATTRIB_ARRAY_SIZE, &attrib_unsigned);
  EXPECT_EQ(attrib_unsigned, 4u);
  glGetVertexAttribIuiv(0, GL_VERTEX_ATTRIB_ARRAY_SIZE, nullptr);

  glDeleteBuffers(1, &buffer);
  zglDestroyContext();
}

TEST(ComputeAndIndirectDrawsReportUnsupported) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  glDispatchCompute(1, 1, 1);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glDispatchComputeIndirect(0);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glDrawArraysIndirect(GL_TRIANGLES, nullptr);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_SHORT, nullptr);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);

  glMemoryBarrier(GL_ALL_BARRIER_BITS);
  glMemoryBarrierByRegion(GL_TEXTURE_FETCH_BARRIER_BIT);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glBindImageTexture(0, 1, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glShaderStorageBlockBinding(0, 0, 0);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  zglDestroyContext();
}

TEST(UniformBlockAndProgramInterfaceQueriesReturnEmpty) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  GLsizei length = 8;
  GLchar name[8] = "garbage";
  glGetActiveUniformBlockName(0, 0, 8, &length, name);
  EXPECT_EQ(length, 0);
  EXPECT_EQ(name[0], '\0');
  glGetActiveUniformBlockName(0, 0, 0, nullptr, nullptr);

  GLint value = 8;
  glGetActiveUniformBlockiv(0, 0, GL_UNIFORM_BLOCK_DATA_SIZE, &value);
  EXPECT_EQ(value, 0);
  glGetActiveUniformBlockiv(0, 0, GL_UNIFORM_BLOCK_DATA_SIZE, nullptr);

  value = 8;
  glGetActiveAtomicCounterBufferiv(0, 0, GL_ATOMIC_COUNTER_BUFFER_SIZE, &value);
  EXPECT_EQ(value, 0);
  glGetActiveAtomicCounterBufferiv(0, 0, 0, nullptr);

  value = 8;
  glGetProgramInterfaceiv(0, GL_PROGRAM_INPUT, GL_ACTIVE_RESOURCES, &value);
  EXPECT_EQ(value, 0);
  glGetProgramInterfaceiv(0, GL_PROGRAM_INPUT, GL_ACTIVE_RESOURCES, nullptr);

  EXPECT_EQ(glGetProgramResourceIndex(0, GL_PROGRAM_INPUT, "a_pos"),
            GL_INVALID_INDEX);
  length = 8;
  name[0] = 'x';
  glGetProgramResourceName(0, GL_PROGRAM_INPUT, 0, 8, &length, name);
  EXPECT_EQ(length, 0);
  glGetProgramResourceName(0, GL_PROGRAM_INPUT, 0, 0, nullptr, nullptr);

  const GLenum properties[2] = {GL_TYPE, GL_ARRAY_SIZE};
  GLint params[2] = {8, 8};
  length = 8;
  glGetProgramResourceiv(0, GL_PROGRAM_INPUT, 0, 2, properties, 2, &length,
                         params);
  EXPECT_EQ(params[0], 0);
  glGetProgramResourceiv(0, GL_PROGRAM_INPUT, 0, 2, properties, 0, nullptr,
                         nullptr);

  EXPECT_EQ(glGetProgramResourceLocation(0, GL_PROGRAM_INPUT, "a_pos"), -1);

  GLsizei binary_length = 8;
  GLenum binary_format = GL_RGBA;
  glGetProgramBinary(0, 0, &binary_length, &binary_format, nullptr);
  EXPECT_EQ(binary_length, 0);
  EXPECT_EQ(binary_format, 0u);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glProgramBinary(0, 0, nullptr, 0);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glProgramParameteri(0, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  zglDestroyContext();
}

TEST(MultisampleAndTextureViewEntryPointsAreStubs) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  glTexStorage2DMultisample(GL_TEXTURE_2D, 4, GL_RGBA8, 8, 8, GL_TRUE);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glTexStorage3DMultisample(GL_TEXTURE_3D, 4, GL_RGBA8, 8, 8, 8, GL_TRUE);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);

  GLfloat sample_position = 0.0f;
  glGetMultisamplefv(GL_SAMPLE_POSITION, 0, &sample_position);
  EXPECT_NEAR(sample_position, 0.5f, 0.001f);
  glGetMultisamplefv(GL_SAMPLE_POSITION, 0, nullptr);
  glSampleMaski(0, 0xFF);
  glMinSampleShading(1.0f);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_WIDTH, 64);
  GLint parameter = 8;
  glGetFramebufferParameteriv(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_WIDTH,
                              &parameter);
  EXPECT_EQ(parameter, 0);
  glGetFramebufferParameteriv(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_WIDTH,
                              nullptr);

  glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA8, 0);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glTexBufferRange(GL_TEXTURE_BUFFER, GL_RGBA8, 0, 0, 16);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glCopyImageSubData(0, GL_TEXTURE_2D, 0, 0, 0, 0, 0, GL_TEXTURE_2D, 0, 0, 0, 0,
                     1, 1, 1);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glPatchParameteri(GL_PATCH_VERTICES, 3);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glPrimitiveBoundingBox(-1, -1, -1, 1, 1, 1, 1, 1);
  glBlendBarrier();
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  zglDestroyContext();
}

TEST(InternalFormatAndTexLevelQueries) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  GLint params[2] = {8, 8};
  glGetInternalformativ(GL_TEXTURE_2D, GL_RGBA8, GL_NUM_SAMPLE_COUNTS, 2,
                        params);
  EXPECT_EQ(params[0], 0);
  glGetInternalformativ(GL_TEXTURE_2D, kCompressedRgbaAstc4x4,
                        GL_NUM_SAMPLE_COUNTS, 0, nullptr);

  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               nullptr);
  GLfloat width = 0.0f;
  glGetTexLevelParameterfv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
  EXPECT_NEAR(width, 8.0f, 0.001f);
  glGetTexLevelParameterfv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, nullptr);
  glDeleteTextures(1, &texture);
  zglDestroyContext();
}

TEST(DebugOutputEntryPointsAreSilent) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  glDebugMessageCallback(nullptr, nullptr);
  glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr,
                        GL_TRUE);
  glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER, 1,
                       GL_DEBUG_SEVERITY_LOW, -1, "message");
  const GLuint count = glGetDebugMessageLog(1, 0, nullptr, nullptr, nullptr,
                                            nullptr, nullptr, nullptr);
  EXPECT_EQ(count, 0u);
  glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 1, -1, "group");
  glPopDebugGroup();
  glObjectLabel(GL_TEXTURE, 3, -1, "label");
  GLsizei length = 8;
  GLchar label[8] = "garbage";
  glGetObjectLabel(GL_TEXTURE, 3, 8, &length, label);
  EXPECT_EQ(length, 0);
  EXPECT_EQ(label[0], '\0');
  glGetObjectLabel(GL_TEXTURE, 3, 0, nullptr, nullptr);
  glObjectPtrLabel(nullptr, -1, "label");
  length = 8;
  label[0] = 'x';
  glGetObjectPtrLabel(nullptr, 8, &length, label);
  EXPECT_EQ(length, 0);
  glGetObjectPtrLabel(nullptr, 0, nullptr, nullptr);

  void* pointer = reinterpret_cast<void*>(1);
  glGetPointerv(static_cast<GLenum>(0x8093), &pointer);
  EXPECT_TRUE(pointer == nullptr);
  glGetPointerv(static_cast<GLenum>(0x8093), nullptr);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  zglDestroyContext();
}
