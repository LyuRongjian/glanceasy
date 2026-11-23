// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Direct unit tests for the software texture pipeline: upload conversion for
// every accepted format/type pair, wrap modes, filtering, mip selection, cube
// face selection and mipmap generation.

#include <stdint.h>
#include <string.h>

#include <vector>

#include "GLES3/gl32.h"
#include "src/zgl_context.h"
#include "src/zgl_texture.h"
#include "tests/zgl_test.h"

namespace {

using zgl::GenerateMipmap;
using zgl::SampleTexture2D;
using zgl::SampleTexture3D;
using zgl::SampleTextureCube;
using zgl::Texture;
using zgl::TextureImage;
using zgl::UploadTextureImage;

void Make2D(Texture* texture, int width, int height, int levels) {
  texture->target = GL_TEXTURE_2D;
  texture->width = width;
  texture->height = height;
  texture->levels = levels;
  texture->Resize(levels, 1);
}

void MakeCube(Texture* texture, int size) {
  texture->target = GL_TEXTURE_CUBE_MAP;
  texture->width = size;
  texture->height = size;
  texture->levels = 1;
  texture->Resize(1, 6);
}

void Make3D(Texture* texture, int width, int height, int depth) {
  texture->target = GL_TEXTURE_3D;
  texture->width = width;
  texture->height = height;
  texture->depth = depth;
  texture->levels = 1;
  texture->Resize(1, 1);
}

bool Upload(TextureImage* image,
            const std::vector<uint8_t>& pixels,
            GLenum format,
            GLenum type) {
  return UploadTextureImage(image, 2, 2, format, type, 0, 0, 0,
                            pixels.empty() ? nullptr : pixels.data());
}

void ExpectColour(const float rgba[4],
                  uint8_t r,
                  uint8_t g,
                  uint8_t b,
                  uint8_t a,
                  float tolerance) {
  EXPECT_NEAR(rgba[0], static_cast<float>(r) / 255.0f, tolerance);
  EXPECT_NEAR(rgba[1], static_cast<float>(g) / 255.0f, tolerance);
  EXPECT_NEAR(rgba[2], static_cast<float>(b) / 255.0f, tolerance);
  EXPECT_NEAR(rgba[3], static_cast<float>(a) / 255.0f, tolerance);
}

}  // namespace

