// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include "GLES3/gl32.h"
#include "src/glsl/glsl_parser.h"
#include "src/zgl_api_common.h"

namespace zgl {
namespace {

glsl::Stage ShaderStage(GLenum type) {
  return type == GL_VERTEX_SHADER ? glsl::Stage::kVertex
                                  : glsl::Stage::kFragment;
}

ShaderObject* LookupShader(Context* context, GLuint name) {
  ShaderObject* shader = context->shaders.Get(name);
  if (!shader)
    context->SetError(GL_INVALID_VALUE);
  return shader;
}

void SetInfoLog(std::string* log, GLchar* info_log, GLsizei max_length) {
  if (!info_log || max_length <= 0)
    return;
  const int length = static_cast<int>(log->size());
  const int copy = length < max_length - 1 ? length : max_length - 1;
  if (copy > 0)
    memcpy(info_log, log->c_str(), static_cast<size_t>(copy));
  info_log[copy] = '\0';
}

// Maps a GLSL type onto the GLenum used by glGetActiveAttrib and
// glGetActiveUniform.  Samplers report their opaque type, matrices and vectors
// report their shape so callers can size their uniform arrays.
GLenum GlslTypeToGlEnum(const glsl::Type& type) {
  switch (type.base()) {
    case glsl::BaseType::kFloat:
      if (type.is_matrix()) {
        switch (type.columns() * 10 + type.rows()) {
          case 22:
            return GL_FLOAT_MAT2;
          case 23:
            return GL_FLOAT_MAT2x3;
          case 24:
            return GL_FLOAT_MAT2x4;
          case 32:
            return GL_FLOAT_MAT3x2;
          case 33:
            return GL_FLOAT_MAT3;
          case 34:
            return GL_FLOAT_MAT3x4;
          case 42:
            return GL_FLOAT_MAT4x2;
          case 43:
            return GL_FLOAT_MAT4x3;
          case 44:
            return GL_FLOAT_MAT4;
          default:
            return GL_FLOAT;
        }
      }
      switch (type.rows()) {
        case 2:
          return GL_FLOAT_VEC2;
        case 3:
          return GL_FLOAT_VEC3;
        case 4:
          return GL_FLOAT_VEC4;
        default:
          return GL_FLOAT;
      }
    case glsl::BaseType::kInt:
      switch (type.rows()) {
        case 2:
          return GL_INT_VEC2;
        case 3:
          return GL_INT_VEC3;
        case 4:
          return GL_INT_VEC4;
        default:
          return GL_INT;
      }
    case glsl::BaseType::kBool:
      switch (type.rows()) {
        case 2:
          return GL_BOOL_VEC2;
        case 3:
          return GL_BOOL_VEC3;
        case 4:
          return GL_BOOL_VEC4;
        default:
          return GL_BOOL;
      }
    case glsl::BaseType::kSampler2D:
      return GL_SAMPLER_2D;
    case glsl::BaseType::kSamplerCube:
      return GL_SAMPLER_CUBE;
    case glsl::BaseType::kSampler3D:
      return GL_SAMPLER_3D;
    default:
      return GL_FLOAT;
  }
}

}  // namespace
}  // namespace zgl

extern "C" {

GLuint glCreateShader(GLenum type) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return 0;
  if (type != GL_VERTEX_SHADER && type != GL_FRAGMENT_SHADER) {
    context->SetError(GL_INVALID_ENUM);
    return 0;
  }
  const GLuint name = context->shaders.Create();
  zgl::ShaderObject* shader = context->shaders.Get(name);
  if (shader) {
    shader->name = name;
    shader->type = type;
  }
  return name;
}

