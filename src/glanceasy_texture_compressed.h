// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GLANCEASY_SRC_TEXTURE_COMPRESSED_H_
#define GLANCEASY_SRC_TEXTURE_COMPRESSED_H_

#include <stdint.h>

#include "glanceasy/glanceasy.h"

namespace glanceasy {

// Decodes one 4x4 ETC2/EAC block into tightly packed RGBA8 texels.  |rgba| is
// laid out row-major with four bytes per texel.  Returns false when |format|
// is not a supported ETC2/EAC format.
bool DecodeEtc2Block(GLenum format, const uint8_t* block, uint8_t* rgba);

// Decodes one signed EAC R11/RG11 block into normalized float RGBA texels.
bool DecodeSignedEacBlock(GLenum format, const uint8_t* block, float* rgba);

// Returns the number of bytes occupied by a 4x4 block of |format|, or zero for
// an unsupported format.
int CompressedBlockBytes(GLenum format);

}  // namespace glanceasy

#endif  // GLANCEASY_SRC_TEXTURE_COMPRESSED_H_
