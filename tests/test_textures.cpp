// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <vector>

#include "GLES3/gl32.h"
#include "tests/zgl_test.h"
#include "zgl/zgl.h"

namespace {

const char* kTexturedVertex =
    "#version 100\n"
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "  v_uv = a_uv;\n"
    "  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

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

GLuint MakeProgram(const char* vertex_source, const char* fragment_source) {
  const GLuint vertex = CompileShader(GL_VERTEX_SHADER, vertex_source);
  const GLuint fragment = CompileShader(GL_FRAGMENT_SHADER, fragment_source);
  const GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glBindAttribLocation(program, 0, "a_pos");
  glBindAttribLocation(program, 1, "a_uv");
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

// A quad with positions and texture coordinates so that texture coordinate
// (0, 0) lands on the lower left corner of the viewport.
const float kQuad[] = {
    -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,  -1.0f, 1.0f, 0.0f,
    1.0f,  1.0f,  1.0f, 1.0f, -1.0f, 1.0f,  0.0f, 1.0f,
};

void DrawQuad(GLuint program) {
  glUseProgram(program);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, kQuad);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, kQuad + 2);
  glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

void ReadPixel(int x, int y, unsigned char rgba[4]) {
  glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

}  // namespace

TEST(TextureFilteringInterpolatesTexels) {
  EXPECT_EQ(zglCreateContext(64, 48), 1);
  const unsigned char texels[16] = {255, 255, 255, 255, 0, 0, 0, 255,
                                    0,   0,   0,   255, 0, 0, 0, 255};
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               texels);

  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "varying vec2 v_uv;\n"
      "uniform sampler2D u_texture;\n"
      "void main() { gl_FragColor = texture2D(u_texture, v_uv); }\n";
  const GLuint program = MakeProgram(kTexturedVertex, fragment);
  glUseProgram(program);
  glUniform1i(glGetUniformLocation(program, "u_texture"), 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  DrawQuad(program);

  // The centre of the quad averages the four texels, one of which is white.
  unsigned char rgba[4] = {};
  ReadPixel(32, 24, rgba);
  EXPECT_TRUE(rgba[0] > 40);
  EXPECT_TRUE(rgba[0] < 120);

  GLint filter = 0;
  glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &filter);
  EXPECT_EQ(filter, GL_LINEAR);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glDeleteProgram(program);
  glDeleteTextures(1, &texture);
  zglDestroyContext();
}

TEST(TextureWrapModesChangeSampling) {
  EXPECT_EQ(zglCreateContext(64, 48), 1);
  const unsigned char texels[8] = {255, 0, 0, 255, 0, 255, 0, 255};
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               texels);

  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "varying vec2 v_uv;\n"
      "uniform sampler2D u_texture;\n"
      "uniform vec2 u_uv;\n"
      "void main() { gl_FragColor = texture2D(u_texture, u_uv); }\n";
  const GLuint program = MakeProgram(kTexturedVertex, fragment);
  glUseProgram(program);
  glUniform1i(glGetUniformLocation(program, "u_texture"), 0);
  GLint uv_location = glGetUniformLocation(program, "u_uv");
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glClear(GL_COLOR_BUFFER_BIT);
  glUniform2f(uv_location, 2.25f, 0.5f);
  DrawQuad(program);
  unsigned char repeated[4] = {};
  ReadPixel(32, 24, repeated);
  EXPECT_TRUE(repeated[0] > 240);  // Wraps back to the red texel.
  EXPECT_TRUE(repeated[1] < 20);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glClear(GL_COLOR_BUFFER_BIT);
  DrawQuad(program);
  unsigned char clamped[4] = {};
  ReadPixel(32, 24, clamped);
  EXPECT_TRUE(clamped[1] > 240);  // Stays on the green edge texel.
  EXPECT_TRUE(clamped[0] < 20);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
  glUniform2f(uv_location, -0.2f, 0.5f);
  glClear(GL_COLOR_BUFFER_BIT);
  DrawQuad(program);
  unsigned char mirrored[4] = {};
  ReadPixel(32, 24, mirrored);
  EXPECT_TRUE(mirrored[0] > 240);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glDeleteProgram(program);
  glDeleteTextures(1, &texture);
  zglDestroyContext();
}

TEST(TextureStorageCanBeUpdatedAndQueried) {
  EXPECT_EQ(zglCreateContext(64, 48), 1);
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexStorage2D(GL_TEXTURE_2D, 3, GL_RGBA8, 4, 4);
  GLint value = 0;
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &value);
  EXPECT_EQ(value, 4);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &value);
  EXPECT_EQ(value, 4);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT,
                           &value);
  EXPECT_EQ(value, GL_RGBA8);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 2, GL_TEXTURE_WIDTH, &value);
  EXPECT_EQ(value, 1);

  const unsigned char patch[4] = {255, 128, 0, 255};
  glTexSubImage2D(GL_TEXTURE_2D, 0, 1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                  patch);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  GLint binding = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
  EXPECT_EQ(binding, static_cast<GLint>(texture));
  glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &value);
  EXPECT_TRUE(value == GL_NEAREST_MIPMAP_LINEAR);

  glGenerateMipmap(GL_TEXTURE_2D);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_WIDTH, &value);
  EXPECT_EQ(value, 2);

  glDeleteTextures(1, &texture);
  EXPECT_EQ(glIsTexture(texture), GL_FALSE);
  zglDestroyContext();
}

