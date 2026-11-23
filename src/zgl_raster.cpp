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
  if (dy > 0.0f)
    return true;
  return dy == 0.0f && dx < 0.0f;
}

bool IncludesWeight(float weight, bool top_left) {
  return weight > 0.0f || (weight == 0.0f && top_left);
}

}  // namespace

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
  const bool top_left0 = IsTopLeftEdge(*v1, *v2);
  const bool top_left1 = IsTopLeftEdge(*v2, *v0);
  const bool top_left2 = IsTopLeftEdge(*v0, *v1);

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
      if (IncludesWeight(w0, top_left0) && IncludesWeight(w1, top_left1) &&
          IncludesWeight(1.0f - w0 - w1, top_left2)) {
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
