// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Entry points that the CPU backend accepts for API compatibility but cannot
// accelerate: transform feedback, compute, indirect draws, shader storage,
// multisampled storage and debug output.  Every one of them reports a sticky
// GL_INVALID_OPERATION where the feature would change observable output, and
// stays silent where the specification allows a no-op.

#include <stddef.h>
#include <stdint.h>

#include "GLES3/gl32.h"
#include "src/zgl_api_common.h"

namespace zgl {
namespace {

void Reject() {
  Context* context = ApiContext();
  if (context)
    context->SetError(GL_INVALID_OPERATION);
}

}  // namespace
}  // namespace zgl

extern "C" {

// ---- Transform feedback ---------------------------------------------------

void glGenTransformFeedbacks(GLsizei n, GLuint* ids) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !ids)
    return;
  for (GLsizei i = 0; i < n; ++i)
    ids[i] = 0;
}

void glDeleteTransformFeedbacks(GLsizei n, const GLuint* ids) {
  (void)n;
  (void)ids;
}

GLboolean glIsTransformFeedback(GLuint id) {
  (void)id;
  return GL_FALSE;
}

void glBindTransformFeedback(GLenum target, GLuint id) {
  (void)target;
  if (id != 0)
    zgl::Reject();
}

void glBeginTransformFeedback(GLenum primitive_mode) {
  (void)primitive_mode;
  zgl::Reject();
}

void glEndTransformFeedback(void) {}

void glPauseTransformFeedback(void) {}

void glResumeTransformFeedback(void) {}

void glTransformFeedbackVaryings(GLuint program,
                                 GLsizei count,
                                 const GLchar* const* varyings,
                                 GLenum buffer_mode) {
  (void)program;
  (void)count;
  (void)varyings;
  (void)buffer_mode;
}

void glGetTransformFeedbackVarying(GLuint program,
                                   GLuint index,
                                   GLsizei buffer_size,
                                   GLsizei* length,
                                   GLsizei* size,
                                   GLenum* type,
                                   GLchar* name) {
  (void)program;
  (void)index;
  (void)buffer_size;
  (void)size;
  (void)type;
  if (length)
    *length = 0;
  if (name && buffer_size > 0)
    name[0] = '\0';
}

void glDrawTransformFeedback(GLenum mode, GLuint id) {
  (void)mode;
  (void)id;
  zgl::Reject();
}

// ---- Buffer binding points and 64-bit queries -----------------------------

void glBindBufferBase(GLenum target, GLuint index, GLuint buffer) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (target != GL_UNIFORM_BUFFER) {
    if (target == GL_TRANSFORM_FEEDBACK_BUFFER ||
        target == GL_SHADER_STORAGE_BUFFER) {
      zgl::Reject();
    } else {
      context->SetError(GL_INVALID_ENUM);
    }
    return;
  }
  if (index >= static_cast<GLuint>(zgl::kMaxUniformBufferBindings)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  zgl::IndexedBufferBinding binding;
  if (buffer != 0) {
    zgl::Buffer* object = context->buffers.Get(buffer);
    if (!object) {
      context->SetError(GL_INVALID_OPERATION);
      return;
    }
    binding.buffer = buffer;
    binding.size = object->data.size();
  }
  context->state.uniform_buffers[index] = binding;
}

void glBindBufferRange(GLenum target,
                       GLuint index,
                       GLuint buffer,
                       GLintptr offset,
                       GLsizeiptr size) {
  zgl::Context* context = zgl::ApiContext();
  if (!context)
    return;
  if (target != GL_UNIFORM_BUFFER) {
    if (target == GL_TRANSFORM_FEEDBACK_BUFFER ||
        target == GL_SHADER_STORAGE_BUFFER) {
      zgl::Reject();
    } else {
      context->SetError(GL_INVALID_ENUM);
    }
    return;
  }
  if (index >= static_cast<GLuint>(zgl::kMaxUniformBufferBindings)) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  if (offset < 0 || size < 0) {
    context->SetError(GL_INVALID_VALUE);
    return;
  }
  zgl::IndexedBufferBinding binding;
  if (buffer != 0) {
    zgl::Buffer* object = context->buffers.Get(buffer);
    if (!object) {
      context->SetError(GL_INVALID_OPERATION);
      return;
    }
    const size_t begin = static_cast<size_t>(offset);
    const size_t length = static_cast<size_t>(size);
    if (length == 0 || begin + length > object->data.size()) {
      context->SetError(GL_INVALID_VALUE);
      return;
    }
    binding.buffer = buffer;
    binding.offset = begin;
    binding.size = length;
  }
  context->state.uniform_buffers[index] = binding;
}

