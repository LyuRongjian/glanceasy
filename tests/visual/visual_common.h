// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZCAT_GLES_TESTS_VISUAL_VISUAL_COMMON_H_
#define ZCAT_GLES_TESTS_VISUAL_VISUAL_COMMON_H_

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "GLES3/gl32.h"
#include "zgl/zgl.h"

namespace zglvis {

// Builds a program from a vertex and fragment shader and binds the supplied
// attribute names to fixed locations. Returns 0 on failure.
GLuint BuildProgram(const char* vertex_source,
                    const char* fragment_source,
                    const std::vector<std::pair<int, const char*>>& bindings);

// Reports whether a GL error is pending and clears the flag.
bool TakeGlError(const char* stage);

// Writes the current colour buffer as a binary PPM, flipping the row order
// because glReadPixels returns the bottom row first.
bool WritePpm(const char* path, int width, int height);

// Reads one RGBA texel using window coordinates with the origin at the top
// left, which is convenient for reasoning about screenshots.
void ReadTexel(int x, int y, int width, int height, unsigned char rgba[4]);

bool ColourNear(const unsigned char rgba[4],
                int red,
                int green,
                int blue,
                int tolerance);
bool ColourIsDark(const unsigned char rgba[4]);

// A small full-screen triangle used by background and shading passes.
void DrawFullscreenTriangle();

}  // namespace zglvis

#endif  // ZCAT_GLES_TESTS_VISUAL_VISUAL_COMMON_H_
