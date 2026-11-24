// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/zgl_raster.h"

#include <stddef.h>

#include <algorithm>
#include <cmath>

namespace zgl {

namespace {

// A pixel centre exactly on an edge belongs to the triangle only when the
// edge is a top or left edge; that removes the double cover that two
// triangles of a quad would otherwise produce along their shared diagonal.
bool IsTopLeftEdge(const RasterVertex& a, const RasterVertex& b) {
  const float dy = b.y - a.y;
  const float dx = b.x - a.x;
  if (dy < 0.0f)
    return true;
  return dy == 0.0f && dx > 0.0f;
}


// Coverage is decided on a fixed sub-pixel grid.  Clipping two triangles that
// share an edge produces vertex coordinates that differ by a few ulps; a pure
// float edge test then either leaves a crack or shades the shared edge twice.
// Snapping the edge function inputs to a deterministic grid makes both
// triangles see exactly the same shared edge.
constexpr int kSubpixelBits = 9;
constexpr int64_t kSubpixelScale = int64_t(1) << kSubpixelBits;
constexpr int64_t kSubpixelHalf = kSubpixelScale / 2;

int64_t QuantizeSubpixel(float value) {
  return static_cast<int64_t>(std::floor(value * kSubpixelScale + 0.5f));
}

int64_t EdgeFunction(int64_t ax, int64_t ay, int64_t bx, int64_t by,
                     int64_t px, int64_t py) {
  return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

bool IsTopLeftEdgeInt(int64_t ax, int64_t ay, int64_t bx, int64_t by) {
  const int64_t dy = by - ay;
  const int64_t dx = bx - ax;
  if (dy < 0)
    return true;
  return dy == 0 && dx > 0;
}

}  // namespace

void Rasterizer::SetSamples(int count, const float (*positions)[2]) {
  samples_ = count > 1 ? count : 1;
  sample_positions_ = samples_ > 1 ? positions : nullptr;
}

void Rasterizer::DrawTriangle(const RasterVertex& a,
                              const RasterVertex& b,
                              const RasterVertex& c,
                              int index_a,
                              int index_b,
                              int index_c,
                              FragmentSink* sink) {
  if (clip_.IsEmpty() || !sink)
    return;

  float min_x = std::min(a.x, std::min(b.x, c.x));
  float max_x = std::max(a.x, std::max(b.x, c.x));
  float min_y = std::min(a.y, std::min(b.y, c.y));
  float max_y = std::max(a.y, std::max(b.y, c.y));
  int x0 = std::max(clip_.x, static_cast<int>(std::floor(min_x)));
  int x1 = std::min(clip_.right() - 1, static_cast<int>(std::ceil(max_x)));
  int y0 = std::max(clip_.y, static_cast<int>(std::floor(min_y)));
  int y1 = std::min(clip_.bottom() - 1, static_cast<int>(std::ceil(max_y)));
  if (x0 > x1 || y0 > y1)
    return;

  const float det = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
  if (!(std::fabs(det) > 1e-9f))
    return;

  const RasterVertex* v0 = &a;
  const RasterVertex* v1 = &b;
  const RasterVertex* v2 = &c;
  int i0 = index_a;
  int i1 = index_b;
  int i2 = index_c;
  if (det < 0.0f) {
    std::swap(v1, v2);
    std::swap(i1, i2);
  }
  const float inv_det = 1.0f / (det < 0.0f ? -det : det);

  // Edge opposite vertex i is (i+1) -> (i+2); its inclusion rule decides
  // whether a pixel exactly on that edge is covered.
  const int64_t qx0 = QuantizeSubpixel(v0->x);
  const int64_t qy0 = QuantizeSubpixel(v0->y);
  const int64_t qx1 = QuantizeSubpixel(v1->x);
  const int64_t qy1 = QuantizeSubpixel(v1->y);
  const int64_t qx2 = QuantizeSubpixel(v2->x);
  const int64_t qy2 = QuantizeSubpixel(v2->y);
  const int64_t det_int =
      (qy1 - qy2) * (qx0 - qx2) - (qx1 - qx2) * (qy0 - qy2);
  const bool top_left_int0 = IsTopLeftEdgeInt(qx1, qy1, qx2, qy2);
  const bool top_left_int1 = IsTopLeftEdgeInt(qx2, qy2, qx0, qy0);
  const bool top_left_int2 = IsTopLeftEdgeInt(qx0, qy0, qx1, qy1);
  const auto covers = [&](int64_t px, int64_t py) {
    if (det_int == 0)
      return false;
    const int64_t e0 = EdgeFunction(qx1, qy1, qx2, qy2, px, py);
    const int64_t e1 = EdgeFunction(qx2, qy2, qx0, qy0, px, py);
    const int64_t e2 = det_int - e0 - e1;
    const bool positive = det_int > 0;
    const int64_t det_magnitude = det_int < 0 ? -det_int : det_int;
    const int64_t tolerance =
        det_magnitude / 8 < kSubpixelScale ? det_magnitude / 8
                                          : kSubpixelScale;
    const auto include = [&](int64_t e, bool top_left) {
      const int64_t oriented = positive ? e : -e;
      return oriented > tolerance || (oriented >= -tolerance && top_left);
    };
    const bool result = include(e0, top_left_int0) &&
                        include(e1, top_left_int1) &&
                        include(e2, top_left_int2);
    return result;
  };

  const float dw0_dx = (v1->y - v2->y) * inv_det;
  const float dw1_dx = (v2->y - v0->y) * inv_det;
  const float z0 = v0->z - v2->z;
  const float z1 = v1->z - v2->z;

  for (int y = y0; y <= y1; ++y) {
    const float py = static_cast<float>(y) + 0.5f;
    float px = static_cast<float>(x0) + 0.5f;
    float w0 =
        ((v1->y - v2->y) * (px - v2->x) + (v2->x - v1->x) * (py - v2->y)) *
        inv_det;
    float w1 =
        ((v2->y - v0->y) * (px - v2->x) + (v0->x - v2->x) * (py - v2->y)) *
        inv_det;
    const float step0 = dw0_dx;
    const float step1 = dw1_dx;

    for (int x = x0; x <= x1; ++x) {
      if (samples_ > 1 && sample_positions_) {
        for (int sample = 0; sample < samples_; ++sample) {
          const float sx = static_cast<float>(x) + 0.5f +
                           sample_positions_[sample][0];
          const float sy = py + sample_positions_[sample][1];
          const float s0 =
              ((v1->y - v2->y) * (sx - v2->x) +
               (v2->x - v1->x) * (sy - v2->y)) *
              inv_det;
          const float s1 =
              ((v2->y - v0->y) * (sx - v2->x) +
               (v0->x - v2->x) * (sy - v2->y)) *
              inv_det;
          if (!covers(QuantizeSubpixel(sx), QuantizeSubpixel(sy)))
            continue;
          Fragment fragment;
          fragment.x = x;
          fragment.y = y;
          fragment.sample = sample;
          fragment.sample_dx = sample_positions_[sample][0];
          fragment.sample_dy = sample_positions_[sample][1];
          fragment.depth = v2->z + s0 * z0 + s1 * z1;
          fragment.bary[0] = s0;
          fragment.bary[1] = s1;
          fragment.bary[2] = 1.0f - s0 - s1;
          fragment.vertex[0] = i0;
          fragment.vertex[1] = i1;
          fragment.vertex[2] = i2;
          sink->Shade(fragment);
        }
      } else if (covers(QuantizeSubpixel(px), QuantizeSubpixel(py))) {
        Fragment fragment;
        fragment.x = x;
        fragment.y = y;
        fragment.depth = v2->z + w0 * z0 + w1 * z1;
        fragment.bary[0] = w0;
        fragment.bary[1] = w1;
        fragment.bary[2] = 1.0f - w0 - w1;
        fragment.vertex[0] = i0;
        fragment.vertex[1] = i1;
        fragment.vertex[2] = i2;
        sink->Shade(fragment);
      }
      w0 += step0;
      w1 += step1;
      px += 1.0f;
    }
  }
}

void Rasterizer::DrawLine(const RasterVertex& a,
                          const RasterVertex& b,
                          int index_a,
                          int index_b,
                          FragmentSink* sink) {
  if (clip_.IsEmpty() || !sink)
    return;
  const float dx = b.x - a.x;
  const float dy = b.y - a.y;
  const int steps =
      static_cast<int>(std::max(std::fabs(dx), std::fabs(dy)) + 0.5f);
  if (steps <= 0) {
    DrawPoint(a, index_a, 1.0f, sink);
    return;
  }
  for (int i = 0; i <= steps; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(steps);
    const int x = static_cast<int>(std::lround(a.x + dx * t));
    const int y = static_cast<int>(std::lround(a.y + dy * t));
    if (!clip_.Contains(x, y))
      continue;
    Fragment fragment;
    fragment.x = x;
    fragment.y = y;
    fragment.depth = a.z + (b.z - a.z) * t;
    fragment.bary[0] = 1.0f - t;
    fragment.bary[1] = t;
    fragment.bary[2] = 0.0f;
    fragment.vertex[0] = index_a;
    fragment.vertex[1] = index_b;
    fragment.vertex[2] = index_b;
    sink->Shade(fragment);
  }
}

void Rasterizer::DrawPoint(const RasterVertex& vertex,
                           int index,
                           float size,
                           FragmentSink* sink) {
  if (clip_.IsEmpty() || !sink)
    return;
  const float point_size = std::max(1.0f, size);
  const float left = vertex.x - point_size * 0.5f;
  const float top = vertex.y - point_size * 0.5f;
  const int x0 = std::max(clip_.x, static_cast<int>(std::floor(left)));
  const int x1 = std::min(clip_.right() - 1,
                          static_cast<int>(std::ceil(left + point_size)) - 1);
  const int y0 = std::max(clip_.y, static_cast<int>(std::floor(top)));
  const int y1 = std::min(clip_.bottom() - 1,
                          static_cast<int>(std::ceil(top + point_size)) - 1);
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      const float px = static_cast<float>(x) + 0.5f;
      const float py = static_cast<float>(y) + 0.5f;
      if (px < left || px >= left + point_size || py < top ||
          py >= top + point_size) {
        continue;
      }
      Fragment fragment;
      fragment.x = x;
      fragment.y = y;
      fragment.depth = vertex.z;
      fragment.bary[0] = 1.0f;
      fragment.bary[1] = 0.0f;
      fragment.bary[2] = 0.0f;
      fragment.vertex[0] = index;
      fragment.vertex[1] = index;
      fragment.vertex[2] = index;
      fragment.point_coord[0] = (px - left) / point_size;
      fragment.point_coord[1] = (top + point_size - py) / point_size;
      sink->Shade(fragment);
    }
  }
}

}  // namespace zgl
