// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Visual regression suite. Each case renders a small scene with the public
// API, asserts a few pixels, and writes a PPM that the README gallery
// displays. Cases are independent: each creates and destroys its own context.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#include <string>
#include <vector>

#include "GLES3/gl32.h"
#include "tests/visual/visual_common.h"
#include "zgl/zgl.h"

namespace {

constexpr float kPi = 3.14159265358979323846f;

bool FinishCase(const char* path, int width, int height, bool ok) {
  if (!zglvis::TakeGlError("case"))
    ok = false;
  if (!zglvis::WritePpm(path, width, height))
    ok = false;
  return ok;
}

// ---------------------------------------------------------------------------
// 01: A full-screen gradient with a vignette, using mix() and smoothstep().

bool RenderGradient(const char* path, int width, int height) {
  if (zglCreateContext(width, height) != 1)
    return false;
  const char* vertex =
      "#version 100\n"
      "attribute vec2 a_pos;\n"
      "varying vec2 v_uv;\n"
      "void main() { v_uv = a_pos * 0.5 + 0.5; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "varying vec2 v_uv;\n"
      "void main() {\n"
      "  vec3 lower = vec3(0.02, 0.05, 0.12);\n"
      "  vec3 upper = vec3(0.10, 0.22, 0.40);\n"
      "  vec3 colour = mix(lower, upper, v_uv.y);\n"
      "  float d = length(v_uv - vec2(0.5, 0.55));\n"
      "  colour += vec3(0.05, 0.10, 0.18) * (1.0 - smoothstep(0.0, 0.7, d));\n"
      "  colour *= 1.0 - 0.35 * d;\n"
      "  gl_FragColor = vec4(colour, 1.0);\n"
      "}\n";
  const GLuint program =
      zglvis::BuildProgram(vertex, fragment, {{0, "a_pos"}});
  if (!program)
    return false;
  glUseProgram(program);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  zglvis::DrawFullscreenTriangle();
  unsigned char centre[4] = {};
  unsigned char top[4] = {};
  zglvis::ReadTexel(width / 2, height / 2, width, height, centre);
  zglvis::ReadTexel(width / 2, 2, width, height, top);
  bool ok = !zglvis::ColourIsDark(centre) && centre[2] > centre[0];
  glDeleteProgram(program);
  return FinishCase(path, width, height, ok);
}

// ---------------------------------------------------------------------------
// 02: A vertex-colour triangle. Exercises attribute arrays and varying
// interpolation.

bool RenderTriangle(const char* path, int width, int height) {
  if (zglCreateContext(width, height) != 1)
    return false;
  const char* vertex =
      "#version 100\n"
      "attribute vec2 a_pos;\n"
      "attribute vec3 a_color;\n"
      "varying vec3 v_color;\n"
      "void main() { v_color = a_color; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "varying vec3 v_color;\n"
      "void main() { gl_FragColor = vec4(v_color, 1.0); }\n";
  const GLuint program =
      zglvis::BuildProgram(vertex, fragment, {{0, "a_pos"}, {1, "a_color"}});
  if (!program)
    return false;
  static const float kPositions[] = {-0.85f, -0.85f, 0.85f, -0.85f, 0.05f, 0.85f};
  static const float kColours[] = {1.00f, 0.20f, 0.15f, 0.15f, 0.90f, 0.30f,
                                   0.20f, 0.35f, 1.00f};
  glUseProgram(program);
  glClearColor(0.02f, 0.02f, 0.05f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, kPositions);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, kColours);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  unsigned char centre[4] = {};
  unsigned char left[4] = {};
  zglvis::ReadTexel(width / 2, height / 2, width, height, centre);
  zglvis::ReadTexel(width * 2 / 5, height * 4 / 5, width, height, left);
  bool ok = !zglvis::ColourIsDark(centre) && left[0] > left[2];
  glDeleteProgram(program);
  return FinishCase(path, width, height, ok);
}

// ---------------------------------------------------------------------------
// 03: An indexed quad backed by an element buffer.

