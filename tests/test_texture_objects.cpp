// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "GLES3/gl32.h"
#include "tests/glanceasy_test.h"
#include "glanceasy/glanceasy.h"

namespace {

enum : int {
  kUnknownPname = 0x0BEE,
  kUnknownEnum = 0x0BEF,
};

}  // namespace

TEST(TextureNamesFollowTheObjectLifecycle) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  GLuint textures[2] = {0, 0};
  glGenTextures(2, textures);
  EXPECT_TRUE(textures[0] != 0);
  EXPECT_TRUE(textures[0] != textures[1]);
  EXPECT_EQ(glIsTexture(textures[0]), GL_TRUE);
  EXPECT_EQ(glIsTexture(0), GL_FALSE);
  glGenTextures(-1, textures);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glDeleteTextures(-1, textures);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glDeleteTextures(2, textures);
  EXPECT_EQ(glIsTexture(textures[0]), GL_FALSE);

  glBindTexture(kUnknownEnum, 0);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glBindTexture(GL_TEXTURE_2D, 0);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glActiveTexture(GL_TEXTURE0 + 32);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glActiveTexture(GL_TEXTURE0 + 31);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glActiveTexture(GL_TEXTURE0);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glBindTexture(GL_TEXTURE_2D, texture);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glDeleteTextures(1, &texture);
  glanceasyDestroyContext();
}

TEST(TextureParametersValidateAndRoundTrip) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, kUnknownEnum);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                  GL_NEAREST_MIPMAP_LINEAR);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, kUnknownEnum);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, kUnknownEnum);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, kUnknownEnum);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glTexParameteri(GL_TEXTURE_2D, kUnknownPname, 0);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE,
                  GL_COMPARE_REF_TO_TEXTURE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_IMMUTABLE_FORMAT, GL_TRUE);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 1);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 3);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  GLint value = 0;
  glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &value);
  EXPECT_EQ(value, GL_LINEAR);
  glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &value);
  EXPECT_EQ(value, GL_NEAREST);
  glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &value);
  EXPECT_EQ(value, GL_CLAMP_TO_EDGE);
  glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &value);
  EXPECT_EQ(value, GL_MIRRORED_REPEAT);
  glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, &value);
  EXPECT_EQ(value, GL_CLAMP_TO_BORDER);
  glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, &value);
  EXPECT_EQ(value, 1);
  glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, &value);
  EXPECT_EQ(value, 3);
  glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_IMMUTABLE_FORMAT, &value);
  EXPECT_EQ(value, GL_FALSE);
  glGetTexParameteriv(GL_TEXTURE_2D, kUnknownPname, &value);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);

  GLfloat float_value = 0.0f;
  glGetTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &float_value);
  EXPECT_NEAR(float_value, static_cast<float>(GL_LINEAR), 0.001f);
  GLint min_filter = GL_LINEAR;
  GLfloat min_filter_float = static_cast<GLfloat>(GL_LINEAR);
  glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &min_filter);
  glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &min_filter_float);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glDeleteTextures(1, &texture);
  glanceasyDestroyContext();
}

TEST(TexSubImageValidatesEveryArgument) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  const unsigned char texel[4] = {1, 2, 3, 4};
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               nullptr);
  glTexSubImage2D(GL_TEXTURE_2D, -1, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                  texel);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glTexSubImage2D(GL_TEXTURE_2D, 1, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                  texel);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glTexSubImage2D(GL_TEXTURE_2D, 0, -1, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                  texel);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 5, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                  texel);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, kUnknownEnum, GL_UNSIGNED_BYTE,
                  texel);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, kUnknownEnum, texel);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                  nullptr);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glDeleteTextures(1, &texture);
  glanceasyDestroyContext();
}

TEST(TexSubImage3DUploadsEachSlice) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_3D, texture);
  glTexStorage3D(GL_TEXTURE_3D, 1, GL_RGBA8, 2, 2, 3);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  unsigned char slice[2 * 2 * 4] = {};
  for (unsigned int i = 0; i < sizeof(slice); i += 4) {
    slice[i] = 200;
    slice[i + 3] = 255;
  }
  glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 1, 2, 2, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                  slice);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glTexSubImage3D(GL_TEXTURE_3D, 1, 0, 0, 0, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                  slice);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 3, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                  slice);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glTexSubImage3D(GL_TEXTURE_2D, 0, 0, 0, 0, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                  slice);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glDeleteTextures(1, &texture);
  glanceasyDestroyContext();
}

