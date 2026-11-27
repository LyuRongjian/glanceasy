// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>

#include <initializer_list>

#include "GLES3/gl32.h"
#include "tests/glanceasy_test.h"
#include "glanceasy/glanceasy.h"

namespace {

bool Contains(const char* haystack, const char* needle) {
  return haystack && strstr(haystack, needle) != nullptr;
}

}  // namespace

TEST(ContextStringsDescribeTheBackend) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
  const char* renderer =
      reinterpret_cast<const char*>(glGetString(GL_RENDERER));
  const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
  const char* language =
      reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
  EXPECT_TRUE(vendor != nullptr);
  EXPECT_TRUE(renderer != nullptr);
  EXPECT_TRUE(version != nullptr);
  EXPECT_TRUE(language != nullptr);
  EXPECT_TRUE(Contains(vendor, "Glanceasy"));
  EXPECT_TRUE(Contains(version, "OpenGL ES 3.0"));
  EXPECT_TRUE(Contains(language, "GLSL ES 3.00"));
  EXPECT_TRUE(glGetString(0x1234) == nullptr);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);

  // ES3 style extension enumeration.
  GLint count = 0;
  glGetIntegerv(GL_NUM_EXTENSIONS, &count);
  EXPECT_TRUE(count >= 0);
  for (GLint i = 0; i < count; ++i)
    EXPECT_TRUE(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)) != nullptr);
  glanceasyDestroyContext();
}

TEST(ImplementationLimitsAreAdvertised) {
  EXPECT_EQ(glanceasyCreateContext(64, 48), 1);
  GLint value = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value);
  EXPECT_EQ(value, 4096);
  glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &value);
  EXPECT_EQ(value, 4096);
  glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &value);
  EXPECT_EQ(value, 16);
  glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &value);
  EXPECT_EQ(value, 16);
  glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &value);
  EXPECT_EQ(value, 32);
  glGetIntegerv(GL_MAX_DRAW_BUFFERS, &value);
  EXPECT_EQ(value, 4);
  glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &value);
  EXPECT_EQ(value, 4);
  glGetIntegerv(GL_MAX_SAMPLES, &value);
  EXPECT_EQ(value, 4);
  glGetIntegerv(GL_SUBPIXEL_BITS, &value);
  EXPECT_EQ(value, 8);
  glGetIntegerv(GL_RED_BITS, &value);
  EXPECT_EQ(value, 8);
  glGetIntegerv(GL_DEPTH_BITS, &value);
  EXPECT_EQ(value, 16);
  glGetIntegerv(GL_STENCIL_BITS, &value);
  EXPECT_EQ(value, 8);
  glGetIntegerv(GL_MAJOR_VERSION, &value);
  EXPECT_EQ(value, 3);
  glGetIntegerv(GL_MINOR_VERSION, &value);
  EXPECT_EQ(value, 0);
  GLint viewport[4] = {};
  glGetIntegerv(GL_VIEWPORT, viewport);
  EXPECT_EQ(viewport[0], 0);
  EXPECT_EQ(viewport[1], 0);
  EXPECT_EQ(viewport[2], 64);
  EXPECT_EQ(viewport[3], 48);

  GLfloat range[2] = {};
  glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, range);
  EXPECT_NEAR(range[0], 1.0f, 0.001f);
  glGetFloatv(GL_ALIASED_POINT_SIZE_RANGE, range);
  EXPECT_TRUE(range[1] > 1.0f);
  glGetFloatv(GL_DEPTH_RANGE, range);
  EXPECT_NEAR(range[0], 0.0f, 0.001f);
  EXPECT_NEAR(range[1], 1.0f, 0.001f);
  glanceasyDestroyContext();
}

TEST(EnableDisableAndQueryCaps) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  EXPECT_FALSE(glIsEnabled(GL_BLEND));
  glEnable(GL_BLEND);
  EXPECT_TRUE(glIsEnabled(GL_BLEND));
  GLint value = 0;
  glGetIntegerv(GL_BLEND, &value);
  EXPECT_EQ(value, 1);
  glDisable(GL_BLEND);
  EXPECT_FALSE(glIsEnabled(GL_BLEND));
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_STENCIL_TEST);
  glEnable(GL_CULL_FACE);
  glEnable(GL_SCISSOR_TEST);
  glEnable(GL_POLYGON_OFFSET_FILL);
  glEnable(GL_DITHER);
  glEnable(GL_RASTERIZER_DISCARD);
  for (GLenum cap :
       {GL_DEPTH_TEST, GL_STENCIL_TEST, GL_CULL_FACE, GL_SCISSOR_TEST,
        GL_POLYGON_OFFSET_FILL, GL_DITHER, GL_RASTERIZER_DISCARD}) {
    EXPECT_TRUE(glIsEnabled(cap));
    glDisable(cap);
    EXPECT_FALSE(glIsEnabled(cap));
  }
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glEnable(0x9999);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glIsEnabled(0x9999);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glanceasyDestroyContext();
}