void glDeleteShader(GLuint shader) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || shader == 0)
    return;
  if (!context->shaders.Get(shader))
    return;
  bool attached = false;
  for (int name = 1; name <= context->programs.size(); ++name) {
    const zgl::ProgramObject* program =
        context->programs.Get(static_cast<GLuint>(name));
    if (!program)
      continue;
    for (GLuint attached_shader : program->shaders) {
      if (attached_shader == shader) {
        attached = true;
        break;
      }
    }
    if (attached)
      break;
  }
  if (attached) {
    std::unique_ptr<zgl::ShaderObject> object =
        context->shaders.Release(shader);
    if (object) {
      object->name = shader;
      context->orphaned_shaders.emplace_back(shader, std::move(object));
    }
    return;
  }
  context->shaders.Delete(shader);
  context->CollectOrphanedShaders();
}

GLboolean glIsShader(GLuint shader) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || shader == 0)
    return GL_FALSE;
  return context->shaders.Get(shader) ? GL_TRUE : GL_FALSE;
}

void glShaderSource(GLuint shader,
                    GLsizei count,
                    const GLchar* const* strings,
                    const GLint* lengths) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !strings)
    return;
  zgl::ShaderObject* object = zgl::LookupShader(context, shader);
  if (!object)
    return;
  if (count < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  std::string source;
  for (GLsizei i = 0; i < count; ++i) {
    if (!strings[i])
      continue;
    if (lengths && lengths[i] >= 0)
      source.append(strings[i], static_cast<size_t>(lengths[i]));
    else
      source.append(strings[i]);
  }
  object->source = source;
}

void glCompileShader(GLuint shader) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  zgl::ShaderObject* object = zgl::LookupShader(context, shader);
  if (!object)
    return;
  zgl::glsl::Parser parser(zgl::ShaderStage(object->type), object->source);
  object->ast = parser.Parse();
  object->compiled = object->ast != nullptr;
  object->log = parser.error();
}

void glGetShaderiv(GLuint shader, GLenum pname, GLint* params) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !params)
    return;
  zgl::ShaderObject* object = zgl::LookupShader(context, shader);
  if (!object)
    return;
  switch (pname) {
    case GL_COMPILE_STATUS:
      params[0] = object->compiled ? GL_TRUE : GL_FALSE;
      return;
    case GL_SHADER_TYPE:
      params[0] = static_cast<GLint>(object->type);
      return;
    case GL_INFO_LOG_LENGTH:
      params[0] = static_cast<GLint>(object->log.size());
      return;
    case GL_SHADER_SOURCE_LENGTH:
      params[0] = static_cast<GLint>(object->source.size());
      return;
    case GL_DELETE_STATUS:
      params[0] = GL_FALSE;
      return;
    default:
      context->SetError(GL_INVALID_ENUM);
      return;
  }
}

void glGetShaderInfoLog(GLuint shader,
                        GLsizei max_length,
                        GLsizei* length,
                        GLchar* info_log) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  zgl::ShaderObject* object = zgl::LookupShader(context, shader);
  if (!object)
    return;
  zgl::SetInfoLog(&object->log, info_log, max_length);
  if (length)
    *length = static_cast<GLsizei>(object->log.size());
}

GLuint glCreateProgram(void) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return 0;
  return context->programs.Create();
}

void glDeleteProgram(GLuint program) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || program == 0)
    return;
  context->programs.Delete(program);
  if (context->state.program == program)
    context->state.program = 0;
  context->CollectOrphanedShaders();
}

GLboolean glIsProgram(GLuint program) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || program == 0)
    return GL_FALSE;
  return context->programs.Get(program) ? GL_TRUE : GL_FALSE;
}

void glAttachShader(GLuint program, GLuint shader) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  zgl::ProgramObject* object = context->programs.Get(program);
  if (!object) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!context->shaders.Get(shader)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  for (GLuint attached : object->shaders) {
    if (attached == shader) {
      context->SetError(GL_INVALID_OPERATION);
      return;
    }
  }
  object->shaders.push_back(shader);
}