bool RenderIndexedQuad(const char* path, int width, int height) {
  if (zglCreateContext(width, height) != 1)
    return false;
  const char* vertex =
      "#version 100\n"
      "attribute vec2 a_pos;\n"
      "attribute vec3 a_color;\n"
      "varying vec3 v_color;\n"
      "void main() { v_color = a_color; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "varying vec3 v_color;\n"
      "void main() { gl_FragColor = vec4(v_color, 1.0); }\n";
  const GLuint program =
      zglvis::BuildProgram(vertex, fragment, {{0, "a_pos"}, {1, "a_color"}});
  if (!program)
    return false;
  static const float kVertices[] = {
      -0.9f, -0.9f, 1.0f, 0.2f, 0.2f,  // red, bottom left
      0.9f,  -0.9f, 0.2f, 1.0f, 0.3f,  // green, bottom right
      0.9f,  0.9f,  0.2f, 0.4f, 1.0f,  // blue, top right
      -0.9f, 0.9f,  1.0f, 0.9f, 0.2f,  // yellow, top left
  };
  static const unsigned short kIndices[] = {0, 1, 2, 0, 2, 3};
  GLuint vertex_buffer = 0;
  GLuint index_buffer = 0;
  glGenBuffers(1, &vertex_buffer);
  glGenBuffers(1, &index_buffer);
  glUseProgram(program);
  glClearColor(0.01f, 0.01f, 0.02f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof(kVertices), kVertices, GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kIndices), kIndices,
               GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        reinterpret_cast<const void*>(2 * sizeof(float)));
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
  unsigned char samples[4][4] = {};
  zglvis::ReadTexel(width / 6, height / 6, width, height, samples[0]);
  zglvis::ReadTexel(width * 5 / 6, height / 6, width, height, samples[1]);
  zglvis::ReadTexel(width * 5 / 6, height * 5 / 6, width, height, samples[2]);
  zglvis::ReadTexel(width / 6, height * 5 / 6, width, height, samples[3]);
  int distinct = 0;
  for (int i = 1; i < 4; ++i) {
    if (abs(static_cast<int>(samples[i][0]) - samples[0][0]) > 30 ||
        abs(static_cast<int>(samples[i][2]) - samples[0][2]) > 30)
      ++distinct;
  }
  bool ok = samples[0][0] + samples[0][1] + samples[0][2] > 30 &&
            samples[2][0] + samples[2][1] + samples[2][2] > 30 &&
            distinct >= 2;
  glDeleteBuffers(1, &index_buffer);
  glDeleteBuffers(1, &vertex_buffer);
  glDeleteProgram(program);
  return FinishCase(path, width, height, ok);
}

// ---------------------------------------------------------------------------
// 04: Depth testing. The near triangle must occlude the far triangle.

bool RenderDepthTest(const char* path, int width, int height) {
  if (zglCreateContext(width, height) != 1)
    return false;
  const char* vertex =
      "#version 100\n"
      "attribute vec3 a_pos;\n"
      "attribute vec3 a_color;\n"
      "varying vec3 v_color;\n"
      "void main() { v_color = a_color; gl_Position = vec4(a_pos, 1.0); }\n";
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "varying vec3 v_color;\n"
      "void main() { gl_FragColor = vec4(v_color, 1.0); }\n";
  const GLuint program =
      zglvis::BuildProgram(vertex, fragment, {{0, "a_pos"}, {1, "a_color"}});
  if (!program)
    return false;
  static const float kFar[] = {
      -0.95f, -0.95f, 0.9f, 0.15f, 0.35f, 1.0f,  0.95f, -0.95f,
      0.9f,   0.15f,  0.35f, 1.0f,  0.0f,  0.95f, 0.9f, 0.15f,
      0.35f,  1.0f,
  };
  static const float kNear[] = {
      -0.55f, -0.55f, 0.1f, 1.0f, 0.2f, 0.2f, 0.55f, -0.55f,
      0.1f,   1.0f,   0.2f, 0.2f, 0.0f, 0.55f, 0.1f,  1.0f,
      0.2f,   0.2f,
  };
  glUseProgram(program);
  glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), kFar);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        kFar + 3);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), kNear);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        kNear + 3);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  unsigned char centre[4] = {};
  unsigned char corner[4] = {};
  zglvis::ReadTexel(width / 2, height / 2, width, height, centre);
  zglvis::ReadTexel(width * 2 / 3, height / 2, width, height, corner);
  bool ok = centre[0] > 180 && centre[2] < 80 && corner[2] > 150;
  glDeleteProgram(program);
  return FinishCase(path, width, height, ok);
}

// ---------------------------------------------------------------------------
// 05: Alpha blending over an opaque base layer.