TEST(TextureStorageValidatesLevelsAndTargets) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexStorage2D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8, 4, 4);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glTexStorage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glTexStorage2D(GL_TEXTURE_2D, 4, GL_RGBA8, 4, 4);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glTexStorage2D(GL_TEXTURE_2D, 1, kUnknownEnum, 4, 4);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 4, 4);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 4, 4);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);

  GLuint cube = 0;
  glGenTextures(1, &cube);
  glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
  glTexStorage2D(GL_TEXTURE_CUBE_MAP, 1, GL_RGBA8, 4, 4);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  GLint immutable = 0;
  glGetTexParameteriv(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_IMMUTABLE_FORMAT,
                      &immutable);
  EXPECT_EQ(immutable, GL_TRUE);
  glDeleteTextures(1, &cube);

  GLuint volume = 0;
  glGenTextures(1, &volume);
  glBindTexture(GL_TEXTURE_3D, volume);
  glTexStorage3D(GL_TEXTURE_2D, 1, GL_RGBA8, 4, 4, 4);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glTexStorage3D(GL_TEXTURE_3D, 1, GL_RGBA8, 4, 4, 0);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glDeleteTextures(1, &texture);
  glDeleteTextures(1, &volume);
  glanceasyDestroyContext();
}

TEST(CompressedFormatsDecodeAndRejectUnknownEncodings) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  const unsigned char block[8] = {0xFF, 0x00, 0x00, 0x00, 0, 0, 0, 0};
  glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB8_ETC2, 4, 4, 0,
                         8, block);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  GLint compressed = 0;
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED, &compressed);
  EXPECT_EQ(compressed, 1);
  // DXT1 is not part of the supported ETC2/EAC subset.
  glCompressedTexImage2D(GL_TEXTURE_2D, 0, 0x83F0, 4, 4, 0, 8, block);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glDeleteTextures(1, &texture);
  glanceasyDestroyContext();
}

TEST(TexLevelQueriesReportFormatSizes) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 8, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               nullptr);
  GLint value = 0;
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &value);
  EXPECT_EQ(value, 8);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &value);
  EXPECT_EQ(value, 4);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_DEPTH, &value);
  EXPECT_EQ(value, 1);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT,
                           &value);
  EXPECT_EQ(value, GL_RGBA8);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_RED_SIZE, &value);
  EXPECT_EQ(value, 8);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_GREEN_SIZE, &value);
  EXPECT_EQ(value, 8);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_BLUE_SIZE, &value);
  EXPECT_EQ(value, 8);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_ALPHA_SIZE, &value);
  EXPECT_EQ(value, 8);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED, &value);
  EXPECT_EQ(value, 0);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 2, GL_TEXTURE_WIDTH, &value);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, kUnknownPname, &value);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB565, 2, 2, 0, GL_RGB,
               GL_UNSIGNED_SHORT_5_6_5, nullptr);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_GREEN_SIZE, &value);
  EXPECT_EQ(value, 6);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, 2, 2, 0, GL_DEPTH_STENCIL,
               GL_UNSIGNED_INT_24_8, nullptr);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_DEPTH_SIZE, &value);
  EXPECT_EQ(value, 24);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_STENCIL_SIZE, &value);
  EXPECT_EQ(value, 8);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 1, 1, 0, GL_ALPHA, GL_UNSIGNED_BYTE,
               nullptr);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_ALPHA_SIZE, &value);
  EXPECT_EQ(value, 8);
  glDeleteTextures(1, &texture);
  glanceasyDestroyContext();
}

