// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <vector>

#include "GLES3/gl32.h"
#include "src/zgl_api_common.h"

namespace zgl {
namespace {

VertexArray* ActiveVertexArray(Context* context) {
  if (context->state.vertex_array == 0)
    return &context->default_vertex_array;
  return context->vertex_arrays.Get(context->state.vertex_array);
}

Buffer* BoundBuffer(Context* context, GLenum target) {
  if (target == GL_ARRAY_BUFFER)
    return context->buffers.Get(context->state.array_buffer);
  if (target == GL_ELEMENT_ARRAY_BUFFER)
    return context->buffers.Get(context->state.element_buffer);
  if (target == GL_COPY_READ_BUFFER)
    return context->buffers.Get(context->state.copy_read_buffer);
  if (target == GL_COPY_WRITE_BUFFER)
    return context->buffers.Get(context->state.copy_write_buffer);
  if (target == GL_UNIFORM_BUFFER)
    return context->buffers.Get(context->state.uniform_buffer);
  if (target == GL_TRANSFORM_FEEDBACK_BUFFER)
    return context->buffers.Get(context->state.transform_feedback_buffer);
  if (target == GL_PIXEL_PACK_BUFFER)
    return context->buffers.Get(context->state.pixel_pack_buffer);
  if (target == GL_PIXEL_UNPACK_BUFFER)
    return context->buffers.Get(context->state.pixel_unpack_buffer);
  return nullptr;
}

bool ValidBufferTarget(GLenum target) {
  switch (target) {
    case GL_ARRAY_BUFFER:
    case GL_ELEMENT_ARRAY_BUFFER:
    case GL_COPY_READ_BUFFER:
    case GL_COPY_WRITE_BUFFER:
    case GL_PIXEL_PACK_BUFFER:
    case GL_PIXEL_UNPACK_BUFFER:
    case GL_UNIFORM_BUFFER:
    case GL_TRANSFORM_FEEDBACK_BUFFER:
      return true;
    default:
      return false;
  }
}

bool ValidAttributeType(GLenum type) {
  switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
    case GL_INT:
    case GL_UNSIGNED_INT:
    case GL_FLOAT:
    case GL_HALF_FLOAT:
    case GL_FIXED:
    case GL_INT_2_10_10_10_REV:
    case GL_UNSIGNED_INT_2_10_10_10_REV:
      return true;
    default:
      return false;
  }
}

void SetAttributePointer(GLuint index,
                         GLint size,
                         GLenum type,
                         GLboolean normalized,
                         GLsizei stride,
                         const void* pointer,
                         GLboolean integer) {
  Context* context = ApiContext();
  if (!context)
    return;
  if (index >= static_cast<GLuint>(kMaxVertexAttribs)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (size < 1 || size > 4) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!ValidAttributeType(type)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (stride < 0 || stride > 255) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  VertexArray* array = ActiveVertexArray(context);
  if (!array) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  VertexAttribute& attribute = array->attributes[index];
  attribute.size = size;
  attribute.type = type;
  attribute.normalized = normalized != GL_FALSE;
  attribute.integer = integer != GL_FALSE;
  attribute.stride = stride;
  attribute.offset = 0;
  attribute.client_pointer = static_cast<const uint8_t*>(pointer);
  attribute.buffer = context->state.array_buffer;
}

}  // namespace
}  // namespace zgl

extern "C" {

// ---- Buffer objects -------------------------------------------------------

void glGenBuffers(GLsizei n, GLuint* buffers) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !buffers)
    return;
  if (n < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  for (GLsizei i = 0; i < n; ++i)
    buffers[i] = context->buffers.Create();
}

