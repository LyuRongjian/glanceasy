// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/zgl_texture_compressed.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace zgl {

namespace {

const int32_t kEtc1Modifiers[8][4] = {
    {2, 8, -2, -8},       {5, 17, -5, -17},     {9, 29, -9, -29},
    {13, 42, -13, -42},   {18, 60, -18, -60},   {24, 80, -24, -80},
    {33, 106, -33, -106}, {47, 183, -47, -183}};

const int32_t kEtc2Modifiers[8] = {3, 6, 11, 16, 23, 32, 41, 64};

const int32_t kEtc2Alpha1Modifiers[8][4] = {
    {0, 8, 0, -8},       {0, 17, 0, -17},     {0, 29, 0, -29},
    {0, 42, 0, -42},     {0, 60, 0, -60},     {0, 80, 0, -80},
    {0, 106, 0, -106},   {0, 183, 0, -183}};

const int32_t kEtc2AlphaModifiers[16][8] = {
    {-3, -6, -9, -15, 2, 5, 8, 14},  {-3, -7, -10, -13, 2, 6, 9, 12},
    {-2, -5, -8, -13, 1, 4, 7, 12},  {-2, -4, -6, -13, 1, 3, 5, 12},
    {-3, -6, -8, -12, 2, 5, 7, 11},  {-3, -7, -9, -11, 2, 6, 8, 10},
    {-4, -7, -8, -11, 3, 6, 7, 10},  {-3, -5, -8, -11, 2, 4, 7, 10},
    {-2, -6, -8, -10, 1, 5, 7, 9},   {-2, -5, -8, -10, 1, 4, 7, 9},
    {-2, -4, -8, -10, 1, 3, 7, 9},   {-2, -5, -7, -10, 1, 4, 6, 9},
    {-3, -4, -7, -10, 2, 3, 6, 9},   {-1, -2, -3, -10, 0, 1, 2, 9},
    {-4, -6, -8, -9, 3, 5, 7, 8},    {-3, -5, -7, -9, 2, 4, 6, 8}};

uint8_t Expand(uint32_t value, int bits) {
  if (bits >= 8)
    return static_cast<uint8_t>(value);
  const uint32_t mask = (1u << bits) - 1u;
  value &= mask;
  uint8_t result = static_cast<uint8_t>(value << (8 - bits));
  result |= static_cast<uint8_t>(value >> (2 * bits - 8));
  return result;
}

uint8_t Saturate(int32_t value) {
  return static_cast<uint8_t>(std::max(0, std::min(255, value)));
}

uint8_t AddModifier(uint8_t base, int32_t modifier) {
  return Saturate(static_cast<int32_t>(base) + modifier);
}

void StorePixel(uint8_t* rgba, int pixel, uint8_t r, uint8_t g, uint8_t b,
                uint8_t a) {
  rgba[pixel * 4 + 0] = r;
  rgba[pixel * 4 + 1] = g;
  rgba[pixel * 4 + 2] = b;
  rgba[pixel * 4 + 3] = a;
}

void DecodePalettePixels(const uint8_t palette[16][4], uint32_t index_lsb,
                         uint32_t index_msb, uint8_t* rgba, bool punch_alpha,
                         bool opaque) {
  for (int i = 0; i < 16; ++i) {
    const int x = i & 3;
    const int y = i >> 2;
    const uint32_t index = (index_lsb & 1u) | ((index_msb & 1u) << 1);
    index_lsb >>= 1;
    index_msb >>= 1;
    const uint8_t* color = palette[index];
    uint8_t alpha = 255;
    if (punch_alpha && !opaque && index == 2)
      alpha = 0;
    StorePixel(rgba, y * 4 + x, color[0], color[1], color[2], alpha);
  }
}

void DecodeTMode(const uint8_t* src, uint8_t* rgba) {
  uint8_t palette[16][4] = {};
  uint8_t c0[3] = {
      Expand((((src[0] >> 1) & 0xc) | (src[0] & 0x3)), 4),
      Expand(src[1] >> 4, 4), Expand(src[1] & 0xf, 4)};
  uint8_t c1[3] = {Expand(src[2] >> 4, 4), Expand(src[2] & 0xf, 4),
                   Expand(src[3] >> 4, 4)};
  const int32_t modifier =
      kEtc2Modifiers[((src[3] >> 1) & 0x6) | (src[3] & 0x1)];
  for (int i = 0; i < 3; ++i) {
    palette[0][i] = c0[i];
    palette[1][i] = AddModifier(c1[i], modifier);
    palette[2][i] = c1[i];
    palette[3][i] = AddModifier(c1[i], -modifier);
  }
  for (int i = 0; i < 4; ++i)
    palette[i][3] = 255;
  DecodePalettePixels(palette, (src[7] << 8) | src[6], (src[5] << 8) | src[4],
                      rgba, false, true);
}

void DecodeHMode(const uint8_t* src, uint8_t* rgba) {
  uint8_t palette[16][4] = {};
  uint8_t c0[3] = {
      Expand((src[0] >> 3) & 0xf, 4),
      Expand((((src[0] << 1) & 0xe) | ((src[1] >> 4) & 0x1)), 4),
      Expand((((src[1] & 0x8) | ((src[1] << 1) & 0x6) | (src[2] >> 7))), 4)};
  uint8_t c1[3] = {Expand((src[2] >> 3) & 0xf, 4),
                   Expand((((src[2] << 1) & 0xe) | (src[3] >> 7)), 4),
                   Expand((src[3] >> 3) & 0xf, 4)};
  const uint32_t color0 =
      (static_cast<uint32_t>(c0[0]) << 16) |
      (static_cast<uint32_t>(c0[1]) << 8) | c0[2];
  const uint32_t color1 =
      (static_cast<uint32_t>(c1[0]) << 16) |
      (static_cast<uint32_t>(c1[1]) << 8) | c1[2];
  const int32_t modifier = kEtc2Modifiers[(src[3] & 0x4) |
                                          ((src[3] << 1) & 0x2) |
                                          (color0 >= color1 ? 1 : 0)];
  for (int i = 0; i < 3; ++i) {
    palette[0][i] = AddModifier(c0[i], modifier);
    palette[1][i] = AddModifier(c0[i], -modifier);
    palette[2][i] = AddModifier(c1[i], modifier);
    palette[3][i] = AddModifier(c1[i], -modifier);
  }
  for (int i = 0; i < 4; ++i)
    palette[i][3] = 255;
  DecodePalettePixels(palette, (src[7] << 8) | src[6], (src[5] << 8) | src[4],
                      rgba, false, true);
}

void DecodePlanarMode(const uint8_t* src, uint8_t* rgba) {
  uint8_t c0[3] = {
      Expand(((src[0] >> 1) & 0x3f), 6),
      Expand((((src[0] & 1) << 6) | ((src[1] >> 1) & 0x3f)), 7),
      Expand((((src[1] & 1) << 5) | (src[2] & 0x18) | ((src[2] << 1) & 6) |
              (src[3] >> 7)), 6)};
  uint8_t ch[3] = {
      Expand((((src[3] >> 1) & 0x3e) | (src[3] & 1)), 6),
      Expand(src[4] >> 1, 7),
      Expand((((src[4] & 1) << 5) | (src[5] >> 3)), 6)};
  uint8_t cv[3] = {
      Expand((((src[5] & 0x7) << 3) | (src[6] >> 5)), 6),
      Expand((((src[6] & 0x1f) << 2) | (src[7] >> 5)), 7),
      Expand((src[7] & 0x3f), 6)};
  int32_t dy[3];
  int32_t sx[3];
  int32_t ex[3];
  for (int c = 0; c < 3; ++c) {
    dy[c] = static_cast<int32_t>(cv[c]) - c0[c];
    sx[c] = static_cast<int32_t>(c0[c]) << 2;
    ex[c] = static_cast<int32_t>(ch[c]) << 2;
  }
  for (int y = 0; y < 4; ++y) {
    int32_t dx[3];
    for (int c = 0; c < 3; ++c)
      dx[c] = (ex[c] - sx[c]) >> 2;
    for (int x = 0; x < 4; ++x) {
      StorePixel(rgba, y * 4 + x,
                 Saturate((sx[2] + dx[2] * x) >> 2),
                 Saturate((sx[1] + dx[1] * x) >> 2),
                 Saturate((sx[0] + dx[0] * x) >> 2), 255);
    }
    for (int c = 0; c < 3; ++c) {
      sx[c] += dy[c];
      ex[c] += dy[c];
    }
  }
}

void DecodeEtc1Block(const uint8_t* src, uint8_t* rgba) {
  const bool differential = (src[3] & 0x2) != 0;
  const bool flip = (src[3] & 0x1) != 0;
  uint8_t base[2][3];
  if (differential) {
    const int32_t dr = static_cast<int8_t>((src[0] & 0x7) << 5) >> 5;
    const int32_t dg = static_cast<int8_t>((src[1] & 0x7) << 5) >> 5;
    const int32_t db = static_cast<int8_t>((src[2] & 0x7) << 5) >> 5;
    const int32_t r = static_cast<int32_t>(src[0] >> 3) + dr;
    const int32_t g = static_cast<int32_t>(src[1] >> 3) + dg;
    const int32_t b = static_cast<int32_t>(src[2] >> 3) + db;
    if (r < 0 || r > 31) {
      DecodeTMode(src, rgba);
      return;
    }
    if (g < 0 || g > 31) {
      DecodeHMode(src, rgba);
      return;
    }
    if (b < 0 || b > 31) {
      DecodePlanarMode(src, rgba);
      return;
    }
    base[0][0] = Expand(static_cast<uint32_t>(src[0] >> 3), 5);
    base[0][1] = Expand(static_cast<uint32_t>(src[1] >> 3), 5);
    base[0][2] = Expand(static_cast<uint32_t>(src[2] >> 3), 5);
    base[1][0] = Expand(static_cast<uint32_t>(r), 5);
    base[1][1] = Expand(static_cast<uint32_t>(g), 5);
    base[1][2] = Expand(static_cast<uint32_t>(b), 5);
  } else {
    base[0][0] = Expand(src[0] >> 4, 4);
    base[0][1] = Expand(src[1] >> 4, 4);
    base[0][2] = Expand(src[2] >> 4, 4);
    base[1][0] = Expand(src[0] & 0xf, 4);
    base[1][1] = Expand(src[1] & 0xf, 4);
    base[1][2] = Expand(src[2] & 0xf, 4);
  }
  const uint32_t table[2] = {(src[3] >> 5) & 0x7u, (src[3] >> 2) & 0x7u};
  uint32_t index_lsb = (src[7] << 8) | src[6];
  uint32_t index_msb = (src[5] << 8) | src[4];
  for (int i = 0; i < 16; ++i) {
    const int x = i & 3;
    const int y = i >> 2;
    const int subblock = flip ? ((i >> 1) & 1) : (i >> 3);
    const uint32_t index = (index_lsb & 1u) | ((index_msb & 1u) << 1);
    index_lsb >>= 1;
    index_msb >>= 1;
    const int32_t modifier = kEtc1Modifiers[table[subblock]][index];
    StorePixel(rgba, y * 4 + x,
               AddModifier(base[subblock][0], modifier),
               AddModifier(base[subblock][1], modifier),
               AddModifier(base[subblock][2], modifier), 255);
  }
}

void ApplyPunchThroughAlpha(const uint8_t* src, uint8_t* rgba, bool opaque) {
  if (opaque)
    return;
  uint32_t index_lsb = (src[7] << 8) | src[6];
  uint32_t index_msb = (src[5] << 8) | src[4];
  for (int i = 0; i < 16; ++i) {
    const int x = i & 3;
    const int y = i >> 2;
    const uint32_t index = (index_lsb & 1u) | ((index_msb & 1u) << 1);
    index_lsb >>= 1;
    index_msb >>= 1;
    if (index == 2)
      rgba[(y * 4 + x) * 4 + 3] = 0;
  }
}

void DecodePunchThroughBlock(const uint8_t* src, uint8_t* rgba) {
  const bool opaque = (src[3] & 0x2) != 0;
  const int32_t dr = static_cast<int8_t>((src[0] & 0x7) << 5) >> 5;
  const int32_t dg = static_cast<int8_t>((src[1] & 0x7) << 5) >> 5;
  const int32_t db = static_cast<int8_t>((src[2] & 0x7) << 5) >> 5;
  const int32_t rr = static_cast<int32_t>(src[0] >> 3) + dr;
  const int32_t gg = static_cast<int32_t>(src[1] >> 3) + dg;
  const int32_t bb = static_cast<int32_t>(src[2] >> 3) + db;
  if (rr < 0 || rr > 31) {
    DecodeTMode(src, rgba);
    ApplyPunchThroughAlpha(src, rgba, opaque);
    return;
  }
  if (gg < 0 || gg > 31) {
    DecodeHMode(src, rgba);
    ApplyPunchThroughAlpha(src, rgba, opaque);
    return;
  }
  if (bb < 0 || bb > 31) {
    DecodePlanarMode(src, rgba);
    return;
  }
  uint8_t base[2][3] = {
      {Expand(static_cast<uint32_t>(src[0] >> 3), 5),
       Expand(static_cast<uint32_t>(src[1] >> 3), 5),
       Expand(static_cast<uint32_t>(src[2] >> 3), 5)},
      {Expand(static_cast<uint32_t>(rr), 5),
       Expand(static_cast<uint32_t>(gg), 5),
       Expand(static_cast<uint32_t>(bb), 5)}};
  const uint32_t table[2] = {(src[3] >> 5) & 0x7u, (src[3] >> 2) & 0x7u};
  const bool flip = (src[3] & 0x1) != 0;
  uint32_t index_lsb = (src[7] << 8) | src[6];
  uint32_t index_msb = (src[5] << 8) | src[4];
  for (int i = 0; i < 16; ++i) {
    const int x = i & 3;
    const int y = i >> 2;
    const int subblock = flip ? ((i >> 1) & 1) : (i >> 3);
    const uint32_t index = (index_lsb & 1u) | ((index_msb & 1u) << 1);
    index_lsb >>= 1;
    index_msb >>= 1;
    if (!opaque && index == 2) {
      StorePixel(rgba, y * 4 + x, 0, 0, 0, 0);
      continue;
    }
    const int32_t modifier =
        opaque ? kEtc1Modifiers[table[subblock]][index]
               : kEtc2Alpha1Modifiers[table[subblock]][index];
    StorePixel(rgba, y * 4 + x,
               AddModifier(base[subblock][0], modifier),
               AddModifier(base[subblock][1], modifier),
               AddModifier(base[subblock][2], modifier), 255);
  }
}

void DecodeAlphaBlock(const uint8_t* src, uint8_t* rgba) {
  const int32_t base = src[0];
  const int32_t* modifiers = kEtc2AlphaModifiers[src[1] & 0xf];
  const int32_t multiplier = (src[1] >> 4) & 0xf;
  uint64_t indices = 0;
  for (int i = 2; i < 8; ++i)
    indices = (indices << 8) | src[i];
  for (int i = 0; i < 16; ++i) {
    const int x = i & 3;
    const int y = i >> 2;
    const int32_t modifier = modifiers[(indices >> (45 - i * 3)) & 0x7];
    rgba[(y * 4 + x) * 4 + 3] =
        Saturate(base + modifier * multiplier);
  }
}

void DecodeEac11(const uint8_t* src, uint8_t* rgba, bool signed_values) {
  const int32_t base = signed_values
                           ? (static_cast<int8_t>(src[0]))
                           : static_cast<int32_t>(src[0]);
  const int32_t* modifiers = kEtc2AlphaModifiers[src[1] & 0xf];
  const int32_t multiplier = (src[1] >> 4) & 0xf;
  uint64_t indices = 0;
  for (int i = 2; i < 8; ++i)
    indices = (indices << 8) | src[i];
  for (int i = 0; i < 16; ++i) {
    const int x = i & 3;
    const int y = i >> 2;
    const int32_t modifier = modifiers[(indices >> (45 - i * 3)) & 0x7];
    int32_t value;
    if (signed_values) {
      value = std::max(-1023, std::min(1023,
                                       base * 8 + modifier * multiplier * 8));
      value = (value + 1023) * 255 / 2046;
    } else {
      value = std::max(0, std::min(2047,
                                   base * 8 + 4 + modifier * multiplier * 8));
      value >>= 3;
    }
    StorePixel(rgba, y * 4 + x, static_cast<uint8_t>(value),
               static_cast<uint8_t>(value), static_cast<uint8_t>(value), 255);
  }
}

}  // namespace