TEST(BlendStateCanBeConfigured) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  GLint value = 0;
  glGetIntegerv(GL_BLEND_SRC_RGB, &value);
  EXPECT_EQ(value, GL_SRC_ALPHA);
  glGetIntegerv(GL_BLEND_DST_RGB, &value);
  EXPECT_EQ(value, GL_ONE_MINUS_SRC_ALPHA);

  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &value);
  EXPECT_EQ(value, GL_ZERO);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &value);
  EXPECT_EQ(value, GL_ONE);

  glBlendEquationSeparate(GL_FUNC_SUBTRACT, GL_FUNC_ADD);
  glGetIntegerv(GL_BLEND_EQUATION_RGB, &value);
  EXPECT_EQ(value, GL_FUNC_SUBTRACT);
  glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &value);
  EXPECT_EQ(value, GL_FUNC_ADD);

  const GLfloat constant[4] = {0.25f, 0.5f, 0.75f, 1.0f};
  glBlendColor(constant[0], constant[1], constant[2], constant[3]);
  GLfloat colour[4] = {};
  glGetFloatv(GL_BLEND_COLOR, colour);
  EXPECT_NEAR(colour[1], 0.5f, 0.001f);

  glBlendFunc(GL_ONE, 0x1234);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glanceasyDestroyContext();
}

TEST(DepthAndStencilStateRoundTrips) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  glDepthFunc(GL_LEQUAL);
  GLint value = 0;
  glGetIntegerv(GL_DEPTH_FUNC, &value);
  EXPECT_EQ(value, GL_LEQUAL);
  glDepthRangef(0.25f, 0.75f);
  GLfloat range[2] = {};
  glGetFloatv(GL_DEPTH_RANGE, range);
  EXPECT_NEAR(range[0], 0.25f, 0.001f);
  glClearDepthf(0.5f);
  glGetFloatv(GL_DEPTH_CLEAR_VALUE, range);
  EXPECT_NEAR(range[0], 0.5f, 0.001f);
  glDepthMask(GL_FALSE);
  glGetIntegerv(GL_DEPTH_WRITEMASK, &value);
  EXPECT_EQ(value, 0);
  glDepthMask(GL_TRUE);
  glGetIntegerv(GL_DEPTH_WRITEMASK, &value);
  EXPECT_EQ(value, 1);

  glStencilFunc(GL_EQUAL, 3, 0xFF);
  glGetIntegerv(GL_STENCIL_FUNC, &value);
  EXPECT_EQ(value, GL_EQUAL);
  glGetIntegerv(GL_STENCIL_REF, &value);
  EXPECT_EQ(value, 3);
  glGetIntegerv(GL_STENCIL_VALUE_MASK, &value);
  EXPECT_EQ(value, 0xFF);

  glStencilOp(GL_KEEP, GL_INCR, GL_DECR_WRAP);
  glGetIntegerv(GL_STENCIL_FAIL, &value);
  EXPECT_EQ(value, GL_KEEP);
  glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &value);
  EXPECT_EQ(value, GL_INCR);
  glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &value);
  EXPECT_EQ(value, GL_DECR_WRAP);

  glStencilMask(0x0F);
  glGetIntegerv(GL_STENCIL_WRITEMASK, &value);
  EXPECT_EQ(value, 0x0F);
  glStencilMaskSeparate(GL_BACK, 0xF0);
  glGetIntegerv(GL_STENCIL_BACK_WRITEMASK, &value);
  EXPECT_EQ(value, 0xF0);

  glDepthFunc(0x1234);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glStencilOp(GL_KEEP, GL_KEEP, 0x1234);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glanceasyDestroyContext();
}

TEST(ColourMaskAcceptsFourBooleans) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  glColorMask(GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
  GLint mask[4] = {};
  glGetIntegerv(GL_COLOR_WRITEMASK, mask);
  EXPECT_EQ(mask[0], 1);
  EXPECT_EQ(mask[1], 0);
  EXPECT_EQ(mask[2], 1);
  EXPECT_EQ(mask[3], 0);
  glanceasyDestroyContext();
}