TEST(UploadConvertsEveryFormatAndType) {
  std::vector<uint8_t> pixels(2 * 2 * 4 * 4, 0);
  for (size_t i = 0; i < pixels.size(); i += 4) {
    pixels[i] = 255;
    pixels[i + 1] = 128;
    pixels[i + 2] = 64;
    pixels[i + 3] = 32;
  }
  Texture texture;
  Make2D(&texture, 2, 2, 1);
  TextureImage* image = texture.mutable_image(0, 0);

  const GLenum formats[] = {
      GL_RGBA,           GL_RGB, GL_RG, GL_RED, GL_ALPHA, GL_LUMINANCE,
      GL_LUMINANCE_ALPHA};
  const GLenum types[] = {GL_UNSIGNED_BYTE, GL_BYTE,         GL_UNSIGNED_SHORT,
                          GL_SHORT,         GL_UNSIGNED_INT, GL_INT,
                          GL_FLOAT,         GL_HALF_FLOAT};
  for (GLenum format : formats) {
    for (GLenum type : types) {
      EXPECT_TRUE(Upload(image, pixels, format, type));
      EXPECT_TRUE(!image->rgba.empty());
    }
  }

  EXPECT_TRUE(Upload(image, pixels, GL_RGBA, GL_UNSIGNED_BYTE));
  float rgba[4];
  zgl::UnpackRgba(image->rgba[0], rgba);
  ExpectColour(rgba, 255, 128, 64, 32, 0.01f);

  // GL_BYTE uses the signed normalised range, so -128 clamps to zero.
  std::vector<uint8_t> signed_pixels(2 * 2 * 4, 0);
  for (size_t i = 0; i < signed_pixels.size(); i += 4) {
    signed_pixels[i] = 0x80;
    signed_pixels[i + 3] = 0x7F;
  }
  EXPECT_TRUE(Upload(image, signed_pixels, GL_RGBA, GL_BYTE));
  zgl::UnpackRgba(image->rgba[0], rgba);
  EXPECT_NEAR(rgba[0], 0.0f, 0.01f);
  EXPECT_NEAR(rgba[3], 1.0f, 0.01f);

  // Half floats: 0x3C00 is 1.0.
  std::vector<uint8_t> half_pixels(2 * 2 * 4 * 2, 0);
  for (size_t i = 0; i < half_pixels.size(); i += 8) {
    half_pixels[i] = 0x00;
    half_pixels[i + 1] = 0x3C;
  }
  EXPECT_TRUE(Upload(image, half_pixels, GL_RED, GL_HALF_FLOAT));
  zgl::UnpackRgba(image->rgba[0], rgba);
  EXPECT_NEAR(rgba[0], 1.0f, 0.01f);

  // Float input is copied without normalisation.
  std::vector<float> float_pixels(2 * 2 * 4, 0.0f);
  for (size_t i = 0; i < float_pixels.size(); i += 4) {
    float_pixels[i] = 0.25f;
    float_pixels[i + 3] = 0.5f;
  }
  EXPECT_TRUE(UploadTextureImage(image, 2, 2, GL_RGBA, GL_FLOAT, 0, 0, 0,
                                 float_pixels.data()));
  zgl::UnpackRgba(image->rgba[0], rgba);
  EXPECT_NEAR(rgba[0], 0.25f, 0.01f);
  EXPECT_NEAR(rgba[3], 0.5f, 0.01f);
}

TEST(UploadDecodesPackedFormats) {
  Texture texture;
  Make2D(&texture, 2, 2, 1);
  TextureImage* image = texture.mutable_image(0, 0);
  std::vector<uint8_t> pixels(2 * 2 * 2, 0);
  float rgba[4];

  uint16_t packed = 0xF800u;  // 5_6_5: pure red.
  memcpy(pixels.data(), &packed, sizeof(packed));
  EXPECT_TRUE(Upload(image, pixels, GL_RGB, GL_UNSIGNED_SHORT_5_6_5));
  zgl::UnpackRgba(image->rgba[0], rgba);
  ExpectColour(rgba, 255, 0, 0, 255, 0.02f);

  packed = 0x000Fu;  // 4_4_4_4: full alpha only.
  memcpy(pixels.data(), &packed, sizeof(packed));
  EXPECT_TRUE(Upload(image, pixels, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4));
  zgl::UnpackRgba(image->rgba[0], rgba);
  ExpectColour(rgba, 0, 0, 0, 255, 0.02f);

  packed = 0xF801u;  // 5_5_5_1: full red with the alpha bit set.
  memcpy(pixels.data(), &packed, sizeof(packed));
  EXPECT_TRUE(Upload(image, pixels, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1));
  zgl::UnpackRgba(image->rgba[0], rgba);
  ExpectColour(rgba, 255, 0, 0, 255, 0.02f);

  packed = 0x0000u;
  memcpy(pixels.data(), &packed, sizeof(packed));
  EXPECT_TRUE(Upload(image, pixels, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1));
  zgl::UnpackRgba(image->rgba[0], rgba);
  ExpectColour(rgba, 0, 0, 0, 0, 0.02f);
}