void glGetBufferParameteri64v(GLenum target, GLenum pname, GLint64* params) {
  GLint value = 0;
  glGetBufferParameteriv(target, pname, &value);
  if (params)
    params[0] = value;
}

void glGetInteger64v(GLenum pname, GLint64* data) {
  GLint value = 0;
  glGetIntegerv(pname, &value);
  if (data)
    data[0] = value;
}

void glGetIntegeri_v(GLenum pname, GLuint index, GLint* data) {
  zgl::Context* context = zgl::ApiContext();
  if (!context || !data)
    return;
  switch (pname) {
    case GL_UNIFORM_BUFFER_BINDING:
    case GL_UNIFORM_BUFFER_START:
    case GL_UNIFORM_BUFFER_SIZE:
      if (index >= static_cast<GLuint>(zgl::kMaxUniformBufferBindings)) {
        context->SetError(GL_INVALID_VALUE);
        return;
      }
      if (pname == GL_UNIFORM_BUFFER_BINDING) {
        data[0] =
            static_cast<GLint>(context->state.uniform_buffers[index].buffer);
      } else if (pname == GL_UNIFORM_BUFFER_START) {
        data[0] =
            static_cast<GLint>(context->state.uniform_buffers[index].offset);
      } else {
        data[0] = static_cast<GLint>(context->state.uniform_buffers[index].size);
      }
      return;
    default:
      context->SetError(GL_INVALID_ENUM);
      return;
  }
}

void glGetInteger64i_v(GLenum pname, GLuint index, GLint64* data) {
  GLint value = 0;
  glGetIntegeri_v(pname, index, &value);
  if (data)
    data[0] = value;
}

void glGetBooleani_v(GLenum pname, GLuint index, GLboolean* data) {
  (void)index;
  GLint value = 0;
  glGetIntegerv(pname, &value);
  if (data)
    data[0] = value ? GL_TRUE : GL_FALSE;
}

void glEnablei(GLenum target, GLuint index) {
  (void)index;
  glEnable(target);
}

void glDisablei(GLenum target, GLuint index) {
  (void)index;
  glDisable(target);
}

GLboolean glIsEnabledi(GLenum target, GLuint index) {
  (void)index;
  return glIsEnabled(target);
}

// ---- Integer vertex attributes -------------------------------------------

void glGetVertexAttribIiv(GLuint index, GLenum pname, GLint* params) {
  glGetVertexAttribiv(index, pname, params);
}

void glGetVertexAttribIuiv(GLuint index, GLenum pname, GLuint* params) {
  GLint value = 0;
  glGetVertexAttribiv(index, pname, &value);
  if (params)
    params[0] = static_cast<GLuint>(value);
}

// ---- Compute and indirect draws ------------------------------------------

void glDispatchCompute(GLuint num_groups_x,
                       GLuint num_groups_y,
                       GLuint num_groups_z) {
  (void)num_groups_x;
  (void)num_groups_y;
  (void)num_groups_z;
  zgl::Reject();
}

void glDispatchComputeIndirect(GLintptr indirect) {
  (void)indirect;
  zgl::Reject();
}

void glDrawArraysIndirect(GLenum mode, const void* indirect) {
  (void)mode;
  (void)indirect;
  zgl::Reject();
}

void glDrawElementsIndirect(GLenum mode, GLenum type, const void* indirect) {
  (void)mode;
  (void)type;
  (void)indirect;
  zgl::Reject();
}

void glMemoryBarrier(GLbitfield barriers) {
  (void)barriers;
}

void glMemoryBarrierByRegion(GLbitfield barriers) {
  (void)barriers;
}

void glBindImageTexture(GLuint unit,
                        GLuint texture,
                        GLint level,
                        GLboolean layered,
                        GLint layer,
                        GLenum access,
                        GLenum format) {
  (void)unit;
  (void)texture;
  (void)level;
  (void)layered;
  (void)layer;
  (void)access;
  (void)format;
  zgl::Reject();
}

void glShaderStorageBlockBinding(GLuint program,
                                 GLuint storage_block_index,
                                 GLuint storage_block_binding) {
  (void)program;
  (void)storage_block_index;
  (void)storage_block_binding;
  zgl::Reject();
}

// ---- Uniform blocks -------------------------------------------------------

void glGetActiveAtomicCounterBufferiv(GLuint program,
                                      GLuint buffer_index,
                                      GLenum pname,
                                      GLint* params) {
  (void)program;
  (void)buffer_index;
  (void)pname;
  if (params)
    params[0] = 0;
}