void glDetachShader(GLuint program, GLuint shader) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  zgl::ProgramObject* object = context->programs.Get(program);
  if (!object) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!context->FindShader(shader)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  auto& shaders = object->shaders;
  if (std::find(shaders.begin(), shaders.end(), shader) == shaders.end()) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  shaders.erase(std::remove(shaders.begin(), shaders.end(), shader),
                shaders.end());
  context->CollectOrphanedShaders();
}

void glBindAttribLocation(GLuint program, GLuint index, const GLchar* name) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !name)
    return;
  if (index >= static_cast<GLuint>(zgl::kMaxVertexAttribs)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  zgl::ProgramObject* object = context->programs.Get(program);
  if (!object) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  object->bindings.emplace_back(name, static_cast<int>(index));
}

void glBindFragDataLocation(GLuint program, GLuint color, const GLchar* name) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (color >= static_cast<GLuint>(zgl::kMaxDrawBuffers)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  zgl::ProgramObject* object = context->programs.Get(program);
  if (!object) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (name)
    object->frag_data_bindings.emplace_back(name, static_cast<int>(color));
}

void glLinkProgram(GLuint program) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  zgl::ProgramObject* object = context->programs.Get(program);
  if (!object) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  zgl::glsl::ShaderAst* vertex = nullptr;
  zgl::glsl::ShaderAst* fragment = nullptr;
  bool failed = false;
  for (GLuint name : object->shaders) {
    zgl::ShaderObject* shader = context->FindShader(name);
    if (!shader || !shader->compiled || !shader->ast) {
      failed = true;
      break;
    }
    if (shader->type == GL_VERTEX_SHADER) {
      if (vertex) {
        failed = true;
        break;
      }
      vertex = shader->ast.get();
    } else {
      if (fragment) {
        failed = true;
        break;
      }
      fragment = shader->ast.get();
    }
  }
  if (failed) {
    object->program.reset();
    object->linked = false;
    object->log = "a shader is missing or failed to compile";
    return;
  }
  object->program.reset(new zgl::glsl::Program());
  object->linked = object->program->Link(
      vertex, fragment, object->bindings, object->frag_data_bindings,
      object->transform_feedback_varyings,
      object->transform_feedback_mode == GL_SEPARATE_ATTRIBS, &object->log);
}

void glGetProgramiv(GLuint program, GLenum pname, GLint* params) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !params)
    return;
  zgl::ProgramObject* object = context->programs.Get(program);
  if (!object) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  switch (pname) {
    case GL_LINK_STATUS:
      params[0] = object->linked ? GL_TRUE : GL_FALSE;
      return;
    case GL_INFO_LOG_LENGTH:
      params[0] = static_cast<GLint>(object->log.size());
      return;
    case GL_ATTACHED_SHADERS:
      params[0] = static_cast<GLint>(object->shaders.size());
      return;
    case GL_ACTIVE_ATTRIBUTES:
      params[0] = object->program
                      ? static_cast<GLint>(object->program->attributes().size())
                      : 0;
      return;
    case GL_ACTIVE_UNIFORMS:
      params[0] = object->program
                      ? static_cast<GLint>(object->program->uniforms().size())
                      : 0;
      return;
    case GL_ACTIVE_UNIFORM_BLOCKS:
      params[0] = 0;
      return;
    case GL_ACTIVE_UNIFORM_BLOCK_MAX_NAME_LENGTH:
      params[0] = 0;
      return;
    case GL_VALIDATE_STATUS:
      params[0] = object->linked ? GL_TRUE : GL_FALSE;
      return;
    case GL_DELETE_STATUS:
      params[0] = GL_FALSE;
      return;
    case GL_TRANSFORM_FEEDBACK_VARYINGS:
      params[0] = static_cast<GLint>(
          object->transform_feedback_varyings.size());
      return;
    case GL_TRANSFORM_FEEDBACK_BUFFER_MODE:
      params[0] = static_cast<GLint>(object->transform_feedback_mode);
      return;
    default:
      context->SetError(GL_INVALID_ENUM);
      return;
  }
}

