// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tests/visual/visual_common.h"

namespace glanceasyvis {
namespace {

GLuint CompileShader(GLenum type, const char* source, const char* stage) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint compiled = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (!compiled) {
    char log[2048] = {};
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    fprintf(stderr, "%s shader failed:\n%s\n", stage, log);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

}  // namespace

GLuint BuildProgram(const char* vertex_source,
                    const char* fragment_source,
                    const std::vector<std::pair<int, const char*>>& bindings) {
  const GLuint vertex = CompileShader(GL_VERTEX_SHADER, vertex_source, "vertex");
  const GLuint fragment =
      CompileShader(GL_FRAGMENT_SHADER, fragment_source, "fragment");
  if (!vertex || !fragment) {
    if (vertex)
      glDeleteShader(vertex);
    if (fragment)
      glDeleteShader(fragment);
    return 0;
  }
  const GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  for (const auto& binding : bindings)
    glBindAttribLocation(program, binding.first, binding.second);
  glLinkProgram(program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (!linked) {
    char log[2048] = {};
    glGetProgramInfoLog(program, sizeof(log), nullptr, log);
    fprintf(stderr, "program failed:\n%s\n", log);
    glDeleteProgram(program);
    return 0;
  }
  return program;
}

bool TakeGlError(const char* stage) {
  const GLenum error = glGetError();
  if (error != GL_NO_ERROR) {
    fprintf(stderr, "%s: GL error 0x%04x\n", stage, error);
    return false;
  }
  return true;
}

bool WritePpm(const char* path, int width, int height) {
  std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  FILE* file = fopen(path, "wb");
  if (!file) {
    fprintf(stderr, "cannot open %s\n", path);
    return false;
  }
  fprintf(file, "P6\n%d %d\n255\n", width, height);
  for (int y = height - 1; y >= 0; --y) {
    for (int x = 0; x < width; ++x) {
      const unsigned char* texel =
          &pixels[(static_cast<size_t>(y) * width + x) * 4];
      fwrite(texel, 1, 3, file);
    }
  }
  fclose(file);
  return true;
}

void ReadTexel(int x, int y, int width, int height, unsigned char rgba[4]) {
  (void)width;
  (void)height;
  // The readback path addresses rows from the top of the surface, which is the
  // same order WritePpm and the README screenshots use.
  glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

bool ColourNear(const unsigned char rgba[4],
                int red,
                int green,
                int blue,
                int tolerance) {
  return abs(static_cast<int>(rgba[0]) - red) <= tolerance &&
         abs(static_cast<int>(rgba[1]) - green) <= tolerance &&
         abs(static_cast<int>(rgba[2]) - blue) <= tolerance;
}

bool ColourIsDark(const unsigned char rgba[4]) {
  return rgba[0] < 45 && rgba[1] < 45 && rgba[2] < 45;
}

void DrawFullscreenTriangle() {
  static const float kFullscreen[6] = {-1.0f, -1.0f, 3.0f,
                                       -1.0f, -1.0f, 3.0f};
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, kFullscreen);
  glDrawArrays(GL_TRIANGLES, 0, 3);
}

}  // namespace glanceasyvis
