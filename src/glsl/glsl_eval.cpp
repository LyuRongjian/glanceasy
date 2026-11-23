// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// AST interpreter for the GLSL ES subset.  The evaluator is intentionally
// straightforward: one Value per variable, component loops instead of SIMD,
// and a hard step budget so that a broken shader cannot stall a frame.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

#include "src/glsl/glsl_builtins.h"
#include "src/glsl/glsl_jit.h"
#include "src/glsl/glsl_program.h"
#include "src/zgl_fastmath.h"
#include "src/zgl_simd.h"

namespace zgl {
namespace glsl {

namespace {

constexpr int kMaxCallDepth = 16;
constexpr int kMaxSteps = 1 << 21;
constexpr float kPi = 3.14159265358979323846f;

float ComponentAt(const Value& value, int index) {
  if (index < 0)
    return 0.0f;
  if (value.type.is_scalar())
    return value.data[0];
  if (index >= value.size())
    return 0.0f;
  return value.Data()[index];
}

// Copies |count| components starting at |offset| into a new value of |type|.
Value Slice(const Value& value, int offset, const Type& type, int count) {
  Value out(type);
  for (int i = 0; i < count && i < out.size(); ++i)
    out.Data()[i] = ComponentAt(value, offset + i);
  return out;
}

// Writes the components of |value| into |out| starting at |offset|.
void WriteSlice(Value* out, int offset, const Value& value, int count) {
  for (int i = 0; i < count && offset + i < out->size(); ++i)
    out->Data()[offset + i] = ComponentAt(value, i);
}

template <typename F>
Value MapScalars(const Value& a, const Type& type, F fn) {
  Value out(type);
  const int count = type.components();
  for (int i = 0; i < count; ++i)
    out.data[i] = fn(ComponentAt(a, i));
  return out;
}

// Vector-width fast-math map.  Four lanes are evaluated at a time where the
// target provides NEON; the scalar tail and the two-component case stay on the
// scalar approximation.
Value MapFastScalars(const Value& a,
                     const Type& type,
                     float (*scalar)(float),
                     void (*vector)(const float*, float*)) {
  Value out(type);
  const int count = type.components();
  if (vector && count >= 3) {
    for (int i = 0; i < count; i += 4) {
      const int lanes = std::min(4, count - i);
      float in[4] = {};
      for (int lane = 0; lane < lanes; ++lane)
        in[lane] = ComponentAt(a, i + lane);
      float result[4];
      vector(in, result);
      for (int lane = 0; lane < lanes; ++lane)
        out.data[i + lane] = result[lane];
    }
    return out;
  }
  for (int i = 0; i < count; ++i)
    out.data[i] = scalar(ComponentAt(a, i));
  return out;
}

template <typename F>
Value MapPairs(const Value& a, const Value& b, const Type& type, F fn) {
  Value out(type);
  const int count = type.components();
  for (int i = 0; i < count; ++i) {
    const float x = ComponentAt(a, i);
    const float y = ComponentAt(b, i);
    out.data[i] = fn(x, y);
  }
  return out;
}

template <typename F>
Value MapTriples(const Value& a,
                 const Value& b,
                 const Value& c,
                 const Type& type,
                 F fn) {
  Value out(type);
  const int count = type.components();
  for (int i = 0; i < count; ++i)
    out.data[i] = fn(ComponentAt(a, i), ComponentAt(b, i), ComponentAt(c, i));
  return out;
}

float DotProduct(const Value& a, const Value& b) {
  const int count = std::max(a.type.components(), b.type.components());
  if (a.type.is_scalar() || b.type.is_scalar()) {
    float sum = 0.0f;
    for (int i = 0; i < count; ++i)
      sum += ComponentAt(a, i) * ComponentAt(b, i);
    return sum;
  }
  return simd::Dot(a.data, b.data, count);
}

float LengthOf(const Value& v) {
  return fastmath::Sqrt(DotProduct(v, v));
}

// Column-major helpers for the matrix built-ins.
void MatToRowMajor(const Value& m, int rows, int columns, float* out) {
  for (int c = 0; c < columns; ++c)
    for (int r = 0; r < rows; ++r)
      out[r * columns + c] = m.data[c * rows + r];
}

Value RowMajorToMat(const float* in, int rows, int columns) {
  Value out(Type::Mat(columns, rows));
  for (int c = 0; c < columns; ++c)
    for (int r = 0; r < rows; ++r)
      out.data[c * rows + r] = in[r * columns + c];
  return out;
}

float Determinant(const Value& m) {
  int n = m.type.rows();
  if (n < 2 || n > 4 || m.type.columns() != n)
    return 0.0f;
  float a[16];
  MatToRowMajor(m, n, n, a);
  float det = 1.0f;
  for (int i = 0; i < n; ++i) {
    int pivot = i;
    for (int r = i + 1; r < n; ++r) {
      if (std::fabs(a[r * n + i]) > std::fabs(a[pivot * n + i]))
        pivot = r;
    }
    if (std::fabs(a[pivot * n + i]) < 1e-12f)
      return 0.0f;
    if (pivot != i) {
      for (int c = 0; c < n; ++c)
        std::swap(a[i * n + c], a[pivot * n + c]);
      det = -det;
    }
    const float diagonal = a[i * n + i];
    det *= diagonal;
    for (int r = i + 1; r < n; ++r) {
      const float factor = a[r * n + i] / diagonal;
      for (int c = i; c < n; ++c)
        a[r * n + c] -= factor * a[i * n + c];
    }
  }
  return det;
}

bool Invert(const Value& m, Value* out) {
  const int n = m.type.rows();
  if (n < 2 || n > 4 || m.type.columns() != n)
    return false;
  float a[16];
  float inv[16] = {0.0f};
  MatToRowMajor(m, n, n, a);
  for (int i = 0; i < n; ++i)
    inv[i * n + i] = 1.0f;
  for (int i = 0; i < n; ++i) {
    int pivot = i;
    for (int r = i + 1; r < n; ++r) {
      if (std::fabs(a[r * n + i]) > std::fabs(a[pivot * n + i]))
        pivot = r;
    }
    if (std::fabs(a[pivot * n + i]) < 1e-12f)
      return false;
    if (pivot != i) {
      for (int c = 0; c < n; ++c) {
        std::swap(a[i * n + c], a[pivot * n + c]);
        std::swap(inv[i * n + c], inv[pivot * n + c]);
      }
    }
    const float diagonal = a[i * n + i];
    for (int c = 0; c < n; ++c) {
      a[i * n + c] /= diagonal;
      inv[i * n + c] /= diagonal;
    }
    for (int r = 0; r < n; ++r) {
      if (r == i)
        continue;
      const float factor = a[r * n + i];
      if (factor == 0.0f)
        continue;
      for (int c = 0; c < n; ++c) {
        a[r * n + c] -= factor * a[i * n + c];
        inv[r * n + c] -= factor * inv[i * n + c];
      }
    }
  }
  *out = RowMajorToMat(inv, n, n);
  return true;
}

}  // namespace

// Evaluates one stage of a linked program.
class Evaluator {
 public:
  Evaluator(const ShaderAst& ast, std::vector<Value>* globals, Program* program)
      : ast_(ast), globals_(globals), program_(program) {}

