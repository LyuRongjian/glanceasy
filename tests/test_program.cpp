// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include "GLES3/gl32.h"
#include "tests/zgl_test.h"
#include "zgl/zgl.h"

namespace {

GLuint MakeShader(GLenum type, const char* source) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  return shader;
}

const char* kVertex =
    "#version 100\n"
    "attribute vec3 a_position;\n"
    "attribute vec2 a_uv;\n"
    "uniform mat4 u_mvp;\n"
    "uniform vec3 u_tint;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "  v_uv = a_uv;\n"
    "  gl_Position = u_mvp * vec4(a_position, 1.0);\n"
    "  gl_PointSize = 2.0;\n"
    "}\n";

const char* kFragment =
    "#version 100\n"
    "precision mediump float;\n"
    "uniform sampler2D u_texture;\n"
    "uniform float u_alpha;\n"
    "varying vec2 v_uv;\n"
    "void main() { gl_FragColor = texture2D(u_texture, v_uv) * u_alpha; }\n";

}  // namespace

TEST(ShaderObjectsCompileAndReportStatus) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const GLuint vertex = MakeShader(GL_VERTEX_SHADER, kVertex);
  EXPECT_EQ(glIsShader(vertex), GL_TRUE);
  GLint value = 0;
  glGetShaderiv(vertex, GL_COMPILE_STATUS, &value);
  EXPECT_EQ(value, GL_TRUE);
  glGetShaderiv(vertex, GL_SHADER_TYPE, &value);
  EXPECT_EQ(value, GL_VERTEX_SHADER);
  glGetShaderiv(vertex, GL_DELETE_STATUS, &value);
  EXPECT_EQ(value, GL_FALSE);
  glGetShaderiv(vertex, GL_INFO_LOG_LENGTH, &value);
  EXPECT_EQ(value, 0);

  const GLuint fragment = MakeShader(GL_FRAGMENT_SHADER, kFragment);
  glGetShaderiv(fragment, GL_COMPILE_STATUS, &value);
  EXPECT_EQ(value, GL_TRUE);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  glDeleteShader(vertex);
  glDeleteShader(fragment);
  EXPECT_EQ(glIsShader(vertex), GL_FALSE);
  zglDestroyContext();
}

TEST(CompileErrorsProduceAnInfoLog) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* broken =
      "#version 100\n"
      "attribute vec2 a_pos;\n"
      "void main() { gl_Position = vec4(a_pos, ; }\n";
  const GLuint shader = MakeShader(GL_VERTEX_SHADER, broken);
  GLint status = GL_TRUE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  EXPECT_EQ(status, GL_FALSE);
  GLint log_length = 0;
  glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
  EXPECT_TRUE(log_length > 0);
  char log[256] = {};
  GLsizei written = 0;
  glGetShaderInfoLog(shader, static_cast<GLsizei>(sizeof(log)), &written, log);
  EXPECT_TRUE(written > 0);
  EXPECT_TRUE(log[0] != '\0');
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glDeleteShader(shader);
  zglDestroyContext();
}

TEST(LinkRequiresTheMandatoryOutputs) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  // A vertex shader without gl_Position cannot link.
  const char* no_position =
      "#version 100\n"
      "attribute vec2 a_pos;\n"
      "void main() { vec4 unused = vec4(a_pos, 0.0, 1.0); }\n";
  const GLuint vs = MakeShader(GL_VERTEX_SHADER, no_position);
  const GLuint fs = MakeShader(GL_FRAGMENT_SHADER, kFragment);
  const GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  GLint linked = GL_TRUE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  EXPECT_EQ(linked, GL_FALSE);

  // Linking a program with no shaders fails as well.
  const GLuint empty = glCreateProgram();
  glLinkProgram(empty);
  glGetProgramiv(empty, GL_LINK_STATUS, &linked);
  EXPECT_EQ(linked, GL_FALSE);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glDeleteProgram(empty);
  glDeleteProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  zglDestroyContext();
}