TEST(UploadStoresDepthAndStencilPayloads) {
  Texture texture;
  Make2D(&texture, 2, 1, 1);
  TextureImage* image = texture.mutable_image(0, 0);

  const uint16_t depth16[2] = {0xFFFF, 0x0000};
  EXPECT_TRUE(UploadTextureImage(image, 2, 1, GL_DEPTH_COMPONENT,
                                 GL_UNSIGNED_SHORT, 0, 0, 0, depth16));
  EXPECT_EQ(image->depth16[0], 0xFFFFu);
  EXPECT_EQ(image->depth16[1], 0x0000u);

  const uint32_t depth32[2] = {0xFFFF0000u, 0x80000000u};
  EXPECT_TRUE(UploadTextureImage(image, 2, 1, GL_DEPTH_COMPONENT,
                                 GL_UNSIGNED_INT, 0, 0, 0, depth32));
  EXPECT_EQ(image->depth16[0], 0xFFFFu);
  EXPECT_EQ(image->depth16[1], 0x8000u);

  const float depth_float[2] = {1.0f, 0.0f};
  EXPECT_TRUE(UploadTextureImage(image, 2, 1, GL_DEPTH_COMPONENT, GL_FLOAT, 0,
                                 0, 0, depth_float));
  EXPECT_EQ(image->depth16[0], 0xFFFFu);
  EXPECT_EQ(image->depth16[1], 0x0000u);

  const uint32_t depth_stencil[2] = {0xFFFF00FFu, 0x00000000u};
  EXPECT_TRUE(UploadTextureImage(image, 2, 1, GL_DEPTH_STENCIL,
                                 GL_UNSIGNED_INT_24_8, 0, 0, 0, depth_stencil));
  EXPECT_EQ(image->depth16[0], 0xFFFFu);
  EXPECT_EQ(image->stencil8[0], 0xFFu);
  EXPECT_EQ(image->depth16[1], 0x0000u);
}

TEST(UploadAppliesPixelStorageOffsets) {
  Texture texture;
  Make2D(&texture, 2, 2, 1);
  TextureImage* image = texture.mutable_image(0, 0);
  std::vector<uint8_t> pixels(4 * 4 * 4, 0);
  for (size_t i = 0; i < pixels.size(); i += 4) {
    pixels[i] = static_cast<uint8_t>(i / 4 + 1);
    pixels[i + 3] = 255;
  }
  EXPECT_TRUE(UploadTextureImage(image, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, 4, 1,
                                 1, pixels.data()));
  float rgba[4];
  zgl::UnpackRgba(image->rgba[0], rgba);
  // Row 1, pixel 1 -> index 4 + 1 = 5 of the sequence 1..16.
  EXPECT_NEAR(rgba[0], 5.0f / 255.0f, 0.01f);
  EXPECT_TRUE(image->rgba.size() == 4);

  EXPECT_FALSE(UploadTextureImage(image, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, 0, 0,
                                  0, nullptr));
  EXPECT_FALSE(UploadTextureImage(image, 0, 2, GL_RGBA, GL_UNSIGNED_BYTE, 0, 0,
                                  0, pixels.data()));
  EXPECT_FALSE(UploadTextureImage(image, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, 0, 0,
                                  0, nullptr));
}

TEST(WrapModesClampRepeatAndMirror) {
  Texture texture;
  Make2D(&texture, 2, 2, 1);
  TextureImage* image = texture.mutable_image(0, 0);
  image->width = 2;
  image->height = 2;
  image->rgba.assign(4, zgl::PackRgba(255, 0, 0, 255));
  float rgba[4];

  SampleTexture2D(texture, 1.25f, 0.5f, -1.0f, GL_NEAREST, GL_NEAREST,
                  GL_REPEAT, GL_CLAMP_TO_EDGE, rgba);
  ExpectColour(rgba, 255, 0, 0, 255, 0.02f);

  SampleTexture2D(texture, 1.25f, 0.5f, -1.0f, GL_NEAREST, GL_NEAREST,
                  GL_MIRRORED_REPEAT, GL_CLAMP_TO_EDGE, rgba);
  ExpectColour(rgba, 255, 0, 0, 255, 0.02f);

  SampleTexture2D(texture, -0.5f, 0.5f, -1.0f, GL_NEAREST, GL_NEAREST,
                  GL_CLAMP_TO_BORDER, GL_CLAMP_TO_EDGE, rgba);
  ExpectColour(rgba, 0, 0, 0, 255, 0.02f);

  SampleTexture2D(texture, -0.5f, 0.5f, -1.0f, GL_NEAREST, GL_NEAREST,
                  GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, rgba);
  ExpectColour(rgba, 255, 0, 0, 255, 0.02f);
}