void glDeleteBuffers(GLsizei n, const GLuint* buffers) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !buffers)
    return;
  if (n < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  for (GLsizei i = 0; i < n; ++i) {
    const GLuint name = buffers[i];
    context->buffers.Delete(name);
    if (context->state.array_buffer == name)
      context->state.array_buffer = 0;
    if (context->state.element_buffer == name) {
      context->state.element_buffer = 0;
      context->default_vertex_array.element_buffer = 0;
    }
    if (context->state.uniform_buffer == name)
      context->state.uniform_buffer = 0;
    if (context->state.transform_feedback_buffer == name)
      context->state.transform_feedback_buffer = 0;
    if (context->state.copy_read_buffer == name)
      context->state.copy_read_buffer = 0;
    if (context->state.copy_write_buffer == name)
      context->state.copy_write_buffer = 0;
    if (context->state.pixel_pack_buffer == name)
      context->state.pixel_pack_buffer = 0;
    if (context->state.pixel_unpack_buffer == name)
      context->state.pixel_unpack_buffer = 0;
    for (int i = 0; i < zgl::kMaxUniformBufferBindings; ++i) {
      if (context->state.uniform_buffers[i].buffer == name)
        context->state.uniform_buffers[i] = zgl::IndexedBufferBinding();
    }
    auto clear_feedback = [name](zgl::TransformFeedback* feedback) {
      if (!feedback)
        return;
      for (int i = 0; i < zgl::kMaxTransformFeedbackBuffers; ++i) {
        if (feedback->buffers[i].buffer == name)
          feedback->buffers[i] = zgl::IndexedBufferBinding();
      }
    };
    // Only the current transform feedback object is unbound.  Bindings in
    // other transform feedback objects remain stale until they are rebound.
    clear_feedback(zgl::CurrentTransformFeedback(context));
  }
}

GLboolean glIsBuffer(GLuint buffer) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || buffer == 0)
    return GL_FALSE;
  return context->buffers.Get(buffer) ? GL_TRUE : GL_FALSE;
}

void glBindBuffer(GLenum target, GLuint buffer) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (!zgl::ValidBufferTarget(target)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (buffer != 0 && !context->buffers.Get(buffer)) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (target == GL_ARRAY_BUFFER) {
    context->state.array_buffer = buffer;
  } else if (target == GL_ELEMENT_ARRAY_BUFFER) {
    context->state.element_buffer = buffer;
    zgl::VertexArray* array = zgl::ActiveVertexArray(context);
    if (array)
      array->element_buffer = buffer;
  } else if (target == GL_COPY_READ_BUFFER) {
    context->state.copy_read_buffer = buffer;
  } else if (target == GL_COPY_WRITE_BUFFER) {
    context->state.copy_write_buffer = buffer;
  } else if (target == GL_UNIFORM_BUFFER) {
    context->state.uniform_buffer = buffer;
  } else if (target == GL_TRANSFORM_FEEDBACK_BUFFER) {
    context->state.transform_feedback_buffer = buffer;
  } else if (target == GL_PIXEL_PACK_BUFFER) {
    context->state.pixel_pack_buffer = buffer;
  } else if (target == GL_PIXEL_UNPACK_BUFFER) {
    context->state.pixel_unpack_buffer = buffer;
  }
}