bool RenderAlphaBlend(const char* path, int width, int height) {
  if (zglCreateContext(width, height) != 1)
    return false;
  const char* vertex =
      "#version 100\n"
      "attribute vec2 a_pos;\n"
      "attribute vec4 a_color;\n"
      "varying vec4 v_color;\n"
      "void main() { v_color = a_color; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "varying vec4 v_color;\n"
      "void main() { gl_FragColor = v_color; }\n";
  const GLuint program =
      zglvis::BuildProgram(vertex, fragment, {{0, "a_pos"}, {1, "a_color"}});
  if (!program)
    return false;
  // Two quads: an opaque blue base and a half-transparent red overlay.
  const float blue[] = {
      -0.8f, -0.55f, 0.1f, 0.25f, 0.9f, 1.0f, 0.2f,  -0.55f,
      0.1f,  0.25f,  0.9f, 1.0f,  0.2f,  0.55f, 0.1f, 0.25f,
      0.9f,  1.0f,   -0.8f, -0.55f, 0.1f, 0.25f, 0.9f, 1.0f,
      0.2f,  0.55f,  0.1f, 0.25f,  0.9f, 1.0f,  -0.8f, 0.55f,
      0.1f,  0.25f,  0.9f, 1.0f,
  };
  const float red[] = {
      -0.15f, -0.55f, 0.95f, 0.12f, 0.2f, 0.5f, 0.8f,  -0.55f,
      0.95f,  0.12f,  0.2f,  0.5f,  0.8f, 0.55f, 0.95f, 0.12f,
      0.2f,   0.5f,   -0.15f, -0.55f, 0.95f, 0.12f, 0.2f, 0.5f,
      0.8f,   0.55f,  0.95f, 0.12f,  0.2f, 0.5f,  -0.15f, 0.55f,
      0.95f,  0.12f,  0.2f,  0.5f,
  };
  glUseProgram(program);
  glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glDisable(GL_BLEND);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), blue);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), blue + 2);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), red);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), red + 2);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  unsigned char base[4] = {};
  unsigned char overlap[4] = {};
  zglvis::ReadTexel(width / 5, height / 2, width, height, base);
  zglvis::ReadTexel(width / 2, height / 2, width, height, overlap);
  bool ok = base[2] > 170 && overlap[0] > 80 && overlap[2] > 60;
  glDeleteProgram(program);
  return FinishCase(path, width, height, ok);
}

// ---------------------------------------------------------------------------
// Textures.

GLuint MakeCheckerTexture(int size, int tile, bool mipmaps) {
  std::vector<unsigned char> pixels(static_cast<size_t>(size) * size * 4);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const bool odd = ((x / tile) + (y / tile)) % 2 != 0;
      const int offset = (y * size + x) * 4;
      pixels[offset + 0] = odd ? 240 : 35;
      pixels[offset + 1] = odd ? 240 : 35;
      pixels[offset + 2] = odd ? 240 : 35;
      pixels[offset + 3] = 255;
    }
  }
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, pixels.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  if (mipmaps)
    glGenerateMipmap(GL_TEXTURE_2D);
  return texture;
}

GLuint MakeTexturedProgram() {
  const char* vertex =
      "#version 100\n"
      "attribute vec2 a_pos;\n"
      "attribute vec2 a_uv;\n"
      "varying vec2 v_uv;\n"
      "void main() { v_uv = a_uv; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "varying vec2 v_uv;\n"
      "uniform sampler2D u_tex;\n"
      "void main() { gl_FragColor = texture2D(u_tex, v_uv); }\n";
  return zglvis::BuildProgram(vertex, fragment,
                              {{0, "a_pos"}, {1, "a_uv"}});
}

void DrawTexturedQuad(float left, float right, float bottom, float top) {
  const float vertices[] = {
      left,  bottom, 0.0f, 0.0f, right, bottom, 1.0f, 0.0f,
      right, top,    1.0f, 1.0f, left,  bottom, 0.0f, 0.0f,
      right, top,    1.0f, 1.0f, left,  top,    0.0f, 1.0f,
  };
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), vertices);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        vertices + 2);
  glDrawArrays(GL_TRIANGLES, 0, 6);
}

bool RenderTextureCase(const char* path,
                       int width,
                       int height,
                       bool linear,
                       bool mipmap_case) {
  if (zglCreateContext(width, height) != 1)
    return false;
  const GLuint program = MakeTexturedProgram();
  if (!program)
    return false;
  const GLuint texture =
      MakeCheckerTexture(mipmap_case ? 256 : 8, mipmap_case ? 4 : 1, true);
  glUseProgram(program);
  glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glUniform1i(glGetUniformLocation(program, "u_tex"), 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  mipmap_case ? GL_LINEAR_MIPMAP_LINEAR
                              : (linear ? GL_LINEAR : GL_NEAREST));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                  linear || mipmap_case ? GL_LINEAR : GL_NEAREST);
  DrawTexturedQuad(-0.9f, 0.9f, -0.9f, 0.9f);
  unsigned char centre[4] = {};
  unsigned char offset[4] = {};
  zglvis::ReadTexel(width / 2, height / 2, width, height, centre);
  zglvis::ReadTexel(width / 2 + 20, height / 2, width, height, offset);
  bool ok = centre[0] + centre[1] + centre[2] > 60 &&
            offset[0] + offset[1] + offset[2] > 60;
  if (linear && !mipmap_case) {
    // Linear magnification must produce intermediate values near a texel
    // boundary instead of the pure black or white of nearest sampling.
    ok = ok && centre[0] > 45 && centre[0] < 230;
  }
  glDeleteTextures(1, &texture);
  glDeleteProgram(program);
  return FinishCase(path, width, height, ok);
}