TEST(TextureUploadAcceptsPackedAndNarrowFormats) {
  EXPECT_EQ(zglCreateContext(64, 48), 1);
  const unsigned char rgb[12] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0};
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE,
               rgb);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  GLint format = 0;
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT,
                           &format);
  EXPECT_EQ(format, GL_RGB);

  // 5-6-5 packed pixels and a single channel texture are both legal uploads.
  const unsigned short packed[4] = {0xf800, 0x07e0, 0x001f, 0xffff};
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB,
               GL_UNSIGNED_SHORT_5_6_5, packed);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  const unsigned char alpha[4] = {0, 64, 128, 255};
  glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 2, 2, 0, GL_ALPHA, GL_UNSIGNED_BYTE,
               alpha);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glDeleteTextures(1, &texture);
  zglDestroyContext();
}

TEST(TextureObjectsRejectInvalidInput) {
  EXPECT_EQ(zglCreateContext(64, 48), 1);
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, -1, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               nullptr);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, static_cast<GLenum>(0x1234),
               nullptr);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glTexImage3D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               nullptr);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_LINEAR);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  // A name that has already been used as a 2D texture cannot become a cube
  // map, and the rejected bind leaves the previous 2D binding in place.
  glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               nullptr);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glDeleteTextures(1, &texture);
  zglDestroyContext();
}

TEST(CubeMapsSampleSixFaces) {
  EXPECT_EQ(zglCreateContext(64, 48), 1);
  const unsigned char colors[6][4] = {{255, 0, 0, 255},   {0, 255, 0, 255},
                                      {0, 0, 255, 255},   {255, 255, 0, 255},
                                      {255, 0, 255, 255}, {0, 255, 255, 255}};
  const GLenum faces[6] = {
      GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
      GL_TEXTURE_CUBE_MAP_POSITIVE_Y, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
      GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z};
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  for (int i = 0; i < 6; ++i) {
    glTexImage2D(faces[i], 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 colors[i]);
  }
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "uniform samplerCube u_cube;\n"
      "uniform vec3 u_dir;\n"
      "void main() { gl_FragColor = textureCube(u_cube, u_dir); }\n";
  const GLuint program = MakeProgram(kTexturedVertex, fragment);
  glUseProgram(program);
  glUniform1i(glGetUniformLocation(program, "u_cube"), 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
  GLint direction = glGetUniformLocation(program, "u_dir");
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

  glClear(GL_COLOR_BUFFER_BIT);
  glUniform3f(direction, 1.0f, 0.0f, 0.0f);
  DrawQuad(program);
  unsigned char rgba[4] = {};
  ReadPixel(32, 24, rgba);
  EXPECT_TRUE(rgba[0] > 240);
  EXPECT_TRUE(rgba[1] < 20);
  EXPECT_TRUE(rgba[2] < 20);

  glClear(GL_COLOR_BUFFER_BIT);
  glUniform3f(direction, 0.0f, 1.0f, 0.0f);
  DrawQuad(program);
  ReadPixel(32, 24, rgba);
  EXPECT_TRUE(rgba[2] > 240);
  EXPECT_TRUE(rgba[0] < 20);
  EXPECT_TRUE(rgba[1] < 20);

  glClear(GL_COLOR_BUFFER_BIT);
  glUniform3f(direction, 0.0f, 0.0f, -1.0f);
  DrawQuad(program);
  ReadPixel(32, 24, rgba);
  EXPECT_TRUE(rgba[0] < 20);
  EXPECT_TRUE(rgba[1] > 240);
  EXPECT_TRUE(rgba[2] > 240);

  glDeleteProgram(program);
  glDeleteTextures(1, &texture);
  zglDestroyContext();
}

TEST(ThreeDimensionalTexturesSampleLayers) {
  EXPECT_EQ(zglCreateContext(64, 48), 1);
  // Two layers: red at z = 0, green at z = 1.
  const unsigned char texels[8] = {255, 0, 0, 255, 255, 0, 0, 255};
  const unsigned char layer1[8] = {0, 255, 0, 255, 0, 255, 0, 255};
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_3D, texture);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA, 1, 1, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               texels);
  glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                  layer1);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "uniform sampler3D u_volume;\n"
      "uniform vec3 u_pos;\n"
      "void main() { gl_FragColor = texture3D(u_volume, u_pos); }\n";
  const GLuint program = MakeProgram(kTexturedVertex, fragment);
  glUseProgram(program);
  glUniform1i(glGetUniformLocation(program, "u_volume"), 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_3D, texture);
  GLint position = glGetUniformLocation(program, "u_pos");
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

  glClear(GL_COLOR_BUFFER_BIT);
  glUniform3f(position, 0.5f, 0.5f, 0.2f);
  DrawQuad(program);
  unsigned char rgba[4] = {};
  ReadPixel(32, 24, rgba);
  EXPECT_TRUE(rgba[0] > 240);
  EXPECT_TRUE(rgba[1] < 20);

  glClear(GL_COLOR_BUFFER_BIT);
  glUniform3f(position, 0.5f, 0.5f, 0.8f);
  DrawQuad(program);
  ReadPixel(32, 24, rgba);
  EXPECT_TRUE(rgba[1] > 240);
  EXPECT_TRUE(rgba[0] < 20);

  glDeleteProgram(program);
  glDeleteTextures(1, &texture);
  zglDestroyContext();
}