  void set_uniforms(const Value* uniforms, const int* map) {
    uniforms_ = uniforms;
    uniform_map_ = map;
  }
  void set_attributes(const float* attributes, int vertex_id, int instance_id) {
    attributes_ = attributes;
    vertex_id_ = vertex_id;
    instance_id_ = instance_id;
  }
  void set_varyings(const float* in, float* out) {
    varyings_in_ = in;
    varyings_out_ = out;
  }
  void set_sampler(TextureSampler* sampler) { sampler_ = sampler; }
  void set_fragment_coord(const float coord[4]) {
    fragment_coord_[0] = coord[0];
    fragment_coord_[1] = coord[1];
    fragment_coord_[2] = coord[2];
    fragment_coord_[3] = coord[3];
  }
  void set_front_facing(bool front) { front_facing_ = front; }
  void set_point_coord(const float coord[2]) {
    point_coord_[0] = coord[0];
    point_coord_[1] = coord[1];
  }
  void set_sample_id(int sample_id) { sample_id_ = sample_id; }
  void set_sample_mask(uint32_t mask) { sample_mask_ = mask; }

  bool discarded() const { return discarded_; }
  const std::string& error() const { return error_; }
  bool HasError() const { return !error_.empty(); }

  // Evaluates a single expression in the global scope; used for the initial
  // values of const globals at link time.
  Value EvalExpression(const Expr& expr) { return Eval(expr); }

  // Pure built-in math shared with the SLJIT backend.  The function has no
  // evaluator state, so the JIT can call it for built-ins that are not worth
  // lowering to machine code.
  static Value EvaluateBuiltinMath(const BuiltinFunc function,
                                   const std::vector<Value>& args);

  // Fills the built-in variables that are inputs to the current stage.
  void PrepareBuiltins();

  bool RunMain();

 private:
  void Fail(const std::string& message) {
    if (error_.empty())
      error_ = message;
  }
  bool BudgetExceeded();

  Value Eval(const Expr& expr);
  void Exec(const Statement& statement);
  void ExecBody(const std::vector<StmtPtr>& body);

  Value ReadVariable(const Expr& expr);
  void WriteVariable(const Expr& expr, const Value& value);
  void Assign(const Expr& target, const Value& value);

  Value EvalField(const Expr& expr);
  Value EvalIndex(const Expr& expr);
  Value EvalConstructor(const Expr& expr);
  Value EvalBuiltin(const Expr& expr);
  Value EvalBinary(const Expr& expr);
  Value EvalUnary(const Expr& expr);
  Value EvalAssignment(const Expr& expr);
  Value CallUserFunction(int function_index, const std::vector<Value>& args);
  bool CallFunction(const Function& function,
                    const std::vector<Value>& args,
                    Value* result);

  Value BinaryOp(Op op,
                 const Value& left,
                 const Value& right,
                 const Type& result_type);
  Value Multiply(const Value& left, const Value& right, const Type& type);

  Value BuiltinTexture(const Expr& expr);
  Value BuiltinRelational(BuiltinFunc function, const std::vector<Value>& args);

  void StoreBuiltin(BuiltinId id, const Value& value);
  int BuiltinSlot(BuiltinId id) const;

  const ShaderAst& ast_;
  std::vector<Value>* globals_ = nullptr;
  Program* program_ = nullptr;