void glGetProgramInfoLog(GLuint program,
                         GLsizei max_length,
                         GLsizei* length,
                         GLchar* info_log) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  zgl::ProgramObject* object = context->programs.Get(program);
  if (!object) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  zgl::SetInfoLog(&object->log, info_log, max_length);
  if (length)
    *length = static_cast<GLsizei>(object->log.size());
}

void glUseProgram(GLuint program) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (program != 0) {
    zgl::ProgramObject* object = context->programs.Get(program);
    if (!object || !object->linked || !object->program) {
      context->SetError(GL_INVALID_OPERATION);
      return;
    }
  }
  context->state.program = program;
}

void glValidateProgram(GLuint program) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (!context->programs.Get(program))
    context->SetError(GL_INVALID_VALUE);
}

int glGetAttribLocation(GLuint program, const GLchar* name) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !name)
    return -1;
  zgl::ProgramObject* object = context->programs.Get(program);
  if (!object || !object->program) {
    context->SetError(GL_INVALID_OPERATION);
    return -1;
  }
  return object->program->AttributeLocation(name);
}

int glGetUniformLocation(GLuint program, const GLchar* name) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !name)
    return -1;
  zgl::ProgramObject* object = context->programs.Get(program);
  if (!object || !object->program) {
    context->SetError(GL_INVALID_OPERATION);
    return -1;
  }
  return object->program->UniformLocation(name);
}

int glGetFragDataLocation(GLuint program, const GLchar* name) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !name)
    return -1;
  zgl::ProgramObject* object = context->programs.Get(program);
  if (!object) {
    context->SetError(GL_INVALID_OPERATION);
    return -1;
  }
  if (!object->program)
    return -1;
  return object->program->FragDataLocation(name);
}

void glGetActiveAttrib(GLuint program,
                       GLuint index,
                       GLsizei buffer_size,
                       GLsizei* length,
                       GLint* size,
                       GLenum* type,
                       GLchar* name) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  zgl::ProgramObject* object = context->programs.Get(program);
  if (!object || !object->program) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  const auto& attributes = object->program->attributes();
  if (index >= attributes.size()) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  const zgl::glsl::AttributeInfo& attribute = attributes[index];
  if (size)
    *size = attribute.type.components();
  if (type)
    *type = zgl::GlslTypeToGlEnum(attribute.type);
  if (length)
    *length = static_cast<GLsizei>(attribute.name.size());
  if (name && buffer_size > 0) {
    const int copy = std::min(static_cast<int>(attribute.name.size()),
                              static_cast<int>(buffer_size) - 1);
    memcpy(name, attribute.name.c_str(), static_cast<size_t>(copy));
    name[copy] = '\0';
  }
}

void glGetActiveUniform(GLuint program,
                        GLuint index,
                        GLsizei buffer_size,
                        GLsizei* length,
                        GLint* size,
                        GLenum* type,
                        GLchar* name) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  zgl::ProgramObject* object = context->programs.Get(program);
  if (!object || !object->program) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  const auto& uniforms = object->program->uniforms();
  if (index >= uniforms.size()) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  const zgl::glsl::UniformInfo& uniform = uniforms[index];
  if (size)
    *size = 1;
  if (type)
    *type = zgl::GlslTypeToGlEnum(uniform.type);
  if (length)
    *length = static_cast<GLsizei>(uniform.name.size());
  if (name && buffer_size > 0) {
    const int copy = std::min(static_cast<int>(uniform.name.size()),
                              static_cast<int>(buffer_size) - 1);
    memcpy(name, uniform.name.c_str(), static_cast<size_t>(copy));
    name[copy] = '\0';
  }
}

