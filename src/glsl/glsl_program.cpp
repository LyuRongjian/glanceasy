// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/glsl/glsl_program.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <functional>
#include <string>
#include <utility>

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

bool Program::Link(
    ShaderAst* vertex,
    ShaderAst* fragment,
    const std::vector<std::pair<std::string, int>>& bindings,
    const std::vector<std::pair<std::string, int>>& frag_bindings,
    const std::vector<std::string>& transform_feedback_varyings,
    bool transform_feedback_separate,
    std::string* log) {
  linked_ = false;
  attributes_.clear();
  uniforms_.clear();
  uniform_blocks_.clear();
  varyings_.clear();
  transform_feedback_varyings_.clear();
  transform_feedback_separate_ = transform_feedback_separate;
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

  // Transform feedback varyings.  Names must be vertex outputs, and the
  // selected components have to fit the separate or interleaved limits.
  {
    if (transform_feedback_separate &&
        transform_feedback_varyings.size() >
            static_cast<size_t>(kMaxTransformFeedbackBuffers)) {
      return fail("too many separate transform feedback varyings");
    }
    int interleaved_components = 0;
    for (size_t i = 0; i < transform_feedback_varyings.size(); ++i) {
      const std::string& name = transform_feedback_varyings[i];
      const VaryingInfo* found = nullptr;
      for (const VaryingInfo& varying : varyings_) {
        if (varying.name == name) {
          found = &varying;
          break;
        }
      }
      if (!found && name == "gl_Position") {
        TransformFeedbackVaryingInfo position;
        position.name = name;
        position.type = Type::Vec(4);
        position.offset = 0;
        position.components = 4;
        position.buffer_index =
            transform_feedback_separate ? static_cast<int>(i) : 0;
        transform_feedback_varyings_.push_back(position);
        if (!transform_feedback_separate)
          interleaved_components += 4;
        continue;
      }
      if (!found)
        return fail("transform feedback varying '" + name +
                    "' is not a vertex output");
      for (const TransformFeedbackVaryingInfo& existing :
           transform_feedback_varyings_) {
        if (existing.name == name)
          return fail("duplicate transform feedback varying '" + name + "'");
      }
      TransformFeedbackVaryingInfo info;
      info.name = name;
      info.type = found->type;
      info.offset = found->offset;
      info.components = found->components;
      info.buffer_index =
          transform_feedback_separate ? static_cast<int>(i) : 0;
      if (transform_feedback_separate) {
        if (info.components > 4)
          return fail("separate transform feedback varying is too wide");
      } else {
        interleaved_components += info.components;
      }
      transform_feedback_varyings_.push_back(info);
    }
    if (!transform_feedback_separate && interleaved_components > 64)
      return fail("too many interleaved transform feedback components");
  }

  // Fragment colour outputs.  A layout(location) or glBindFragDataLocation()
  // binding selects the draw buffer; the slot is the global the shader writes.
  {
    for (const FragmentOut& output : fragment->outputs) {
      int location = output.location;
      for (const auto& binding : frag_bindings) {
        if (binding.first == output.name)
          location = binding.second;
      }
      if (location < 0 || location >= kMaxFragmentOutputs)
        return fail("fragment output '" + output.name +
                    "' has an invalid location");
      for (const FragmentOutputInfo& existing : fragment_outputs_) {
        if (existing.location == location)
          return fail("two fragment outputs share location " +
                      std::to_string(location));
      }
      FragmentOutputInfo info;
      info.name = output.name;
      info.location = location;
      info.slot = output.slot;
      info.base = output.base;
      fragment_outputs_.push_back(info);
    }
    if (fragment_outputs_.empty()) {
      int slot =
          fragment->builtin_slots[static_cast<int>(BuiltinId::kFragColor)];
      if (slot < 0)
        slot = fragment->builtin_slots[static_cast<int>(BuiltinId::kFragData)];
      FragmentOutputInfo info;
      info.name = "gl_FragColor";
      info.location = 0;
      info.slot = slot;
      fragment_outputs_.push_back(info);
    }
  }

  // Uniform blocks.  The same block name may appear in both stages with the
  // same std140 layout; its members occupy one program-wide uniform slot per
  // member and are refreshed from the bound buffer before each draw.
  std::vector<int> vertex_block_map;
  std::vector<int> fragment_block_map;
  {
    const auto merge_blocks = [&](ShaderAst* ast, bool is_vertex,
                                  std::vector<int>* map) {
      map->assign(ast->uniform_blocks.size(), -1);
      for (size_t i = 0; i < ast->uniform_blocks.size(); ++i) {
        const UniformBlock& source = ast->uniform_blocks[i];
        int index = -1;
        for (size_t j = 0; j < uniform_blocks_.size(); ++j) {
          if (uniform_blocks_[j].name == source.name) {
            index = static_cast<int>(j);
            break;
          }
        }
        if (index < 0) {
          UniformBlockInfo info;
          info.name = source.name;
          info.size = source.size;
          info.binding = 0;
          info.vertex_stage = is_vertex;
          info.fragment_stage = !is_vertex;
          for (const UniformBlockMember& member : source.members) {
            UniformBlockMemberInfo member_info;
            member_info.name = member.name;
            member_info.type = member.type;
            member_info.offset = member.offset;
            info.members.push_back(member_info);
          }
          uniform_blocks_.push_back(std::move(info));
          index = static_cast<int>(uniform_blocks_.size()) - 1;
        } else {
          const UniformBlockInfo& existing = uniform_blocks_[index];
          if (existing.size != source.size ||
              existing.members.size() != source.members.size()) {
            return false;
          }
          for (size_t m = 0; m < source.members.size(); ++m) {
            if (existing.members[m].name != source.members[m].name ||
                existing.members[m].type != source.members[m].type ||
                existing.members[m].offset != source.members[m].offset) {
              return false;
            }
          }
          if (is_vertex)
            uniform_blocks_[index].vertex_stage = true;
          else
            uniform_blocks_[index].fragment_stage = true;
        }
        (*map)[i] = index;
      }
      return true;
    };
    if (!merge_blocks(vertex, true, &vertex_block_map) ||
        !merge_blocks(fragment, false, &fragment_block_map)) {
      return fail("uniform block layouts do not match between the two stages");
    }
  }

  // Uniforms.  Names and types must agree across stages; the merged list is
  // the program-wide uniform namespace that glGetUniformLocation() exposes.
  {
    std::vector<UniformInfo> merged;
    const auto merge_stage = [&](ShaderAst* ast, const std::vector<int>& blocks,
                                 std::vector<int>* map) {
      map->assign(ast->uniforms.size(), -1);
      for (size_t i = 0; i < ast->uniforms.size(); ++i) {
        const Variable& uniform = ast->uniforms[i];
        const int block_index =
            uniform.ubo_block >= 0 &&
                    uniform.ubo_block < static_cast<int>(blocks.size())
                ? blocks[uniform.ubo_block]
                : -1;
        const int block_offset = block_index >= 0 ? uniform.ubo_offset : 0;
        int index = -1;
        for (size_t j = 0; j < merged.size(); ++j) {
          if (merged[j].name == uniform.name &&
              merged[j].block_index == block_index &&
              merged[j].block_offset == block_offset) {
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
          info.block_index = block_index;
          info.block_offset = block_offset;
          merged.push_back(info);
          index = info.location;
        }
        (*map)[i] = index;
      }
      return true;
    };
    if (!merge_stage(vertex, vertex_block_map, &vertex_uniform_map_) ||
        !merge_stage(fragment, fragment_block_map, &fragment_uniform_map_)) {
      return fail("uniform declarations do not match between the two stages");
    }
    uniforms_ = merged;
    uniform_values_.assign(uniforms_.size(), Value());
    for (size_t i = 0; i < uniforms_.size(); ++i)
      uniform_values_[i].ResetType(uniforms_[i].type);
  }

  for (size_t b = 0; b < uniform_blocks_.size(); ++b) {
    for (UniformBlockMemberInfo& member : uniform_blocks_[b].members) {
      for (size_t u = 0; u < uniforms_.size(); ++u) {
        if (uniforms_[u].block_index == static_cast<int>(b) &&
            uniforms_[u].name == member.name) {
          member.uniform_index = static_cast<int>(u);
          break;
        }
      }
      if (member.uniform_index < 0)
        return fail("uniform block member was not merged");
    }
  }

  vertex_globals_.assign(vertex->global_slots, Value());
  fragment_globals_.assign(fragment->global_slots, Value());
  for (int i = 0; i < vertex->global_slots; ++i)
    vertex_globals_[i].ResetType(Type::Float());
  for (int i = 0; i < fragment->global_slots; ++i)
    fragment_globals_[i].ResetType(Type::Float());
  for (const Variable& global : vertex->globals) {
    if (global.index >= 0 &&
        global.index < static_cast<int>(vertex_globals_.size()))
      vertex_globals_[global.index].ResetType(global.type);
  }
  for (const Variable& global : fragment->globals) {
    if (global.index >= 0 &&
        global.index < static_cast<int>(fragment_globals_.size())) {
      fragment_globals_[global.index].ResetType(global.type);
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

int Program::FragDataLocation(const std::string& name) const {
  for (const FragmentOutputInfo& output : fragment_outputs_) {
    if (output.name == name)
      return output.location;
  }
  return -1;
}

int Program::UniformLocation(const std::string& name) const {
  for (const UniformInfo& info : uniforms_) {
    if (info.name == name && info.block_index < 0)
      return info.location;
  }
  // Array element locations are encoded relative to the uniform's storage
  // slot.  Element zero uses the slot itself; later elements use a negative
  // code so that glGetUniform* can return exactly one element.
  const size_t bracket = name.find('[');
  if (bracket != std::string::npos && name.back() == ']') {
    const std::string base = name.substr(0, bracket);
    const std::string index =
        name.substr(bracket + 1, name.size() - bracket - 2);
    int element = -1;
    if (!index.empty() && index.find_first_not_of("0123456789") ==
                              std::string::npos) {
      element = std::stoi(index);
    }
    if (element >= 0) {
      for (const UniformInfo& info : uniforms_) {
        if (info.name == base && info.block_index < 0 &&
            info.type.is_array() && element < info.type.array_size()) {
          if (element == 0)
            return info.location;
          return -(info.location * 256 + element + 1);
        }
      }
    }
  }
  return -1;
}

int Program::UniformBlockIndex(const std::string& name) const {
  for (size_t i = 0; i < uniform_blocks_.size(); ++i) {
    if (uniform_blocks_[i].name == name)
      return static_cast<int>(i);
  }
  return -1;
}

bool Program::SetUniformBlockBinding(int block_index, int binding) {
  if (block_index < 0 ||
      block_index >= static_cast<int>(uniform_blocks_.size()) || binding < 0) {
    return false;
  }
  uniform_blocks_[block_index].binding = binding;
  return true;
}

void Program::UploadUniformBlock(int block_index,
                                 const uint8_t* bytes,
                                 size_t byte_count) {
  if (!bytes || block_index < 0 ||
      block_index >= static_cast<int>(uniform_blocks_.size())) {
    return;
  }
  const UniformBlockInfo& block = uniform_blocks_[block_index];

  std::function<void(int, const Type&, Value*, int)> read_member;
  read_member = [&](int byte_offset, const Type& type, Value* value,
                    int component_offset) {
    if (!value)
      return;
    if (type.is_array()) {
      const Type& element = type.array_element();
      const int stride = Std140ArrayStride(element);
      const int element_components = element.components();
      for (int i = 0; i < type.array_size(); ++i) {
        read_member(byte_offset + i * stride, element, value,
                    component_offset + i * element_components);
      }
      return;
    }
    if (type.is_struct()) {
      const StructInfo* info = type.struct_info();
      if (!info)
        return;
      for (const StructMember& member : info->members) {
        read_member(byte_offset + member.byte_offset, member.type, value,
                    component_offset + member.component_offset);
      }
      return;
    }
    if (type.is_matrix()) {
      const int rows = type.rows();
      const int columns = type.columns();
      for (int c = 0; c < columns; ++c) {
        for (int r = 0; r < rows; ++r) {
          const size_t position =
              static_cast<size_t>(byte_offset) + static_cast<size_t>(c) * 16 +
              static_cast<size_t>(r) * 4;
          const int index = component_offset + c * rows + r;
          float* destination = value->Data() + index;
          if (position + sizeof(float) <= byte_count)
            memcpy(destination, bytes + position, sizeof(float));
          else
            *destination = 0.0f;
        }
      }
      return;
    }
    const int components = type.components();
    for (int i = 0; i < components; ++i) {
      const size_t position =
          static_cast<size_t>(byte_offset) + static_cast<size_t>(i) * 4;
      float* destination = value->Data() + component_offset + i;
      if (position + sizeof(int32_t) > byte_count) {
        *destination = 0.0f;
        continue;
      }
      if (type.base() == BaseType::kFloat) {
        memcpy(destination, bytes + position, sizeof(float));
      } else {
        int32_t integer = 0;
        memcpy(&integer, bytes + position, sizeof(int32_t));
        *destination = type.base() == BaseType::kBool
                           ? (integer != 0 ? 1.0f : 0.0f)
                           : static_cast<float>(integer);
      }
    }
  };

  for (const UniformBlockMemberInfo& member : block.members) {
    Value* value = uniform_value(member.uniform_index);
    if (!value)
      continue;
    read_member(member.offset, member.type, value, 0);
  }
}

}  // namespace glsl
}  // namespace zgl