TEST(ProgramIntrospectionListsAttributesAndUniforms) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const GLuint vs = MakeShader(GL_VERTEX_SHADER, kVertex);
  const GLuint fs = MakeShader(GL_FRAGMENT_SHADER, kFragment);
  const GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glBindAttribLocation(program, 1, "a_uv");
  glLinkProgram(program);
  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  EXPECT_EQ(linked, GL_TRUE);
  GLint attached = 0;
  glGetProgramiv(program, GL_ATTACHED_SHADERS, &attached);
  EXPECT_EQ(attached, 2);

  GLint count = 0;
  glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &count);
  EXPECT_EQ(count, 2);
  for (GLint i = 0; i < count; ++i) {
    char name[64] = {};
    GLint size = 0;
    GLenum type = 0;
    GLsizei length = 0;
    glGetActiveAttrib(program, static_cast<GLuint>(i),
                      static_cast<GLsizei>(sizeof(name)), &length, &size, &type,
                      name);
    EXPECT_TRUE(length > 0);
    if (strcmp(name, "a_position") == 0) {
      EXPECT_EQ(size, 3);
      EXPECT_EQ(type, GL_FLOAT_VEC3);
    } else {
      EXPECT_EQ(strcmp(name, "a_uv"), 0);
      EXPECT_EQ(size, 2);
      EXPECT_EQ(type, GL_FLOAT_VEC2);
    }
  }
  EXPECT_EQ(glGetAttribLocation(program, "a_uv"), 1);
  EXPECT_EQ(glGetAttribLocation(program, "missing"), -1);

  glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &count);
  EXPECT_EQ(count, 4);
  for (GLint i = 0; i < count; ++i) {
    char name[64] = {};
    GLint size = 0;
    GLenum type = 0;
    glGetActiveUniform(program, static_cast<GLuint>(i), 64, nullptr, &size,
                       &type, name);
    if (strcmp(name, "u_mvp") == 0)
      EXPECT_EQ(type, GL_FLOAT_MAT4);
    else if (strcmp(name, "u_tint") == 0)
      EXPECT_EQ(type, GL_FLOAT_VEC3);
    else if (strcmp(name, "u_texture") == 0)
      EXPECT_EQ(type, GL_SAMPLER_2D);
    else
      EXPECT_EQ(strcmp(name, "u_alpha"), 0);
  }
  EXPECT_TRUE(glGetUniformLocation(program, "u_mvp") >= 0);
  EXPECT_EQ(glGetUniformLocation(program, "no_such_uniform"), -1);

  glDeleteProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  zglDestroyContext();
}

TEST(UniformValuesRoundTripThroughGetters) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* vertex =
      "#version 100\n"
      "attribute vec2 a_pos;\n"
      "uniform mat4 u_m;\n"
      "uniform vec4 u_v;\n"
      "uniform ivec3 u_i;\n"
      "uniform bool u_b;\n"
      "void main() { gl_Position = u_m * vec4(a_pos, u_v.z, 1.0) + "
      "vec4(float(u_i.x), float(u_b), 0.0, 0.0); }\n";
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "uniform float u_f;\n"
      "void main() { gl_FragColor = vec4(u_f, 0.0, 0.0, 1.0); }\n";
  const GLuint vs = MakeShader(GL_VERTEX_SHADER, vertex);
  const GLuint fs = MakeShader(GL_FRAGMENT_SHADER, fragment);
  const GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  glUseProgram(program);

  GLint location = glGetUniformLocation(program, "u_f");
  EXPECT_TRUE(location >= 0);
  glUniform1f(location, 0.75f);
  GLfloat result = 0.0f;
  glGetUniformfv(program, location, &result);
  EXPECT_NEAR(result, 0.75f, 0.001f);

  location = glGetUniformLocation(program, "u_v");
  glUniform4f(location, 1.0f, 2.0f, 3.0f, 4.0f);
  GLfloat vector[4] = {};
  glGetUniformfv(program, location, vector);
  EXPECT_NEAR(vector[2], 3.0f, 0.001f);
  glUniform4fv(location, 1, vector);

  location = glGetUniformLocation(program, "u_i");
  glUniform3i(location, 5, 6, 7);
  GLint ivec[3] = {};
  glGetUniformiv(program, location, ivec);
  EXPECT_EQ(ivec[1], 6);
  glUniform3iv(location, 1, ivec);

  location = glGetUniformLocation(program, "u_b");
  glUniform1i(location, 1);
  GLint boolean = 0;
  glGetUniformiv(program, location, &boolean);
  EXPECT_EQ(boolean, 1);

  location = glGetUniformLocation(program, "u_m");
  GLfloat matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  glUniformMatrix4fv(location, 1, GL_FALSE, matrix);
  GLfloat read_matrix[16] = {};
  glGetUniformfv(program, location, read_matrix);
  EXPECT_NEAR(read_matrix[5], 1.0f, 0.001f);

  // Setting a uniform while no program is bound is silently ignored.
  glUseProgram(0);
  glUniform1f(location, 0.0f);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glDeleteProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  zglDestroyContext();
}