void glGetActiveUniformBlockName(GLuint program,
                                 GLuint block_index,
                                 GLsizei buffer_size,
                                 GLsizei* length,
                                 GLchar* name) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  zgl::ProgramObject* object = context->programs.Get(program);
  if (!object || !object->program || !object->linked) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  const auto& blocks = object->program->uniform_blocks();
  if (block_index >= blocks.size()) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  const std::string& block_name = blocks[block_index].name;
  if (length)
    *length = static_cast<GLsizei>(block_name.size());
  if (name && buffer_size > 0) {
    const int copy = std::min(static_cast<int>(block_name.size()),
                              static_cast<int>(buffer_size) - 1);
    memcpy(name, block_name.c_str(), static_cast<size_t>(copy));
    name[copy] = '\0';
  }
}

void glGetActiveUniformBlockiv(GLuint program,
                               GLuint block_index,
                               GLenum pname,
                               GLint* params) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  zgl::ProgramObject* object = context->programs.Get(program);
  if (!object || !object->program || !object->linked) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  const auto& blocks = object->program->uniform_blocks();
  if (block_index >= blocks.size()) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!params)
    return;
  const zgl::glsl::UniformBlockInfo& block = blocks[block_index];
  switch (pname) {
    case GL_UNIFORM_BLOCK_BINDING:
      params[0] = block.binding;
      return;
    case GL_UNIFORM_BLOCK_DATA_SIZE:
      params[0] = block.size;
      return;
    case GL_UNIFORM_BLOCK_NAME_LENGTH:
      params[0] = static_cast<GLint>(block.name.size() + 1);
      return;
    case GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS:
      params[0] = static_cast<GLint>(block.members.size());
      return;
    case GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES:
      for (size_t i = 0; i < block.members.size(); ++i)
        params[i] = block.members[i].uniform_index;
      return;
    case GL_UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER:
      params[0] = block.vertex_stage ? GL_TRUE : GL_FALSE;
      return;
    case GL_UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER:
      params[0] = block.fragment_stage ? GL_TRUE : GL_FALSE;
      return;
    default:
      context->SetError(GL_INVALID_ENUM);
      return;
  }
}

void glGetActiveUniformsiv(GLuint program,
                           GLsizei count,
                           const GLuint* indices,
                           GLenum pname,
                           GLint* params) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  zgl::ProgramObject* object = context->programs.Get(program);
  if (!object || !object->program || !object->linked) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (count < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!indices || !params)
    return;
  const auto& uniforms = object->program->uniforms();
  switch (pname) {
    case GL_UNIFORM_BLOCK_INDEX:
    case GL_UNIFORM_OFFSET:
    case GL_UNIFORM_ARRAY_STRIDE:
    case GL_UNIFORM_MATRIX_STRIDE:
    case GL_UNIFORM_IS_ROW_MAJOR:
      break;
    default:
      context->SetError(GL_INVALID_ENUM);
      return;
  }
  for (GLsizei i = 0; i < count; ++i) {
    if (indices[i] >= uniforms.size()) {
      context->SetError(GL_INVALID_VALUE);
      return;
    }
    const zgl::glsl::UniformInfo& uniform = uniforms[indices[i]];
    switch (pname) {
      case GL_UNIFORM_BLOCK_INDEX:
        params[i] = uniform.block_index;
        break;
      case GL_UNIFORM_OFFSET:
        params[i] = uniform.block_index >= 0 ? uniform.block_offset : -1;
        break;
      case GL_UNIFORM_ARRAY_STRIDE:
        params[i] = 0;
        break;
      case GL_UNIFORM_MATRIX_STRIDE:
        params[i] = uniform.type.is_matrix() ? 16 : 0;
        break;
      case GL_UNIFORM_IS_ROW_MAJOR:
        params[i] = 0;
        break;
      default:
        break;
    }
  }
}

// ---- Uniform values -------------------------------------------------------

void glUniform1f(GLint location, GLfloat v0) {
  const GLfloat values[1] = {v0};
  zgl::SetUniformFloats(location, values, 1, false);
}