  const Value* uniforms_ = nullptr;
  const int* uniform_map_ = nullptr;
  const float* attributes_ = nullptr;
  int vertex_id_ = 0;
  int instance_id_ = 0;
  const float* varyings_in_ = nullptr;
  float* varyings_out_ = nullptr;
  TextureSampler* sampler_ = nullptr;
  float fragment_coord_[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  int sample_id_ = 0;
  uint32_t sample_mask_ = 0xFFFFFFFFu;
  bool front_facing_ = true;
  float point_coord_[2] = {0.0f, 0.0f};

  std::vector<Value> locals_;
  Value return_value_;
  bool returned_ = false;
  bool broke_ = false;
  bool continued_ = false;
  bool discarded_ = false;
  int call_depth_ = 0;
  // Offset of the current function frame inside |locals_|.  Nested calls push
  // their parameters and locals above the caller, so every slot reference is
  // relative to this base.
  int local_base_ = 0;
  int steps_ = 0;
  std::string error_;
};

bool Evaluator::BudgetExceeded() {
  if (steps_++ < kMaxSteps)
    return false;
  Fail("shader exceeded the step budget (possible infinite loop)");
  return true;
}

int Evaluator::BuiltinSlot(BuiltinId id) const {
  const int index = static_cast<int>(id);
  if (index < 0 || index >= static_cast<int>(ast_.builtin_slots.size()))
    return -1;
  return ast_.builtin_slots[index];
}

void Evaluator::StoreBuiltin(BuiltinId id, const Value& value) {
  const int slot = BuiltinSlot(id);
  if (slot < 0 || !globals_ || slot >= static_cast<int>(globals_->size()))
    return;
  (*globals_)[slot] = value;
}

void Evaluator::PrepareBuiltins() {
  if (ast_.stage == Stage::kVertex) {
    StoreBuiltin(BuiltinId::kVertexId, MakeInt(vertex_id_));
    StoreBuiltin(BuiltinId::kInstanceId, MakeInt(instance_id_));
    return;
  }
  Value coord(Type::Vec(4));
  for (int i = 0; i < 4; ++i)
    coord.data[i] = fragment_coord_[i];
  StoreBuiltin(BuiltinId::kFragCoord, coord);
  StoreBuiltin(BuiltinId::kFrontFacing, MakeBool(front_facing_));
  Value point(Type::Vec(2));
  point.data[0] = point_coord_[0];
  point.data[1] = point_coord_[1];
  StoreBuiltin(BuiltinId::kPointCoord, point);
  StoreBuiltin(BuiltinId::kSampleId, MakeInt(sample_id_));
  StoreBuiltin(BuiltinId::kSampleMaskIn, MakeInt(static_cast<int>(sample_mask_)));
}

bool Evaluator::RunMain() {
  const Function* main = ast_.main();
  if (!main) {
    Fail("missing main()");
    return false;
  }
  PrepareBuiltins();
  Value unused;
  if (!CallFunction(*main, std::vector<Value>(), &unused))
    return false;
  return error_.empty();
}

bool Evaluator::CallFunction(const Function& function,
                             const std::vector<Value>& args,
                             Value* result) {
  if (call_depth_ >= kMaxCallDepth) {
    Fail("call depth exceeded in '" + function.name + "'");
    return false;
  }
  if (args.size() != function.parameters.size()) {
    Fail("wrong argument count for '" + function.name + "'");
    return false;
  }
  const size_t base = locals_.size();
  const int saved_local_base = local_base_;
  const size_t needed =
      base + static_cast<size_t>(std::max(0, function.slot_count));
  if (needed > 4096) {
    Fail("too many local variables in '" + function.name + "'");
    return false;
  }
  locals_.resize(needed);
  for (size_t i = base; i < locals_.size(); ++i)
    locals_[i] = Value();
  for (size_t i = 0; i < args.size(); ++i) {
    locals_[base + i] = Convert(args[i], function.parameters[i].type);
  }

  const bool saved_returned = returned_;
  const bool saved_broke = broke_;
  const bool saved_continued = continued_;
  returned_ = false;
  broke_ = false;
  continued_ = false;
  ++call_depth_;
  local_base_ = static_cast<int>(base);
  return_value_ = Value();

  ExecBody(function.body);

  const Value returned_value = return_value_;
  returned_ = saved_returned;
  broke_ = saved_broke;
  continued_ = saved_continued;
  --call_depth_;
  local_base_ = saved_local_base;
  locals_.resize(base);
  if (result)
    *result = returned_value;
  return error_.empty();
}

void Evaluator::ExecBody(const std::vector<StmtPtr>& body) {
  for (const StmtPtr& statement : body) {
    Exec(*statement);
    if (returned_ || broke_ || continued_ || discarded_ || HasError())
      break;
  }
}

void Evaluator::Exec(const Statement& statement) {
  if (HasError() || returned_ || broke_ || continued_ || discarded_)
    return;
  if (BudgetExceeded())
    return;

  switch (statement.kind) {
    case Statement::Kind::kBlock: {
      for (const StmtPtr& child : statement.statements) {
        Exec(*child);
        if (returned_ || broke_ || continued_ || discarded_ || HasError())
          return;
      }
      return;
    }
    case Statement::Kind::kDeclaration: {
      for (const auto& declarator : statement.declarators) {
        Value value(declarator.variable.type);
        if (declarator.initializer) {
          value =
              Convert(Eval(*declarator.initializer), declarator.variable.type);
        }
        Expr variable;
        variable.kind = Expr::Kind::kVariable;
        variable.storage = Storage::kLocal;
        variable.index = declarator.variable.index;
        variable.type = declarator.variable.type;
        WriteVariable(variable, value);
      }
      return;
    }
    case Statement::Kind::kExpression:
      if (statement.expression)
        Eval(*statement.expression);
      return;
    case Statement::Kind::kIf: {
      Value condition = statement.condition && statement.condition->expression
                            ? Eval(*statement.condition->expression)
                            : MakeBool(false);
      if (IsTrue(condition)) {
        if (statement.then_branch)
          Exec(*statement.then_branch);
      } else if (statement.else_branch) {
        Exec(*statement.else_branch);
      }
      return;
    }
    case Statement::Kind::kFor: {
      if (statement.init)
        Exec(*statement.init);
      for (;;) {
        if (returned_ || discarded_ || HasError())
          return;
        if (statement.condition) {
          if (!IsTrue(Eval(*statement.condition->expression)))
            return;
        }
        if (BudgetExceeded())
          return;
        if (statement.then_branch)
          Exec(*statement.then_branch);
        if (returned_ || discarded_ || HasError())
          return;
        if (broke_) {
          broke_ = false;
          return;
        }
        if (continued_)
          continued_ = false;
        if (statement.step)
          Eval(*statement.step->expression);
      }
    }
    case Statement::Kind::kWhile: {
      for (;;) {
        if (returned_ || discarded_ || HasError())
          return;
        if (!IsTrue(Eval(*statement.condition->expression)))
          return;
        if (BudgetExceeded())
          return;
        if (statement.then_branch)
          Exec(*statement.then_branch);
        if (returned_ || discarded_ || HasError())
          return;
        if (broke_) {
          broke_ = false;
          return;
        }
        if (continued_)
          continued_ = false;
      }
    }
    case Statement::Kind::kDoWhile: {
      for (;;) {
        if (statement.then_branch)
          Exec(*statement.then_branch);
        if (returned_ || discarded_ || HasError())
          return;
        if (broke_) {
          broke_ = false;
          return;
        }
        if (continued_)
          continued_ = false;
        if (!IsTrue(Eval(*statement.condition->expression)))
          return;
      }
    }
    case Statement::Kind::kReturn:
      if (statement.expression)
        return_value_ = Eval(*statement.expression);
      returned_ = true;
      return;
    case Statement::Kind::kBreak:
      broke_ = true;
      return;
    case Statement::Kind::kContinue:
      continued_ = true;
      return;
    case Statement::Kind::kDiscard:
      discarded_ = true;
      returned_ = true;
      return;
  }
}

Value Evaluator::ReadVariable(const Expr& expr) {
  switch (expr.storage) {
    case Storage::kLocal:
      if (expr.index >= 0 && expr.index < static_cast<int>(locals_.size()))
        return Convert(locals_[local_base_ + expr.index], expr.type);
      return Value(expr.type);
    case Storage::kGlobal:
      if (globals_ && expr.index >= 0 &&
          expr.index < static_cast<int>(globals_->size()))
        return Convert((*globals_)[expr.index], expr.type);
      return Value(expr.type);
    case Storage::kUniform: {
      const int program_index = uniform_map_ ? uniform_map_[expr.index] : -1;
      const Value* value =
          program_ ? program_->uniform_value(program_index) : nullptr;
      return value ? Convert(*value, expr.type) : Value(expr.type);
    }
    case Storage::kAttribute: {
      Value value(expr.type);
      if (attributes_) {
        if (expr.type.is_matrix()) {
          const int rows = expr.type.rows();
          for (int c = 0; c < expr.type.columns(); ++c) {
            for (int r = 0; r < rows; ++r) {
              value.data[c * rows + r] =
                  attributes_[(expr.index + c) * 4 + r];
            }
          }
        } else {
          for (int i = 0; i < std::min(4, expr.type.components()); ++i)
            value.data[i] = attributes_[expr.index * 4 + i];
        }
      }
      return value;
    }
    case Storage::kVaryingIn: {
      Value value(expr.type);
      const int offset =
          expr.index >= 0 && expr.index < static_cast<int>(ast_.varyings.size())
              ? ast_.varyings[expr.index].varying_offset
              : -1;
      if (varyings_in_ && offset >= 0) {
        for (int i = 0; i < std::min(4, expr.type.components()); ++i)
          value.data[i] = varyings_in_[offset + i];
      }
      return value;
    }
    case Storage::kVaryingOut: {
      Value value(expr.type);
      const int offset =
          expr.index >= 0 && expr.index < static_cast<int>(ast_.varyings.size())
              ? ast_.varyings[expr.index].varying_offset
              : -1;
      if (varyings_out_ && offset >= 0) {
        for (int i = 0; i < std::min(4, expr.type.components()); ++i)
          value.data[i] = varyings_out_[offset + i];
      }
      return value;
    }
    case Storage::kBuiltin:
      break;
  }
  return Value(expr.type);
}

void Evaluator::WriteVariable(const Expr& expr, const Value& value) {
  const Value converted = Convert(value, expr.type);
  switch (expr.storage) {
    case Storage::kLocal:
      if (expr.index >= 0 && expr.index < static_cast<int>(locals_.size()))
        locals_[local_base_ + expr.index] = converted;
      return;
    case Storage::kGlobal:
      if (globals_ && expr.index >= 0 &&
          expr.index < static_cast<int>(globals_->size()))
        (*globals_)[expr.index] = converted;
      return;
    case Storage::kVaryingOut: {
      const int offset =
          expr.index >= 0 && expr.index < static_cast<int>(ast_.varyings.size())
              ? ast_.varyings[expr.index].varying_offset
              : -1;
      if (varyings_out_ && offset >= 0) {
        for (int i = 0; i < std::min(4, expr.type.components()); ++i)
          varyings_out_[offset + i] = converted.data[i];
      }
      return;
    }
    default:
      Fail("assignment to a read-only variable");
      return;
  }
}

void Evaluator::Assign(const Expr& target, const Value& value) {
  if (target.kind == Expr::Kind::kVariable) {
    WriteVariable(target, value);
    return;
  }
  if (target.kind == Expr::Kind::kField) {
    Value base = Eval(*target.base);
    if (base.type.is_struct()) {
      const Type& field_type = target.type;
      const int count = field_type.components();
      const Value converted = Convert(value, field_type);
      WriteSlice(&base, target.field_offset, converted, count);
      Assign(*target.base, base);
      return;
    }
    const int length = static_cast<int>(target.text.size());
    const char* sets[] = {"xyzw", "rgba", "stpq"};
    const std::string& field = target.text;
    for (int i = 0; i < length; ++i) {
      int component = 0;
      for (const char* set : sets) {
        const char* found = strchr(set, field[i]);
        if (found) {
          component = static_cast<int>(found - set);
          break;
        }
      }
      if (component < kValueFloats) {
        const float source =
            value.type.is_scalar() ? value.data[0] : ComponentAt(value, i);
        base.data[component] =
            Convert(MakeFloat(source), base.type.Element()).data[0];
      }
    }
    Assign(*target.base, base);
    return;
  }
  if (target.kind == Expr::Kind::kIndex) {
    Value base = Eval(*target.base);
    const Value index_value = Eval(*target.args[0]);
    const int index = static_cast<int>(ComponentAt(index_value, 0));
    if (base.type.is_array()) {
      if (index < 0 || index >= base.type.array_size())
        return;
      const Type& element_type = base.type.array_element();
      const int count = element_type.components();
      const Value converted = Convert(value, element_type);
      WriteSlice(&base, index * count, converted, count);
      Assign(*target.base, base);
      return;
    }
    if (base.type.is_matrix()) {
      if (index < 0 || index >= base.type.columns())
        return;
      const int rows = base.type.rows();
      for (int r = 0; r < rows; ++r)
        base.data[index * rows + r] = ComponentAt(value, r);
    } else {
      if (index < 0 || index >= base.type.components())
        return;
      base.data[index] = ComponentAt(value, 0);
    }
    Assign(*target.base, base);
    return;
  }
  Fail("expression is not assignable");
}

Value Evaluator::EvalField(const Expr& expr) {
  const Value base = Eval(*expr.base);
  if (base.type.is_struct()) {
    return Slice(base, expr.field_offset, expr.type, expr.type.components());
  }
  const std::string& field = expr.text;
  const int length = static_cast<int>(field.size());
  const char* sets[] = {"xyzw", "rgba", "stpq"};
  Value out(expr.type);
  for (int i = 0; i < length; ++i) {
    int component = 0;
    for (const char* set : sets) {
      const char* found = strchr(set, field[i]);
      if (found) {
        component = static_cast<int>(found - set);
        break;
      }
    }
    out.data[i] = component < kValueFloats ? base.data[component] : 0.0f;
  }
  return out;
}

Value Evaluator::EvalIndex(const Expr& expr) {
  const Value base = Eval(*expr.base);
  const Value index_value = Eval(*expr.args[0]);
  const int index = static_cast<int>(ComponentAt(index_value, 0));
  if (base.type.is_array()) {
    if (index < 0 || index >= base.type.array_size())
      return Value(expr.type);
    const Type& element_type = base.type.array_element();
    return Slice(base, index * element_type.components(), element_type,
                 element_type.components());
  }
  if (base.type.is_matrix()) {
    Value column(expr.type);
    if (index >= 0 && index < base.type.columns()) {
      const int rows = base.type.rows();
      for (int r = 0; r < rows && r < kValueFloats; ++r)
        column.data[r] = base.data[index * rows + r];
    }
    return column;
  }
  Value element(expr.type);
  if (index >= 0 && index < base.type.components())
    element.data[0] = base.data[index];
  return element;
}

Value Evaluator::EvalConstructor(const Expr& expr) {
  if (expr.type.is_struct()) {
    Value out(expr.type);
    const StructInfo* info = expr.type.struct_info();
    if (!info)
      return out;
    if (expr.args.size() == 1 && expr.args[0]->type == expr.type)
      return Convert(Eval(*expr.args[0]), expr.type);
    if (expr.args.size() != info->members.size()) {
      Fail("constructor for structure '" + info->name +
           "' expects one argument per member");
      return out;
    }
    for (size_t i = 0; i < info->members.size(); ++i) {
      const StructMember& member = info->members[i];
      const Value value = Convert(Eval(*expr.args[i]), member.type);
      WriteSlice(&out, member.component_offset, value,
                 member.type.components());
    }
    return out;
  }
  if (expr.type.is_array()) {
    Value out(expr.type);
    const Type& element_type = expr.type.array_element();
    const int element_components = element_type.components();
    if (expr.args.size() == 1 && expr.args[0]->type == expr.type)
      return Convert(Eval(*expr.args[0]), expr.type);
    int offset = 0;
    for (const ExprPtr& argument : expr.args) {
      const Value value = Eval(*argument);
      if (offset + element_components > out.size())
        break;
      if (value.type == element_type) {
        WriteSlice(&out, offset, value, element_components);
      } else if (value.type.is_aggregate()) {
        WriteSlice(&out, offset, value,
                   std::min(element_components, value.size()));
      } else {
        const int count = value.type.is_scalar() ? 1 : value.size();
        for (int i = 0; i < count && i < element_components; ++i)
          out.Data()[offset + i] = ComponentAt(value, i);
      }
      offset += element_components;
    }
    return out;
  }
  float components[kValueFloats] = {0.0f};
  int count = 0;
  for (const ExprPtr& argument : expr.args) {
    const Value value = Eval(*argument);
    if (value.type.is_matrix()) {
      const int total = value.type.components();
      for (int i = 0; i < total && count < kValueFloats; ++i)
        components[count++] = value.data[i];
      continue;
    }
    const int total = value.type.is_scalar() ? 1 : value.type.components();
    for (int i = 0; i < total && count < kValueFloats; ++i)
      components[count++] =
          value.type.is_scalar() ? value.data[0] : value.data[i];
  }

  Value out(expr.type);
  if (expr.type.is_matrix()) {
    const int rows = expr.type.rows();
    const int columns = expr.type.columns();
    if (count == 1) {
      for (int c = 0; c < columns; ++c)
        for (int r = 0; r < rows; ++r)
          out.data[c * rows + r] = (c == r) ? components[0] : 0.0f;
    } else {
      for (int i = 0; i < count && i < kValueFloats; ++i)
        out.data[i] = components[i];
    }
    return out;
  }
  if (count == 1 && !expr.type.is_scalar()) {
    for (int i = 0; i < expr.type.components(); ++i)
      out.data[i] = components[0];
    return out;
  }
  const int total = expr.type.is_scalar() ? 1 : expr.type.components();
  for (int i = 0; i < total; ++i) {
    if (i < count)
      out.data[i] = components[i];
    else
      out.data[i] = (i == 3) ? 1.0f : 0.0f;
  }
  return out;
}

Value Evaluator::Eval(const Expr& expr) {
  if (HasError())
    return Value(expr.type);
  if (BudgetExceeded())
    return Value(expr.type);
  switch (expr.kind) {
    case Expr::Kind::kLiteral: {
      Value value(expr.type);
      value.data[0] = expr.literal;
      return value;
    }
    case Expr::Kind::kVariable:
      return ReadVariable(expr);
    case Expr::Kind::kField:
      return EvalField(expr);
    case Expr::Kind::kIndex:
      return EvalIndex(expr);
    case Expr::Kind::kCall: {
      if (expr.is_constructor)
        return EvalConstructor(expr);
      if (expr.builtin_func != BuiltinFunc::kNone)
        return EvalBuiltin(expr);
      std::vector<Value> args;
      args.reserve(expr.args.size());
      for (const ExprPtr& argument : expr.args)
        args.push_back(Eval(*argument));
      return CallUserFunction(expr.function_index, args);
    }
    case Expr::Kind::kUnary:
      return EvalUnary(expr);
    case Expr::Kind::kBinary:
      return EvalBinary(expr);
    case Expr::Kind::kAssign:
      return EvalAssignment(expr);
    case Expr::Kind::kTernary: {
      if (IsTrue(Eval(*expr.args[0])))
        return Eval(*expr.args[1]);
      return Eval(*expr.args[2]);
    }
    case Expr::Kind::kComma: {
      Value last(expr.type);
      for (const ExprPtr& argument : expr.args)
        last = Eval(*argument);
      return last;
    }
  }
  return Value(expr.type);
}

Value Evaluator::CallUserFunction(int function_index,
                                  const std::vector<Value>& args) {
  if (function_index < 0 ||
      function_index >= static_cast<int>(ast_.functions.size())) {
    Fail("invalid function index");
    return Value();
  }
  Value result;
  CallFunction(ast_.functions[function_index], args, &result);
  return result;
}

Value Evaluator::EvalUnary(const Expr& expr) {
  const Op op = expr.op;
  if (op == Op::kPreIncrement || op == Op::kPreDecrement ||
      op == Op::kPostIncrement || op == Op::kPostDecrement) {
    const Expr& target = *expr.args[0];
    const Value old = Eval(target);
    Value next(expr.type);
    const bool increment = op == Op::kPreIncrement || op == Op::kPostIncrement;
    for (int i = 0; i < expr.type.components(); ++i)
      next.data[i] = old.data[i] + (increment ? 1.0f : -1.0f);
    Assign(target, next);
    return (op == Op::kPostIncrement || op == Op::kPostDecrement) ? old : next;
  }

  const Value operand = Eval(*expr.args[0]);
  if (op == Op::kNot)
    return MakeBool(!IsTrue(operand));
  if (op == Op::kBitNot) {
    Value out(Type::Int());
    out.data[0] = static_cast<float>(~static_cast<int32_t>(operand.data[0]));
    return out;
  }
  if (op == Op::kNegate) {
    Value out(expr.type);
    for (int i = 0; i < expr.type.components(); ++i)
      out.data[i] = -operand.data[i];
    return out;
  }
  return operand;
}

Value Evaluator::Multiply(const Value& left,
                          const Value& right,
                          const Type& type) {
  if (!type.is_matrix() && left.type == type && right.type == type) {
    if (type.base() == BaseType::kUInt) {
      return MapPairs(left, right, type, [](float a, float b) {
        return static_cast<float>(static_cast<uint32_t>(a) *
                                  static_cast<uint32_t>(b));
      });
    }
    if (type.base() == BaseType::kInt) {
      return MapPairs(left, right, type, [](float a, float b) {
        return static_cast<float>(static_cast<int32_t>(a) *
                                  static_cast<int32_t>(b));
      });
    }
    return MapPairs(left, right, type, [](float a, float b) { return a * b; });
  }
  if (left.type.is_matrix() && right.type.is_matrix()) {
    Value out(type);
    const int left_rows = left.type.rows();
    const int left_columns = left.type.columns();
    const int right_rows = right.type.rows();
    if (left_columns != right_rows)
      return out;
    const int out_rows = left_rows;
    const int out_columns = right.type.columns();
    for (int c = 0; c < out_columns; ++c) {
      for (int r = 0; r < out_rows; ++r) {
        float sum = 0.0f;
        for (int k = 0; k < left_columns; ++k)
          sum += left.data[k * left_rows + r] * right.data[c * right_rows + k];
        out.data[c * out_rows + r] = sum;
      }
    }
    return out;
  }
  if (left.type.is_matrix()) {
    Value out(type);
    const int rows = left.type.rows();
    const int columns = left.type.columns();
    for (int r = 0; r < rows; ++r) {
      float sum = 0.0f;
      for (int c = 0; c < columns; ++c)
        sum += left.data[c * rows + r] * ComponentAt(right, c);
      out.data[r] = sum;
    }
    return out;
  }
  if (right.type.is_matrix()) {
    Value out(type);
    const int rows = right.type.rows();
    const int columns = right.type.columns();
    for (int c = 0; c < columns; ++c) {
      float sum = 0.0f;
      for (int r = 0; r < rows; ++r)
        sum += ComponentAt(left, r) * right.data[c * rows + r];
      out.data[c] = sum;
    }
    return out;
  }
  return MapPairs(left, right, type, [](float a, float b) { return a * b; });
}

Value Evaluator::BinaryOp(Op op,
                          const Value& left,
                          const Value& right,
                          const Type& result_type) {
  const bool is_uint = result_type.base() == BaseType::kUInt;
  const bool is_int = result_type.is_integer();
  switch (op) {
    case Op::kAdd:
      return MapPairs(left, right, result_type, [is_int](float a, float b) {
        return is_int ? static_cast<float>(static_cast<int32_t>(a) +
                                           static_cast<int32_t>(b))
                      : a + b;
      });
    case Op::kSubtract:
      return MapPairs(left, right, result_type, [is_int](float a, float b) {
        return is_int ? static_cast<float>(static_cast<int32_t>(a) -
                                           static_cast<int32_t>(b))
                      : a - b;
      });
    case Op::kMultiply:
      return Multiply(left, right, result_type);
    case Op::kDivide:
      return MapPairs(left, right, result_type, [is_int](float a, float b) {
        if (!is_int)
          return b == 0.0f ? 0.0f : a / b;
        const int32_t divisor = static_cast<int32_t>(b);
        return divisor == 0 ? 0.0f
                            : static_cast<float>(static_cast<int32_t>(a) /
                                                 divisor);
      });
    case Op::kModulo:
      return MapPairs(left, right, result_type, [is_int](float a, float b) {
        if (!is_int)
          return a - b * std::floor(a / b);
        const int32_t divisor = static_cast<int32_t>(b);
        return divisor == 0 ? 0.0f
                            : static_cast<float>(static_cast<int32_t>(a) %
                                                 divisor);
      });
    case Op::kShiftLeft:
    case Op::kShiftRight:
    case Op::kBitAnd:
    case Op::kBitXor:
    case Op::kBitOr:
      return MapPairs(left, right, result_type, [op, is_uint](float a, float b) {
        uint32_t x = is_uint ? static_cast<uint32_t>(a)
                             : static_cast<uint32_t>(static_cast<int32_t>(a));
        const uint32_t y = static_cast<uint32_t>(b) & 31u;
        uint32_t value = 0;
        switch (op) {
          case Op::kShiftLeft:
            value = x << y;
            break;
          case Op::kShiftRight:
            value = is_uint ? x >> y
                            : static_cast<uint32_t>(
                                  static_cast<int32_t>(x) >> y);
            break;
          case Op::kBitAnd:
            value = x & static_cast<uint32_t>(b);
            break;
          case Op::kBitXor:
            value = x ^ static_cast<uint32_t>(b);
            break;
          case Op::kBitOr:
            value = x | static_cast<uint32_t>(b);
            break;
          default:
            break;
        }
        if (is_uint)
          return static_cast<float>(value);
        return static_cast<float>(static_cast<int32_t>(value));
      });
    case Op::kLess:
      return MakeBool(ComponentAt(left, 0) < ComponentAt(right, 0));
    case Op::kLessEqual:
      return MakeBool(ComponentAt(left, 0) <= ComponentAt(right, 0));
    case Op::kGreater:
      return MakeBool(ComponentAt(left, 0) > ComponentAt(right, 0));
    case Op::kGreaterEqual:
      return MakeBool(ComponentAt(left, 0) >= ComponentAt(right, 0));
    case Op::kEqual:
    case Op::kNotEqual: {
      const int count =
          std::max(left.type.components(), right.type.components());
      bool equal = true;
      for (int i = 0; i < count; ++i) {
        if (ComponentAt(left, i) != ComponentAt(right, i)) {
          equal = false;
          break;
        }
      }
      return MakeBool(op == Op::kEqual ? equal : !equal);
    }
    case Op::kLogicalAnd:
      return MakeBool(IsTrue(left) && IsTrue(right));
    case Op::kLogicalOr:
      return MakeBool(IsTrue(left) || IsTrue(right));
    case Op::kLogicalXor:
      return MakeBool(IsTrue(left) != IsTrue(right));
    default:
      break;
  }
  return Value(result_type);
}

Value Evaluator::EvalBinary(const Expr& expr) {
  const Value left = Eval(*expr.args[0]);
  // && and || short circuit, as required by GLSL.
  if (expr.op == Op::kLogicalAnd && !IsTrue(left))
    return MakeBool(false);
  if (expr.op == Op::kLogicalOr && IsTrue(left))
    return MakeBool(true);
  const Value right = Eval(*expr.args[1]);
  return BinaryOp(expr.op, left, right, expr.type);
}

Value Evaluator::EvalAssignment(const Expr& expr) {
  const Expr& target = *expr.args[0];
  Value value = Eval(*expr.args[1]);
  if (expr.op != Op::kAssign) {
    Op op = Op::kAssign;
    switch (expr.op) {
      case Op::kAddAssign:
        op = Op::kAdd;
        break;
      case Op::kSubtractAssign:
        op = Op::kSubtract;
        break;
      case Op::kMultiplyAssign:
        op = Op::kMultiply;
        break;
      case Op::kDivideAssign:
        op = Op::kDivide;
        break;
      case Op::kModuloAssign:
        op = Op::kModulo;
        break;
      case Op::kShiftLeftAssign:
        op = Op::kShiftLeft;
        break;
      case Op::kShiftRightAssign:
        op = Op::kShiftRight;
        break;
      case Op::kBitAndAssign:
        op = Op::kBitAnd;
        break;
      case Op::kBitXorAssign:
        op = Op::kBitXor;
        break;
      case Op::kBitOrAssign:
        op = Op::kBitOr;
        break;
      default:
        break;
    }
    value = BinaryOp(op, Eval(target), value, target.type);
  }
  const Value converted = Convert(value, target.type);
  Assign(target, converted);
  return converted;
}

Value Evaluator::BuiltinTexture(const Expr& expr) {
  std::vector<Value> args;
  args.reserve(expr.args.size());
  for (const ExprPtr& argument : expr.args)
    args.push_back(Eval(*argument));

  Value out(Type::Vec(4));
  out.data[3] = 1.0f;
  if (args.size() < 2 || !sampler_)
    return out;
  const int unit = static_cast<int>(args[0].data[0]);
  float coords[3] = {0.0f, 0.0f, 0.0f};
  float lod = 0.0f;

  if (expr.builtin_func == BuiltinFunc::kTexture2DProj) {
    const int last = args[1].type.components() - 1;
    const float w = last >= 1 ? args[1].data[last] : 1.0f;
    coords[0] = w != 0.0f ? args[1].data[0] / w : 0.0f;
    coords[1] = w != 0.0f ? args[1].data[1] / w : 0.0f;
    out = Value(Type::Vec(4));
    sampler_->Sample2D(unit, coords[0], coords[1], lod, out.data);
    return out;
  }

  const BaseType base = args[0].type.base();
  const bool cube = expr.builtin_func == BuiltinFunc::kTextureCube ||
                    base == BaseType::kSamplerCube ||
                    base == BaseType::kISamplerCube ||
                    base == BaseType::kUSamplerCube;
  const bool volume = expr.builtin_func == BuiltinFunc::kTexture3D ||
                      base == BaseType::kSampler3D ||
                      base == BaseType::kISampler3D ||
                      base == BaseType::kUSampler3D;
  const bool array = base == BaseType::kSampler2DArray ||
                     base == BaseType::kISampler2DArray ||
                     base == BaseType::kUSampler2DArray;
  if (expr.builtin_func == BuiltinFunc::kTextureLod && args.size() > 2)
    lod = args[2].data[0];

  if (cube) {
    for (int i = 0; i < 3; ++i)
      coords[i] = args[1].data[i];
    out = Value(Type::Vec(4));
    sampler_->SampleCube(unit, coords, lod, out.data);
    return out;
  }
  if (volume) {
    for (int i = 0; i < 3; ++i)
      coords[i] = args[1].data[i];
    out = Value(Type::Vec(4));
    sampler_->Sample3D(unit, coords, lod, out.data);
    return out;
  }
  if (array) {
    coords[0] = args[1].data[0];
    coords[1] = args[1].data[1];
    coords[2] = args[1].data[2];
    out = Value(Type::Vec(4));
    sampler_->Sample2DArray(unit, coords[0], coords[1], coords[2], lod,
                            out.data);
    return out;
  }
  coords[0] = args[1].data[0];
  coords[1] = args[1].data[1];
  out = Value(Type::Vec(4));
  sampler_->Sample2D(unit, coords[0], coords[1], lod, out.data);
  return out;
}

Value Evaluator::EvaluateBuiltinMath(const BuiltinFunc function,
                                     const std::vector<Value>& args) {
  if (args.empty())
    return Value(Type::Float());
  const Type type = args[0].type;
  const Value& a = args[0];
  switch (function) {
    case BuiltinFunc::kRadians:
      return MapScalars(a, type, [](float v) { return v * (kPi / 180.0f); });
    case BuiltinFunc::kDegrees:
      return MapScalars(a, type, [](float v) { return v * (180.0f / kPi); });
    case BuiltinFunc::kSin:
      return MapFastScalars(a, type, fastmath::Sin, fastmath::Sin4);
    case BuiltinFunc::kCos:
      return MapFastScalars(a, type, fastmath::Cos, fastmath::Cos4);
    case BuiltinFunc::kTan:
      return MapScalars(a, type, [](float v) { return fastmath::Tan(v); });
    case BuiltinFunc::kAsin:
      return MapScalars(a, type, [](float v) { return fastmath::Asin(v); });
    case BuiltinFunc::kAcos:
      return MapScalars(a, type, [](float v) { return fastmath::Acos(v); });
    case BuiltinFunc::kAtan:
      if (args.size() == 2) {
        return MapPairs(args[0], args[1], type,
                        [](float y, float x) { return fastmath::Atan2(y, x); });
      }
      return MapScalars(a, type, [](float v) { return fastmath::Atan(v); });
    case BuiltinFunc::kPow:
      return MapPairs(args[0], args[1], type,
                      [](float x, float y) { return fastmath::Pow(x, y); });
    case BuiltinFunc::kExp:
      return MapScalars(a, type, [](float v) { return fastmath::Exp(v); });
    case BuiltinFunc::kLog:
      return MapScalars(a, type, [](float v) { return fastmath::Log(v); });
    case BuiltinFunc::kExp2:
      return MapScalars(a, type, [](float v) { return fastmath::Exp2(v); });
    case BuiltinFunc::kLog2:
      return MapScalars(a, type, [](float v) { return fastmath::Log2(v); });
    case BuiltinFunc::kSqrt:
      return MapScalars(a, type, [](float v) { return fastmath::Sqrt(v); });
    case BuiltinFunc::kInverseSqrt:
      return MapScalars(a, type,
                        [](float v) { return fastmath::InverseSqrt(v); });
    case BuiltinFunc::kAbs:
      return MapScalars(a, type, [](float v) { return std::fabs(v); });
    case BuiltinFunc::kSign:
      return MapScalars(a, type, [](float v) {
        return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f);
      });
    case BuiltinFunc::kFloor:
      return MapScalars(a, type, [](float v) { return std::floor(v); });
    case BuiltinFunc::kCeil:
      return MapScalars(a, type, [](float v) { return std::ceil(v); });
    case BuiltinFunc::kFract:
      return MapScalars(a, type, [](float v) { return v - std::floor(v); });
    case BuiltinFunc::kMod:
      return MapPairs(args[0], args[1], type, [](float x, float y) {
        return y == 0.0f ? 0.0f : x - y * std::floor(x / y);
      });
    case BuiltinFunc::kMin:
      return MapPairs(args[0], args[1], type,
                      [](float x, float y) { return x < y ? x : y; });
    case BuiltinFunc::kMax:
      return MapPairs(args[0], args[1], type,
                      [](float x, float y) { return x > y ? x : y; });
    case BuiltinFunc::kClamp:
      return MapTriples(args[0], args[1], args[2], type,
                        [](float x, float lo, float hi) {
                          return x < lo ? lo : (x > hi ? hi : x);
                        });
    case BuiltinFunc::kMix: {
      const Value& selector = args[2];
      if (selector.type.base() == BaseType::kBool) {
        Value out(type);
        for (int i = 0; i < type.components(); ++i)
          out.data[i] = ComponentAt(selector, i) != 0.0f
                            ? ComponentAt(args[1], i)
                            : ComponentAt(args[0], i);
        return out;
      }
      return MapTriples(
          args[0], args[1], selector, type,
          [](float x, float y, float a) { return x + (y - x) * a; });
    }
    case BuiltinFunc::kStep:
      return MapPairs(args[0], args[1], type, [](float edge, float x) {
        return x < edge ? 0.0f : 1.0f;
      });
    case BuiltinFunc::kSmoothStep:
      return MapTriples(args[0], args[1], args[2], type,
                        [](float edge0, float edge1, float x) {
                          const float denom = edge1 - edge0;
                          float t = denom == 0.0f ? 0.0f : (x - edge0) / denom;
                          t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
                          return t * t * (3.0f - 2.0f * t);
                        });
    case BuiltinFunc::kLength: {
      Value out(Type::Float());
      out.data[0] = LengthOf(a);
      return out;
    }
    case BuiltinFunc::kDistance: {
      Value out(Type::Float());
      Value delta(type);
      for (int i = 0; i < type.components(); ++i)
        delta.data[i] = ComponentAt(args[0], i) - ComponentAt(args[1], i);
      out.data[0] = LengthOf(delta);
      return out;
    }
    case BuiltinFunc::kDot: {
      Value out(Type::Float());
      out.data[0] = DotProduct(args[0], args[1]);
      return out;
    }
    case BuiltinFunc::kCross: {
      Value out(Type::Vec(3));
      const float ax = args[0].data[0], ay = args[0].data[1],
                  az = args[0].data[2];
      const float bx = args[1].data[0], by = args[1].data[1],
                  bz = args[1].data[2];
      out.data[0] = ay * bz - az * by;
      out.data[1] = az * bx - ax * bz;
      out.data[2] = ax * by - ay * bx;
      return out;
    }
    case BuiltinFunc::kNormalize: {
      Value out(type);
      const float squared = DotProduct(a, a);
      if (squared > 0.0f &&
          squared < std::numeric_limits<float>::max()) {
        const float inverse = fastmath::InverseSqrt(squared);
        simd::Scale(a.data, inverse, out.data, type.components());
        return out;
      }
      const float length = fastmath::Sqrt(squared);
      if (length > 0.0f) {
        for (int i = 0; i < type.components(); ++i)
          out.data[i] = a.data[i] / length;
      }
      return out;
    }
    case BuiltinFunc::kFaceForward: {
      Value out(type);
      const float dot = DotProduct(args[2], args[1]);
      for (int i = 0; i < type.components(); ++i)
        out.data[i] =
            dot < 0.0f ? ComponentAt(args[0], i) : -ComponentAt(args[0], i);
      return out;
    }
    case BuiltinFunc::kReflect: {
      Value out(type);
      const float dot = DotProduct(args[1], args[0]);
      for (int i = 0; i < type.components(); ++i)
        out.data[i] =
            ComponentAt(args[0], i) - 2.0f * dot * ComponentAt(args[1], i);
      return out;
    }
    case BuiltinFunc::kRefract: {
      Value out(type);
      const float eta = args[2].data[0];
      const float dot = DotProduct(args[1], args[0]);
      const float k = 1.0f - eta * eta * (1.0f - dot * dot);
      if (k < 0.0f)
        return out;
      const float scale = eta * dot + fastmath::Sqrt(k);
      for (int i = 0; i < type.components(); ++i)
        out.data[i] =
            eta * ComponentAt(args[0], i) - scale * ComponentAt(args[1], i);
      return out;
    }
    case BuiltinFunc::kMatrixCompMult: {
      Value out(type);
      for (int i = 0; i < type.components(); ++i)
        out.data[i] = args[0].data[i] * args[1].data[i];
      return out;
    }
    case BuiltinFunc::kTranspose: {
      const int rows = args[0].type.rows();
      const int columns = args[0].type.columns();
      Value out(Type::Mat(rows, columns));
      for (int c = 0; c < columns; ++c)
        for (int r = 0; r < rows; ++r)
          out.data[r * columns + c] = args[0].data[c * rows + r];
      return out;
    }
    case BuiltinFunc::kDeterminant: {
      Value out(Type::Float());
      out.data[0] = Determinant(args[0]);
      return out;
    }
    case BuiltinFunc::kInverse: {
      Value out(type);
      Invert(args[0], &out);
      return out;
    }
    default:
      break;
  }
  return Value(type);
}

Value Evaluator::BuiltinRelational(BuiltinFunc function,
                                   const std::vector<Value>& args) {
  const int count = std::max(2, args[0].type.components());
  Value out(Type::BVec(count));
  for (int i = 0; i < count; ++i) {
    const float x = ComponentAt(args[0], i);
    const float y = ComponentAt(args[1], i);
    bool value = false;
    switch (function) {
      case BuiltinFunc::kLessThan:
        value = x < y;
        break;
      case BuiltinFunc::kLessThanEqual:
        value = x <= y;
        break;
      case BuiltinFunc::kGreaterThan:
        value = x > y;
        break;
      case BuiltinFunc::kGreaterThanEqual:
        value = x >= y;
        break;
      case BuiltinFunc::kEqualFunc:
        value = x == y;
        break;
      case BuiltinFunc::kNotEqualFunc:
        value = x != y;
        break;
      default:
        break;
    }
    out.data[i] = value ? 1.0f : 0.0f;
  }
  return out;
}

Value Evaluator::EvalBuiltin(const Expr& expr) {
  if (expr.builtin_func == BuiltinFunc::kTexture2D ||
      expr.builtin_func == BuiltinFunc::kTexture2DProj ||
      expr.builtin_func == BuiltinFunc::kTextureCube ||
      expr.builtin_func == BuiltinFunc::kTexture3D ||
      expr.builtin_func == BuiltinFunc::kTexture ||
      expr.builtin_func == BuiltinFunc::kTextureLod) {
    return BuiltinTexture(expr);
  }

  std::vector<Value> args;
  args.reserve(expr.args.size());
  for (const ExprPtr& argument : expr.args)
    args.push_back(Eval(*argument));

  switch (expr.builtin_func) {
    case BuiltinFunc::kLessThan:
    case BuiltinFunc::kLessThanEqual:
    case BuiltinFunc::kGreaterThan:
    case BuiltinFunc::kGreaterThanEqual:
    case BuiltinFunc::kEqualFunc:
    case BuiltinFunc::kNotEqualFunc:
      return BuiltinRelational(expr.builtin_func, args);
    case BuiltinFunc::kAny: {
      const int count = args[0].type.components();
      bool any = false;
      for (int i = 0; i < count; ++i)
        any = any || args[0].data[i] != 0.0f;
      return MakeBool(any);
    }
    case BuiltinFunc::kAll: {
      const int count = args[0].type.components();
      bool all = true;
      for (int i = 0; i < count; ++i)
        all = all && args[0].data[i] != 0.0f;
      return MakeBool(all);
    }
    case BuiltinFunc::kNot: {
      Value out(expr.type);
      for (int i = 0; i < expr.type.components(); ++i)
        out.data[i] = ComponentAt(args[0], i) == 0.0f ? 1.0f : 0.0f;
      return out;
    }
    case BuiltinFunc::kTextureSize: {
      const int unit = args[0].type.is_sampler()
                           ? static_cast<int>(args[0].data[0])
                           : 0;
      int size[3] = {0, 0, 0};
      if (sampler_)
        sampler_->Size(unit, size);
      Value out(expr.type);
      for (int i = 0; i < expr.type.components() && i < 3; ++i)
        out.data[i] = static_cast<float>(size[i]);
      return out;
    }
    case BuiltinFunc::kDfdx:
    case BuiltinFunc::kDfdy:
    case BuiltinFunc::kFwidth: {
      // No neighbour information reaches the interpreter; the derivative
      // built-ins report zero and are documented as such.
      Value out(expr.type);
      return out;
    }
    default:
      break;
  }
  return EvaluateBuiltinMath(expr.builtin_func, args);
}

// ---- Program entry points ------------------------------------------------

void Program::RunVertex(const VertexInput& input,
                        float out_position[4],
                        float* out_point_size,
                        float* out_varyings) {
  out_position[0] = 0.0f;
  out_position[1] = 0.0f;
  out_position[2] = 0.0f;
  out_position[3] = 1.0f;
  if (out_point_size)
    *out_point_size = 1.0f;
  if (!linked_ || !vertex_)
    return;
  if (varying_floats_ > 0 && out_varyings)
    memset(out_varyings, 0, sizeof(float) * varying_floats_);

  Evaluator evaluator(*vertex_, &vertex_globals_, this);
  evaluator.set_uniforms(input.uniforms, vertex_uniform_map_.data());
  evaluator.set_attributes(input.attributes, input.vertex_id,
                           input.instance_id);
  evaluator.set_varyings(nullptr, out_varyings);
  evaluator.set_sampler(input.sampler);

  bool used_jit = false;
#if defined(ZGL_ENABLE_JIT)
  if (vertex_jit_) {
    // Built-ins live in the global slot array for both backends.  Filling them
    // here keeps the JIT from needing a second copy of the GLSL input rules.
    evaluator.PrepareBuiltins();
    JitRunInputs jit_inputs;
    jit_inputs.uniforms = input.uniforms;
    jit_inputs.uniform_map = vertex_uniform_map_.data();
    jit_inputs.attributes = input.attributes;
    jit_inputs.varyings_out = out_varyings;
    jit_inputs.sampler = input.sampler;
    jit_inputs.globals = vertex_globals_.data();
    bool discarded = false;
    used_jit = vertex_jit_->Run(jit_inputs, &discarded);
  }
#endif
  if (!used_jit && !evaluator.RunMain()) {
    runtime_error_ = evaluator.error();
    return;
  }

  const int position_slot =
      vertex_->builtin_slots[static_cast<int>(BuiltinId::kPosition)];
  if (position_slot >= 0 &&
      position_slot < static_cast<int>(vertex_globals_.size())) {
    const Value& value = vertex_globals_[position_slot];
    for (int i = 0; i < 4; ++i)
      out_position[i] = value.data[i];
  }
  if (out_point_size) {
    const int slot =
        vertex_->builtin_slots[static_cast<int>(BuiltinId::kPointSize)];
    if (slot >= 0 && slot < static_cast<int>(vertex_globals_.size()))
      *out_point_size = vertex_globals_[slot].data[0];
  }
}

bool Program::RunFragment(const FragmentInput& input, FragmentOutput* output) {
  for (int i = 0; i < kMaxFragmentOutputs; ++i) {
    output->color[i][0] = 0.0f;
    output->color[i][1] = 0.0f;
    output->color[i][2] = 0.0f;
    output->color[i][3] = 1.0f;
    output->color_base[i] = BaseType::kFloat;
    output->written[i] = false;
  }
  output->depth = 0.0f;
  output->writes_depth = false;
  output->discarded = false;
  if (!linked_ || !fragment_)
    return false;

  Evaluator evaluator(*fragment_, &fragment_globals_, this);
  evaluator.set_uniforms(input.uniforms, fragment_uniform_map_.data());
  evaluator.set_varyings(input.varyings, nullptr);
  evaluator.set_sampler(input.sampler);
  evaluator.set_fragment_coord(input.frag_coord);
  evaluator.set_front_facing(input.front_facing);
  evaluator.set_point_coord(input.point_coord);
  evaluator.set_sample_id(input.sample_id);
  evaluator.set_sample_mask(input.sample_mask);

  bool used_jit = false;
#if defined(ZGL_ENABLE_JIT)
  if (fragment_jit_) {
    evaluator.PrepareBuiltins();
    JitRunInputs jit_inputs;
    jit_inputs.uniforms = input.uniforms;
    jit_inputs.uniform_map = fragment_uniform_map_.data();
    jit_inputs.varyings_in = input.varyings;
    jit_inputs.sampler = input.sampler;
    jit_inputs.globals = fragment_globals_.data();
    bool discarded = false;
    used_jit = fragment_jit_->Run(jit_inputs, &discarded);
    if (used_jit && discarded) {
      output->discarded = true;
      return true;
    }
  }
#endif
  if (!used_jit && !evaluator.RunMain()) {
    runtime_error_ = evaluator.error();
    return false;
  }
  if (!used_jit && evaluator.discarded()) {
    output->discarded = true;
    return true;
  }

  for (const FragmentOutputInfo& fragment_output : fragment_outputs_) {
    if (fragment_output.location < 0 ||
        fragment_output.location >= kMaxFragmentOutputs ||
        fragment_output.slot < 0 ||
        fragment_output.slot >= static_cast<int>(fragment_globals_.size()))
      continue;
    const Value& value = fragment_globals_[fragment_output.slot];
    for (int i = 0; i < 4; ++i)
      output->color[fragment_output.location][i] = value.data[i];
    output->color_base[fragment_output.location] = fragment_output.base;
    output->written[fragment_output.location] = true;
  }
  if (uses_frag_depth_) {
    const int depth_slot =
        fragment_->builtin_slots[static_cast<int>(BuiltinId::kFragDepth)];
    if (depth_slot >= 0 &&
        depth_slot < static_cast<int>(fragment_globals_.size())) {
      output->depth = fragment_globals_[depth_slot].data[0];
      output->writes_depth = true;
    }
  }
  return true;
}

bool EvaluateGlobalInitializers(ShaderAst* ast,
                                std::vector<Value>* globals,
                                const int* uniform_map,
                                const Value* uniforms,
                                std::string* error) {
  if (!ast || ast->global_initializers.empty())
    return true;
  Program* program = nullptr;
  Evaluator evaluator(*ast, globals, program);
  evaluator.set_uniforms(uniforms, uniform_map);
  for (const GlobalInit& init : ast->global_initializers) {
    if (init.slot < 0 || init.slot >= static_cast<int>(globals->size()))
      continue;
    const Value value = evaluator.EvalExpression(*init.expression);
    if (evaluator.HasError()) {
      if (error)
        *error = evaluator.error();
      return false;
    }
    (*globals)[init.slot] = Convert(value, (*globals)[init.slot].type);
  }
  return true;
}

Value EvaluateBuiltinMath(const BuiltinFunc function,
                          const std::vector<Value>& args) {
  return Evaluator::EvaluateBuiltinMath(function, args);
}

}  // namespace glsl
}  // namespace zgl
