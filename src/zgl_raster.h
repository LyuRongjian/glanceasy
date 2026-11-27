// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZGL_SRC_ZGL_RASTER_H_
#define ZGL_SRC_ZGL_RASTER_H_

#include "src/zgl_common.h"

namespace zgl {

// A vertex after the fixed function transform: window coordinates (y grows
// downwards, row 0 is the top row of the framebuffer), a depth already
// mapped through the depth range, and the clip-space w used for
// perspective-correct interpolation.
struct RasterVertex {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float w = 1.0f;
};

struct Fragment {
  int x = 0;
  int y = 0;
  float depth = 0.0f;
  int sample = 0;      // Sample index for multisample targets.
  float sample_dx = 0.0f;
  float sample_dy = 0.0f;
  // Barycentric weights of the three vertices of the sub-triangle; weights
  // 1 and 2 are zero for lines and points.
  float bary[3] = {0.0f, 0.0f, 0.0f};
  // Screen-space derivatives of the barycentric weights, used to estimate
  // implicit texture LOD in the fragment shader.
  float dbary[3][2] = {{0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}};
  // Indices into the caller's vertex array, so that the sink can interpolate
  // the per-vertex varyings.
  int vertex[3] = {0, 0, 0};
  // Vertex that supplies flat-shaded varyings (the provoking vertex).
  int provoking = -1;
  // gl_PointCoord for point primitives.
  float point_coord[2] = {0.0f, 0.0f};
};

class FragmentSink {
 public:
  virtual ~FragmentSink() = default;
  virtual void Shade(const Fragment& fragment) = 0;
};

// Scanline-free triangle rasteriser: for every row of the bounding box the
// barycentric weights are advanced incrementally, so the inner loop costs two
// additions, three comparisons and one depth interpolation per pixel.  That
// matters on Cortex-A7 class parts, where a division per pixel is unaffordable.
class Rasterizer {
 public:
  void SetClip(const Rect& clip) { clip_ = clip; }
  // |count| sample positions per pixel, in pixel units relative to the centre.
  void SetSamples(int count, const float (*positions)[2]);

  void DrawTriangle(const RasterVertex& a,
                    const RasterVertex& b,
                    const RasterVertex& c,
                    int index_a,
                    int index_b,
                    int index_c,
                    FragmentSink* sink);
  void DrawLine(const RasterVertex& a,
                const RasterVertex& b,
                int index_a,
                int index_b,
                FragmentSink* sink);
  void DrawPoint(const RasterVertex& vertex,
                 int index,
                 float size,
                 FragmentSink* sink);

 private:
  Rect clip_;
  int samples_ = 1;
  const float (*sample_positions_)[2] = nullptr;
};

}  // namespace zgl

#endif  // ZGL_SRC_ZGL_RASTER_H_
