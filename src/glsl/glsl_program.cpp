// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/glsl/glsl_program.h"

#include <stddef.h>

#include <algorithm>
#include <string>

#include "src/glsl/glsl_jit.h"

namespace zgl {
namespace glsl {

bool EvaluateGlobalInitializers(ShaderAst* ast,
                                std::vector<Value>* globals,
                                const int* uniform_map,
                                const Value* uniforms,
                                std::string* error);

namespace {

bool TypeMismatch(const Type& a, const Type& b) {
  return a != b;
}

}  // namespace

Program::Program() = default;
Program::~Program() = default;

bool Program::Link(ShaderAst* vertex,
                   ShaderAst* fragment,
                   const std::vector<std::pair<std::string, int>>& bindings,
                   std::string* log) {
  linked_ = false;
  attributes_.clear();
  uniforms_.clear();
  varyings_.clear();
  uniform_values_.clear();
  vertex_uniform_map_.clear();
  fragment_uniform_map_.clear();
  vertex_jit_.reset();
  fragment_jit_.reset();
  varying_floats_ = 0;
  runtime_error_.clear();
  uses_frag_depth_ = false;
  uses_discard_ = false;
  uses_point_size_ = false;

  const auto fail = [log](const std::string& message) {
    if (log)
      *log = message;
    return false;
  };

  if (!vertex || !fragment)
    return fail("a vertex and a fragment shader are required");
  if (vertex->stage != Stage::kVertex || fragment->stage != Stage::kFragment)
    return fail("shader stages do not match the attachment points");
  if (!vertex->main() || !fragment->main())
    return fail("both shaders must define void main()");
  if (!vertex->declares_position)
    return fail("the vertex shader does not write gl_Position");
  if (!fragment->uses_frag_color && !fragment->uses_frag_data)
    return fail("the fragment shader does not write a colour output");

  vertex_ = vertex;
  fragment_ = fragment;

  // Attributes.  glBindAttribLocation() overrides the declaration order.
  {
    std::vector<int> used;
    for (const Variable& attribute : vertex->attributes) {
      int location = attribute.index;
      for (const auto& binding : bindings) {
        if (binding.first == attribute.name)
          location = binding.second;
      }
      if (location < 0 || location >= kMaxVertexAttribs)
        return fail("attribute '" + attribute.name +
                    "' has an invalid location");
      if (std::find(used.begin(), used.end(), location) != used.end())
        return fail("two attributes share location " +
                    std::to_string(location));
      used.push_back(location);
      AttributeInfo info;
      info.name = attribute.name;
      info.type = attribute.type;
      info.location = location;
      attributes_.push_back(info);
    }
  }

  // Varyings.  Every vertex output gets a slot; every fragment input must be
  // produced by the vertex shader with exactly the same type.
  {
    for (Variable& varying : vertex->varyings) {
      VaryingInfo info;
      info.name = varying.name;
      info.type = varying.type;
      info.offset = varying_floats_;
      info.components = varying.type.components();
      varying.varying_offset = info.offset;
      varying_floats_ += info.components;
      varyings_.push_back(info);
    }
    for (Variable& input : fragment->varyings) {
      int offset = -1;
      for (const VaryingInfo& info : varyings_) {
        if (info.name == input.name) {
          if (TypeMismatch(info.type, input.type)) {
            return fail("varying '" + input.name +
                        "' has a different type in the two stages");
          }
          offset = info.offset;
          break;
        }
      }
      if (offset < 0)
        return fail("varying '" + input.name +
                    "' is not written by the vertex shader");
      input.varying_offset = offset;
    }
  }
  if (varying_floats_ > kMaxVaryingVectors * 4)
    return fail("too many varying components");

  // Uniforms.  Names and types must agree across stages; the merged list is
  // the program-wide uniform namespace that glGetUniformLocation() exposes.
  {
    std::vector<UniformInfo> merged;
    const auto merge_stage = [&](ShaderAst* ast, std::vector<int>* map) {
      map->assign(ast->uniforms.size(), -1);
      for (size_t i = 0; i < ast->uniforms.size(); ++i) {
        const Variable& uniform = ast->uniforms[i];
        int index = -1;
        for (size_t j = 0; j < merged.size(); ++j) {
          if (merged[j].name == uniform.name) {
            index = static_cast<int>(j);
            break;
          }
        }
        if (index >= 0) {
          if (TypeMismatch(merged[index].type, uniform.type))
            return false;
        } else {
          if (merged.size() >= static_cast<size_t>(kMaxUniformVectors))
            return false;
          UniformInfo info;
          info.name = uniform.name;
          info.type = uniform.type;
          info.location = static_cast<int>(merged.size());
          merged.push_back(info);
          index = info.location;
        }
        (*map)[i] = index;
      }
      return true;
    };
    if (!merge_stage(vertex, &vertex_uniform_map_) ||
        !merge_stage(fragment, &fragment_uniform_map_)) {
      return fail("uniform declarations do not match between the two stages");
    }
    uniforms_ = merged;
    uniform_values_.assign(uniforms_.size(), Value());
    for (size_t i = 0; i < uniforms_.size(); ++i)
      uniform_values_[i].type = uniforms_[i].type;
  }

  vertex_globals_.assign(vertex->global_slots, Value());
  fragment_globals_.assign(fragment->global_slots, Value());
  for (int i = 0; i < vertex->global_slots; ++i)
    vertex_globals_[i].type = Type::Float();
  for (int i = 0; i < fragment->global_slots; ++i)
    fragment_globals_[i].type = Type::Float();
  for (const Variable& global : vertex->globals) {
    if (global.index >= 0 &&
        global.index < static_cast<int>(vertex_globals_.size()))
      vertex_globals_[global.index].type = global.type;
  }
  for (const Variable& global : fragment->globals) {
    if (global.index >= 0 &&
        global.index < static_cast<int>(fragment_globals_.size())) {
      fragment_globals_[global.index].type = global.type;
    }
  }

  std::string init_error;
  if (!EvaluateGlobalInitializers(vertex, &vertex_globals_, nullptr, nullptr,
                                  &init_error) ||
      !EvaluateGlobalInitializers(fragment, &fragment_globals_, nullptr,
                                  nullptr, &init_error)) {
    return fail("global initialiser failed: " + init_error);
  }

  uses_frag_depth_ = fragment->uses_frag_depth;
  uses_discard_ = fragment->uses_discard;
  uses_point_size_ = vertex->uses_point_size;
  linked_ = true;

#if defined(ZGL_ENABLE_JIT)
  // The JIT is an optimisation, not part of link validation.  A construct the
  // native backend cannot lower only means that stage keeps the evaluator.
  vertex_jit_ = ShaderJit::Compile(*vertex, nullptr);
  fragment_jit_ = ShaderJit::Compile(*fragment, nullptr);
#endif

  if (log)
    log->clear();
  return true;
}

int Program::AttributeLocation(const std::string& name) const {
  for (const AttributeInfo& info : attributes_) {
    if (info.name == name)
      return info.location;
  }
  return -1;
}

int Program::UniformLocation(const std::string& name) const {
  for (const UniformInfo& info : uniforms_) {
    if (info.name == name)
      return info.location;
  }
  return -1;
}

}  // namespace glsl
}  // namespace zgl