void glUniform2f(GLint location, GLfloat v0, GLfloat v1) {
  const GLfloat values[2] = {v0, v1};
  zgl::SetUniformFloats(location, values, 2, false);
}

void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) {
  const GLfloat values[3] = {v0, v1, v2};
  zgl::SetUniformFloats(location, values, 3, false);
}

void glUniform4f(GLint location,
                 GLfloat v0,
                 GLfloat v1,
                 GLfloat v2,
                 GLfloat v3) {
  const GLfloat values[4] = {v0, v1, v2, v3};
  zgl::SetUniformFloats(location, values, 4, false);
}

void glUniform1i(GLint location, GLint v0) {
  const GLfloat values[1] = {static_cast<GLfloat>(v0)};
  zgl::SetUniformFloats(location, values, 1, true);
}

void glUniform2i(GLint location, GLint v0, GLint v1) {
  const GLfloat values[2] = {static_cast<GLfloat>(v0),
                             static_cast<GLfloat>(v1)};
  zgl::SetUniformFloats(location, values, 2, true);
}

void glUniform3i(GLint location, GLint v0, GLint v1, GLint v2) {
  const GLfloat values[3] = {static_cast<GLfloat>(v0), static_cast<GLfloat>(v1),
                             static_cast<GLfloat>(v2)};
  zgl::SetUniformFloats(location, values, 3, true);
}

void glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3) {
  const GLfloat values[4] = {static_cast<GLfloat>(v0), static_cast<GLfloat>(v1),
                             static_cast<GLfloat>(v2),
                             static_cast<GLfloat>(v3)};
  zgl::SetUniformFloats(location, values, 4, true);
}

void glUniform1fv(GLint location, GLsizei count, const GLfloat* value) {
  if (!value || count < 1)
    return;
  zgl::SetUniformFloats(location, value, 1, false);
}

void glUniform2fv(GLint location, GLsizei count, const GLfloat* value) {
  if (!value || count < 1)
    return;
  zgl::SetUniformFloats(location, value, 2, false);
}

void glUniform3fv(GLint location, GLsizei count, const GLfloat* value) {
  if (!value || count < 1)
    return;
  zgl::SetUniformFloats(location, value, 3, false);
}

void glUniform4fv(GLint location, GLsizei count, const GLfloat* value) {
  if (!value || count < 1)
    return;
  zgl::SetUniformFloats(location, value, 4, false);
}

void glUniform1iv(GLint location, GLsizei count, const GLint* value) {
  // Array uniforms are outside the interpreted subset; |count| is ignored.
  (void)count;
  if (!value)
    return;
  const GLfloat converted[1] = {static_cast<GLfloat>(value[0])};
  zgl::SetUniformFloats(location, converted, 1, true);
}

void glUniform2iv(GLint location, GLsizei count, const GLint* value) {
  // Array uniforms are outside the interpreted subset; |count| is ignored.
  (void)count;
  if (!value)
    return;
  const GLfloat converted[2] = {static_cast<GLfloat>(value[0]),
                                static_cast<GLfloat>(value[1])};
  zgl::SetUniformFloats(location, converted, 2, true);
}

void glUniform3iv(GLint location, GLsizei count, const GLint* value) {
  // Array uniforms are outside the interpreted subset; |count| is ignored.
  (void)count;
  if (!value)
    return;
  const GLfloat converted[3] = {static_cast<GLfloat>(value[0]),
                                static_cast<GLfloat>(value[1]),
                                static_cast<GLfloat>(value[2])};
  zgl::SetUniformFloats(location, converted, 3, true);
}

void glUniform4iv(GLint location, GLsizei count, const GLint* value) {
  // Array uniforms are outside the interpreted subset; |count| is ignored.
  (void)count;
  if (!value)
    return;
  const GLfloat converted[4] = {
      static_cast<GLfloat>(value[0]), static_cast<GLfloat>(value[1]),
      static_cast<GLfloat>(value[2]), static_cast<GLfloat>(value[3])};
  zgl::SetUniformFloats(location, converted, 4, true);
}