// ---- Program interfaces and binaries --------------------------------------

void glGetProgramInterfaceiv(GLuint program,
                             GLenum program_interface,
                             GLenum pname,
                             GLint* params) {
  (void)program;
  (void)program_interface;
  (void)pname;
  if (params)
    params[0] = 0;
}

GLuint glGetProgramResourceIndex(GLuint program,
                                 GLenum program_interface,
                                 const GLchar* name) {
  (void)program;
  (void)program_interface;
  (void)name;
  return GL_INVALID_INDEX;
}

void glGetProgramResourceName(GLuint program,
                              GLenum program_interface,
                              GLuint index,
                              GLsizei buffer_size,
                              GLsizei* length,
                              GLchar* name) {
  (void)program;
  (void)program_interface;
  (void)index;
  if (length)
    *length = 0;
  if (name && buffer_size > 0)
    name[0] = '\0';
}

void glGetProgramResourceiv(GLuint program,
                            GLenum program_interface,
                            GLuint index,
                            GLsizei prop_count,
                            const GLenum* properties,
                            GLsizei buffer_size,
                            GLsizei* length,
                            GLint* params) {
  (void)program;
  (void)program_interface;
  (void)index;
  (void)prop_count;
  (void)properties;
  (void)buffer_size;
  if (length)
    *length = 0;
  if (params && buffer_size > 0)
    params[0] = 0;
}

GLint glGetProgramResourceLocation(GLuint program,
                                   GLenum program_interface,
                                   const GLchar* name) {
  (void)program;
  (void)program_interface;
  (void)name;
  return -1;
}

void glGetProgramBinary(GLuint program,
                        GLsizei buffer_size,
                        GLsizei* length,
                        GLenum* binary_format,
                        void* binary) {
  (void)program;
  (void)buffer_size;
  (void)binary;
  if (length)
    *length = 0;
  if (binary_format)
    *binary_format = 0;
  zgl::Reject();
}

void glProgramBinary(GLuint program,
                     GLenum binary_format,
                     const void* binary,
                     GLsizei length) {
  (void)program;
  (void)binary_format;
  (void)binary;
  (void)length;
  zgl::Reject();
}

void glProgramParameteri(GLuint program, GLenum pname, GLint value) {
  (void)program;
  (void)pname;
  (void)value;
  zgl::Reject();
}

// ---- Multisampling --------------------------------------------------------

void glTexStorage2DMultisample(GLenum target,
                               GLsizei samples,
                               GLenum internal_format,
                               GLsizei width,
                               GLsizei height,
                               GLboolean fixed_sample_locations) {
  (void)target;
  (void)samples;
  (void)internal_format;
  (void)width;
  (void)height;
  (void)fixed_sample_locations;
  zgl::Reject();
}

void glTexStorage3DMultisample(GLenum target,
                               GLsizei samples,
                               GLenum internal_format,
                               GLsizei width,
                               GLsizei height,
                               GLsizei depth,
                               GLboolean fixed_sample_locations) {
  (void)target;
  (void)samples;
  (void)internal_format;
  (void)width;
  (void)height;
  (void)depth;
  (void)fixed_sample_locations;
  zgl::Reject();
}

void glGetMultisamplefv(GLenum pname, GLuint index, GLfloat* value) {
  (void)pname;
  (void)index;
  if (value)
    value[0] = 0.5f;
}

void glSampleMaski(GLuint mask_number, GLbitfield mask) {
  (void)mask_number;
  (void)mask;
}

void glMinSampleShading(GLfloat value) {
  (void)value;
}

// ---- Framebuffer parameters and texture views ----------------------------

void glFramebufferParameteri(GLenum target, GLenum pname, GLint param) {
  (void)target;
  (void)pname;
  (void)param;
}

void glGetFramebufferParameteriv(GLenum target, GLenum pname, GLint* params) {
  (void)target;
  (void)pname;
  if (params)
    params[0] = 0;
}

void glTexBuffer(GLenum target, GLenum internal_format, GLuint buffer) {
  (void)target;
  (void)internal_format;
  (void)buffer;
  zgl::Reject();
}

void glTexBufferRange(GLenum target,
                      GLenum internal_format,
                      GLuint buffer,
                      GLintptr offset,
                      GLsizeiptr size) {
  (void)target;
  (void)internal_format;
  (void)buffer;
  (void)offset;
  (void)size;
  zgl::Reject();
}

