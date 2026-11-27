// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZGL_SRC_ZGL_TEXTURE_H_
#define ZGL_SRC_ZGL_TEXTURE_H_

#include "src/zgl_context.h"

namespace zgl {

// Reduces a mipmapped filter to its base filter when the LOD happens to be
// zero, which is the only case the CPU rasteriser resolves implicitly.
GLenum BaseFilter(GLenum filter);

// WrapCoordinate, sampling helpers.
float WrapCoordinate(float coordinate, GLenum mode);

// Unfiltered texel read used by texelFetch(); |index| addresses the texel in
// the flattened (layer-major) image row order.
void FetchTexelImage(const TextureImage& image, size_t index, float rgba[4]);

// Samples a bound texture.  |lod| is the level of detail: values <= 0 select
// the base level.  Bilinear filtering, the three wrap modes, 2D, cube and 3D
// textures are supported.
void SampleTexture2D(const Texture& texture,
                     float u,
                     float v,
                     float lod,
                     GLenum min_filter,
                     GLenum mag_filter,
                     GLenum wrap_s,
                     GLenum wrap_t,
                     float out[4],
                     float min_lod = 0.0f,
                     float max_lod = -1.0f);
void SampleTextureCube(const Texture& texture,
                       const float direction[3],
                       float lod,
                       GLenum min_filter,
                       GLenum mag_filter,
                       float out[4],
                       GLenum compare_func = 0,
                       float ref = 0.0f,
                       float min_lod = 0.0f,
                       float max_lod = -1.0f);
// Samples a cube depth texture with GL_LINEAR semantics.  Returns the number
// of samples (4) and fills |depths| / |weights| for compare-before-filter
// shadow lookups.  Returns 0 when the active filter is not GL_LINEAR.
int SampleCubeShadowLinearSamples(const Texture& texture,
                                  const float direction[3],
                                  float lod,
                                  GLenum min_filter,
                                  GLenum mag_filter,
                                  float depths[4],
                                  float weights[4]);
void SampleTexture2DArray(const Texture& texture,
                          float u,
                          float v,
                          float layer,
                          float lod,
                          GLenum min_filter,
                          GLenum mag_filter,
                          GLenum wrap_s,
                          GLenum wrap_t,
                          float out[4],
                          float min_lod = 0.0f,
                          float max_lod = -1.0f);

void SampleTexture3D(const Texture& texture,
                     const float position[3],
                     float lod,
                     GLenum min_filter,
                     GLenum mag_filter,
                     GLenum wrap_s,
                     GLenum wrap_t,
                     GLenum wrap_r,
                     float out[4],
                     float min_lod = 0.0f,
                     float max_lod = -1.0f);

// Copies client pixels into a texture image.  |row_length|, |skip_rows| and
// |skip_pixels| are the unpack parameters already resolved to element counts
// (zero means tightly packed).  Returns false for an unsupported combination.
bool UploadTextureImage(TextureImage* image,
                        int width,
                        int height,
                        GLenum format,
                        GLenum type,
                        int row_length,
                        int skip_rows,
                        int skip_pixels,
                        const void* pixels,
                        int alignment = 4);

// Copies client pixels into a floating-point texture image.  The destination
// must already have |float_rgba| sized for |width| by |height| texels.
bool UploadTextureImageFloat(TextureImage* image,
                             int width,
                             int height,
                             GLenum format,
                             GLenum type,
                             int row_length,
                             int skip_rows,
                             int skip_pixels,
                             const void* pixels,
                             int alignment = 4);

bool UploadTextureImageInteger(TextureImage* image,
                               int width,
                               int height,
                               GLenum format,
                               GLenum type,
                               int row_length,
                               int skip_rows,
                               int skip_pixels,
                               const void* pixels,
                               int alignment = 4);

// Fills the mip chain below level 0 with box-filtered copies of level 0.
void GenerateMipmap(Texture* texture);

}  // namespace zgl

#endif  // ZGL_SRC_ZGL_TEXTURE_H_
