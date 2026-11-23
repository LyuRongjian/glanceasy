// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZGL_SRC_ZGL_TEXTURE_COMPRESSED_H_
#define ZGL_SRC_ZGL_TEXTURE_COMPRESSED_H_

#include <stdint.h>

#include "zgl/zgl.h"

namespace zgl {

// Decodes one 4x4 ETC2/EAC block into tightly packed RGBA8 texels.  |rgba| is
// laid out row-major with four bytes per texel.  Returns false when |format|
// is not a supported ETC2/EAC format.
bool DecodeEtc2Block(GLenum format, const uint8_t* block, uint8_t* rgba);

// Returns the number of bytes occupied by a 4x4 block of |format|, or zero for
// an unsupported format.
int CompressedBlockBytes(GLenum format);

}  // namespace zgl

#endif  // ZGL_SRC_ZGL_TEXTURE_COMPRESSED_H_