TEST(BilinearFilteringBlendsNeighbours) {
  Texture texture;
  Make2D(&texture, 2, 2, 1);
  TextureImage* image = texture.mutable_image(0, 0);
  image->width = 2;
  image->height = 2;
  image->rgba = {zgl::PackRgba(255, 0, 0, 255), zgl::PackRgba(0, 255, 0, 255),
                 zgl::PackRgba(0, 0, 255, 255),
                 zgl::PackRgba(255, 255, 255, 255)};
  float rgba[4];
  SampleTexture2D(texture, 0.5f, 0.5f, -1.0f, GL_LINEAR, GL_LINEAR, GL_REPEAT,
                  GL_REPEAT, rgba);
  ExpectColour(rgba, 128, 128, 128, 255, 0.03f);

  SampleTexture2D(texture, 0.25f, 0.5f, -1.0f, GL_LINEAR, GL_LINEAR, GL_REPEAT,
                  GL_REPEAT, rgba);
  ExpectColour(rgba, 128, 0, 128, 255, 0.03f);
}

TEST(MipSelectionUsesTheRequestedLevel) {
  Texture texture;
  Make2D(&texture, 4, 4, 3);
  const uint32_t colours[3] = {zgl::PackRgba(255, 0, 0, 255),
                               zgl::PackRgba(0, 255, 0, 255),
                               zgl::PackRgba(0, 0, 255, 255)};
  for (int level = 0; level < 3; ++level) {
    TextureImage* image = texture.mutable_image(level, 0);
    const int size = 4 >> level;
    image->width = size;
    image->height = size;
    image->rgba.assign(static_cast<size_t>(size) * size, colours[level]);
  }
  EXPECT_TRUE(texture.IsComplete());
  float rgba[4];
  SampleTexture2D(texture, 0.5f, 0.5f, 1.9f, GL_NEAREST_MIPMAP_NEAREST,
                  GL_NEAREST, GL_REPEAT, GL_REPEAT, rgba);
  ExpectColour(rgba, 0, 255, 0, 255, 0.02f);
  SampleTexture2D(texture, 0.5f, 0.5f, 0.2f, GL_NEAREST_MIPMAP_NEAREST,
                  GL_NEAREST, GL_REPEAT, GL_REPEAT, rgba);
  ExpectColour(rgba, 255, 0, 0, 255, 0.02f);

  // A texture whose level zero is empty samples as transparent black.
  Texture incomplete;
  Make2D(&incomplete, 4, 4, 1);
  incomplete.mutable_image(0, 0)->rgba.clear();
  EXPECT_FALSE(incomplete.IsComplete());
  SampleTexture2D(incomplete, 0.5f, 0.5f, 0.0f, GL_NEAREST, GL_NEAREST,
                  GL_REPEAT, GL_REPEAT, rgba);
  ExpectColour(rgba, 0, 0, 0, 255, 0.02f);
}