void glCopyImageSubData(GLuint src_name,
                        GLenum src_target,
                        GLint src_level,
                        GLint src_x,
                        GLint src_y,
                        GLint src_z,
                        GLuint dst_name,
                        GLenum dst_target,
                        GLint dst_level,
                        GLint dst_x,
                        GLint dst_y,
                        GLint dst_z,
                        GLsizei src_width,
                        GLsizei src_height,
                        GLsizei src_depth) {
  (void)src_name;
  (void)src_target;
  (void)src_level;
  (void)src_x;
  (void)src_y;
  (void)src_z;
  (void)dst_name;
  (void)dst_target;
  (void)dst_level;
  (void)dst_x;
  (void)dst_y;
  (void)dst_z;
  (void)src_width;
  (void)src_height;
  (void)src_depth;
  zgl::Reject();
}

// ---- Tessellation and geometry -------------------------------------------

void glPatchParameteri(GLenum pname, GLint value) {
  (void)pname;
  (void)value;
  zgl::Reject();
}

void glPrimitiveBoundingBox(GLfloat min_x,
                            GLfloat min_y,
                            GLfloat min_z,
                            GLfloat min_w,
                            GLfloat max_x,
                            GLfloat max_y,
                            GLfloat max_z,
                            GLfloat max_w) {
  (void)min_x;
  (void)min_y;
  (void)min_z;
  (void)min_w;
  (void)max_x;
  (void)max_y;
  (void)max_z;
  (void)max_w;
}

void glBlendBarrier(void) {}

void glGetTexLevelParameterfv(GLenum target,
                              GLint level,
                              GLenum pname,
                              GLfloat* params) {
  GLint value = 0;
  glGetTexLevelParameteriv(target, level, pname, &value);
  if (params)
    params[0] = static_cast<GLfloat>(value);
}

void glGetInternalformativ(GLenum target,
                           GLenum internal_format,
                           GLenum pname,
                           GLsizei buffer_size,
                           GLint* params) {
  (void)target;
  (void)internal_format;
  (void)pname;
  if (params && buffer_size > 0)
    params[0] = 0;
}

// ---- Debug output --------------------------------------------------------

void glDebugMessageCallback(GLDEBUGPROC callback, const void* user_param) {
  (void)callback;
  (void)user_param;
}

void glDebugMessageControl(GLenum source,
                           GLenum type,
                           GLenum severity,
                           GLsizei count,
                           const GLuint* ids,
                           GLboolean enabled) {
  (void)source;
  (void)type;
  (void)severity;
  (void)count;
  (void)ids;
  (void)enabled;
}

void glDebugMessageInsert(GLenum source,
                          GLenum type,
                          GLuint id,
                          GLenum severity,
                          GLsizei length,
                          const GLchar* message) {
  (void)source;
  (void)type;
  (void)id;
  (void)severity;
  (void)length;
  (void)message;
}

GLuint glGetDebugMessageLog(GLuint count,
                            GLsizei buffer_size,
                            GLenum* sources,
                            GLenum* types,
                            GLuint* ids,
                            GLenum* severities,
                            GLsizei* lengths,
                            GLchar* message_log) {
  (void)count;
  (void)buffer_size;
  (void)sources;
  (void)types;
  (void)ids;
  (void)severities;
  (void)lengths;
  (void)message_log;
  return 0;
}

void glPushDebugGroup(GLenum source,
                      GLuint id,
                      GLsizei length,
                      const GLchar* message) {
  (void)source;
  (void)id;
  (void)length;
  (void)message;
}

void glPopDebugGroup(void) {}

void glObjectLabel(GLenum identifier,
                   GLuint name,
                   GLsizei length,
                   const GLchar* label) {
  (void)identifier;
  (void)name;
  (void)length;
  (void)label;
}

void glGetObjectLabel(GLenum identifier,
                      GLuint name,
                      GLsizei buffer_size,
                      GLsizei* length,
                      GLchar* label) {
  (void)identifier;
  (void)name;
  if (length)
    *length = 0;
  if (label && buffer_size > 0)
    label[0] = '\0';
}

void glObjectPtrLabel(const void* pointer,
                      GLsizei length,
                      const GLchar* label) {
  (void)pointer;
  (void)length;
  (void)label;
}

void glGetObjectPtrLabel(const void* pointer,
                         GLsizei buffer_size,
                         GLsizei* length,
                         GLchar* label) {
  (void)pointer;
  if (length)
    *length = 0;
  if (label && buffer_size > 0)
    label[0] = '\0';
}

// ---- Miscellaneous -------------------------------------------------------

void glGetPointerv(GLenum pname, void** params) {
  (void)pname;
  if (params)
    *params = nullptr;
}

}  // extern "C"
