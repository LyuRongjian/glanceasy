// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "GLES3/gl32.h"
#include "src/glsl/glsl_parser.h"
#include "src/zgl_api_common.h"

namespace zgl {

bool DecodeUniformLocation(GLint location, int* index, int* element) {
  if (location >= 0) {
    *index = location;
    *element = 0;
    return true;
  }
  const int code = -location - 1;
  *index = code / 256;
  *element = code % 256;
  return true;
}


glsl::Stage ShaderStage(GLenum type) {
  return type == GL_VERTEX_SHADER ? glsl::Stage::kVertex
                                  : glsl::Stage::kFragment;
}

ShaderObject* LookupShader(Context* context, GLuint name) {
  ShaderObject* shader = context->FindShader(name);
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
int UniformArraySize(const zgl::glsl::UniformInfo& uniform) {
  return uniform.type.is_array() ? uniform.type.array_size() : 1;
}

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
    case glsl::BaseType::kUInt:
      switch (type.rows()) {
        case 2:
          return GL_UNSIGNED_INT_VEC2;
        case 3:
          return GL_UNSIGNED_INT_VEC3;
        case 4:
          return GL_UNSIGNED_INT_VEC4;
        default:
          return GL_UNSIGNED_INT;
      }
    case glsl::BaseType::kSampler2D:
      return GL_SAMPLER_2D;
    case glsl::BaseType::kSamplerCube:
      return GL_SAMPLER_CUBE;
    case glsl::BaseType::kSampler3D:
      return GL_SAMPLER_3D;
    case glsl::BaseType::kSampler2DShadow:
      return GL_SAMPLER_2D_SHADOW;
    case glsl::BaseType::kSampler2DArray:
      return GL_SAMPLER_2D_ARRAY;
    case glsl::BaseType::kSampler2DArrayShadow:
      return GL_SAMPLER_2D_ARRAY_SHADOW;
    case glsl::BaseType::kSamplerCubeShadow:
      return GL_SAMPLER_CUBE_SHADOW;
    case glsl::BaseType::kISampler2D:
      return GL_INT_SAMPLER_2D;
    case glsl::BaseType::kISampler3D:
      return GL_INT_SAMPLER_3D;
    case glsl::BaseType::kISamplerCube:
      return GL_INT_SAMPLER_CUBE;
    case glsl::BaseType::kISampler2DArray:
      return GL_INT_SAMPLER_2D_ARRAY;
    case glsl::BaseType::kUSampler2D:
      return GL_UNSIGNED_INT_SAMPLER_2D;
    case glsl::BaseType::kUSampler3D:
      return GL_UNSIGNED_INT_SAMPLER_3D;
    case glsl::BaseType::kUSamplerCube:
      return GL_UNSIGNED_INT_SAMPLER_CUBE;
    case glsl::BaseType::kUSampler2DArray:
      return GL_UNSIGNED_INT_SAMPLER_2D_ARRAY;
    default:
      return GL_FLOAT;
  }
}

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
      object->delete_status = true;
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
  return context->FindShader(shader) ? GL_TRUE : GL_FALSE;
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
      params[0] = object->log.empty()
                      ? 0
                      : static_cast<GLint>(object->log.size() + 1);
      return;
    case GL_SHADER_SOURCE_LENGTH:
      params[0] = object->source.empty()
                      ? 0
                      : static_cast<GLint>(object->source.size() + 1);
      return;
    case GL_DELETE_STATUS:
      params[0] = object->delete_status ? GL_TRUE : GL_FALSE;
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
    *length = static_cast<GLsizei>(
        std::min(static_cast<int>(object->log.size()),
                 std::max(0, static_cast<int>(max_length) - 1)));
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
  zgl::ProgramObject* object = context->programs.Get(program);
  if (!object) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  // Keep the name queryable so GL_DELETE_STATUS remains observable, and leave
  // the current binding untouched until glUseProgram replaces it.
  object->delete_status = true;
  object->linked = false;
  object->program.reset();
}