// 06: Nearest-neighbour magnification of an 8x8 checker texture.
bool RenderTextureNearest(const char* path, int width, int height) {
  return RenderTextureCase(path, width, height, false, false);
}

// 07: Bilinear magnification of the same checker texture.
bool RenderTextureLinear(const char* path, int width, int height) {
  return RenderTextureCase(path, width, height, true, false);
}

// 08: Two mip levels of a 256x256 checker texture side by side.
bool RenderMipmap(const char* path, int width, int height) {
  if (zglCreateContext(width, height) != 1)
    return false;
  const char* vertex =
      "#version 300 es\n"
      "in vec2 a_pos;\n"
      "out vec2 v_uv;\n"
      "void main() { v_uv = a_pos * 0.5 + 0.5; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
  const char* fragment =
      "#version 300 es\n"
      "precision mediump float;\n"
      "in vec2 v_uv;\n"
      "uniform sampler2D u_tex;\n"
      "uniform float u_lod;\n"
      "out vec4 o_colour;\n"
      "void main() { o_colour = textureLod(u_tex, v_uv, u_lod); }\n";
  const GLuint program =
      zglvis::BuildProgram(vertex, fragment, {{0, "a_pos"}});
  if (!program)
    return false;
  const GLuint texture = MakeCheckerTexture(256, 2, true);
  glUseProgram(program);
  glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glUniform1i(glGetUniformLocation(program, "u_tex"), 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glUniform1f(glGetUniformLocation(program, "u_lod"), 0.0f);
  DrawTexturedQuad(-0.92f, -0.04f, -0.9f, 0.9f);
  glUniform1f(glGetUniformLocation(program, "u_lod"), 3.0f);
  DrawTexturedQuad(0.04f, 0.92f, -0.9f, 0.9f);
  unsigned char left[4] = {};
  unsigned char right[4] = {};
  zglvis::ReadTexel(width / 4, height / 2, width, height, left);
  zglvis::ReadTexel(width * 3 / 4, height / 2, width, height, right);
  bool ok = left[0] + left[1] + left[2] > 60 &&
            right[0] + right[1] + right[2] > 60;
  glDeleteTextures(1, &texture);
  glDeleteProgram(program);
  return FinishCase(path, width, height, ok);
}

// ---------------------------------------------------------------------------
// 09: Back-face culling. Only the counter-clockwise triangle may survive.

bool RenderCulling(const char* path, int width, int height) {
  if (zglCreateContext(width, height) != 1)
    return false;
  const char* vertex =
      "#version 100\n"
      "attribute vec2 a_pos;\n"
      "attribute vec3 a_color;\n"
      "varying vec3 v_color;\n"
      "void main() { v_color = a_color; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "varying vec3 v_color;\n"
      "void main() { gl_FragColor = vec4(v_color, 1.0); }\n";
  const GLuint program =
      zglvis::BuildProgram(vertex, fragment, {{0, "a_pos"}, {1, "a_color"}});
  if (!program)
    return false;
  // Green is counter-clockwise and front-facing; the red copy is clockwise and
  // must be removed by GL_BACK culling.
  static const float kGreen[] = {
      -0.8f, -0.7f, 0.2f, 0.9f, 0.3f, 0.8f, -0.7f, 0.2f, 0.9f,
      0.3f,  0.0f,  0.85f, 0.2f, 0.9f, 0.3f,
  };
  static const float kRed[] = {
      -0.8f, 0.1f, 1.0f, 0.15f, 0.2f, 0.85f, 0.1f, 1.0f, 0.15f,
      0.2f,  0.0f, -0.75f, 1.0f, 0.15f, 0.2f,
  };
  glUseProgram(program);
  glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glFrontFace(GL_CCW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), kRed);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), kRed + 2);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), kGreen);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        kGreen + 2);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  unsigned char centre[4] = {};
  zglvis::ReadTexel(width / 2, height / 2, width, height, centre);
  bool ok = centre[1] > 150 && centre[0] < 100;
  glDeleteProgram(program);
  return FinishCase(path, width, height, ok);
}

// ---------------------------------------------------------------------------
// 10: Scissor rectangle clips both clears and draws.

