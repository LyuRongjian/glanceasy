// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <vector>

#include "GLES3/gl32.h"
#include "src/glanceasy_api_common.h"

namespace glanceasy {
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
  const bool packed_type = type == GL_INT_2_10_10_10_REV ||
                           type == GL_UNSIGNED_INT_2_10_10_10_REV;
  if (integer != GL_FALSE && packed_type) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (!ValidAttributeType(type)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (!integer && packed_type && size != 4) {
    context->SetError(GL_INVALID_OPERATION);
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
  if (context->state.vertex_array != 0 && context->state.array_buffer == 0 &&
      pointer != nullptr) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  VertexAttribute& attribute = array->attributes[index];
  if (getenv("GLANCEASY_DEBUG_VIEWPORT") && index == 1 && pointer) {
    const float* values = static_cast<const float*>(pointer);
    fprintf(stderr, "[attribptr] %u %.3f %.3f %.3f %.3f %.3f %.3f\n", index,
            values[0], values[1], values[2], values[3], values[4], values[5]);
  }
  attribute.size = size;
  attribute.type = type;
  attribute.normalized = normalized != GL_FALSE;
  attribute.integer = integer != GL_FALSE;
  attribute.stride = stride;
  attribute.buffer = context->state.array_buffer;
  if (attribute.buffer != 0) {
    // A pointer argument names a byte offset inside the bound buffer.
    attribute.offset = reinterpret_cast<intptr_t>(pointer);
    attribute.client_pointer = nullptr;
  } else {
    attribute.offset = 0;
    attribute.client_pointer = static_cast<const uint8_t*>(pointer);
  }
}

}  // namespace
}  // namespace glanceasy

extern "C" {

// ---- Buffer objects -------------------------------------------------------

void glGenBuffers(GLsizei n, GLuint* buffers) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (n < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!buffers)
    return;
  for (GLsizei i = 0; i < n; ++i)
    buffers[i] = context->buffers.Generate();
}

void glDeleteBuffers(GLsizei n, const GLuint* buffers) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (n < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!buffers)
    return;
  for (GLsizei i = 0; i < n; ++i) {
    const GLuint name = buffers[i];
    if (name == 0)
      continue;
    // A deleted buffer stays alive while a vertex array or transform feedback
    // object still references it.  Its data remains readable by draws.
    if (glanceasy::Buffer* object = context->buffers.Get(name))
      object->delete_status = true;
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
    for (int i = 0; i < glanceasy::kMaxUniformBufferBindings; ++i) {
      if (context->state.uniform_buffers[i].buffer == name)
        context->state.uniform_buffers[i] = glanceasy::IndexedBufferBinding();
    }
    // Deleting a buffer does not break a transform-feedback attachment: the
    // container keeps the name, but must no longer write into storage that a
    // later glGenBuffers may recycle for a different object.
    auto mark_deleted = [name](glanceasy::TransformFeedback& feedback) {
      for (int i = 0; i < glanceasy::kMaxTransformFeedbackBuffers; ++i) {
        if (feedback.buffers[i].buffer == name)
          feedback.buffers[i].attachment_deleted = true;
      }
    };
    mark_deleted(context->default_transform_feedback);
    context->transform_feedbacks.ForEach(mark_deleted);
  }
}

GLboolean glIsBuffer(GLuint buffer) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context || buffer == 0)
    return GL_FALSE;
  const glanceasy::Buffer* object = context->buffers.Get(buffer);
  return object && !object->delete_status ? GL_TRUE : GL_FALSE;
}