TEST(UnsignedUniformsAndMatricesRoundTrip) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* vertex =
      "#version 300 es\n"
      "in vec2 a_pos;\n"
      "uniform mat2 u_m2;\n"
      "uniform mat2x3 u_m23;\n"
      "uniform uvec2 u_u;\n"
      "void main() { gl_Position = vec4(a_pos, u_m2[0][0] + u_m23[0][0] + "
      "float(u_u.x), 1.0); }\n";
  const char* fragment =
      "#version 300 es\n"
      "precision mediump float;\n"
      "out vec4 o_colour;\n"
      "void main() { o_colour = vec4(1.0); }\n";
  const GLuint vs = MakeShader(GL_VERTEX_SHADER, vertex);
  const GLuint fs = MakeShader(GL_FRAGMENT_SHADER, fragment);
  const GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  GLint location = glGetUniformLocation(program, "u_u");
  if (location >= 0) {
    glUniform2ui(location, 7u, 8u);
    GLuint uvec[2] = {};
    glGetUniformuiv(program, location, uvec);
    EXPECT_EQ(uvec[0], 7u);
  }
  location = glGetUniformLocation(program, "u_m2");
  if (location >= 0) {
    const GLfloat m2[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    glUniformMatrix2fv(location, 1, GL_FALSE, m2);
    GLfloat read_back[4] = {};
    glGetUniformfv(program, location, read_back);
    EXPECT_NEAR(read_back[0], 1.0f, 0.001f);
  }
  glDeleteProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  zglDestroyContext();
}

TEST(UniformArraysAreOutsideTheSupportedSubset) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* vertex =
      "#version 100\n"
      "attribute vec2 a_pos;\n"
      "uniform float u_values[2];\n"
      "void main() { gl_Position = vec4(a_pos + u_values[1], 0.0, 1.0); }\n";
  const char* fragment =
      "#version 100\n"
      "precision mediump float;\n"
      "void main() { gl_FragColor = vec4(1.0); }\n";
  const GLuint vs = MakeShader(GL_VERTEX_SHADER, vertex);
  const GLuint fs = MakeShader(GL_FRAGMENT_SHADER, fragment);
  const GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  glUseProgram(program);

  // Array declarations are outside the supported subset, so linking fails and
  // the location lookup reports -1 rather than crashing.
  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  EXPECT_EQ(linked, GL_FALSE);
  EXPECT_EQ(glGetUniformLocation(program, "u_values"), -1);
  glDeleteProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  zglDestroyContext();
}

TEST(ValidationAndUseProgramRejectBadInput) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  glUseProgram(12345);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glValidateProgram(12345);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glGetProgramiv(12345, GL_LINK_STATUS, nullptr);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);

  const GLuint vs = MakeShader(GL_VERTEX_SHADER, kVertex);
  const GLuint fs = MakeShader(GL_FRAGMENT_SHADER, kFragment);
  const GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  glValidateProgram(program);
  GLint validated = GL_FALSE;
  glGetProgramiv(program, GL_VALIDATE_STATUS, &validated);
  EXPECT_EQ(validated, GL_TRUE);

  glDetachShader(program, vs);
  GLint attached = 0;
  glGetProgramiv(program, GL_ATTACHED_SHADERS, &attached);
  EXPECT_EQ(attached, 1);
  glDetachShader(program, 9999);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glAttachShader(9999, fs);
  EXPECT_EQ(glGetError(), GL_INVALID_VALUE);
  glDetachShader(program, fs);
  glAttachShader(program, fs);
  glDetachShader(program, fs);
  glDetachShader(program, fs);
  EXPECT_EQ(glGetError(), GL_INVALID_OPERATION);
  glDeleteProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  zglDestroyContext();
}