bool RenderScissor(const char* path, int width, int height) {
  if (zglCreateContext(width, height) != 1)
    return false;
  glClearColor(0.65f, 0.10f, 0.12f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_SCISSOR_TEST);
  glScissor(width / 4, height / 4, width / 2, height / 2);
  glClearColor(0.10f, 0.35f, 0.80f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glDisable(GL_SCISSOR_TEST);
  unsigned char centre[4] = {};
  unsigned char corner[4] = {};
  zglvis::ReadTexel(width / 2, height / 2, width, height, centre);
  zglvis::ReadTexel(4, 4, width, height, corner);
  bool ok = centre[2] > 150 && corner[0] > 120;
  return FinishCase(path, width, height, ok);
}

// ---------------------------------------------------------------------------
// 11: Stencil mask. The gradient is only visible inside a diamond.

bool RenderStencil(const char* path, int width, int height) {
  if (zglCreateContext(width, height) != 1)
    return false;
  const char* solid_vertex =
      "#version 100\n"
      "attribute vec2 a_pos;\n"
      "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
  const char* solid_fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "void main() { gl_FragColor = vec4(1.0); }\n";
  const GLuint solid =
      zglvis::BuildProgram(solid_vertex, solid_fragment, {{0, "a_pos"}});
  const char* gradient_vertex =
      "#version 100\n"
      "attribute vec2 a_pos;\n"
      "varying vec2 v_uv;\n"
      "void main() { v_uv = a_pos * 0.5 + 0.5; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
  const char* gradient_fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "varying vec2 v_uv;\n"
      "void main() {\n"
      "  vec3 a = vec3(0.95, 0.25, 0.55);\n"
      "  vec3 b = vec3(0.15, 0.85, 0.95);\n"
      "  gl_FragColor = vec4(mix(a, b, v_uv.y), 1.0);\n"
      "}\n";
  const GLuint gradient = zglvis::BuildProgram(
      gradient_vertex, gradient_fragment, {{0, "a_pos"}});
  if (!solid || !gradient)
    return false;
  static const float kDiamond[] = {
      0.0f, 0.9f, -0.9f, 0.0f, 0.0f, -0.9f,
      0.0f, 0.9f, 0.0f,  -0.9f, 0.9f, 0.0f,
  };
  glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  glEnable(GL_STENCIL_TEST);
  glStencilFunc(GL_ALWAYS, 1, 0xFF);
  glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  glUseProgram(solid);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, kDiamond);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glStencilFunc(GL_EQUAL, 1, 0xFF);
  glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
  glUseProgram(gradient);
  zglvis::DrawFullscreenTriangle();
  glDisable(GL_STENCIL_TEST);
  unsigned char centre[4] = {};
  unsigned char corner[4] = {};
  zglvis::ReadTexel(width / 2, height / 2, width, height, centre);
  zglvis::ReadTexel(4, 4, width, height, corner);
  bool ok = centre[0] > 100 && centre[2] > 60 && zglvis::ColourIsDark(corner);
  glDeleteProgram(gradient);
  glDeleteProgram(solid);
  return FinishCase(path, width, height, ok);
}

// ---------------------------------------------------------------------------
// 12: Instanced triangles with per-instance offsets and colours.