void glBufferData(GLenum target,
                  GLsizeiptr size,
                  const void* data,
                  GLenum usage) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (!zgl::ValidBufferTarget(target)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (size < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  switch (usage) {
    case GL_STREAM_DRAW:
    case GL_STATIC_DRAW:
    case GL_DYNAMIC_DRAW:
    case GL_STREAM_READ:
    case GL_STREAM_COPY:
    case GL_STATIC_READ:
    case GL_STATIC_COPY:
    case GL_DYNAMIC_READ:
    case GL_DYNAMIC_COPY:
      break;
    default:
      context->SetError(GL_INVALID_ENUM);
      return;
  }
  zgl::Buffer* buffer = zgl::BoundBuffer(context, target);
  if (!buffer) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  buffer->data.assign(static_cast<size_t>(size), 0u);
  if (data && size > 0)
    memcpy(buffer->data.data(), data, static_cast<size_t>(size));
  buffer->usage = usage;
  buffer->target = target;
}

void glBufferSubData(GLenum target,
                     GLintptr offset,
                     GLsizeiptr size,
                     const void* data) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (!zgl::ValidBufferTarget(target)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (offset < 0 || size < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  zgl::Buffer* buffer = zgl::BoundBuffer(context, target);
  if (!buffer) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  const size_t begin = static_cast<size_t>(offset);
  const size_t length = static_cast<size_t>(size);
  if (begin + length > buffer->data.size()) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (data && length > 0)
    memcpy(buffer->data.data() + begin, data, length);
}

void glCopyBufferSubData(GLenum read_target,
                         GLenum write_target,
                         GLintptr read_offset,
                         GLintptr write_offset,
                         GLsizeiptr size) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  zgl::Buffer* source = zgl::BoundBuffer(context, read_target);
  zgl::Buffer* destination = zgl::BoundBuffer(context, write_target);
  if (!source || !destination) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (read_offset < 0 || write_offset < 0 || size < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  const size_t read_begin = static_cast<size_t>(read_offset);
  const size_t write_begin = static_cast<size_t>(write_offset);
  const size_t length = static_cast<size_t>(size);
  if (read_begin + length > source->data.size() ||
      write_begin + length > destination->data.size()) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (source == destination && read_begin < write_begin + length &&
      write_begin < read_begin + length) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  memmove(destination->data.data() + write_begin,
          source->data.data() + read_begin, length);
}

void* glMapBufferRange(GLenum target,
                       GLintptr offset,
                       GLsizeiptr length,
                       GLbitfield access) {
  (void)access;
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return nullptr;
  zgl::Buffer* buffer = zgl::BoundBuffer(context, target);
  if (!buffer) {
    context->SetError(GL_INVALID_OPERATION);
    return nullptr;
  }
  if (offset < 0 || length < 0 || buffer->mapped) {
    context->SetError(GL_INVALID_OPERATION);
    return nullptr;
  }
  const size_t begin = static_cast<size_t>(offset);
  const size_t size = static_cast<size_t>(length);
  if (begin + size > buffer->data.size()) {
    context->SetError(GL_INVALID_VALUE);
    return nullptr;
  }
  buffer->mapped = true;
  buffer->access_flags = access;
  buffer->map_offset = begin;
  buffer->map_length = size;
  return buffer->data.data() + begin;
}

GLboolean glUnmapBuffer(GLenum target) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return GL_FALSE;
  zgl::Buffer* buffer = zgl::BoundBuffer(context, target);
  if (!buffer || !buffer->mapped) {
    context->SetError(GL_INVALID_OPERATION);
    return GL_FALSE;
  }
  buffer->mapped = false;
  buffer->access_flags = 0;
  buffer->map_offset = 0;
  buffer->map_length = 0;
  return GL_TRUE;
}

void glFlushMappedBufferRange(GLenum target,
                              GLintptr offset,
                              GLsizeiptr length) {
  (void)target;
  (void)offset;
  (void)length;
}

void glGetBufferParameteriv(GLenum target, GLenum pname, GLint* params) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !params)
    return;
  zgl::Buffer* buffer = zgl::BoundBuffer(context, target);
  if (!buffer) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  switch (pname) {
    case GL_BUFFER_SIZE:
      params[0] = static_cast<GLint>(buffer->data.size());
      return;
    case GL_BUFFER_USAGE:
      params[0] = static_cast<GLint>(buffer->usage);
      return;
    case GL_BUFFER_MAPPED:
      params[0] = buffer->mapped ? GL_TRUE : GL_FALSE;
      return;
    case GL_BUFFER_ACCESS_FLAGS:
      params[0] = static_cast<GLint>(buffer->access_flags);
      return;
    case GL_BUFFER_MAP_OFFSET:
      params[0] = static_cast<GLint>(buffer->map_offset);
      return;
    case GL_BUFFER_MAP_LENGTH:
      params[0] = static_cast<GLint>(buffer->map_length);
      return;
    default:
      context->SetError(GL_INVALID_ENUM);
      return;
  }
}

void glGetBufferPointerv(GLenum target, GLenum pname, void** params) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !params)
    return;
  if (pname != GL_BUFFER_MAP_POINTER) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  zgl::Buffer* buffer = zgl::BoundBuffer(context, target);
  if (!buffer) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  *params = buffer->mapped ? buffer->data.data() + buffer->map_offset : nullptr;
}

// ---- Vertex array objects -------------------------------------------------

void glGenVertexArrays(GLsizei n, GLuint* arrays) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !arrays)
    return;
  if (n < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  for (GLsizei i = 0; i < n; ++i)
    arrays[i] = context->vertex_arrays.Create();
}

void glDeleteVertexArrays(GLsizei n, const GLuint* arrays) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !arrays)
    return;
  if (n < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  for (GLsizei i = 0; i < n; ++i) {
    context->vertex_arrays.Delete(arrays[i]);
    if (context->state.vertex_array == arrays[i])
      context->state.vertex_array = 0;
  }
}