TEST(DeletedShadersStayAliveWhileAttached) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const GLuint vs = MakeShader(GL_VERTEX_SHADER, kVertex);
  const GLuint fs = MakeShader(GL_FRAGMENT_SHADER, kFragment);
  const GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glDeleteShader(vs);
  // The deleted shader still links because the program holds a reference.
  glAttachShader(program, fs);
  glLinkProgram(program);
  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  EXPECT_EQ(linked, GL_TRUE);
  GLuint names[1] = {};
  glGenTextures(1, names);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glDeleteProgram(program);
  glDeleteShader(fs);
  glDeleteTextures(1, names);
  zglDestroyContext();
}

TEST(SamplerAndBooleanUniformsSetIntegers) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const GLuint vs = MakeShader(GL_VERTEX_SHADER, kVertex);
  const GLuint fs = MakeShader(GL_FRAGMENT_SHADER, kFragment);
  const GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  glUseProgram(program);
  const GLint sampler = glGetUniformLocation(program, "u_texture");
  EXPECT_TRUE(sampler >= 0);
  glUniform1i(sampler, 3);
  GLint unit = -1;
  glGetUniformiv(program, sampler, &unit);
  EXPECT_EQ(unit, 3);
  glActiveTexture(GL_TEXTURE3);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  glDeleteProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  zglDestroyContext();
}

TEST(ProgramInfoLogIsEmptyAfterSuccessfulLink) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const GLuint vs = MakeShader(GL_VERTEX_SHADER, kVertex);
  const GLuint fs = MakeShader(GL_FRAGMENT_SHADER, kFragment);
  const GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  GLint length = -1;
  glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
  EXPECT_EQ(length, 0);
  char log[32] = "x";
  GLsizei written = -1;
  glGetProgramInfoLog(program, 32, &written, log);
  EXPECT_EQ(written, 0);
  EXPECT_EQ(log[0], '\0');
  glDeleteProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  zglDestroyContext();
}

TEST(FragmentOutputLocationsAreBoundedAndUnique) {
  EXPECT_EQ(zglCreateContext(32, 32), 1);
  const char* five_outputs =
      "#version 300 es\n"
      "precision mediump float;\n"
      "layout(location = 0) out vec4 c0;\n"
      "layout(location = 1) out vec4 c1;\n"
      "layout(location = 2) out vec4 c2;\n"
      "layout(location = 3) out vec4 c3;\n"
      "layout(location = 4) out vec4 c4;\n"
      "void main() { c0 = vec4(1.0); c1 = vec4(1.0); c2 = vec4(1.0);\n"
      "             c3 = vec4(1.0); c4 = vec4(1.0); }\n";
  const GLuint fs = MakeShader(GL_FRAGMENT_SHADER, five_outputs);
  GLint status = GL_TRUE;
  glGetShaderiv(fs, GL_COMPILE_STATUS, &status);
  EXPECT_EQ(status, GL_FALSE);

  const char* duplicate =
      "#version 300 es\n"
      "precision mediump float;\n"
      "layout(location = 1) out vec4 c0;\n"
      "layout(location = 1) out vec4 c1;\n"
      "void main() { c0 = vec4(1.0); c1 = vec4(1.0); }\n";
  const GLuint duplicate_shader =
      MakeShader(GL_FRAGMENT_SHADER, duplicate);
  status = GL_TRUE;
  glGetShaderiv(duplicate_shader, GL_COMPILE_STATUS, &status);
  EXPECT_EQ(status, GL_FALSE);
  glDeleteShader(fs);
  glDeleteShader(duplicate_shader);
  zglDestroyContext();
}