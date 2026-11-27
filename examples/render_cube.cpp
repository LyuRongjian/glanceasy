// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Renders a small 3D scene with the software GLES implementation and writes it
// to a binary PPM file. The demo is deliberately dependency free so the same
// source can run on the host, under qemu-arm-static, or on an RV1106 board.
//
// Usage: glanceasy_cube_demo [output.ppm] [width] [height]

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "GLES3/gl32.h"
#include "glanceasy/glanceasy.h"

namespace {

constexpr int kDefaultWidth = 800;
constexpr int kDefaultHeight = 600;
constexpr float kPi = 3.14159265358979323846f;

GLuint CompileShader(GLenum type, const char* source) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint compiled = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (!compiled) {
    char log[1024] = {};
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    fprintf(stderr, "shader compilation failed:\n%s\n", log);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

GLuint LinkProgram(const char* vertex_source, const char* fragment_source) {
  const GLuint vertex = CompileShader(GL_VERTEX_SHADER, vertex_source);
  const GLuint fragment = CompileShader(GL_FRAGMENT_SHADER, fragment_source);
  if (!vertex || !fragment)
    return 0;
  const GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glBindAttribLocation(program, 0, "a_pos");
  glBindAttribLocation(program, 1, "a_color");
  glLinkProgram(program);
  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (!linked) {
    char log[1024] = {};
    glGetProgramInfoLog(program, sizeof(log), nullptr, log);
    fprintf(stderr, "program link failed:\n%s\n", log);
    glDeleteProgram(program);
    return 0;
  }
  return program;
}

// Multiplies two column-major 4x4 matrices: out = a * b.
void Multiply(const float a[16], const float b[16], float out[16]) {
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

void Identity(float out[16]) {
  memset(out, 0, 16 * sizeof(float));
  out[0] = out[5] = out[10] = out[15] = 1.0f;
}

void Perspective(float fov_y,
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

void Translation(float x, float y, float z, float out[16]) {
  Identity(out);
  out[12] = x;
  out[13] = y;
  out[14] = z;
}

void Scaling(float x, float y, float z, float out[16]) {
  Identity(out);
  out[0] = x;
  out[5] = y;
  out[10] = z;
}

void RotationX(float radians, float out[16]) {
  Identity(out);
  const float cosine = cosf(radians);
  const float sine = sinf(radians);
  out[5] = cosine;
  out[6] = sine;
  out[9] = -sine;
  out[10] = cosine;
}

void RotationY(float radians, float out[16]) {
  Identity(out);
  const float cosine = cosf(radians);
  const float sine = sinf(radians);
  out[0] = cosine;
  out[2] = -sine;
  out[8] = sine;
  out[10] = cosine;
}

struct Face {
  int corners[4];
  float colour[3];
};

// Two triangles per face, six vertices, six floats per vertex (position and
// colour). Duplicating vertices keeps the demo free of index buffers.
void BuildCube(std::vector<float>* vertices) {
  static const float kCorners[8][3] = {
      {-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f},   {0.5f, 0.5f, 0.5f},
      {-0.5f, 0.5f, 0.5f},  {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f},
      {0.5f, 0.5f, -0.5f},  {-0.5f, 0.5f, -0.5f},
  };
  static const Face kFaces[] = {
      {{0, 1, 2, 3}, {0.96f, 0.28f, 0.30f}},  // +Z
      {{1, 5, 6, 2}, {0.98f, 0.66f, 0.20f}},  // +X
      {{3, 2, 6, 7}, {0.32f, 0.82f, 0.52f}},  // +Y
      {{5, 4, 7, 6}, {0.28f, 0.44f, 0.92f}},  // -Z
      {{4, 0, 3, 7}, {0.64f, 0.32f, 0.86f}},  // -X
      {{4, 5, 1, 0}, {0.20f, 0.72f, 0.80f}},  // -Y
  };
  vertices->clear();
  vertices->reserve(6 * 6 * 6);
  for (const Face& face : kFaces) {
    const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; ++i) {
      const int corner = face.corners[order[i]];
      vertices->push_back(kCorners[corner][0]);
      vertices->push_back(kCorners[corner][1]);
      vertices->push_back(kCorners[corner][2]);
      vertices->push_back(face.colour[0]);
      vertices->push_back(face.colour[1]);
      vertices->push_back(face.colour[2]);
    }
  }
}

bool WritePpm(const char* path, int width, int height) {
  std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  FILE* file = fopen(path, "wb");
  if (!file) {
    fprintf(stderr, "cannot open %s for writing\n", path);
    return false;
  }
  fprintf(file, "P6\n%d %d\n255\n", width, height);
  // glReadPixels returns rows bottom-up; PPM expects them top-down.
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

}  // namespace

int main(int argc, char** argv) {
  const char* output = argc > 1 ? argv[1] : "glanceasy_cube.ppm";
  const int width = argc > 2 ? atoi(argv[2]) : kDefaultWidth;
  const int height = argc > 3 ? atoi(argv[3]) : kDefaultHeight;
  if (width <= 0 || height <= 0) {
    fprintf(stderr, "invalid size %dx%d\n", width, height);
    return 1;
  }
  if (glanceasyCreateContext(width, height) != 1) {
    fprintf(stderr, "cannot create the glanceasy context\n");
    return 1;
  }

  const char* background_vertex =
      "#version 100\n"
      "attribute vec2 a_pos;\n"
      "varying vec2 v_uv;\n"
      "void main() { v_uv = a_pos * 0.5 + 0.5; gl_Position = vec4(a_pos, 0.0, "
      "1.0); }\n";
  const char* background_fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "varying vec2 v_uv;\n"
      "void main() {\n"
      "  vec3 lower = vec3(0.020, 0.035, 0.070);\n"
      "  vec3 upper = vec3(0.085, 0.150, 0.290);\n"
      "  vec3 colour = mix(lower, upper, v_uv.y);\n"
      "  float d = length(v_uv - vec2(0.5, 0.52));\n"
      "  colour += vec3(0.045, 0.080, 0.140) * (1.0 - smoothstep(0.0, 0.62, "
      "d));\n"
      "  colour = colour * (1.0 - 0.35 * d);\n"
      "  gl_FragColor = vec4(colour, 1.0);\n"
      "}\n";
  const GLuint background = LinkProgram(background_vertex, background_fragment);
  if (!background)
    return 1;

  const char* cube_vertex =
      "#version 100\n"
      "attribute vec3 a_pos;\n"
      "attribute vec3 a_color;\n"
      "uniform mat4 u_mvp;\n"
      "varying vec3 v_color;\n"
      "void main() { v_color = a_color; gl_Position = u_mvp * vec4(a_pos, "
      "1.0); }\n";
  const char* cube_fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "varying vec3 v_color;\n"
      "void main() { gl_FragColor = vec4(v_color, 1.0); }\n";
  const GLuint cube = LinkProgram(cube_vertex, cube_fragment);
  if (!cube)
    return 1;

  glViewport(0, 0, width, height);
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glUseProgram(background);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  static const float kFullscreen[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, kFullscreen);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  glDepthMask(GL_TRUE);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glClear(GL_DEPTH_BUFFER_BIT);

  float projection[16];
  float view[16];
  float rotation_x[16];
  float rotation_y[16];
  float scale[16];
  float model[16];
  float mvp[16];
  Perspective(45.0f * kPi / 180.0f, static_cast<float>(width) / height, 0.1f,
              20.0f, projection);
  Translation(0.0f, 0.0f, -3.6f, view);
  RotationX(34.0f * kPi / 180.0f, rotation_x);
  RotationY(34.0f * kPi / 180.0f, rotation_y);
  Scaling(1.45f, 1.45f, 1.45f, scale);
  Multiply(rotation_x, rotation_y, model);
  Multiply(model, scale, model);
  Multiply(view, model, mvp);
  Multiply(projection, mvp, mvp);

  std::vector<float> vertices;
  BuildCube(&vertices);
  glUseProgram(cube);
  const GLint mvp_location = glGetUniformLocation(cube, "u_mvp");
  if (mvp_location >= 0)
    glUniformMatrix4fv(mvp_location, 1, GL_FALSE, mvp);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        vertices.data());
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        vertices.data() + 3);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size() / 6));

  const bool written = WritePpm(output, width, height);
  glDeleteProgram(cube);
  glDeleteProgram(background);
  glanceasyDestroyContext();
  if (!written)
    return 1;
  printf("wrote %s (%dx%d)\n", output, width, height);
  return 0;
}
