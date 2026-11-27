// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/glsl/glsl_program.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cstdio>
#include <cstdlib>

#include <algorithm>
#include <functional>
#include <string>
#include <utility>

#include "src/glsl/glsl_jit.h"

namespace glanceasy {
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

  // Attributes.  Explicit locations and glBindAttribLocation() are fixed;
  // remaining attributes take the lowest free slots in declaration order.
  {
    std::vector<bool> occupied(kMaxVertexAttribs, false);
    std::vector<int> locations(vertex->attributes.size(), -1);
    for (size_t i = 0; i < vertex->attributes.size(); ++i) {
      const Variable& attribute = vertex->attributes[i];
      int location = attribute.explicit_location ? attribute.index : -1;
      if (location < 0) {
        // The most recent glBindAttribLocation() call wins.
        for (auto it = bindings.rbegin(); it != bindings.rend(); ++it) {
          if (it->first == attribute.name) {
            location = it->second;
            break;
          }
        }
      }
      if (location < 0)
        continue;
      const int slots = attribute.type.is_matrix() ? attribute.type.columns()
                       : attribute.type.is_array() ? attribute.type.array_size()
                                                   : 1;
      if (location + slots > kMaxVertexAttribs)
        return fail("attribute '" + attribute.name +
                    "' has an invalid location");
      locations[i] = location;
      for (int slot = 0; slot < slots; ++slot)
        occupied[location + slot] = true;
    }
    for (size_t i = 0; i < vertex->attributes.size(); ++i) {
      if (locations[i] >= 0)
        continue;
      const Variable& attribute = vertex->attributes[i];
      const int slots = attribute.type.is_matrix() ? attribute.type.columns()
                       : attribute.type.is_array() ? attribute.type.array_size()
                                                   : 1;
      int location = 0;
      for (; location + slots <= kMaxVertexAttribs; ++location) {
        bool free = true;
        for (int slot = 0; slot < slots; ++slot) {
          if (occupied[location + slot]) {
            free = false;
            break;
          }
        }
        if (free)
          break;
      }
      if (location + slots > kMaxVertexAttribs)
        return fail("too many vertex attributes");
      locations[i] = location;
      for (int slot = 0; slot < slots; ++slot)
        occupied[location + slot] = true;
    }
    for (size_t i = 0; i < vertex->attributes.size(); ++i) {
      AttributeInfo info;
      info.name = vertex->attributes[i].name;
      info.type = vertex->attributes[i].type;
      info.location = locations[i];
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
      info.flat = varying.flat;
      varying.varying_offset = info.offset;
      varying_floats_ += info.components;
      varyings_.push_back(info);
    }
    for (Variable& input : fragment->varyings) {
      // A fragment input that is declared but never statically used does not
      // have to be produced by the vertex stage.
      if (!input.used)
        continue;
      int offset = -1;
      for (const VaryingInfo& info : varyings_) {
        if (info.name == input.name) {
          if (TypeMismatch(info.type, input.type)) {
            return fail("varying '" + input.name +
                        "' has a different type in the two stages");
          }
          if (info.flat != input.flat) {
            return fail("varying '" + input.name +
                        "' has mismatched interpolation qualifiers");
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
      int element = -1;
      std::string base_name = name;
      for (const VaryingInfo& varying : varyings_) {
        if (varying.name == name) {
          found = &varying;
          break;
        }
      }
      // GLES permits selecting a single element of an array output, for
      // example "v_color[1]".  Resolve the suffix to the declared array so
      // the capture fetches only that element.
      if (!found) {
        const size_t bracket = name.rfind('[');
        if (bracket != std::string::npos && !name.empty() &&
            name.back() == ']') {
          base_name = name.substr(0, bracket);
          const std::string index_text =
              name.substr(bracket + 1, name.size() - bracket - 2);
          if (!index_text.empty() &&
              index_text.find_first_not_of("0123456789") == std::string::npos) {
            element = std::atoi(index_text.c_str());
            for (const VaryingInfo& varying : varyings_) {
              if (varying.name == base_name) {
                found = &varying;
                break;
              }
            }
          }
        }
      }
      if (!found && name == "gl_Position") {
        TransformFeedbackVaryingInfo position;
        position.name = name;
        position.type = Type::Vec(4);
        position.offset = -3;  // Captured from the built-in position output.
        position.components = 4;
        position.buffer_index =
            transform_feedback_separate ? static_cast<int>(i) : 0;
        transform_feedback_varyings_.push_back(position);
        if (!transform_feedback_separate)
          interleaved_components += 4;
        continue;
      }
      if (!found && name == "gl_PointSize") {
        TransformFeedbackVaryingInfo point_size;
        point_size.name = name;
        point_size.type = Type::Float();
        point_size.offset = -2;  // Captured from the built-in point size.
        point_size.components = 1;
        point_size.buffer_index =
            transform_feedback_separate ? static_cast<int>(i) : 0;
        transform_feedback_varyings_.push_back(point_size);
        if (!transform_feedback_separate)
          interleaved_components += 1;
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
      const Type element_type =
          element >= 0 && found->type.is_array() ? found->type.array_element()
                                                 : found->type;
      if (element >= 0 &&
          (!found->type.is_array() || element >= found->type.array_size()))
        return fail("transform feedback varying '" + name +
                    "' has an invalid array index");
      TransformFeedbackVaryingInfo info;
      info.name = name;
      info.type = element_type;
      info.offset =
          found->offset +
          (element >= 0 ? element * element_type.components() : 0);
      info.components = element >= 0 ? element_type.components()
                                     : found->components;
      info.buffer_index =
          transform_feedback_separate ? static_cast<int>(i) : 0;
      if (transform_feedback_separate) {
        if (info.components > 64)
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
      info.element = output.element;
      info.components = output.components;
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
      info.components = 4;
      info.base = BaseType::kFloat;
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
        const int instances = source.instance_array_size > 0
                                  ? source.instance_array_size
                                  : 1;
        bool source_used = false;
        for (const UniformBlockMember& member : source.members) {
          if (!member.instance_uniform_indices.empty()) {
            for (int u : member.instance_uniform_indices)
              if (u >= 0 && u < static_cast<int>(ast->uniforms.size()) &&
                  ast->uniforms[u].used) {
                source_used = true;
                break;
              }
          } else if (member.uniform_index >= 0 &&
                     member.uniform_index <
                         static_cast<int>(ast->uniforms.size()) &&
                     ast->uniforms[member.uniform_index].used) {
            source_used = true;
          }
          if (source_used)
            break;
        }
        int first_index = -1;
        for (int instance = 0; instance < instances; ++instance) {
          const std::string block_name =
              instances > 1 ? source.name + "[" + std::to_string(instance) + "]"
                            : source.name;
          int index = -1;
          for (size_t j = 0; j < uniform_blocks_.size(); ++j) {
            if (uniform_blocks_[j].name == block_name) {
              index = static_cast<int>(j);
              break;
            }
          }
          if (index < 0) {
            UniformBlockInfo info;
            info.name = block_name;
            info.size = source.size;
            info.matrix_layout = source.matrix_layout;
            info.std140 = source.std140;
            info.packed = source.packed;
            info.instance_array = source.instance_array && instances <= 1;
            info.instance_name = source.instance_name;
            info.binding = 0;
            info.vertex_stage = is_vertex;
            info.fragment_stage = !is_vertex;
            info.used = source_used;
            info.has_instance = !source.instance_name.empty();
            for (const UniformBlockMember& member : source.members) {
              UniformBlockMemberInfo member_info;
              member_info.name = member.name;
              member_info.type = member.type;
              member_info.offset = member.offset;
              member_info.matrix_layout = member.matrix_layout;
              if (instance <
                  static_cast<int>(member.instance_uniform_indices.size()))
                member_info.uniform_index =
                    member.instance_uniform_indices[instance];
              else
                member_info.uniform_index = member.uniform_index;
              info.members.push_back(member_info);
            }
            uniform_blocks_.push_back(std::move(info));
            index = static_cast<int>(uniform_blocks_.size()) - 1;
          } else {
            const UniformBlockInfo& existing = uniform_blocks_[index];
            if (existing.size != source.size ||
                existing.matrix_layout != source.matrix_layout ||
                existing.members.size() != source.members.size()) {
              return false;
            }
            for (size_t m = 0; m < source.members.size(); ++m) {
              if (existing.members[m].name != source.members[m].name ||
                  existing.members[m].type != source.members[m].type ||
                  existing.members[m].offset != source.members[m].offset ||
                  existing.members[m].matrix_layout !=
                      source.members[m].matrix_layout) {
                return false;
              }
            }
            if (is_vertex)
              uniform_blocks_[index].vertex_stage = true;
            else
              uniform_blocks_[index].fragment_stage = true;
            uniform_blocks_[index].used =
                uniform_blocks_[index].used || source_used;
            uniform_blocks_[index].has_instance =
                uniform_blocks_[index].has_instance ||
                !source.instance_name.empty();
          }
          if (instance == 0)
            first_index = index;
        }
        (*map)[i] = first_index;
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
        const int block_base =
            uniform.ubo_block >= 0 &&
                    uniform.ubo_block < static_cast<int>(blocks.size())
                ? blocks[uniform.ubo_block]
                : -1;
        const int block_index =
            block_base >= 0 ? block_base + uniform.ubo_instance : -1;
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

    // Struct members are addressable by their dotted GLSL name even though
    // they live inside the parent uniform's flattened value.  Generate a
    // stable location for every reachable member so glGetUniformLocation()
    // and glUniform*() can target individual fields.
    std::function<void(const std::string&, const Type&, int, int)>
        add_struct_fields;
    add_struct_fields = [&](const std::string& prefix, const Type& type,
                            int parent, int base_offset) {
      if (type.is_array()) {
        const Type& element = type.array_element();
        const int stride = element.components();
        for (int i = 0; i < type.array_size(); ++i) {
          const std::string element_name =
              prefix + "[" + std::to_string(i) + "]";
          if (element.is_struct() || element.is_array()) {
            add_struct_fields(element_name, element, parent,
                              base_offset + i * stride);
            continue;
          }
          UniformFieldInfo field;
          field.name = element_name;
          field.type = element;
          field.parent = parent;
          field.component_offset = base_offset + i * stride;
          field.location = static_cast<int>(uniforms_.size() +
                                            uniform_fields_.size());
          uniform_fields_.push_back(field);
          if (GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_UNIFLOC"))
            fprintf(stderr, "[field] '%s' type=%s loc=%d\n",
                    field.name.c_str(), element.Name().c_str(),
                    field.location);
        }
        return;
      }
      if (!type.is_struct() || !type.struct_info())
        return;
      for (const StructMember& member : type.struct_info()->members) {
        const std::string field_name = prefix + "." + member.name;
        UniformFieldInfo field;
        field.name = field_name;
        field.type = member.type;
        field.parent = parent;
        field.component_offset = base_offset + member.component_offset;
        field.location = static_cast<int>(uniforms_.size() +
                                          uniform_fields_.size());
        uniform_fields_.push_back(field);
        if (GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_UNIFLOC"))
          fprintf(stderr, "[field] '%s' type=%s loc=%d\n", field_name.c_str(),
                  member.type.Name().c_str(), field.location);
        add_struct_fields(field_name, member.type, parent,
                          field.component_offset);
      }
    };
    for (size_t i = 0; i < uniforms_.size(); ++i) {
      if (uniforms_[i].type.is_struct() ||
          (uniforms_[i].type.is_array() &&
           uniforms_[i].type.array_element().is_struct())) {
        add_struct_fields(uniforms_[i].name, uniforms_[i].type,
                          static_cast<int>(i), 0);
      }
    }
  }

  for (size_t b = 0; b < uniform_blocks_.size(); ++b) {
    for (UniformBlockMemberInfo& member : uniform_blocks_[b].members) {
      for (size_t u = 0; u < uniforms_.size(); ++u) {
        if (uniforms_[u].block_index == static_cast<int>(b) &&
            (uniforms_[u].name == member.name ||
             (uniforms_[u].name.size() > member.name.size() + 1 &&
              uniforms_[u].name.compare(uniforms_[u].name.size() -
                                            member.name.size() - 1,
                                        member.name.size() + 1,
                                        "." + member.name) == 0))) {
          member.uniform_index = static_cast<int>(u);
          break;
        }
      }
      if (member.uniform_index < 0)
        return fail("uniform block member was not merged");
    }
  }

  // Expose nested structure members of uniform blocks as active uniforms.
  // GL reports block members of structure type one leaf at a time, so build a
  // query-only uniform entry for each leaf while the runtime keeps the
  // top-level aggregate value used by the shader.
  for (size_t b = 0; b < uniform_blocks_.size(); ++b) {
    UniformBlockInfo& block = uniform_blocks_[b];
    std::string base_name = block.name;
    const size_t bracket = base_name.find('[');
    if (bracket != std::string::npos)
      base_name.resize(bracket);

    std::function<void(const std::string&, const Type&, int)> flatten;
    flatten = [&](const std::string& name, const Type& type, int offset) {
      if (type.is_array()) {
        const Type& element = type.array_element();
        if (element.is_struct()) {
          const int stride = Std140ArrayStride(element, block.matrix_layout);
          for (int i = 0; i < type.array_size(); ++i)
            flatten(name + "[" + std::to_string(i) + "]", element,
                    offset + i * stride);
          return;
        }
        UniformInfo info;
        info.name = name;
        info.type = type;
        info.block_index = static_cast<int>(b);
        info.block_offset = offset;
        block.active_uniform_indices.push_back(
            static_cast<int>(uniforms_.size()));
        uniforms_.push_back(std::move(info));
        return;
      }
      if (type.is_struct()) {
        const StructInfo* info = type.struct_info();
        if (!info)
          return;
        for (int mi = 0; mi < static_cast<int>(info->members.size()); ++mi) {
          const StructMember& member = info->members[mi];
          flatten(name + "." + member.name, member.type,
                  offset + Std140StructMemberOffset(*info, mi,
                                                     block.matrix_layout));
        }
        return;
      }
      UniformInfo leaf;
      leaf.name = name;
      leaf.type = type;
      leaf.block_index = static_cast<int>(b);
      leaf.block_offset = offset;
      block.active_uniform_indices.push_back(static_cast<int>(uniforms_.size()));
      uniforms_.push_back(std::move(leaf));
    };

    for (const UniformBlockMemberInfo& member : block.members) {
      if (member.type.is_struct() ||
          (member.type.is_array() &&
           member.type.array_element().is_struct())) {
        const std::string member_prefix =
            block.has_instance ? base_name + "." + member.name : member.name;
        flatten(member_prefix, member.type, member.offset);
      } else {
        block.active_uniform_indices.push_back(member.uniform_index);
      }
    }
  }

  uniform_values_.resize(uniforms_.size());
  for (size_t i = 0; i < uniforms_.size(); ++i)
    uniform_values_[i].ResetType(uniforms_[i].type);
  BuildActiveUniforms();

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

  // Fragment output arrays use one builtin slot for all elements.  Restore
  // the aggregate type so element writes and reads address the full array.
  std::vector<int> output_elements(fragment_globals_.size(), 1);
  for (const FragmentOut& output : fragment->outputs) {
    if (output.slot >= 0 &&
        output.slot < static_cast<int>(output_elements.size()) &&
        output.element + 1 > output_elements[output.slot]) {
      output_elements[output.slot] = output.element + 1;
    }
  }
  for (const FragmentOut& output : fragment->outputs) {
    if (output.slot < 0 ||
        output.slot >= static_cast<int>(fragment_globals_.size()))
      continue;
    const int count = output_elements[output.slot];
    if (count > 1) {
      Type element(output.base, output.components, 1);
      fragment_globals_[output.slot].ResetType(Type::Array(element, count));
    }
  }

  std::string init_error;
  if (!EvaluateGlobalInitializers(vertex, &vertex_globals_, nullptr, nullptr,
                                  &init_error) ||
      !EvaluateGlobalInitializers(fragment, &fragment_globals_, nullptr,
                                  nullptr, &init_error)) {
    return fail("global initialiser failed: " + init_error);
  }

  vertex_global_initials_ = vertex_globals_;
  fragment_global_initials_ = fragment_globals_;

  uses_frag_depth_ = fragment->uses_frag_depth;
  uses_discard_ = fragment->uses_discard;
  uses_point_size_ = vertex->uses_point_size;
  linked_ = true;

#if defined(GLANCEASY_ENABLE_JIT)
  // The JIT is an optimisation, not part of link validation.  A construct the
  // native backend cannot lower only means that stage keeps the evaluator.
  bool has_integer_io = false;
  for (const Variable& varying : vertex->varyings) {
    if (varying.type.base() == BaseType::kInt ||
        varying.type.base() == BaseType::kUInt) {
      has_integer_io = true;
      break;
    }
  }
  for (const Variable& attribute : vertex->attributes) {
    if (attribute.type.base() == BaseType::kInt ||
        attribute.type.base() == BaseType::kUInt) {
      has_integer_io = true;
      break;
    }
  }
  for (const FragmentOut& output : fragment->outputs) {
    if (output.base == BaseType::kInt || output.base == BaseType::kUInt) {
      has_integer_io = true;
      break;
    }
  }
  // Integer stage IO needs exact 32-bit payloads; the scalar JIT models
  // varyings as floats, so leave those programs to the interpreter.
  if (!has_integer_io)
    vertex_jit_ = ShaderJit::Compile(*vertex, nullptr);
  // The scalar JIT does not address fragment output array elements; use the
  // interpreter for shaders that declare them.
  bool has_output_array = false;
  for (const FragmentOut& output : fragment->outputs) {
    if (output.element > 0) {
      has_output_array = true;
      break;
    }
  }
  if (!has_output_array && !has_integer_io) {
    std::string jit_reason;
    fragment_jit_ = ShaderJit::Compile(*fragment, &jit_reason);
    if (!fragment_jit_ && GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_JIT"))
      fprintf(stderr, "[jit] fragment fallback: %s\n", jit_reason.c_str());
  }
#endif

  if (log)
    log->clear();
  return true;
}

void Program::BuildActiveUniforms() {
  active_uniforms_.clear();
  for (size_t i = 0; i < uniforms_.size(); ++i) {
    const UniformInfo& uniform = uniforms_[i];
    // Uniform-block members are already flattened into leaf entries by the
    // std140 layout pass; only basic arrays need collapsing to name[0].
    if (uniform.block_index >= 0) {
      ActiveUniformInfo info;
      info.name = uniform.name;
      info.type = uniform.type;
      info.size = 1;
      if (uniform.type.is_array()) {
        info.name += "[0]";
        info.type = uniform.type.array_element();
        info.size = uniform.type.array_size();
      }
      info.uniform_index = static_cast<int>(i);
      info.block_index = uniform.block_index;
      info.block_offset = uniform.block_offset;
      if (uniform.type.is_array() && uniform.block_index >= 0 &&
          uniform.block_index < static_cast<int>(uniform_blocks_.size())) {
        const auto& block = uniform_blocks_[uniform.block_index];
        info.array_stride =
            Std140ArrayStride(uniform.type.array_element(), block.matrix_layout);
      }
      active_uniforms_.push_back(std::move(info));
      continue;
    }

    std::function<void(const std::string&, const Type&, int)> expand;
    expand = [&](const std::string& name, const Type& type, int parent) {
      if (type.is_array()) {
        const Type& element = type.array_element();
        const int count = type.array_size();
        if (element.is_struct()) {
          for (int element_index = 0; element_index < count; ++element_index) {
            expand(name + "[" + std::to_string(element_index) + "]", element,
                   parent);
          }
        } else {
          ActiveUniformInfo info;
          info.name = name + "[0]";
          info.type = element;
          info.size = count;
          info.uniform_index = parent;
          active_uniforms_.push_back(std::move(info));
        }
        return;
      }
      if (type.is_struct() && type.struct_info()) {
        for (const StructMember& member : type.struct_info()->members)
          expand(name + "." + member.name, member.type, parent);
        return;
      }
      ActiveUniformInfo info;
      info.name = name;
      info.type = type;
      info.size = 1;
      info.uniform_index = parent;
      active_uniforms_.push_back(std::move(info));
    };
    expand(uniform.name, uniform.type, static_cast<int>(i));
  }
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
  for (const UniformFieldInfo& field : uniform_fields_) {
    if (field.name == name) {
      // "array[i]" addresses an element of an array field.  Element zero is
      // the array itself; later elements encode the base field plus index so
      // glUniform*v can still write a contiguous run starting at that element.
      const size_t open = name.rfind('[');
      if (open != std::string::npos && name.back() == ']') {
        const std::string index_text =
            name.substr(open + 1, name.size() - open - 2);
        if (!index_text.empty() &&
            index_text.find_first_not_of("0123456789") == std::string::npos) {
          const int element = std::atoi(index_text.c_str());
          const std::string base_name = name.substr(0, open);
          for (const UniformFieldInfo& base : uniform_fields_) {
            if (base.name == base_name && base.type.is_array()) {
              if (element >= 0 && element < base.type.array_size()) {
                if (element == 0)
                  return base.location;
                return 0x20000000 + base.location * 256 + element;
              }
              return -1;
            }
          }
        }
      }
      if (GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_UNIFLOC"))
        fprintf(stderr, "[unifloc] '%s' -> %d (parent=%d off=%d)\n",
                name.c_str(), field.location, field.parent,
                field.component_offset);
      return field.location;
    }
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
          return 0x10000000 + info.location * 256 + element;
        }
      }
      for (const UniformFieldInfo& field : uniform_fields_) {
        if (field.name == base && field.type.is_array() &&
            element < field.type.array_size()) {
          if (element == 0)
            return field.location;
          return 0x10000000 + field.location * 256 + element;
        }
      }
    }
  }
  if (GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_UNIFLOC")) {
    fprintf(stderr, "[unifloc] lookup '%s' failed; fields=%zu\n", name.c_str(),
            uniform_fields_.size());
    for (const UniformFieldInfo& field : uniform_fields_)
      fprintf(stderr, "  field '%s' loc=%d parent=%d off=%d\n",
              field.name.c_str(), field.location, field.parent,
              field.component_offset);
  }
  return -1;
}

bool Program::UniformStorage(int location,
                              Value** value,
                              int* component_offset,
                              const Type** type) {
  if (!value || !component_offset || !type)
    return false;
  if (location >= 0x20000000) {
    const int code = location - 0x20000000;
    const int base_location = code / 256;
    const int element_index = code % 256;
    if (!UniformStorage(base_location, value, component_offset, type))
      return false;
    if (!(*type)->is_array() || element_index >= (*type)->array_size())
      return false;
    *component_offset += element_index * (*type)->array_element().components();
    return true;
  }
  if (location >= 0x10000000) {
    const int code = location - 0x10000000;
    const int base_location = code / 256;
    const int element_index = code % 256;
    if (!UniformStorage(base_location, value, component_offset, type))
      return false;
    if (!(*type)->is_array() || element_index >= (*type)->array_size())
      return false;
    *component_offset += element_index * (*type)->array_element().components();
    return true;
    return true;
  }
  if (location >= 0 && location < static_cast<int>(uniform_values_.size())) {
    *value = &uniform_values_[location];
    *component_offset = 0;
    *type = &uniform_values_[location].type;
    return true;
  }
  const int field_index = location - static_cast<int>(uniform_values_.size());
  if (field_index < 0 ||
      field_index >= static_cast<int>(uniform_fields_.size())) {
    return false;
  }
  const UniformFieldInfo& field = uniform_fields_[field_index];
  if (field.parent < 0 ||
      field.parent >= static_cast<int>(uniform_values_.size()))
    return false;
  *value = &uniform_values_[field.parent];
  *component_offset = field.component_offset;
  *type = &field.type;
  return true;
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

  std::function<void(int, const Type&, Value*, int, int)> read_member;
  read_member = [&](int byte_offset, const Type& type, Value* value,
                    int component_offset, int matrix_layout) {
    if (!value)
      return;
    if (type.is_array()) {
      const Type& element = type.array_element();
      int stride = Std140ArrayStride(element, matrix_layout);
      if (matrix_layout != 0 && element.is_matrix())
        stride = ((element.rows() * 16 + 15) / 16) * 16;
      const int element_components = element.components();
      for (int i = 0; i < type.array_size(); ++i) {
        read_member(byte_offset + i * stride, element, value,
                    component_offset + i * element_components, matrix_layout);
      }
      return;
    }
    if (type.is_struct()) {
      const StructInfo* info = type.struct_info();
      if (!info)
        return;
      for (int mi = 0; mi < static_cast<int>(info->members.size()); ++mi) {
        const StructMember& member = info->members[mi];
        read_member(byte_offset + Std140StructMemberOffset(*info, mi,
                                                           matrix_layout),
                    member.type, value, component_offset + member.component_offset,
                    matrix_layout);
      }
      return;
    }
    if (type.is_matrix()) {
      const int rows = type.rows();
      const int columns = type.columns();
      const int matrix_stride =
          block.packed
              ? (matrix_layout != 0 ? columns * 4 : rows * 4)
              : 16;
      for (int c = 0; c < columns; ++c) {
        for (int r = 0; r < rows; ++r) {
          const size_t position =
              static_cast<size_t>(byte_offset) +
              static_cast<size_t>(matrix_layout != 0 ? r : c) *
                  static_cast<size_t>(matrix_stride) +
              static_cast<size_t>(matrix_layout != 0 ? c : r) * 4;
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
        value->IntData()[component_offset + i] =
            type.base() == BaseType::kBool
                ? (integer != 0 ? 1u : 0u)
                : static_cast<uint32_t>(integer);
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
    read_member(member.offset, member.type, value, 0, member.matrix_layout);
    if (GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_UBO") && block.name == "BlockE") {
      fprintf(stderr, "RAW");
      for (int q = 80; q < 352 && q + 4 <= static_cast<int>(byte_count);
           q += 4)
        fprintf(stderr, " %.0f", *reinterpret_cast<const float*>(bytes + q));
      fprintf(stderr, "\n");
    }
    if (GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_UBO")) {
      fprintf(stderr, "UBO %s packed=%d row=%d off=%d %s:", block.name.c_str(),
              block.packed ? 1 : 0, member.matrix_layout, member.offset,
              member.name.c_str());
      for (int k = 0; k < value->size() && k < 64; ++k)
        fprintf(stderr, " %g", value->Data()[k]);
      fprintf(stderr, "\n");
    }
  }
}

}  // namespace glsl
}  // namespace glanceasy