GLboolean glIsVertexArray(GLuint array) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || array == 0)
    return GL_FALSE;
  return context->vertex_arrays.Get(array) ? GL_TRUE : GL_FALSE;
}

void glBindVertexArray(GLuint array) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (array != 0 && !context->vertex_arrays.Get(array)) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  context->state.vertex_array = array;
  zgl::VertexArray* active = zgl::ActiveVertexArray(context);
  context->state.element_buffer = active ? active->element_buffer : 0;
}

// ---- Vertex attributes ----------------------------------------------------

void glEnableVertexAttribArray(GLuint index) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (index >= static_cast<GLuint>(zgl::kMaxVertexAttribs)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  zgl::VertexArray* array = zgl::ActiveVertexArray(context);
  if (array)
    array->attributes[index].enabled = true;
}

void glDisableVertexAttribArray(GLuint index) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (index >= static_cast<GLuint>(zgl::kMaxVertexAttribs)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  zgl::VertexArray* array = zgl::ActiveVertexArray(context);
  if (array)
    array->attributes[index].enabled = false;
}

void glVertexAttribPointer(GLuint index,
                           GLint size,
                           GLenum type,
                           GLboolean normalized,
                           GLsizei stride,
                           const void* pointer) {
  zgl::SetAttributePointer(index, size, type, normalized, stride, pointer,
                           GL_FALSE);
}

void glVertexAttribIPointer(GLuint index,
                            GLint size,
                            GLenum type,
                            GLsizei stride,
                            const void* pointer) {
  zgl::SetAttributePointer(index, size, type, GL_FALSE, stride, pointer,
                           GL_TRUE);
}

void glVertexAttribDivisor(GLuint index, GLuint divisor) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (index >= static_cast<GLuint>(zgl::kMaxVertexAttribs)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  zgl::VertexArray* array = zgl::ActiveVertexArray(context);
  if (array)
    array->attributes[index].divisor = static_cast<int>(divisor);
}

void glVertexAttrib1f(GLuint index, GLfloat x) {
  GLfloat values[4] = {x, 0.0f, 0.0f, 1.0f};
  glVertexAttrib4fv(index, values);
}

void glVertexAttrib2f(GLuint index, GLfloat x, GLfloat y) {
  GLfloat values[4] = {x, y, 0.0f, 1.0f};
  glVertexAttrib4fv(index, values);
}

void glVertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z) {
  GLfloat values[4] = {x, y, z, 1.0f};
  glVertexAttrib4fv(index, values);
}

void glVertexAttrib4f(GLuint index,
                      GLfloat x,
                      GLfloat y,
                      GLfloat z,
                      GLfloat w) {
  GLfloat values[4] = {x, y, z, w};
  glVertexAttrib4fv(index, values);
}

void glVertexAttrib1fv(GLuint index, const GLfloat* values) {
  GLfloat expanded[4] = {values ? values[0] : 0.0f, 0.0f, 0.0f, 1.0f};
  glVertexAttrib4fv(index, expanded);
}

void glVertexAttrib2fv(GLuint index, const GLfloat* values) {
  GLfloat expanded[4] = {values ? values[0] : 0.0f, values ? values[1] : 0.0f,
                         0.0f, 1.0f};
  glVertexAttrib4fv(index, expanded);
}

void glVertexAttrib3fv(GLuint index, const GLfloat* values) {
  GLfloat expanded[4] = {values ? values[0] : 0.0f, values ? values[1] : 0.0f,
                         values ? values[2] : 0.0f, 1.0f};
  glVertexAttrib4fv(index, expanded);
}

void glVertexAttrib4fv(GLuint index, const GLfloat* values) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !values)
    return;
  if (index >= static_cast<GLuint>(zgl::kMaxVertexAttribs)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  for (int i = 0; i < 4; ++i) {
    context->state.current_attribute[index][i] = values[i];
    context->state.current_attribute_integer[index][i] =
        values[i] >= 0.0f ? static_cast<GLint>(values[i] + 0.5f)
                          : static_cast<GLint>(values[i] - 0.5f);
  }
}

void glVertexAttribI4i(GLuint index, GLint x, GLint y, GLint z, GLint w) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (index >= static_cast<GLuint>(zgl::kMaxVertexAttribs)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  const GLint values[4] = {x, y, z, w};
  for (int i = 0; i < 4; ++i) {
    context->state.current_attribute[index][i] =
        static_cast<float>(values[i]);
    context->state.current_attribute_integer[index][i] = values[i];
  }
}