void glBindBuffer(GLenum target, GLuint buffer) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (!glanceasy::ValidBufferTarget(target)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (buffer != 0 && !context->buffers.GetOrCreate(buffer))
    return;
  if (target == GL_ARRAY_BUFFER) {
    context->state.array_buffer = buffer;
  } else if (target == GL_ELEMENT_ARRAY_BUFFER) {
    context->state.element_buffer = buffer;
    glanceasy::VertexArray* array = glanceasy::ActiveVertexArray(context);
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
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (!glanceasy::ValidBufferTarget(target)) {
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
  glanceasy::Buffer* buffer = glanceasy::BoundBuffer(context, target);
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
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (!glanceasy::ValidBufferTarget(target)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (offset < 0 || size < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  glanceasy::Buffer* buffer = glanceasy::BoundBuffer(context, target);
  if (!buffer) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (buffer->mapped) {
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
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  glanceasy::Buffer* source = glanceasy::BoundBuffer(context, read_target);
  glanceasy::Buffer* destination = glanceasy::BoundBuffer(context, write_target);
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
  if (source->mapped || destination->mapped) {
    context->SetError(GL_INVALID_OPERATION);
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
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return nullptr;
  glanceasy::Buffer* buffer = glanceasy::BoundBuffer(context, target);
  if (!buffer) {
    context->SetError(GL_INVALID_OPERATION);
    return nullptr;
  }
  if (offset < 0 || length <= 0) {
    context->SetError(GL_INVALID_VALUE);
    return nullptr;
  }
  if (buffer->mapped) {
    context->SetError(GL_INVALID_OPERATION);
    return nullptr;
  }
  const size_t begin = static_cast<size_t>(offset);
  const size_t size = static_cast<size_t>(length);
  if (begin + size > buffer->data.size()) {
    context->SetError(GL_INVALID_VALUE);
    return nullptr;
  }
  const GLbitfield valid_bits = GL_MAP_READ_BIT | GL_MAP_WRITE_BIT |
                                GL_MAP_INVALIDATE_RANGE_BIT |
                                GL_MAP_INVALIDATE_BUFFER_BIT |
                                GL_MAP_FLUSH_EXPLICIT_BIT |
                                GL_MAP_UNSYNCHRONIZED_BIT;
  if (access & ~valid_bits) {
    context->SetError(GL_INVALID_VALUE);
    return nullptr;
  }
  const bool read = (access & GL_MAP_READ_BIT) != 0;
  const bool write = (access & GL_MAP_WRITE_BIT) != 0;
  if ((!read && !write) ||
      (read && (access & (GL_MAP_INVALIDATE_RANGE_BIT |
                          GL_MAP_INVALIDATE_BUFFER_BIT |
                          GL_MAP_UNSYNCHRONIZED_BIT))) ||
      ((access & GL_MAP_FLUSH_EXPLICIT_BIT) && !write)) {
    context->SetError(GL_INVALID_OPERATION);
    return nullptr;
  }
  buffer->mapped = true;
  buffer->access_flags = access;
  buffer->map_offset = begin;
  buffer->map_length = size;
  return buffer->data.data() + begin;
}

GLboolean glUnmapBuffer(GLenum target) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return GL_FALSE;
  glanceasy::Buffer* buffer = glanceasy::BoundBuffer(context, target);
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
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (!glanceasy::ValidBufferTarget(target)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  glanceasy::Buffer* buffer = glanceasy::BoundBuffer(context, target);
  if (!buffer) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (offset < 0 || length < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!buffer->mapped || !(buffer->access_flags & GL_MAP_FLUSH_EXPLICIT_BIT)) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  if (static_cast<size_t>(offset) + static_cast<size_t>(length) >
      buffer->map_length) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
}

void glGetBufferParameteriv(GLenum target, GLenum pname, GLint* params) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context || !params)
    return;
  if (!glanceasy::ValidBufferTarget(target)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  glanceasy::Buffer* buffer = glanceasy::BoundBuffer(context, target);
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
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context || !params)
    return;
  if (!glanceasy::ValidBufferTarget(target)) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  if (pname != GL_BUFFER_MAP_POINTER) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  glanceasy::Buffer* buffer = glanceasy::BoundBuffer(context, target);
  if (!buffer) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  *params = buffer->mapped ? buffer->data.data() + buffer->map_offset : nullptr;
}

// ---- Vertex array objects -------------------------------------------------

void glGenVertexArrays(GLsizei n, GLuint* arrays) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (n < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!arrays)
    return;
  for (GLsizei i = 0; i < n; ++i)
    arrays[i] = context->vertex_arrays.Generate();
}

void glDeleteVertexArrays(GLsizei n, const GLuint* arrays) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (n < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (!arrays)
    return;
  for (GLsizei i = 0; i < n; ++i) {
    context->vertex_arrays.Delete(arrays[i]);
    if (context->state.vertex_array == arrays[i])
      context->state.vertex_array = 0;
  }
}

GLboolean glIsVertexArray(GLuint array) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context || array == 0)
    return GL_FALSE;
  return context->vertex_arrays.Get(array) ? GL_TRUE : GL_FALSE;
}

void glBindVertexArray(GLuint array) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (array != 0 && !context->vertex_arrays.GetOrCreateReserved(array)) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  context->state.vertex_array = array;
  glanceasy::VertexArray* active = glanceasy::ActiveVertexArray(context);
  context->state.element_buffer = active ? active->element_buffer : 0;
}