void glUniformMatrix2fv(GLint location,
                        GLsizei count,
                        GLboolean transpose,
                        const GLfloat* value) {
  if (!value || count < 1)
    return;
  zgl::SetUniformMatrix(location, value, 2, transpose != GL_FALSE);
}

void glUniformMatrix3fv(GLint location,
                        GLsizei count,
                        GLboolean transpose,
                        const GLfloat* value) {
  if (!value || count < 1)
    return;
  zgl::SetUniformMatrix(location, value, 3, transpose != GL_FALSE);
}

void glUniformMatrix4fv(GLint location,
                        GLsizei count,
                        GLboolean transpose,
                        const GLfloat* value) {
  if (!value || count < 1)
    return;
  zgl::SetUniformMatrix(location, value, 4, transpose != GL_FALSE);
}

void glUniformMatrix2x3fv(GLint location,
                          GLsizei count,
                          GLboolean transpose,
                          const GLfloat* value) {
  (void)location;
  (void)count;
  (void)transpose;
  (void)value;
}

void glUniformMatrix3x2fv(GLint location,
                          GLsizei count,
                          GLboolean transpose,
                          const GLfloat* value) {
  (void)location;
  (void)count;
  (void)transpose;
  (void)value;
}

void glUniformMatrix2x4fv(GLint location,
                          GLsizei count,
                          GLboolean transpose,
                          const GLfloat* value) {
  (void)location;
  (void)count;
  (void)transpose;
  (void)value;
}

void glUniformMatrix4x2fv(GLint location,
                          GLsizei count,
                          GLboolean transpose,
                          const GLfloat* value) {
  (void)location;
  (void)count;
  (void)transpose;
  (void)value;
}

void glUniformMatrix3x4fv(GLint location,
                          GLsizei count,
                          GLboolean transpose,
                          const GLfloat* value) {
  (void)location;
  (void)count;
  (void)transpose;
  (void)value;
}

void glUniformMatrix4x3fv(GLint location,
                          GLsizei count,
                          GLboolean transpose,
                          const GLfloat* value) {
  (void)location;
  (void)count;
  (void)transpose;
  (void)value;
}

void glUniform1ui(GLint location, GLuint v0) {
  glUniform1i(location, static_cast<GLint>(v0));
}

void glUniform2ui(GLint location, GLuint v0, GLuint v1) {
  glUniform2i(location, static_cast<GLint>(v0), static_cast<GLint>(v1));
}

void glUniform3ui(GLint location, GLuint v0, GLuint v1, GLuint v2) {
  glUniform3i(location, static_cast<GLint>(v0), static_cast<GLint>(v1),
              static_cast<GLint>(v2));
}

void glUniform4ui(GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3) {
  glUniform4i(location, static_cast<GLint>(v0), static_cast<GLint>(v1),
              static_cast<GLint>(v2), static_cast<GLint>(v3));
}

void glUniform1uiv(GLint location, GLsizei count, const GLuint* value) {
  // Array uniforms are outside the interpreted subset; |count| is ignored.
  (void)count;
  if (!value)
    return;
  const GLfloat converted[1] = {static_cast<GLfloat>(value[0])};
  zgl::SetUniformFloats(location, converted, 1, true);
}

void glUniform2uiv(GLint location, GLsizei count, const GLuint* value) {
  // Array uniforms are outside the interpreted subset; |count| is ignored.
  (void)count;
  if (!value)
    return;
  const GLfloat converted[2] = {static_cast<GLfloat>(value[0]),
                                static_cast<GLfloat>(value[1])};
  zgl::SetUniformFloats(location, converted, 2, true);
}