TEST(ViewportScissorAndCullState) {
  EXPECT_EQ(glanceasyCreateContext(64, 48), 1);
  glViewport(4, 6, 20, 30);
  GLint viewport[4] = {};
  glGetIntegerv(GL_VIEWPORT, viewport);
  EXPECT_EQ(viewport[0], 4);
  EXPECT_EQ(viewport[1], 6);
  EXPECT_EQ(viewport[2], 20);
  EXPECT_EQ(viewport[3], 30);

  glScissor(2, 3, 10, 11);
  GLint box[4] = {};
  glGetIntegerv(GL_SCISSOR_BOX, box);
  EXPECT_EQ(box[0], 2);
  EXPECT_EQ(box[1], 3);
  EXPECT_EQ(box[2], 10);
  EXPECT_EQ(box[3], 11);

  glCullFace(GL_FRONT_AND_BACK);
  GLint value = 0;
  glGetIntegerv(GL_CULL_FACE_MODE, &value);
  EXPECT_EQ(value, GL_FRONT_AND_BACK);
  glFrontFace(GL_CW);
  glGetIntegerv(GL_FRONT_FACE, &value);
  EXPECT_EQ(value, GL_CW);

  glViewport(0, 0, -10, 10);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glanceasyDestroyContext();
}

TEST(PixelStoreStateIsTracked) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  glPixelStorei(GL_PACK_ALIGNMENT, 8);
  GLint value = 0;
  glGetIntegerv(GL_PACK_ALIGNMENT, &value);
  EXPECT_EQ(value, 8);
  glPixelStorei(GL_PACK_ALIGNMENT, 3);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 5);
  glGetIntegerv(GL_UNPACK_ROW_LENGTH, &value);
  EXPECT_EQ(value, 5);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 2);
  glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &value);
  EXPECT_EQ(value, 2);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, -1);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glPixelStorei(0x1234, 1);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glanceasyDestroyContext();
}

TEST(ClearValuesAndHintsSurviveStateChanges) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  glClearColor(0.1f, 0.2f, 0.3f, 0.4f);
  GLfloat colour[4] = {};
  glGetFloatv(GL_COLOR_CLEAR_VALUE, colour);
  EXPECT_NEAR(colour[0], 0.1f, 0.005f);
  EXPECT_NEAR(colour[2], 0.3f, 0.005f);
  glClearStencil(7);
  GLint value = 0;
  glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &value);
  EXPECT_EQ(value, 7);
  glClearDepthf(0.5f);
  GLfloat depth = 0.0f;
  glGetFloatv(GL_DEPTH_CLEAR_VALUE, &depth);
  EXPECT_NEAR(depth, 0.5f, 0.001f);

  glHint(GL_FRAGMENT_SHADER_DERIVATIVE_HINT, GL_NICEST);
  glHint(GL_GENERATE_MIPMAP_HINT, GL_FASTEST);
  glHint(0x1234, GL_FASTEST);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glFlush();
  glFinish();
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glanceasyDestroyContext();
}

TEST(ErrorFlagIsLatchedAndClearedByTheQuery) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glEnable(0x1234);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  // The first error sticks until it is read.
  glEnable(0x1234);
  glViewport(-1, 0, 4, 4);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glanceasyDestroyContext();
}

TEST(ClearHonoursTheColourMask) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
  glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_FALSE);
  glClear(GL_COLOR_BUFFER_BIT);
  unsigned char rgba[4] = {};
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  EXPECT_EQ(rgba[0], 255);
  EXPECT_EQ(rgba[1], 0);
  EXPECT_EQ(rgba[3], 0);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glanceasyDestroyContext();
}

TEST(ScissorClipsClearAndDraw) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_SCISSOR_TEST);
  glScissor(0, 0, 16, 16);
  glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  unsigned char inside[4] = {};
  unsigned char outside[4] = {};
  // Scissor 0,0,16,16 is anchored at the bottom left of the viewport, which
  // is the last rows of the top down framebuffer.
  glReadPixels(8, 24, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, inside);
  glReadPixels(24, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, outside);
  EXPECT_EQ(inside[0], 255);
  EXPECT_EQ(inside[2], 0);
  EXPECT_EQ(outside[0], 0);
  EXPECT_EQ(outside[2], 255);
  glanceasyDestroyContext();
}

TEST(CullingRemovesBackFacingTriangles) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  const char* vertex =
      "#version 100\n"
      "attribute vec2 a_pos;\n"
      "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
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

  // The triangle is counter clockwise in NDC, which is the front face by
  // default.  Declaring clockwise as front facing culls it.
  const float triangle[6] = {-0.5f, -0.5f, 0.5f, -0.5f, 0.0f, 0.5f};
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, triangle);
  glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glFrontFace(GL_CW);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  unsigned char culled[4] = {};
  glReadPixels(16, 12, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, culled);
  EXPECT_EQ(culled[0], 0);
  glCullFace(GL_FRONT);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  unsigned char drawn[4] = {};
  glReadPixels(16, 12, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, drawn);
  EXPECT_EQ(drawn[0], 255);
  glDeleteProgram(program);
  glanceasyDestroyContext();
}

