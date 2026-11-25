// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/zgl_texture_compressed.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace zgl {

namespace {

// ETC2 block bit layout follows the Khronos ETC2 specification.  The 64-bit
// word is stored big-endian, so bit 63 is the MSB of the first byte.
uint64_t Get64(const uint8_t* src) {
  uint64_t block = 0;
  for (int i = 0; i < 8; ++i)
    block = (block << 8) | static_cast<uint64_t>(src[i]);
  return block;
}

uint32_t GetBit(uint64_t src, int bit) {
  return static_cast<uint32_t>((src >> bit) & 1u);
}

uint32_t GetBits(uint64_t src, int low, int high) {
  const int count = high - low + 1;
  if (count >= 32)
    return static_cast<uint32_t>(src >> low);
  return static_cast<uint32_t>((src >> low) & ((1ull << count) - 1ull));
}

uint8_t Extend4To8(uint8_t value) {
  return static_cast<uint8_t>((value << 4) | value);
}

uint8_t Extend5To8(uint8_t value) {
  return static_cast<uint8_t>((value << 3) | (value >> 2));
}

uint8_t Extend6To8(uint8_t value) {
  return static_cast<uint8_t>((value << 2) | (value >> 4));
}

uint8_t Extend7To8(uint8_t value) {
  return static_cast<uint8_t>((value << 1) | (value >> 6));
}

int8_t ExtendSigned3To8(uint8_t value) {
  return static_cast<int8_t>((value & 4u) ? (value | 0xf8u) : value);
}

uint8_t Clamp8(int32_t value) {
  return static_cast<uint8_t>(std::max(0, std::min(255, value)));
}

void StorePixel(uint8_t* rgba, int pixel, uint8_t r, uint8_t g, uint8_t b,
                uint8_t a) {
  rgba[pixel * 4 + 0] = r;
  rgba[pixel * 4 + 1] = g;
  rgba[pixel * 4 + 2] = b;
  rgba[pixel * 4 + 3] = a;
}

const int32_t kEtc1Modifiers[8][4] = {
    {2, 8, -2, -8},       {5, 17, -5, -17},     {9, 29, -9, -29},
    {13, 42, -13, -42},   {18, 60, -18, -60},   {24, 80, -24, -80},
    {33, 106, -33, -106}, {47, 183, -47, -183}};

const int32_t kEtc2Distances[8] = {3, 6, 11, 16, 23, 32, 41, 64};

const int32_t kEtc2AlphaModifiers[16][8] = {
    {-3, -6, -9, -15, 2, 5, 8, 14},  {-3, -7, -10, -13, 2, 6, 9, 12},
    {-2, -5, -8, -13, 1, 4, 7, 12},  {-2, -4, -6, -13, 1, 3, 5, 12},
    {-3, -6, -8, -12, 2, 5, 7, 11},  {-3, -7, -9, -11, 2, 6, 8, 10},
    {-4, -7, -8, -11, 3, 6, 7, 10},  {-3, -5, -8, -11, 2, 4, 7, 10},
    {-2, -6, -8, -10, 1, 5, 7, 9},   {-2, -5, -8, -10, 1, 4, 7, 9},
    {-2, -4, -8, -10, 1, 3, 7, 9},   {-2, -5, -7, -10, 1, 4, 6, 9},
    {-3, -4, -7, -10, 2, 3, 6, 9},   {-1, -2, -3, -10, 0, 1, 2, 9},
    {-4, -6, -8, -9, 3, 5, 7, 8},    {-3, -5, -7, -9, 2, 4, 6, 8}};

void DecodeEtc2Color(uint8_t* rgba, uint64_t src, bool alpha_mode) {
  const bool diff_opaque = GetBit(src, 33) != 0;
  const int32_t sel_br = static_cast<int32_t>(GetBits(src, 59, 63));
  const int32_t sel_bg = static_cast<int32_t>(GetBits(src, 51, 55));
  const int32_t sel_bb = static_cast<int32_t>(GetBits(src, 43, 47));
  const int32_t sel_dr =
      ExtendSigned3To8(static_cast<uint8_t>(GetBits(src, 56, 58)));
  const int32_t sel_dg =
      ExtendSigned3To8(static_cast<uint8_t>(GetBits(src, 48, 50)));
  const int32_t sel_db =
      ExtendSigned3To8(static_cast<uint8_t>(GetBits(src, 40, 42)));
  const bool differential_valid = sel_br + sel_dr >= 0 &&
                                  sel_br + sel_dr <= 31 &&
                                  sel_bg + sel_dg >= 0 &&
                                  sel_bg + sel_dg <= 31 &&
                                  sel_bb + sel_db >= 0 &&
                                  sel_bb + sel_db <= 31;

  if (!alpha_mode && !diff_opaque) {
    // Individual mode.
    const bool flip = GetBit(src, 32) != 0;
    const uint32_t table[2] = {GetBits(src, 37, 39), GetBits(src, 34, 36)};
    uint8_t base[2][3] = {
        {Extend4To8(static_cast<uint8_t>(GetBits(src, 60, 63))),
         Extend4To8(static_cast<uint8_t>(GetBits(src, 52, 55))),
         Extend4To8(static_cast<uint8_t>(GetBits(src, 44, 47)))},
        {Extend4To8(static_cast<uint8_t>(GetBits(src, 56, 59))),
         Extend4To8(static_cast<uint8_t>(GetBits(src, 48, 51))),
         Extend4To8(static_cast<uint8_t>(GetBits(src, 40, 43)))}};
    for (int i = 0; i < 16; ++i) {
      const int x = i / 4;
      const int y = i % 4;
      const int subblock = ((flip ? y : x) >= 2) ? 1 : 0;
      const uint32_t index = (GetBit(src, 16 + i) << 1) | GetBit(src, i);
      const int32_t modifier = kEtc1Modifiers[table[subblock]][index];
      StorePixel(rgba, y * 4 + x, Clamp8(base[subblock][0] + modifier),
                 Clamp8(base[subblock][1] + modifier),
                 Clamp8(base[subblock][2] + modifier), 255);
    }
    return;
  }

  if (differential_valid) {
    const bool flip = GetBit(src, 32) != 0;
    const uint32_t table[2] = {GetBits(src, 37, 39), GetBits(src, 34, 36)};
    uint8_t base[2][3] = {
        {Extend5To8(static_cast<uint8_t>(sel_br)),
         Extend5To8(static_cast<uint8_t>(sel_bg)),
         Extend5To8(static_cast<uint8_t>(sel_bb))},
        {Extend5To8(static_cast<uint8_t>(sel_br + sel_dr)),
         Extend5To8(static_cast<uint8_t>(sel_bg + sel_dg)),
         Extend5To8(static_cast<uint8_t>(sel_bb + sel_db))}};
    for (int i = 0; i < 16; ++i) {
      const int x = i / 4;
      const int y = i % 4;
      const int subblock = ((flip ? y : x) >= 2) ? 1 : 0;
      const uint32_t index = (GetBit(src, 16 + i) << 1) | GetBit(src, i);
      int32_t modifier = kEtc1Modifiers[table[subblock]][index];
      if (alpha_mode && !diff_opaque && index == 2) {
        StorePixel(rgba, y * 4 + x, 0, 0, 0, 0);
        continue;
      }
      if (alpha_mode && !diff_opaque && (index == 0 || index == 2))
        modifier = 0;
      StorePixel(rgba, y * 4 + x, Clamp8(base[subblock][0] + modifier),
                 Clamp8(base[subblock][1] + modifier),
                 Clamp8(base[subblock][2] + modifier), 255);
    }
    return;
  }

  if (sel_br + sel_dr < 0 || sel_br + sel_dr > 31) {
    // T mode.
    const uint8_t r1a = static_cast<uint8_t>(GetBits(src, 59, 60));
    const uint8_t r1b = static_cast<uint8_t>(GetBits(src, 56, 57));
    const uint8_t g1 = static_cast<uint8_t>(GetBits(src, 52, 55));
    const uint8_t b1 = static_cast<uint8_t>(GetBits(src, 48, 51));
    const uint8_t r2 = static_cast<uint8_t>(GetBits(src, 44, 47));
    const uint8_t g2 = static_cast<uint8_t>(GetBits(src, 40, 43));
    const uint8_t b2 = static_cast<uint8_t>(GetBits(src, 36, 39));
    const uint32_t dist_index = (GetBits(src, 34, 35) << 1) | GetBit(src, 32);
    const int32_t distance = kEtc2Distances[dist_index];
    uint8_t paint[4][3];
    paint[0][0] = Extend4To8(static_cast<uint8_t>((r1a << 2) | r1b));
    paint[0][1] = Extend4To8(g1);
    paint[0][2] = Extend4To8(b1);
    paint[2][0] = Extend4To8(r2);
    paint[2][1] = Extend4To8(g2);
    paint[2][2] = Extend4To8(b2);
    for (int c = 0; c < 3; ++c) {
      paint[1][c] = Clamp8(static_cast<int32_t>(paint[2][c]) + distance);
      paint[3][c] = Clamp8(static_cast<int32_t>(paint[2][c]) - distance);
    }
    for (int i = 0; i < 16; ++i) {
      const int x = i / 4;
      const int y = i % 4;
      const uint32_t index = (GetBit(src, 16 + i) << 1) | GetBit(src, i);
      if (alpha_mode && !diff_opaque && index == 2) {
        StorePixel(rgba, y * 4 + x, 0, 0, 0, 0);
        continue;
      }
      StorePixel(rgba, y * 4 + x, paint[index][0], paint[index][1],
                 paint[index][2], 255);
    }
    return;
  }

  // H mode.
  if (sel_bg + sel_dg < 0 || sel_bg + sel_dg > 31) {
    const uint8_t r1 = static_cast<uint8_t>(GetBits(src, 59, 62));
    const uint8_t g1a = static_cast<uint8_t>(GetBits(src, 56, 58));
    const uint8_t g1b = static_cast<uint8_t>(GetBit(src, 52));
    const uint8_t b1a = static_cast<uint8_t>(GetBit(src, 51));
    const uint8_t b1b = static_cast<uint8_t>(GetBits(src, 47, 49));
    const uint8_t r2 = static_cast<uint8_t>(GetBits(src, 43, 46));
    const uint8_t g2 = static_cast<uint8_t>(GetBits(src, 39, 42));
    const uint8_t b2 = static_cast<uint8_t>(GetBits(src, 35, 38));
    uint8_t base[2][3] = {{Extend4To8(r1),
                           Extend4To8(static_cast<uint8_t>((g1a << 1) | g1b)),
                           Extend4To8(static_cast<uint8_t>((b1a << 3) | b1b))},
                          {Extend4To8(r2), Extend4To8(g2), Extend4To8(b2)}};
    const uint32_t base0 = (static_cast<uint32_t>(base[0][0]) << 16) |
                           (static_cast<uint32_t>(base[0][1]) << 8) |
                           base[0][2];
    const uint32_t base1 = (static_cast<uint32_t>(base[1][0]) << 16) |
                           (static_cast<uint32_t>(base[1][1]) << 8) |
                           base[1][2];
    const uint32_t dist_index = (GetBit(src, 34) << 2) |
                                (GetBit(src, 32) << 1) |
                                (base0 >= base1 ? 1u : 0u);
    const int32_t distance = kEtc2Distances[dist_index];
    uint8_t paint[4][3];
    for (int c = 0; c < 3; ++c) {
      paint[0][c] = Clamp8(static_cast<int32_t>(base[0][c]) + distance);
      paint[1][c] = Clamp8(static_cast<int32_t>(base[0][c]) - distance);
      paint[2][c] = Clamp8(static_cast<int32_t>(base[1][c]) + distance);
      paint[3][c] = Clamp8(static_cast<int32_t>(base[1][c]) - distance);
    }
    for (int i = 0; i < 16; ++i) {
      const int x = i / 4;
      const int y = i % 4;
      const uint32_t index = (GetBit(src, 16 + i) << 1) | GetBit(src, i);
      if (alpha_mode && !diff_opaque && index == 2) {
        StorePixel(rgba, y * 4 + x, 0, 0, 0, 0);
        continue;
      }
      StorePixel(rgba, y * 4 + x, paint[index][0], paint[index][1],
                 paint[index][2], 255);
    }
    return;
  }

  // Planar mode.
  const uint8_t go1 = static_cast<uint8_t>(GetBit(src, 56));
  const uint8_t go2 = static_cast<uint8_t>(GetBits(src, 49, 54));
  const uint8_t bo1 = static_cast<uint8_t>(GetBit(src, 48));
  const uint8_t bo2 = static_cast<uint8_t>(GetBits(src, 43, 44));
  const uint8_t bo3 = static_cast<uint8_t>(GetBits(src, 39, 41));
  const uint8_t rh1 = static_cast<uint8_t>(GetBits(src, 34, 38));
  const uint8_t rh2 = static_cast<uint8_t>(GetBit(src, 32));
  const uint8_t ro = Extend6To8(static_cast<uint8_t>(GetBits(src, 57, 62)));
  const uint8_t go = Extend7To8(static_cast<uint8_t>((go1 << 6) | go2));
  const uint8_t bo =
      Extend6To8(static_cast<uint8_t>((bo1 << 5) | (bo2 << 3) | bo3));
  const uint8_t rh = Extend6To8(static_cast<uint8_t>((rh1 << 1) | rh2));
  const uint8_t gh = Extend7To8(static_cast<uint8_t>(GetBits(src, 25, 31)));
  const uint8_t bh = Extend6To8(static_cast<uint8_t>(GetBits(src, 19, 24)));
  const uint8_t rv = Extend6To8(static_cast<uint8_t>(GetBits(src, 13, 18)));
  const uint8_t gv = Extend7To8(static_cast<uint8_t>(GetBits(src, 6, 12)));
  const uint8_t bv = Extend6To8(static_cast<uint8_t>(GetBits(src, 0, 5)));

  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      const int32_t r = (x * (static_cast<int32_t>(rh) - ro) +
                         y * (static_cast<int32_t>(rv) - ro) + 4 * ro + 2) >>
                        2;
      const int32_t g = (x * (static_cast<int32_t>(gh) - go) +
                         y * (static_cast<int32_t>(gv) - go) + 4 * go + 2) >>
                        2;
      const int32_t b = (x * (static_cast<int32_t>(bh) - bo) +
                         y * (static_cast<int32_t>(bv) - bo) + 4 * bo + 2) >>
                        2;
      StorePixel(rgba, y * 4 + x, Clamp8(r), Clamp8(g), Clamp8(b), 255);
    }
  }
}