bool RenderInstancing(const char* path, int width, int height) {
  if (zglCreateContext(width, height) != 1)
    return false;
  const char* vertex =
      "#version 100\n"
      "attribute vec2 a_pos;\n"
      "attribute vec2 a_offset;\n"
      "attribute vec3 a_color;\n"
      "varying vec3 v_color;\n"
      "void main() {\n"
      "  v_color = a_color;\n"
      "  gl_Position = vec4(a_pos + a_offset, 0.0, 1.0);\n"
      "}\n";
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "varying vec3 v_color;\n"
      "void main() { gl_FragColor = vec4(v_color, 1.0); }\n";
  const GLuint program = zglvis::BuildProgram(
      vertex, fragment, {{0, "a_pos"}, {1, "a_offset"}, {2, "a_color"}});
  if (!program)
    return false;
  const float quad[] = {
      -0.18f, -0.18f, 0.18f, -0.18f, 0.18f, 0.18f,
      -0.18f, -0.18f, 0.18f, 0.18f,  -0.18f, 0.18f,
  };
  const float offsets[] = {
      -0.55f, -0.55f, 0.00f, -0.55f, 0.55f, -0.55f,
      -0.55f, 0.00f,  0.00f, 0.00f,  0.55f, 0.00f,
      -0.55f, 0.55f,  0.00f, 0.55f,  0.55f, 0.55f,
  };
  const float colours[] = {
      0.95f, 0.30f, 0.25f, 0.95f, 0.85f, 0.30f, 0.95f, 0.55f, 0.25f,
      0.30f, 0.95f, 0.55f, 0.95f, 0.95f, 0.95f, 0.30f, 0.55f, 0.95f,
      0.85f, 0.30f, 0.95f, 0.20f, 0.80f, 0.95f, 0.95f, 0.40f, 0.30f,
  };
  GLuint quad_buffer = 0;
  GLuint offset_buffer = 0;
  GLuint colour_buffer = 0;
  glGenBuffers(1, &quad_buffer);
  glGenBuffers(1, &offset_buffer);
  glGenBuffers(1, &colour_buffer);
  glUseProgram(program);
  glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glBindBuffer(GL_ARRAY_BUFFER, quad_buffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  glBindBuffer(GL_ARRAY_BUFFER, offset_buffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof(offsets), offsets, GL_STATIC_DRAW);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  glBindBuffer(GL_ARRAY_BUFFER, colour_buffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof(colours), colours, GL_STATIC_DRAW);
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
  glVertexAttribDivisor(1, 1);
  glVertexAttribDivisor(2, 1);
  glDrawArraysInstanced(GL_TRIANGLES, 0, 6, 9);
  unsigned char centre[4] = {};
  unsigned char corner[4] = {};
  zglvis::ReadTexel(width / 2, height / 2, width, height, centre);
  zglvis::ReadTexel(12, 12, width, height, corner);
  bool ok = centre[0] > 120 && centre[1] > 120 && centre[2] > 120 &&
            zglvis::ColourIsDark(corner);
  glDeleteBuffers(1, &colour_buffer);
  glDeleteBuffers(1, &offset_buffer);
  glDeleteBuffers(1, &quad_buffer);
  glDeleteProgram(program);
  return FinishCase(path, width, height, ok);
}

// ---------------------------------------------------------------------------
// 13: Rendering into an offscreen framebuffer and displaying its texture.

bool RenderOffscreenFramebuffer(const char* path, int width, int height) {
  if (zglCreateContext(width, height) != 1)
    return false;
  const char* vertex =
      "#version 100\n"
      "attribute vec2 a_pos;\n"
      "varying vec2 v_uv;\n"
      "void main() { v_uv = a_pos * 0.5 + 0.5; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "varying vec2 v_uv;\n"
      "void main() {\n"
      "  float checker = mod(floor(v_uv.x * 10.0) + floor(v_uv.y * 10.0), 2.0);\n"
      "  vec3 warm = mix(vec3(0.95, 0.22, 0.12), vec3(1.0, 0.82, 0.25), v_uv.y);\n"
      "  vec3 cool = mix(vec3(0.08, 0.28, 0.68), vec3(0.10, 0.80, 0.95), checker);\n"
      "  gl_FragColor = vec4(mix(warm, cool, 0.55), 1.0);\n"
      "}\n";
  const GLuint program =
      zglvis::BuildProgram(vertex, fragment, {{0, "a_pos"}});
  const GLuint textured = MakeTexturedProgram();
  if (!program || !textured)
    return false;
  GLuint framebuffer = 0;
  GLuint texture = 0;
  glGenFramebuffers(1, &framebuffer);
  glGenTextures(1, &texture);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         texture, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    fprintf(stderr, "offscreen framebuffer incomplete\n");
    return false;
  }
  glViewport(0, 0, width, height);
  glUseProgram(program);
  zglvis::DrawFullscreenTriangle();
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(textured);
  glUniform1i(glGetUniformLocation(textured, "u_tex"), 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  DrawTexturedQuad(-1.0f, 1.0f, -1.0f, 1.0f);
  unsigned char centre[4] = {};
  zglvis::ReadTexel(width / 2, height / 2, width, height, centre);
  bool ok = !zglvis::ColourIsDark(centre);
  glDeleteTextures(1, &texture);
  glDeleteFramebuffers(1, &framebuffer);
  glDeleteProgram(textured);
  glDeleteProgram(program);
  return FinishCase(path, width, height, ok);
}

// ---------------------------------------------------------------------------
// 14: Procedural GLSL pattern using sin, length, mix and smoothstep.

bool RenderGlslPattern(const char* path, int width, int height) {
  if (zglCreateContext(width, height) != 1)
    return false;
  const char* vertex =
      "#version 100\n"
      "attribute vec2 a_pos;\n"
      "varying vec2 v_uv;\n"
      "void main() { v_uv = a_pos * 0.5 + 0.5; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "varying vec2 v_uv;\n"
      "void main() {\n"
      "  vec2 p = v_uv - vec2(0.5, 0.5);\n"
      "  float r = length(p);\n"
      "  float ring = 0.5 + 0.5 * sin(r * 55.0 - 2.0);\n"
      "  vec3 a = vec3(0.08, 0.35, 0.95);\n"
      "  vec3 b = vec3(1.00, 0.25, 0.45);\n"
      "  vec3 colour = mix(a, b, ring);\n"
      "  float glow = 1.0 - smoothstep(0.0, 0.8, r);\n"
      "  gl_FragColor = vec4(colour * (0.35 + 0.75 * glow), 1.0);\n"
      "}\n";
  const GLuint program =
      zglvis::BuildProgram(vertex, fragment, {{0, "a_pos"}});
  if (!program)
    return false;
  glUseProgram(program);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  zglvis::DrawFullscreenTriangle();
  unsigned char centre[4] = {};
  unsigned char first_ring[4] = {};
  zglvis::ReadTexel(width / 2, height / 2, width, height, centre);
  zglvis::ReadTexel(width / 2 + 40, height / 2, width, height, first_ring);
  bool ok = !zglvis::ColourIsDark(centre) &&
            (abs(static_cast<int>(centre[0]) - first_ring[0]) > 20 ||
             abs(static_cast<int>(centre[2]) - first_ring[2]) > 20);
  glDeleteProgram(program);
  return FinishCase(path, width, height, ok);
}

// ---------------------------------------------------------------------------
// 15: Perspective cube with matrix uniforms and depth testing.

void MatrixIdentity(float out[16]) {
  memset(out, 0, 16 * sizeof(float));
  out[0] = out[5] = out[10] = out[15] = 1.0f;
}

void MatrixMultiply(const float a[16], const float b[16], float out[16]) {
  float result[16] = {};
  for (int column = 0; column < 4; ++column) {
    for (int row = 0; row < 4; ++row) {
      float sum = 0.0f;
      for (int k = 0; k < 4; ++k)
        sum += a[k * 4 + row] * b[column * 4 + k];
      result[column * 4 + row] = sum;
    }
  }
  memcpy(out, result, sizeof(result));
}

void MatrixPerspective(float fov_y,
                       float aspect,
                       float near_z,
                       float far_z,
                       float out[16]) {
  memset(out, 0, 16 * sizeof(float));
  const float focal = 1.0f / tanf(fov_y * 0.5f);
  out[0] = focal / aspect;
  out[5] = focal;
  out[10] = (far_z + near_z) / (near_z - far_z);
  out[11] = -1.0f;
  out[14] = (2.0f * far_z * near_z) / (near_z - far_z);
}

void MatrixTranslation(float x, float y, float z, float out[16]) {
  MatrixIdentity(out);
  out[12] = x;
  out[13] = y;
  out[14] = z;
}

void MatrixRotationX(float radians, float out[16]) {
  MatrixIdentity(out);
  const float cosine = cosf(radians);
  const float sine = sinf(radians);
  out[5] = cosine;
  out[6] = sine;
  out[9] = -sine;
  out[10] = cosine;
}

void MatrixRotationY(float radians, float out[16]) {
  MatrixIdentity(out);
  const float cosine = cosf(radians);
  const float sine = sinf(radians);
  out[0] = cosine;
  out[2] = -sine;
  out[8] = sine;
  out[10] = cosine;
}

void MatrixScaling(float x, float y, float z, float out[16]) {
  MatrixIdentity(out);
  out[0] = x;
  out[5] = y;
  out[10] = z;
}

void BuildCubeVertices(std::vector<float>* vertices) {
  static const float kCorners[8][3] = {
      {-0.5f, -0.5f, 0.5f},  {0.5f, -0.5f, 0.5f},
      {0.5f, 0.5f, 0.5f},    {-0.5f, 0.5f, 0.5f},
      {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f},
      {0.5f, 0.5f, -0.5f},   {-0.5f, 0.5f, -0.5f},
  };
  static const int kFaces[6][4] = {
      {0, 1, 2, 3}, {1, 5, 6, 2}, {3, 2, 6, 7},
      {5, 4, 7, 6}, {4, 0, 3, 7}, {4, 5, 1, 0},
  };
  static const float kColours[6][3] = {
      {0.96f, 0.28f, 0.30f}, {0.98f, 0.66f, 0.20f}, {0.32f, 0.82f, 0.52f},
      {0.28f, 0.44f, 0.92f}, {0.64f, 0.32f, 0.86f}, {0.20f, 0.72f, 0.80f},
  };
  vertices->clear();
  vertices->reserve(36 * 6);
  for (int face = 0; face < 6; ++face) {
    const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; ++i) {
      const int corner = kFaces[face][order[i]];
      vertices->push_back(kCorners[corner][0]);
      vertices->push_back(kCorners[corner][1]);
      vertices->push_back(kCorners[corner][2]);
      vertices->push_back(kColours[face][0]);
      vertices->push_back(kColours[face][1]);
      vertices->push_back(kColours[face][2]);
    }
  }
}