TEST(BlendingMixesFragmentsWithDestination) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  const char* vertex =
      "#version 100\n"
      "attribute vec2 a_pos;\n"
      "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "void main() { gl_FragColor = vec4(1.0, 0.0, 0.0, 0.5); }\n";
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
  glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  unsigned char rgba[4] = {};
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  EXPECT_TRUE(rgba[0] > 120 && rgba[0] < 135);
  EXPECT_TRUE(rgba[2] > 120 && rgba[2] < 135);
  glDeleteProgram(program);
  glanceasyDestroyContext();
}

TEST(StencilTestMasksFragments) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  const char* vertex =
      "#version 100\n"
      "attribute vec2 a_pos;\n"
      "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
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

  const float quad[6] = {-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f};
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClearStencil(0);
  glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  glEnable(GL_STENCIL_TEST);
  glStencilFunc(GL_NOTEQUAL, 0, 0xFF);
  glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  unsigned char rgba[4] = {};
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  EXPECT_EQ(rgba[0], 0);

  glStencilFunc(GL_ALWAYS, 0, 0xFF);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  EXPECT_EQ(rgba[0], 255);
  glDeleteProgram(program);
  glanceasyDestroyContext();
}

TEST(DepthTestCanBeDisabledPerDraw) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
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

  const float far_triangle[9] = {-1.0f, -1.0f, 0.9f, 3.0f, -1.0f,
                                 0.9f,  -1.0f, 3.0f, 0.9f};
  const float near_triangle[9] = {-0.5f, -0.5f, 0.1f, 0.5f, -0.5f,
                                  0.1f,  -0.5f, 0.5f, 0.1f};
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, far_triangle);
  glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
  glClearDepthf(1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, near_triangle);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  unsigned char rgba[4] = {};
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  EXPECT_EQ(rgba[0], 255);

  glDisable(GL_DEPTH_TEST);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, far_triangle);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  EXPECT_EQ(rgba[0], 255);
  glDeleteProgram(program);
  glanceasyDestroyContext();
}

TEST(RasterizerDiscardSuppressesDrawing) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  const char* vertex =
      "#version 100\n"
      "attribute vec2 a_pos;\n"
      "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
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

  const float triangle[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, triangle);
  glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_RASTERIZER_DISCARD);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  unsigned char rgba[4] = {};
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  EXPECT_EQ(rgba[0], 0);
  glDisable(GL_RASTERIZER_DISCARD);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  EXPECT_EQ(rgba[0], 255);
  glDeleteProgram(program);
  glanceasyDestroyContext();
}

TEST(PolygonOffsetShiftsDepth) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
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

  const float near_triangle[9] = {-0.5f, -0.5f, 0.2f, 0.5f, -0.5f,
                                  0.2f,  -0.5f, 0.5f, 0.2f};
  const float far_triangle[9] = {-0.5f, -0.5f, 0.2f, 0.5f, -0.5f,
                                 0.2f,  -0.5f, 0.5f, 0.2f};
  glEnableVertexAttribArray(0);
  glEnable(GL_DEPTH_TEST);
  glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
  glClearDepthf(1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, near_triangle);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glDepthFunc(GL_LEQUAL);
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(-1.0f, -1.0f);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, far_triangle);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  unsigned char rgba[4] = {};
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  EXPECT_EQ(rgba[0], 255);
  glDeleteProgram(program);
  glanceasyDestroyContext();
}

TEST(PointSizeAndLineWidthAreClamped) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  glLineWidth(2.5f);
  GLfloat width = 0.0f;
  glGetFloatv(GL_LINE_WIDTH, &width);
  EXPECT_NEAR(width, 2.5f, 0.001f);
  glLineWidth(-1.0f);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glLineWidth(0.0f);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glanceasyDestroyContext();
}

TEST(SampleCoverageIsAcceptedAsNoOp) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  glEnable(GL_SAMPLE_COVERAGE);
  glSampleCoverage(0.5f, GL_FALSE);
  GLfloat value = 0.0f;
  glGetFloatv(GL_SAMPLE_COVERAGE_VALUE, &value);
  EXPECT_NEAR(value, 0.5f, 0.001f);
  GLint invert = 0;
  glGetIntegerv(GL_SAMPLE_COVERAGE_INVERT, &invert);
  EXPECT_EQ(invert, 0);
  glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
  EXPECT_TRUE(glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE));
  glanceasyDestroyContext();
}