void DecodeEac8(uint8_t* rgba, uint64_t src, int channel) {
  const uint8_t base = static_cast<uint8_t>(GetBits(src, 56, 63));
  const uint8_t multiplier = static_cast<uint8_t>(GetBits(src, 52, 55));
  const uint32_t table = GetBits(src, 48, 51);
  for (int i = 0; i < 16; ++i) {
    const int x = i / 4;
    const int y = i % 4;
    const int bit = 45 - 3 * i;
    const uint32_t index = (GetBit(src, bit + 2) << 2) |
                           (GetBit(src, bit + 1) << 1) | GetBit(src, bit);
    const int32_t modifier = kEtc2AlphaModifiers[table][index];
    const uint8_t value =
        Clamp8(static_cast<int32_t>(base) + multiplier * modifier);
    rgba[(y * 4 + x) * 4 + channel] = value;
  }
}

void DecodeEac11(uint8_t* rgba, uint64_t src, bool signed_mode, int channel) {
  const int32_t multiplier = static_cast<int32_t>(GetBits(src, 52, 55));
  const uint32_t table = GetBits(src, 48, 51);
  int32_t base = static_cast<int32_t>(GetBits(src, 56, 63));
  if (signed_mode) {
    if (base > 127)
      base -= 256;
    if (base == -128)
      base = -127;
  }
  for (int i = 0; i < 16; ++i) {
    const int x = i / 4;
    const int y = i % 4;
    const int bit = 45 - 3 * i;
    const uint32_t index = (GetBit(src, bit + 2) << 2) |
                           (GetBit(src, bit + 1) << 1) | GetBit(src, bit);
    const int32_t modifier = kEtc2AlphaModifiers[table][index];
    int32_t value;
    if (signed_mode) {
      value = multiplier != 0 ? base * 8 + multiplier * modifier * 8
                              : base * 8 + modifier;
      value = std::max(-1023, std::min(1023, value));
      value = (value + 1023) * 255 / 2046;
    } else {
      value = multiplier != 0 ? base * 8 + 4 + multiplier * modifier * 8
                              : base * 8 + 4 + modifier;
      value = std::max(0, std::min(2047, value)) >> 3;
    }
    rgba[(y * 4 + x) * 4 + channel] = static_cast<uint8_t>(value);
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
      DecodeEtc2Color(rgba, Get64(block), false);
      return true;
    case GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2:
    case GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2:
      DecodeEtc2Color(rgba, Get64(block), true);
      return true;
    case GL_COMPRESSED_RGBA8_ETC2_EAC:
    case GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC:
      // RGBA8 EAC stores the alpha block first, followed by the colour block.
      DecodeEtc2Color(rgba, Get64(block + 8), false);
      DecodeEac8(rgba, Get64(block), 3);
      return true;
    case GL_COMPRESSED_R11_EAC:
    case GL_COMPRESSED_SIGNED_R11_EAC:
      DecodeEac11(rgba, Get64(block),
                  format == GL_COMPRESSED_SIGNED_R11_EAC, 0);
      for (int i = 0; i < 16; ++i) {
        rgba[i * 4 + 1] = rgba[i * 4];
        rgba[i * 4 + 2] = rgba[i * 4];
        rgba[i * 4 + 3] = 255;
      }
      return true;
    case GL_COMPRESSED_RG11_EAC:
    case GL_COMPRESSED_SIGNED_RG11_EAC: {
      const bool signed_values = format == GL_COMPRESSED_SIGNED_RG11_EAC;
      DecodeEac11(rgba, Get64(block), signed_values, 0);
      DecodeEac11(rgba, Get64(block + 8), signed_values, 1);
      for (int i = 0; i < 16; ++i) {
        rgba[i * 4 + 2] = 0;
        rgba[i * 4 + 3] = 255;
      }
      return true;
    }
    default:
      return false;
  }
}

}  // namespace zgl