GLboolean glIsProgram(GLuint program) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || program == 0)
    return GL_FALSE;
  const zgl::ProgramObject* object = context->programs.Get(program);
  if (!object)
    return GL_FALSE;
  return !object->delete_status || context->state.program == program
             ? GL_TRUE
             : GL_FALSE;
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
      params[0] = object->log.empty()
                      ? 0
                      : static_cast<GLint>(object->log.size() + 1);
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
    case GL_ACTIVE_ATTRIBUTE_MAX_LENGTH: {
      int length = 0;
      if (object->program) {
        for (const auto& attribute : object->program->attributes()) {
          length = std::max(length, static_cast<int>(attribute.name.size()) + 1);
        }
      }
      params[0] = length;
      return;
    }
    case GL_ACTIVE_UNIFORM_MAX_LENGTH: {
      int length = 0;
      if (object->program) {
        for (const auto& uniform : object->program->uniforms()) {
          length = std::max(length, static_cast<int>(uniform.name.size()) + 1);
        }
      }
      params[0] = length;
      return;
    }
    case GL_ACTIVE_UNIFORM_BLOCKS:
      params[0] = object->program
                      ? static_cast<GLint>(
                            object->program->uniform_blocks().size())
                      : 0;
      return;
    case GL_ACTIVE_UNIFORM_BLOCK_MAX_NAME_LENGTH: {
      int length = 0;
      if (object->program) {
        for (const auto& block : object->program->uniform_blocks())
          length = std::max(length, static_cast<int>(block.name.size()) + 1);
      }
      params[0] = length;
      return;
    }
    case GL_VALIDATE_STATUS:
      params[0] = object->linked ? GL_TRUE : GL_FALSE;
      return;
    case GL_DELETE_STATUS:
      params[0] = object->delete_status ? GL_TRUE : GL_FALSE;
      return;
    case GL_TRANSFORM_FEEDBACK_VARYINGS:
      params[0] = object->program
                      ? static_cast<GLint>(object->program
                                               ->transform_feedback_varyings()
                                               .size())
                      : 0;
      return;
    case GL_TRANSFORM_FEEDBACK_VARYING_MAX_LENGTH: {
      int length = 0;
      if (object->program) {
        for (const auto& varying :
             object->program->transform_feedback_varyings()) {
          length = std::max(length, static_cast<int>(varying.name.size()) + 1);
        }
      }
      params[0] = length;
      return;
    }
    case GL_PROGRAM_BINARY_LENGTH:
      params[0] = 0;
      return;
    case GL_PROGRAM_BINARY_RETRIEVABLE_HINT:
      params[0] = object->program_binary_retrievable_hint ? GL_TRUE : GL_FALSE;
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
    *length = static_cast<GLsizei>(
        std::min(static_cast<int>(object->log.size()),
                 std::max(0, static_cast<int>(max_length) - 1)));
}

void glGetShaderSource(GLuint shader,
                       GLsizei max_length,
                       GLsizei* length,
                       GLchar* source) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  zgl::ShaderObject* object = zgl::LookupShader(context, shader);
  if (!object)
    return;
  const int copy = std::min(static_cast<int>(object->source.size()),
                            std::max(0, static_cast<int>(max_length) - 1));
  if (source && max_length > 0) {
    if (copy > 0)
      memcpy(source, object->source.c_str(), static_cast<size_t>(copy));
    source[copy] = '\0';
  }
  if (length)
    *length = copy;
}