TEST(SamplerObjectsStoreAndReportState) {
  EXPECT_EQ(glanceasyCreateContext(32, 32), 1);
  GLuint samples[2] = {0, 0};
  glGenSamplers(2, samples);
  EXPECT_TRUE(samples[0] != 0);
  EXPECT_EQ(glIsSampler(samples[0]), GL_TRUE);
  EXPECT_EQ(glIsSampler(0), GL_FALSE);
  glGenSamplers(-1, samples);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);

  glBindSampler(99, 0);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glBindSampler(0, 999);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glBindSampler(0, samples[0]);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glSamplerParameteri(999, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glSamplerParameteri(samples[0], GL_TEXTURE_MIN_FILTER, kUnknownEnum);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glSamplerParameteri(samples[0], GL_TEXTURE_MAG_FILTER,
                      GL_LINEAR_MIPMAP_LINEAR);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glSamplerParameteri(samples[0], kUnknownPname, 0);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);

  glSamplerParameteri(samples[0], GL_TEXTURE_MIN_FILTER,
                      GL_NEAREST_MIPMAP_LINEAR);
  glSamplerParameteri(samples[0], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glSamplerParameteri(samples[0], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glSamplerParameteri(samples[0], GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
  glSamplerParameteri(samples[0], GL_TEXTURE_WRAP_R, GL_REPEAT);
  glSamplerParameteri(samples[0], GL_TEXTURE_COMPARE_MODE,
                      GL_COMPARE_REF_TO_TEXTURE);
  glSamplerParameteri(samples[0], GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
  glSamplerParameterf(samples[0], GL_TEXTURE_MIN_LOD, 1.5f);
  glSamplerParameterf(samples[0], GL_TEXTURE_MAX_LOD, 4.5f);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  GLint value = 0;
  glGetSamplerParameteriv(samples[0], GL_TEXTURE_MIN_FILTER, &value);
  EXPECT_EQ(value, GL_NEAREST_MIPMAP_LINEAR);
  glGetSamplerParameteriv(samples[0], GL_TEXTURE_MAG_FILTER, &value);
  EXPECT_EQ(value, GL_LINEAR);
  glGetSamplerParameteriv(samples[0], GL_TEXTURE_WRAP_S, &value);
  EXPECT_EQ(value, GL_CLAMP_TO_EDGE);
  glGetSamplerParameteriv(samples[0], GL_TEXTURE_WRAP_T, &value);
  EXPECT_EQ(value, GL_MIRRORED_REPEAT);
  glGetSamplerParameteriv(samples[0], GL_TEXTURE_COMPARE_MODE, &value);
  EXPECT_EQ(value, GL_COMPARE_REF_TO_TEXTURE);
  glGetSamplerParameteriv(samples[0], GL_TEXTURE_COMPARE_FUNC, &value);
  EXPECT_EQ(value, GL_LEQUAL);
  glGetSamplerParameteriv(samples[0], kUnknownPname, &value);
  EXPECT_EQ(glGetError(), GL_INVALID_ENUM);
  glGetSamplerParameteriv(999, GL_TEXTURE_MAG_FILTER, &value);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);

  GLfloat float_value = 0.0f;
  glGetSamplerParameterfv(samples[0], GL_TEXTURE_MIN_LOD, &float_value);
  EXPECT_NEAR(float_value, 1.5f, 0.001f);
  glGetSamplerParameterfv(samples[0], GL_TEXTURE_MAX_LOD, &float_value);
  EXPECT_NEAR(float_value, 4.5f, 0.001f);
  glGetSamplerParameterfv(samples[0], GL_TEXTURE_MAG_FILTER, &float_value);
  EXPECT_NEAR(float_value, static_cast<float>(GL_LINEAR), 0.001f);
  glGetSamplerParameterfv(999, GL_TEXTURE_MAG_FILTER, &float_value);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);

  GLuint values[2] = {samples[0], samples[1]};
  GLint valid_filter = GL_NEAREST;
  GLfloat valid_lod = 0.0f;
  glSamplerParameteriv(samples[0], GL_TEXTURE_MIN_FILTER, &valid_filter);
  glSamplerParameterfv(samples[0], GL_TEXTURE_MIN_LOD, &valid_lod);
  glDeleteSamplers(-1, values);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glDeleteSamplers(2, values);
  EXPECT_EQ(glIsSampler(samples[0]), GL_FALSE);
  glanceasyDestroyContext();
}