void glUniform3uiv(GLint location, GLsizei count, const GLuint* value) {
  // Array uniforms are outside the interpreted subset; |count| is ignored.
  (void)count;
  if (!value)
    return;
  const GLfloat converted[3] = {static_cast<GLfloat>(value[0]),
                                static_cast<GLfloat>(value[1]),
                                static_cast<GLfloat>(value[2])};
  zgl::SetUniformFloats(location, converted, 3, true);
}

void glUniform4uiv(GLint location, GLsizei count, const GLuint* value) {
  // Array uniforms are outside the interpreted subset; |count| is ignored.
  (void)count;
  if (!value)
    return;
  const GLfloat converted[4] = {
      static_cast<GLfloat>(value[0]), static_cast<GLfloat>(value[1]),
      static_cast<GLfloat>(value[2]), static_cast<GLfloat>(value[3])};
  zgl::SetUniformFloats(location, converted, 4, true);
}

void glGetUniformfv(GLuint program, GLint location, GLfloat* params) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !params)
    return;
  zgl::ProgramObject* object = context->programs.Get(program);
  if (!object || !object->program) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  const zgl::glsl::Value* value = object->program->uniform_value(location);
  if (!value)
    return;
  for (int i = 0; i < value->type.components(); ++i)
    params[i] = value->data[i];
}

void glGetUniformiv(GLuint program, GLint location, GLint* params) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !params)
    return;
  zgl::ProgramObject* object = context->programs.Get(program);
  if (!object || !object->program) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  const zgl::glsl::Value* value = object->program->uniform_value(location);
  if (!value)
    return;
  for (int i = 0; i < value->type.components(); ++i)
    params[i] = static_cast<GLint>(value->data[i]);
}

void glGetUniformuiv(GLuint program, GLint location, GLuint* params) {
  GLint converted[16] = {};
  glGetUniformiv(program, location, converted);
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  const zgl::ProgramObject* object = context->programs.Get(program);
  if (!object || !object->program)
    return;
  const zgl::glsl::Value* value = object->program->uniform_value(location);
  if (!value)
    return;
  for (int i = 0; i < value->type.components(); ++i)
    params[i] = static_cast<GLuint>(converted[i]);
}

void glUniformBlockBinding(GLuint program,
                           GLuint block_index,
                           GLuint block_binding) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  zgl::ProgramObject* object = context->programs.Get(program);
  if (!object || !object->program || !object->linked) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (block_binding >=
      static_cast<GLuint>(zgl::kMaxUniformBufferBindings)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!object->program->SetUniformBlockBinding(
          static_cast<int>(block_index), static_cast<int>(block_binding))) {
    context->SetError(GL_INVALID_VALUE);
  }
}

GLuint glGetUniformBlockIndex(GLuint program, const GLchar* name) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return GL_INVALID_INDEX;
  zgl::ProgramObject* object = context->programs.Get(program);
  if (!object || !object->program || !object->linked) {
    context->SetError(GL_INVALID_OPERATION);
    return GL_INVALID_INDEX;
  }
  if (!name) {
    context->SetError(GL_INVALID_VALUE);
    return GL_INVALID_INDEX;
  }
  const int index = object->program->UniformBlockIndex(name);
  return index < 0 ? GL_INVALID_INDEX : static_cast<GLuint>(index);
}

void glGetUniformIndices(GLuint program,
                         GLsizei count,
                         const GLchar* const* names,
                         GLuint* indices) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  zgl::ProgramObject* object = context->programs.Get(program);
  if (!object || !object->program || !object->linked) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (count < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!names || !indices)
    return;
  const auto& uniforms = object->program->uniforms();
  for (GLsizei i = 0; i < count; ++i) {
    indices[i] = GL_INVALID_INDEX;
    if (!names[i])
      continue;
    for (size_t u = 0; u < uniforms.size(); ++u) {
      if (uniforms[u].name == names[i]) {
        indices[i] = static_cast<GLuint>(u);
        break;
      }
    }
  }
}

}  // extern "C"