// ---- Vertex attributes ----------------------------------------------------

void glEnableVertexAttribArray(GLuint index) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (index >= static_cast<GLuint>(glanceasy::kMaxVertexAttribs)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  glanceasy::VertexArray* array = glanceasy::ActiveVertexArray(context);
  if (array)
    array->attributes[index].enabled = true;
}

void glDisableVertexAttribArray(GLuint index) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (index >= static_cast<GLuint>(glanceasy::kMaxVertexAttribs)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  glanceasy::VertexArray* array = glanceasy::ActiveVertexArray(context);
  if (array)
    array->attributes[index].enabled = false;
}

void glVertexAttribPointer(GLuint index,
                           GLint size,
                           GLenum type,
                           GLboolean normalized,
                           GLsizei stride,
                           const void* pointer) {
  glanceasy::SetAttributePointer(index, size, type, normalized, stride, pointer,
                           GL_FALSE);
}

void glVertexAttribIPointer(GLuint index,
                            GLint size,
                            GLenum type,
                            GLsizei stride,
                            const void* pointer) {
  glanceasy::SetAttributePointer(index, size, type, GL_FALSE, stride, pointer,
                           GL_TRUE);
}

void glVertexAttribDivisor(GLuint index, GLuint divisor) {
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (index >= static_cast<GLuint>(glanceasy::kMaxVertexAttribs)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  glanceasy::VertexArray* array = glanceasy::ActiveVertexArray(context);
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
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context || !values)
    return;
  if (index >= static_cast<GLuint>(glanceasy::kMaxVertexAttribs)) {
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
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (index >= static_cast<GLuint>(glanceasy::kMaxVertexAttribs)) {
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
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context)
    return;
  if (index >= static_cast<GLuint>(glanceasy::kMaxVertexAttribs)) {
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
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context || !params)
    return;
  if (index >= static_cast<GLuint>(glanceasy::kMaxVertexAttribs)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  const glanceasy::VertexArray* array = glanceasy::ActiveVertexArray(context);
  if (!array) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  const glanceasy::VertexAttribute& attribute = array->attributes[index];
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
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context || !params)
    return;
  if (index >= static_cast<GLuint>(glanceasy::kMaxVertexAttribs)) {
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
  glanceasy::Context* context = glanceasy::ApiContext();
  if (!context || !pointer)
    return;
  if (index >= static_cast<GLuint>(glanceasy::kMaxVertexAttribs)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (pname != GL_VERTEX_ATTRIB_ARRAY_POINTER) {
    context->SetError(GL_INVALID_ENUM);
    return;
  }
  const glanceasy::VertexArray* array = glanceasy::ActiveVertexArray(context);
  if (!array) {
    context->SetError(GL_INVALID_OPERATION);
    return;
  }
  const glanceasy::VertexAttribute& attribute = array->attributes[index];
  *pointer = attribute.buffer
                 ? reinterpret_cast<void*>(
                       static_cast<intptr_t>(attribute.offset))
                 : const_cast<uint8_t*>(attribute.client_pointer);
}

}  // extern "C"