bool RenderPerspectiveCube(const char* path, int width, int height) {
  if (zglCreateContext(width, height) != 1)
    return false;
  const char* vertex =
      "#version 100\n"
      "attribute vec3 a_pos;\n"
      "attribute vec3 a_color;\n"
      "uniform mat4 u_mvp;\n"
      "varying vec3 v_color;\n"
      "void main() { v_color = a_color; gl_Position = u_mvp * vec4(a_pos, 1.0); }\n";
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "varying vec3 v_color;\n"
      "void main() { gl_FragColor = vec4(v_color, 1.0); }\n";
  const GLuint program =
      zglvis::BuildProgram(vertex, fragment, {{0, "a_pos"}, {1, "a_color"}});
  if (!program)
    return false;
  float projection[16];
  float view[16];
  float rotation_x[16];
  float rotation_y[16];
  float scale[16];
  float model[16];
  float mvp[16];
  MatrixPerspective(45.0f * kPi / 180.0f, static_cast<float>(width) / height,
                    0.1f, 20.0f, projection);
  MatrixTranslation(0.0f, 0.0f, -3.6f, view);
  MatrixRotationX(34.0f * kPi / 180.0f, rotation_x);
  MatrixRotationY(34.0f * kPi / 180.0f, rotation_y);
  MatrixScaling(1.45f, 1.45f, 1.45f, scale);
  MatrixMultiply(rotation_x, rotation_y, model);
  MatrixMultiply(model, scale, model);
  MatrixMultiply(view, model, mvp);
  MatrixMultiply(projection, mvp, mvp);
  std::vector<float> vertices;
  BuildCubeVertices(&vertices);
  glUseProgram(program);
  glClearColor(0.03f, 0.05f, 0.10f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glUniformMatrix4fv(glGetUniformLocation(program, "u_mvp"), 1, GL_FALSE,
                     mvp);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        vertices.data());
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        vertices.data() + 3);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size() / 6));
  unsigned char centre[4] = {};
  zglvis::ReadTexel(width / 2, height / 2, width, height, centre);
  bool ok = centre[0] > 120 && centre[2] > 60;
  glDeleteProgram(program);
  return FinishCase(path, width, height, ok);
}