TEST(CubeSamplingSelectsTheMajorAxisFace) {
  Texture texture;
  MakeCube(&texture, 1);
  const uint32_t colours[6] = {
      zgl::PackRgba(255, 0, 0, 255),   zgl::PackRgba(0, 255, 0, 255),
      zgl::PackRgba(0, 0, 255, 255),   zgl::PackRgba(255, 255, 0, 255),
      zgl::PackRgba(255, 0, 255, 255), zgl::PackRgba(0, 255, 255, 255)};
  for (int face = 0; face < 6; ++face) {
    TextureImage* image = texture.mutable_image(0, face);
    image->width = 1;
    image->height = 1;
    image->rgba.assign(1, colours[face]);
  }
  EXPECT_TRUE(texture.IsComplete());
  float rgba[4];
  const float directions[6][3] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                  {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
  for (int face = 0; face < 6; ++face) {
    SampleTextureCube(texture, directions[face], 0.0f, GL_NEAREST, GL_NEAREST,
                      rgba);
    EXPECT_NEAR(rgba[0], (colours[face] & 0xFFu) / 255.0f, 0.02f);
    EXPECT_NEAR(rgba[1], ((colours[face] >> 8) & 0xFFu) / 255.0f, 0.02f);
    EXPECT_NEAR(rgba[2], ((colours[face] >> 16) & 0xFFu) / 255.0f, 0.02f);
  }
  SampleTextureCube(texture, directions[0], 1.0f, GL_NEAREST, GL_NEAREST, rgba);
  EXPECT_NEAR(rgba[0], 1.0f, 0.02f);
}

TEST(ThreeDimensionalSamplingBlendsSlices) {
  Texture texture;
  Make3D(&texture, 1, 1, 2);
  TextureImage* image = texture.mutable_image(0, 0);
  image->width = 1;
  image->height = 1;
  image->depth = 2;
  image->rgba = {zgl::PackRgba(255, 0, 0, 255), zgl::PackRgba(0, 0, 255, 255)};
  EXPECT_TRUE(texture.IsComplete());
  float rgba[4];
  const float centre[3] = {0.5f, 0.5f, 0.5f};
  SampleTexture3D(texture, centre, 0.0f, GL_LINEAR, GL_LINEAR, GL_REPEAT,
                  GL_REPEAT, GL_REPEAT, rgba);
  ExpectColour(rgba, 128, 0, 128, 255, 0.03f);

  Texture single;
  Make3D(&single, 1, 1, 1);
  TextureImage* one = single.mutable_image(0, 0);
  one->width = 1;
  one->height = 1;
  one->depth = 1;
  one->rgba.assign(1, zgl::PackRgba(10, 20, 30, 255));
  SampleTexture3D(single, centre, 0.0f, GL_NEAREST, GL_NEAREST, GL_REPEAT,
                  GL_REPEAT, GL_REPEAT, rgba);
  ExpectColour(rgba, 10, 20, 30, 255, 0.02f);

  const float outside[3] = {-0.5f, 0.5f, 0.5f};
  SampleTexture3D(single, outside, 0.0f, GL_NEAREST, GL_NEAREST,
                  GL_CLAMP_TO_BORDER, GL_REPEAT, GL_REPEAT, rgba);
  ExpectColour(rgba, 0, 0, 0, 255, 0.02f);
}

TEST(GenerateMipmapAveragesTwoByTwoBlocks) {
  Texture texture;
  Make2D(&texture, 4, 4, 3);
  TextureImage* level0 = texture.mutable_image(0, 0);
  level0->width = 4;
  level0->height = 4;
  level0->rgba.assign(16, zgl::PackRgba(255, 0, 0, 255));
  GenerateMipmap(&texture);
  const TextureImage* level1 = texture.image(1, 0);
  EXPECT_EQ(level1->width, 2);
  EXPECT_EQ(level1->height, 2);
  float rgba[4];
  zgl::UnpackRgba(level1->rgba[0], rgba);
  EXPECT_NEAR(rgba[0], 1.0f, 0.02f);

  const TextureImage* level2 = texture.image(2, 0);
  EXPECT_EQ(level2->width, 1);
  EXPECT_EQ(level2->height, 1);
  zgl::UnpackRgba(level2->rgba[0], rgba);
  EXPECT_NEAR(rgba[0], 1.0f, 0.02f);

  // Degenerate textures and null pointers are ignored.
  GenerateMipmap(nullptr);
  Texture empty;
  GenerateMipmap(&empty);
  EXPECT_EQ(empty.levels, 0);
}
