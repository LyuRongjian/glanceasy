// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GLANCEASY_SRC_DRAW_H_
#define GLANCEASY_SRC_DRAW_H_

#include <stdint.h>

#include "GLES3/gl32.h"

namespace glanceasy {

// Runs a complete draw call against the current context: attribute fetch,
// vertex shading, primitive assembly, homogeneous clipping, rasterisation,
// fragment shading and the per-fragment pipeline (depth, stencil, blend,
// write masks).
//
// |index_type| selects the element width; when |indices| is null the call
// behaves like glDrawArrays() and |first| is used as the base vertex.
void Draw(GLenum mode,
          GLint first,
          GLsizei count,
          GLenum index_type,
          const void* indices,
          GLsizei instance_count);

}  // namespace glanceasy

#endif  // GLANCEASY_SRC_DRAW_H_
