// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/glanceasy_raster.h"

#include <stddef.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace glanceasy {

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
  if (GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_RAST")) {
    static int dbg = 0;
    if (dbg < 8) {
      fprintf(stderr, "[rast] bbox=%d,%d..%d,%d clip=%d,%d,%d,%d\n", x0, y0, x1,
              y1, clip_.x, clip_.y, clip_.right(), clip_.bottom());
      ++dbg;
    }
  }
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
  const float db0_dx = (v1->y - v2->y) * inv_det;
  const float db0_dy = (v2->x - v1->x) * inv_det;
  const float db1_dx = (v2->y - v0->y) * inv_det;
  const float db1_dy = (v0->x - v2->x) * inv_det;

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
    const auto include = [&](int64_t e, bool top_left) {
      const int64_t oriented = positive ? e : -e;
      return oriented > 0 || (oriented == 0 && top_left);
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

  Fragment batch[4];
  int batch_count = 0;
  auto flush_batch = [&]() {
    if (batch_count > 0) {
      sink->ShadeBatch(batch, batch_count);
      batch_count = 0;
    }
  };
  auto shade_pixel = [&](int x, int y, float w0, float w1) {
    Fragment& fragment = batch[batch_count++];
    fragment.x = x;
    fragment.y = y;
    fragment.depth = v2->z + w0 * z0 + w1 * z1;
    fragment.bary[0] = w0;
    fragment.bary[1] = w1;
    fragment.bary[2] = 1.0f - w0 - w1;
    fragment.dbary[0][0] = db0_dx;
    fragment.dbary[0][1] = db0_dy;
    fragment.dbary[1][0] = db1_dx;
    fragment.dbary[1][1] = db1_dy;
    fragment.dbary[2][0] = -db0_dx - db1_dx;
    fragment.dbary[2][1] = -db0_dy - db1_dy;
    fragment.vertex[0] = i0;
    fragment.vertex[1] = i1;
    fragment.vertex[2] = i2;
    fragment.provoking = index_c;
    if (batch_count == 4)
      flush_batch();
  };

  // A run is a short horizontal span of pixels that share the same row.  For
  // a convex triangle, every pixel between two strictly interior endpoints is
  // also inside; checking the endpoints once removes three edge functions per
  // pixel for the common fill-heavy case.
  auto run_inside = [&](int xa, int xb, int y) {
    if (det_int == 0 || xa > xb)
      return false;
    const int64_t qy = QuantizeSubpixel(static_cast<float>(y) + 0.5f);
    const int64_t qxa = QuantizeSubpixel(static_cast<float>(xa) + 0.5f);
    const int64_t qxb = QuantizeSubpixel(static_cast<float>(xb) + 0.5f);
    const bool positive = det_int > 0;
    const auto inside_edge = [&](int64_t e) {
      return positive ? e > 0 : e < 0;
    };
    const auto edge_values = [&](int64_t qx, int64_t* e0, int64_t* e1,
                                 int64_t* e2) {
      *e0 = EdgeFunction(qx1, qy1, qx2, qy2, qx, qy);
      *e1 = EdgeFunction(qx2, qy2, qx0, qy0, qx, qy);
      *e2 = det_int - *e0 - *e1;
    };
    int64_t a0, a1, a2, b0, b1, b2;
    edge_values(qxa, &a0, &a1, &a2);
    edge_values(qxb, &b0, &b1, &b2);
    return inside_edge(a0) && inside_edge(a1) && inside_edge(a2) &&
           inside_edge(b0) && inside_edge(b1) && inside_edge(b2);
  };

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

    if (samples_ > 1 && sample_positions_) {
      for (int x = x0; x <= x1; ++x) {
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
          fragment.dbary[0][0] = db0_dx;
          fragment.dbary[0][1] = db0_dy;
          fragment.dbary[1][0] = db1_dx;
          fragment.dbary[1][1] = db1_dy;
          fragment.dbary[2][0] = -db0_dx - db1_dx;
          fragment.dbary[2][1] = -db0_dy - db1_dy;
          fragment.vertex[0] = i0;
          fragment.vertex[1] = i1;
          fragment.vertex[2] = i2;
          fragment.provoking = index_c;
          sink->Shade(fragment);
        }
        w0 += step0;
        w1 += step1;
        px += 1.0f;
      }
    } else {
      int x = x0;
      while (x <= x1) {
        const int run_end = std::min(x + 7, x1);
        // A convex triangle is covered everywhere between two interior
        // endpoints, so an eight-pixel run usually needs no per-pixel edge
        // tests at all.
        const bool interior = run_inside(x, run_end, y);
        for (; x <= run_end; ++x) {
          if (interior ||
              covers(QuantizeSubpixel(px), QuantizeSubpixel(py))) {
            shade_pixel(x, y, w0, w1);
          }
          w0 += step0;
          w1 += step1;
          px += 1.0f;
        }
      }
    }
    flush_batch();
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
  const float length_squared = dx * dx + dy * dy;
  if (length_squared <= 0.0f) {
    DrawPoint(a, index_a, 1.0f, sink);
    return;
  }
  if (samples_ <= 1) {
    // Single-sample lines use the diamond-exit rule: a pixel is covered when
    // the segment exits its diamond through the bottom-right or top-right
    // edge.  This matches the Khronos reference rasterizer and avoids the
    // gaps and double-covered pixels of a Bresenham walk.
    const int bx0 =
        std::max(clip_.x, static_cast<int>(std::floor(std::min(a.x, b.x))) - 1);
    const int bx1 = std::min(clip_.right() - 1,
                            static_cast<int>(std::ceil(std::max(a.x, b.x))) + 1);
    const int by0 =
        std::max(clip_.y, static_cast<int>(std::floor(std::min(a.y, b.y))) - 1);
    const int by1 = std::min(clip_.bottom() - 1,
                            static_cast<int>(std::ceil(std::max(a.y, b.y))) + 1);
    for (int y = by0; y <= by1; ++y) {
      for (int x = bx0; x <= bx1; ++x) {
        const float cx = static_cast<float>(x) + 0.5f;
        const float cy = static_cast<float>(y) + 0.5f;
        // Half planes of the diamond |dx| + |dy| <= 0.5.  Planes 0 and 2 are
        // the bottom-right and top-right edges, which own the exit point.
        static const float kPlaneN[4][2] = {
            {1.0f, 1.0f}, {-1.0f, 1.0f}, {1.0f, -1.0f}, {-1.0f, -1.0f}};
        float enter = 0.0f;
        float leave = 1.0f;
        int leave_plane = -1;
        bool reject = false;
        for (int plane = 0; plane < 4; ++plane) {
          const float nx = kPlaneN[plane][0];
          const float ny = kPlaneN[plane][1];
          const float start = nx * (a.x - cx) + ny * (a.y - cy);
          const float delta = nx * dx + ny * dy;
          if (std::fabs(delta) < 1e-9f) {
            if (start > 0.5f) {
              reject = true;
              break;
            }
            continue;
          }
          const float t = (0.5f - start) / delta;
          if (delta > 0.0f) {
            if (start > 0.5f) {
              // Outside and moving further outside.
              reject = true;
              break;
            }
            if (t < leave) {
              leave = t;
              leave_plane = plane;
            }
          } else {
            // Moving towards the half plane; a point outside enters it.
            if (start > 0.5f && t > enter)
              enter = t;
          }
        }
        if (reject || enter > leave || leave < 0.0f || enter > 1.0f)
          continue;
        const float t =
            std::clamp(((cx - a.x) * dx + (cy - a.y) * dy) / length_squared,
                       0.0f, 1.0f);
        Fragment fragment;
        fragment.x = x;
        fragment.y = y;
        fragment.depth = a.z + (b.z - a.z) * t;
        fragment.bary[0] = 1.0f - t;
        fragment.bary[1] = t;
        fragment.bary[2] = 0.0f;
        fragment.dbary[0][0] = -dx / length_squared;
        fragment.dbary[0][1] = -dy / length_squared;
        fragment.dbary[1][0] = dx / length_squared;
        fragment.dbary[1][1] = dy / length_squared;
        fragment.vertex[0] = index_a;
        fragment.vertex[1] = index_b;
        fragment.vertex[2] = index_b;
        fragment.provoking = index_b;
        sink->Shade(fragment);
      }
    }
    return;
  }
  // Expand the segment by half a pixel so that every sample that the line
  // touches is visited, then test each sample position against the segment.
  const int x0 = static_cast<int>(std::floor(std::min(a.x, b.x) - 0.5f));
  const int x1 = static_cast<int>(std::ceil(std::max(a.x, b.x) + 0.5f));
  const int y0 = static_cast<int>(std::floor(std::min(a.y, b.y) - 0.5f));
  const int y1 = static_cast<int>(std::ceil(std::max(a.y, b.y) + 0.5f));
  const int sample_count = samples_ > 1 ? samples_ : 1;
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      if (!clip_.Contains(x, y))
        continue;
      for (int sample = 0; sample < sample_count; ++sample) {
        const float sx = static_cast<float>(x) + 0.5f +
                         (sample_positions_ ? sample_positions_[sample][0]
                                            : 0.0f);
        const float sy = static_cast<float>(y) + 0.5f +
                         (sample_positions_ ? sample_positions_[sample][1]
                                            : 0.0f);
        const float px = sx - a.x;
        const float py = sy - a.y;
        const float t = (px * dx + py * dy) / length_squared;
        if (t < 0.0f || t > 1.0f)
          continue;
        const float cx = a.x + dx * t;
        const float cy = a.y + dy * t;
        const float distance =
            std::sqrt((sx - cx) * (sx - cx) + (sy - cy) * (sy - cy));
        if (distance >= 0.5f)
          continue;
        Fragment fragment;
        fragment.x = x;
        fragment.y = y;
        fragment.sample = sample;
        if (sample_positions_) {
          fragment.sample_dx = sample_positions_[sample][0];
          fragment.sample_dy = sample_positions_[sample][1];
        }
        fragment.depth = a.z + (b.z - a.z) * t;
        fragment.bary[0] = 1.0f - t;
        fragment.bary[1] = t;
        fragment.bary[2] = 0.0f;
        fragment.dbary[0][0] = -dx / length_squared;
        fragment.dbary[0][1] = -dy / length_squared;
        fragment.dbary[1][0] = dx / length_squared;
        fragment.dbary[1][1] = dy / length_squared;
        fragment.vertex[0] = index_a;
        fragment.vertex[1] = index_b;
        fragment.vertex[2] = index_b;
        fragment.provoking = index_b;
        sink->Shade(fragment);
      }
    }
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
      const int sample_count = samples_ > 1 ? samples_ : 1;
      for (int sample = 0; sample < sample_count; ++sample) {
        const float sx = px + (sample_positions_ ? sample_positions_[sample][0] : 0.0f);
        const float sy = py + (sample_positions_ ? sample_positions_[sample][1] : 0.0f);
        if (sx < left || sx >= left + point_size || sy < top ||
            sy >= top + point_size)
          continue;
        Fragment fragment;
        fragment.x = x;
        fragment.y = y;
        fragment.sample = sample;
        if (sample_positions_) {
          fragment.sample_dx = sample_positions_[sample][0];
          fragment.sample_dy = sample_positions_[sample][1];
        }
        fragment.depth = vertex.z;
        fragment.bary[0] = 1.0f;
        fragment.bary[1] = 0.0f;
        fragment.bary[2] = 0.0f;
        fragment.vertex[0] = index;
        fragment.vertex[1] = index;
        fragment.vertex[2] = index;
        fragment.provoking = index;
        fragment.point_coord[0] = (sx - left) / point_size;
        fragment.point_coord[1] = (sy - top) / point_size;
        sink->Shade(fragment);
      }
    }
  }
}

}  // namespace glanceasy