struct VisualCase {
  const char* name;
  const char* title;
  bool (*render)(const char*, int, int);
};

const VisualCase kCases[] = {
    {"01_gradient", "Gradient and vignette", RenderGradient},
    {"02_triangle", "Vertex-colour interpolation", RenderTriangle},
    {"03_indexed_quad", "Element buffer quad", RenderIndexedQuad},
    {"04_depth_test", "Depth test occlusion", RenderDepthTest},
    {"05_alpha_blend", "Alpha blending", RenderAlphaBlend},
    {"06_texture_nearest", "Nearest texture", RenderTextureNearest},
    {"07_texture_linear", "Linear texture", RenderTextureLinear},
    {"08_mipmap", "Mipmapped texture", RenderMipmap},
    {"09_culling", "Back-face culling", RenderCulling},
    {"10_scissor", "Scissor rectangle", RenderScissor},
    {"11_stencil", "Stencil mask", RenderStencil},
    {"12_instancing", "Instanced draw", RenderInstancing},
    {"13_framebuffer", "Offscreen framebuffer",
     RenderOffscreenFramebuffer},
    {"14_glsl_pattern", "GLSL procedural pattern", RenderGlslPattern},
    {"15_perspective_cube", "Perspective cube", RenderPerspectiveCube},
};

}  // namespace

int main(int argc, char** argv) {
  const char* output_dir = argc > 1 ? argv[1] : "visual-out";
  const int width = argc > 2 ? atoi(argv[2]) : 640;
  const int height = argc > 3 ? atoi(argv[3]) : 480;
#if defined(_WIN32)
  _mkdir(output_dir);
#else
  mkdir(output_dir, 0755);
#endif
  int failures = 0;
  for (const VisualCase& visual_case : kCases) {
    std::string path =
        std::string(output_dir) + "/" + visual_case.name + ".ppm";
    printf("RUN %s (%s)\n", visual_case.name, visual_case.title);
    const bool ok = visual_case.render(path.c_str(), width, height);
    if (!ok) {
      printf("FAIL %s\n", visual_case.name);
      ++failures;
    }
  }
  printf("%zu visual cases, %d failed\n", sizeof(kCases) / sizeof(kCases[0]),
         failures);
  return failures == 0 ? 0 : 1;
}