void glGetAttachedShaders(GLuint program,
                          GLsizei max_count,
                          GLsizei* count,
                          GLuint* shaders) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  zgl::ProgramObject* object = context->programs.Get(program);
  if (!object) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  const int total = static_cast<int>(object->shaders.size());
  const int written =
      max_count < 0 ? 0 : std::min(static_cast<int>(max_count), total);
  if (shaders && written > 0) {
    for (int i = 0; i < written; ++i)
      shaders[i] = object->shaders[static_cast<size_t>(i)];
  }
  if (count)
    *count = written;
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
    *size = attribute.type.is_array() ? attribute.type.array_size() : 1;
  if (type)
    *type = zgl::GlslTypeToGlEnum(attribute.type);
  const int copy = buffer_size > 0
                       ? std::min(static_cast<int>(attribute.name.size()),
                                  static_cast<int>(buffer_size) - 1)
                       : 0;
  if (length)
    *length = copy;
  if (name && buffer_size > 0) {
    if (copy > 0)
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
  const int copy = buffer_size > 0
                       ? std::min(static_cast<int>(uniform.name.size()),
                                  static_cast<int>(buffer_size) - 1)
                       : 0;
  if (length)
    *length = copy;
  if (name && buffer_size > 0) {
    if (copy > 0)
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
  const int copy = buffer_size > 0
                       ? std::min(static_cast<int>(block_name.size()),
                                  static_cast<int>(buffer_size) - 1)
                       : 0;
  if (length)
    *length = copy;
  if (name && buffer_size > 0) {
    if (copy > 0)
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
    case GL_UNIFORM_NAME_LENGTH:
    case GL_UNIFORM_TYPE:
    case GL_UNIFORM_SIZE:
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
        params[i] = uniform.type.is_array()
                        ? uniform.type.array_element().components() * 4
                        : 0;
        break;
      case GL_UNIFORM_NAME_LENGTH:
        params[i] = static_cast<GLint>(uniform.name.size() + 1);
        break;
      case GL_UNIFORM_TYPE:
        params[i] = static_cast<GLint>(zgl::GlslTypeToGlEnum(
            uniform.type.is_array() ? uniform.type.array_element()
                                    : uniform.type));
        break;
      case GL_UNIFORM_SIZE:
        params[i] = zgl::UniformArraySize(uniform);
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

namespace {

void SetFloatArray(GLint location, GLsizei count, int components,
                   const GLfloat* value) {
  if (!value || count < 1)
    return;
  zgl::SetUniformFloats(location, value, components, count, false);
}

void SetIntArray(GLint location, GLsizei count, int components,
                 const GLint* value) {
  if (!value || count < 1)
    return;
  std::vector<GLfloat> converted(static_cast<size_t>(components) *
                                 static_cast<size_t>(count));
  for (size_t i = 0; i < converted.size(); ++i)
    converted[i] = static_cast<GLfloat>(value[i]);
  zgl::SetUniformFloats(location, converted.data(), components, count, true);
}

void SetUIntArray(GLint location, GLsizei count, int components,
                  const GLuint* value) {
  if (!value || count < 1)
    return;
  std::vector<GLfloat> converted(static_cast<size_t>(components) *
                                 static_cast<size_t>(count));
  for (size_t i = 0; i < converted.size(); ++i)
    converted[i] = static_cast<GLfloat>(value[i]);
  zgl::SetUniformFloats(location, converted.data(), components, count, true);
}

void SetMatrixArray(GLint location,
                    GLsizei count,
                    int rows,
                    int columns,
                    GLboolean transpose,
                    const GLfloat* value) {
  if (!value || count < 1)
    return;
  zgl::SetUniformMatrix(location, value, rows, columns, count,
                        transpose != GL_FALSE);
}

}  // namespace

void glUniform1f(GLint location, GLfloat v0) {
  SetFloatArray(location, 1, 1, &v0);
}

void glUniform2f(GLint location, GLfloat v0, GLfloat v1) {
  const GLfloat values[2] = {v0, v1};
  SetFloatArray(location, 1, 2, values);
}

void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) {
  const GLfloat values[3] = {v0, v1, v2};
  SetFloatArray(location, 1, 3, values);
}

void glUniform4f(GLint location,
                 GLfloat v0,
                 GLfloat v1,
                 GLfloat v2,
                 GLfloat v3) {
  const GLfloat values[4] = {v0, v1, v2, v3};
  SetFloatArray(location, 1, 4, values);
}

void glUniform1i(GLint location, GLint v0) {
  const GLint values[1] = {v0};
  SetIntArray(location, 1, 1, values);
}

void glUniform2i(GLint location, GLint v0, GLint v1) {
  const GLint values[2] = {v0, v1};
  SetIntArray(location, 1, 2, values);
}

void glUniform3i(GLint location, GLint v0, GLint v1, GLint v2) {
  const GLint values[3] = {v0, v1, v2};
  SetIntArray(location, 1, 3, values);
}

void glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3) {
  const GLint values[4] = {v0, v1, v2, v3};
  SetIntArray(location, 1, 4, values);
}

void glUniform1fv(GLint location, GLsizei count, const GLfloat* value) {
  SetFloatArray(location, count, 1, value);
}

void glUniform2fv(GLint location, GLsizei count, const GLfloat* value) {
  SetFloatArray(location, count, 2, value);
}

void glUniform3fv(GLint location, GLsizei count, const GLfloat* value) {
  SetFloatArray(location, count, 3, value);
}

void glUniform4fv(GLint location, GLsizei count, const GLfloat* value) {
  SetFloatArray(location, count, 4, value);
}

void glUniform1iv(GLint location, GLsizei count, const GLint* value) {
  SetIntArray(location, count, 1, value);
}

void glUniform2iv(GLint location, GLsizei count, const GLint* value) {
  SetIntArray(location, count, 2, value);
}

void glUniform3iv(GLint location, GLsizei count, const GLint* value) {
  SetIntArray(location, count, 3, value);
}

void glUniform4iv(GLint location, GLsizei count, const GLint* value) {
  SetIntArray(location, count, 4, value);
}

void glUniformMatrix2fv(GLint location,
                        GLsizei count,
                        GLboolean transpose,
                        const GLfloat* value) {
  SetMatrixArray(location, count, 2, 2, transpose, value);
}

void glUniformMatrix3fv(GLint location,
                        GLsizei count,
                        GLboolean transpose,
                        const GLfloat* value) {
  SetMatrixArray(location, count, 3, 3, transpose, value);
}

void glUniformMatrix4fv(GLint location,
                        GLsizei count,
                        GLboolean transpose,
                        const GLfloat* value) {
  SetMatrixArray(location, count, 4, 4, transpose, value);
}

void glUniformMatrix2x3fv(GLint location,
                          GLsizei count,
                          GLboolean transpose,
                          const GLfloat* value) {
  SetMatrixArray(location, count, 3, 2, transpose, value);
}

void glUniformMatrix3x2fv(GLint location,
                          GLsizei count,
                          GLboolean transpose,
                          const GLfloat* value) {
  SetMatrixArray(location, count, 2, 3, transpose, value);
}

void glUniformMatrix2x4fv(GLint location,
                          GLsizei count,
                          GLboolean transpose,
                          const GLfloat* value) {
  SetMatrixArray(location, count, 4, 2, transpose, value);
}

void glUniformMatrix4x2fv(GLint location,
                          GLsizei count,
                          GLboolean transpose,
                          const GLfloat* value) {
  SetMatrixArray(location, count, 2, 4, transpose, value);
}

void glUniformMatrix3x4fv(GLint location,
                          GLsizei count,
                          GLboolean transpose,
                          const GLfloat* value) {
  SetMatrixArray(location, count, 4, 3, transpose, value);
}

void glUniformMatrix4x3fv(GLint location,
                          GLsizei count,
                          GLboolean transpose,
                          const GLfloat* value) {
  SetMatrixArray(location, count, 3, 4, transpose, value);
}

void glUniform1ui(GLint location, GLuint v0) {
  const GLuint values[1] = {v0};
  SetUIntArray(location, 1, 1, values);
}

void glUniform2ui(GLint location, GLuint v0, GLuint v1) {
  const GLuint values[2] = {v0, v1};
  SetUIntArray(location, 1, 2, values);
}

void glUniform3ui(GLint location, GLuint v0, GLuint v1, GLuint v2) {
  const GLuint values[3] = {v0, v1, v2};
  SetUIntArray(location, 1, 3, values);
}

void glUniform4ui(GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3) {
  const GLuint values[4] = {v0, v1, v2, v3};
  SetUIntArray(location, 1, 4, values);
}

void glUniform1uiv(GLint location, GLsizei count, const GLuint* value) {
  SetUIntArray(location, count, 1, value);
}

void glUniform2uiv(GLint location, GLsizei count, const GLuint* value) {
  SetUIntArray(location, count, 2, value);
}

void glUniform3uiv(GLint location, GLsizei count, const GLuint* value) {
  SetUIntArray(location, count, 3, value);
}

void glUniform4uiv(GLint location, GLsizei count, const GLuint* value) {
  SetUIntArray(location, count, 4, value);
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
  int index = 0;
  int element = 0;
  zgl::DecodeUniformLocation(location, &index, &element);
  const zgl::glsl::Value* value = object->program->uniform_value(index);
  if (!value)
    return;
  const int components = value->type.is_array()
                             ? value->type.array_element().components()
                             : value->type.components();
  const int offset = value->type.is_array() ? element * components : 0;
  for (int i = 0; i < components; ++i)
    params[i] = value->Data()[offset + i];
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
  int index = 0;
  int element = 0;
  zgl::DecodeUniformLocation(location, &index, &element);
  const zgl::glsl::Value* value = object->program->uniform_value(index);
  if (!value)
    return;
  const int components = value->type.is_array()
                             ? value->type.array_element().components()
                             : value->type.components();
  const int offset = value->type.is_array() ? element * components : 0;
  for (int i = 0; i < components; ++i) {
    params[i] = value->type.base() == zgl::glsl::BaseType::kBool
                    ? (value->Data()[offset + i] != 0.0f ? 1 : 0)
                    : static_cast<GLint>(value->Data()[offset + i]);
  }
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
    if (indices[i] == GL_INVALID_INDEX) {
      for (const auto& block : object->program->uniform_blocks()) {
        for (const auto& member : block.members) {
          if (block.name + "." + member.name == names[i] &&
              member.uniform_index >= 0) {
            indices[i] = static_cast<GLuint>(member.uniform_index);
            break;
          }
        }
        if (indices[i] != GL_INVALID_INDEX)
          break;
      }
    }
  }
}

}  // extern "C"