void glVertexAttribI4iv(GLuint index, const GLint* values) {
  if (!values)
    return;
  glVertexAttribI4i(index, values[0], values[1], values[2], values[3]);
}

void glVertexAttribI4ui(GLuint index, GLuint x, GLuint y, GLuint z, GLuint w) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (index >= static_cast<GLuint>(zgl::kMaxVertexAttribs)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  const GLuint values[4] = {x, y, z, w};
  for (int i = 0; i < 4; ++i) {
    context->state.current_attribute[index][i] =
        static_cast<float>(values[i]);
    context->state.current_attribute_integer[index][i] =
        static_cast<GLint>(values[i]);
  }
}

void glGetVertexAttribiv(GLuint index, GLenum pname, GLint* params) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !params)
    return;
  if (index >= static_cast<GLuint>(zgl::kMaxVertexAttribs)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  const zgl::VertexArray* array = zgl::ActiveVertexArray(context);
  if (!array) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  const zgl::VertexAttribute& attribute = array->attributes[index];
  switch (pname) {
    case GL_VERTEX_ATTRIB_ARRAY_ENABLED:
      params[0] = attribute.enabled ? GL_TRUE : GL_FALSE;
      return;
    case GL_VERTEX_ATTRIB_ARRAY_SIZE:
      params[0] = attribute.size;
      return;
    case GL_VERTEX_ATTRIB_ARRAY_TYPE:
      params[0] = static_cast<GLint>(attribute.type);
      return;
    case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
      params[0] = attribute.normalized ? GL_TRUE : GL_FALSE;
      return;
    case GL_VERTEX_ATTRIB_ARRAY_STRIDE:
      params[0] = attribute.stride;
      return;
    case GL_VERTEX_ATTRIB_ARRAY_DIVISOR:
      params[0] = attribute.divisor;
      return;
    case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING:
      params[0] = static_cast<GLint>(attribute.buffer);
      return;
    case GL_VERTEX_ATTRIB_ARRAY_INTEGER:
      params[0] = attribute.integer ? GL_TRUE : GL_FALSE;
      return;
    case GL_CURRENT_VERTEX_ATTRIB:
      for (int i = 0; i < 4; ++i)
        params[i] = context->state.current_attribute_integer[index][i];
      return;
    default:
      context->SetError(GL_INVALID_ENUM);
      return;
  }
}

void glVertexAttribI4uiv(GLuint index, const GLuint* values) {
  if (!values)
    return;
  glVertexAttribI4ui(index, values[0], values[1], values[2], values[3]);
}

void glGetVertexAttribIiv(GLuint index, GLenum pname, GLint* params) {
  glGetVertexAttribiv(index, pname, params);
}

void glGetVertexAttribIuiv(GLuint index, GLenum pname, GLuint* params) {
  GLint values[4] = {};
  glGetVertexAttribiv(index, pname, values);
  if (!params)
    return;
  const int count = pname == GL_CURRENT_VERTEX_ATTRIB ? 4 : 1;
  for (int i = 0; i < count; ++i)
    params[i] = static_cast<GLuint>(values[i]);
}

void glGetVertexAttribfv(GLuint index, GLenum pname, GLfloat* params) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !params)
    return;
  if (index >= static_cast<GLuint>(zgl::kMaxVertexAttribs)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (pname == GL_CURRENT_VERTEX_ATTRIB) {
    for (int i = 0; i < 4; ++i)
      params[i] = context->state.current_attribute[index][i];
    return;
  }
  GLint value = 0;
  glGetVertexAttribiv(index, pname, &value);
  params[0] = static_cast<GLfloat>(value);
}

void glGetVertexAttribPointerv(GLuint index, GLenum pname, void** pointer) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !pointer)
    return;
  if (index >= static_cast<GLuint>(zgl::kMaxVertexAttribs)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (pname != GL_VERTEX_ATTRIB_ARRAY_POINTER) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  const zgl::VertexArray* array = zgl::ActiveVertexArray(context);
  if (!array) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  *pointer = const_cast<uint8_t*>(array->attributes[index].client_pointer);
}

}  // extern "C"