int CompressedBlockBytes(GLenum format) {
  switch (format) {
    case GL_COMPRESSED_R11_EAC:
    case GL_COMPRESSED_SIGNED_R11_EAC:
    case GL_COMPRESSED_RGB8_ETC2:
    case GL_COMPRESSED_SRGB8_ETC2:
    case GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2:
    case GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2:
      return 8;
    case GL_COMPRESSED_RG11_EAC:
    case GL_COMPRESSED_SIGNED_RG11_EAC:
    case GL_COMPRESSED_RGBA8_ETC2_EAC:
    case GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC:
      return 16;
    default:
      return 0;
  }
}

bool DecodeEtc2Block(GLenum format, const uint8_t* block, uint8_t* rgba) {
  if (!block || !rgba)
    return false;
  switch (format) {
    case GL_COMPRESSED_RGB8_ETC2:
    case GL_COMPRESSED_SRGB8_ETC2:
      DecodeEtc1Block(block, rgba);
      return true;
    case GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2:
    case GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2:
      DecodePunchThroughBlock(block, rgba);
      return true;
    case GL_COMPRESSED_RGBA8_ETC2_EAC:
    case GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC:
      DecodeEtc1Block(block, rgba);
      DecodeAlphaBlock(block + 8, rgba);
      return true;
    case GL_COMPRESSED_R11_EAC:
      DecodeEac11(block, rgba, false);
      return true;
    case GL_COMPRESSED_SIGNED_R11_EAC:
      DecodeEac11(block, rgba, true);
      return true;
    case GL_COMPRESSED_RG11_EAC:
    case GL_COMPRESSED_SIGNED_RG11_EAC: {
      // The two EAC halves map to red and green respectively.
      uint8_t red[16 * 4] = {};
      uint8_t green[16 * 4] = {};
      const bool signed_values = format == GL_COMPRESSED_SIGNED_RG11_EAC;
      DecodeEac11(block, red, signed_values);
      DecodeEac11(block + 8, green, signed_values);
      for (int i = 0; i < 16; ++i)
        StorePixel(rgba, i, red[i * 4], green[i * 4], 0, 255);
      return true;
    }
    default:
      return false;
  }
}

}  // namespace zgl