TEST(SamplerObjectsOverrideTextureState) {
  EXPECT_EQ(zglCreateContext(64, 48), 1);
  GLuint sampler = 0;
  glGenSamplers(1, &sampler);
  EXPECT_EQ(glIsSampler(sampler), GL_TRUE);
  glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glSamplerParameterf(sampler, GL_TEXTURE_MIN_LOD, -1.0f);

  GLint value = 0;
  glGetSamplerParameteriv(sampler, GL_TEXTURE_MIN_FILTER, &value);
  EXPECT_EQ(value, GL_LINEAR);
  GLfloat min_lod = 0.0f;
  glGetSamplerParameterfv(sampler, GL_TEXTURE_MIN_LOD, &min_lod);
  EXPECT_EQ(min_lod, -1.0f);

  glBindSampler(0, sampler);
  GLint binding = 0;
  glGetIntegerv(GL_SAMPLER_BINDING, &binding);
  EXPECT_EQ(binding, static_cast<GLint>(sampler));
  glBindSampler(0, 0);
  glGetIntegerv(GL_SAMPLER_BINDING, &binding);
  EXPECT_EQ(binding, 0);

  glDeleteSamplers(1, &sampler);
  EXPECT_EQ(glIsSampler(sampler), GL_FALSE);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  zglDestroyContext();
}

TEST(FloatTextureSamplingPreservesHdrValues) {
  EXPECT_EQ(zglCreateContext(8, 8), 1);
  const float texels[4] = {2.0f, -1.0f, 0.25f, 0.5f};
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1, 1, 0, GL_RGBA, GL_FLOAT,
               texels);
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "varying vec2 v_uv;\n"
      "uniform sampler2D u_texture;\n"
      "void main() {\n"
      "  gl_FragColor = vec4(texture2D(u_texture, v_uv).r * 0.25, 0.0, 0.0,"
      " 1.0);\n"
      "}\n";
  const GLuint program = MakeProgram(kTexturedVertex, fragment);
  glUseProgram(program);
  glUniform1i(glGetUniformLocation(program, "u_texture"), 0);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  DrawQuad(program);
  unsigned char rgba[4] = {};
  ReadPixel(4, 4, rgba);
  // A quantised RGBA8 upload would clamp 2.0 to 1.0 and resolve to 64.
  EXPECT_TRUE(rgba[0] > 120 && rgba[0] < 136);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glDeleteProgram(program);
  glDeleteTextures(1, &texture);
  zglDestroyContext();
}

TEST(CompressedEtc2TextureUploadsAndSamples) {
  EXPECT_EQ(zglCreateContext(8, 8), 1);
  // Solid red in ETC2 individual mode.
  const unsigned char block[8] = {0xFF, 0x00, 0x00, 0x00, 0, 0, 0, 0};
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB8_ETC2, 4, 4, 0,
                         8, block);
  GLint compressed = 0;
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED, &compressed);
  EXPECT_EQ(compressed, 1);
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "varying vec2 v_uv;\n"
      "uniform sampler2D u_texture;\n"
      "void main() { gl_FragColor = texture2D(u_texture, v_uv); }\n";
  const GLuint program = MakeProgram(kTexturedVertex, fragment);
  glUseProgram(program);
  glUniform1i(glGetUniformLocation(program, "u_texture"), 0);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  DrawQuad(program);
  unsigned char rgba[4] = {};
  ReadPixel(4, 4, rgba);
  EXPECT_TRUE(rgba[0] > 240);
  EXPECT_TRUE(rgba[1] < 16);
  EXPECT_TRUE(rgba[2] < 16);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glDeleteProgram(program);
  glDeleteTextures(1, &texture);
  zglDestroyContext();
}
