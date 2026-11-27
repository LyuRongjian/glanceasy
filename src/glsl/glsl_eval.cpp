// Copyright 2026 The Glanceasy Authors. All rights reserved.
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
#include "src/glanceasy_fastmath.h"
#include "src/glanceasy_simd.h"

namespace glanceasy {
namespace glsl {

namespace {

float ConvertComponent(BaseType from, BaseType to, float v) {
  if (from == to)
    return v;
  switch (to) {
    case BaseType::kFloat:
      return from == BaseType::kBool ? (v != 0.0f ? 1.0f : 0.0f) : v;
    case BaseType::kInt:
      return from == BaseType::kBool
                 ? (v != 0.0f ? 1.0f : 0.0f)
                 : static_cast<float>(static_cast<int32_t>(v));
    case BaseType::kUInt:
      return from == BaseType::kBool
                 ? (v != 0.0f ? 1.0f : 0.0f)
                 : static_cast<float>(static_cast<uint32_t>(v));
    case BaseType::kBool:
      return v != 0.0f ? 1.0f : 0.0f;
    default:
      return v;
  }
}

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
  for (int i = 0; i < count && i < out.size(); ++i) {
    out.Data()[i] = ComponentAt(value, offset + i);
    out.IntData()[i] = value.IntData()[offset + i];
  }
  return out;
}

// Writes the components of |value| into |out| starting at |offset|.
void WriteSlice(Value* out, int offset, const Value& value, int count) {
  for (int i = 0; i < count && offset + i < out->size(); ++i) {
    out->Data()[offset + i] = ComponentAt(value, i);
    out->IntData()[offset + i] = value.IntData()[i];
  }
}

bool HasSideEffects(const Expr& expr) {
  if (expr.kind == Expr::Kind::kAssign)
    return true;
  if (expr.kind == Expr::Kind::kUnary &&
      (expr.op == Op::kPreIncrement || expr.op == Op::kPreDecrement ||
       expr.op == Op::kPostIncrement || expr.op == Op::kPostDecrement))
    return true;
  if (expr.kind == Expr::Kind::kCall && !expr.is_constructor &&
      expr.builtin_func == BuiltinFunc::kNone)
    return true;
  if (expr.base && HasSideEffects(*expr.base))
    return true;
  for (const ExprPtr& arg : expr.args) {
    if (arg && HasSideEffects(*arg))
      return true;
  }
  return false;
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

// Scalar-safe read of an integer component:  scalar operands broadcast across
// vector-width operations, mirroring ComponentAt() for float data.
uint32_t IntComponentAt(const Value& value, int index) {
  if (index < 0)
    return 0;
  if (value.type.is_scalar())
    return value.IntData()[0];
  if (index >= value.size())
    return 0;
  return value.IntData()[index];
}

template <typename F>
Value MapIntPairs(const Value& a, const Value& b, const Type& type, F fn) {
  Value out(type);
  const int count = type.components();
  const bool is_uint = type.base() == BaseType::kUInt;
  for (int i = 0; i < count; ++i) {
    const uint32_t x = IntComponentAt(a, i);
    const uint32_t y = IntComponentAt(b, i);
    const uint32_t v = fn(x, y, is_uint);
    out.IntData()[i] = v;
    out.data[i] = is_uint ? static_cast<float>(v)
                          : static_cast<float>(static_cast<int32_t>(v));
  }
  return out;
}

template <typename F>
Value MapPairs(const Value& a, const Value& b, const Type& type, F fn) {
  Value out(type);
  const int count = type.components();
  const bool integer = type.base() == BaseType::kInt ||
                       type.base() == BaseType::kUInt;
  for (int i = 0; i < count; ++i) {
    const float x = ComponentAt(a, i);
    const float y = ComponentAt(b, i);
    out.data[i] = fn(x, y);
    if (integer)
      out.IntData()[i] = type.base() == BaseType::kUInt
                             ? static_cast<uint32_t>(out.data[i])
                             : static_cast<uint32_t>(
                                   static_cast<int32_t>(out.data[i]));
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

float Det3(float a00, float a01, float a02, float a10, float a11,
           float a12, float a20, float a21, float a22) {
  return a00 * (a11 * a22 - a12 * a21) +
         a01 * (a12 * a20 - a10 * a22) +
         a02 * (a10 * a21 - a11 * a20);
}

float Determinant(const Value& m) {
  const int n = m.type.rows();
  if (n < 2 || n > 4 || m.type.columns() != n)
    return 0.0f;
  const float* d = m.data;
  if (n == 2) {
    return d[0] * d[3] - d[2] * d[1];
  }
  if (n == 3) {
    return Det3(d[0], d[3], d[6], d[1], d[4], d[7], d[2], d[5], d[8]);
  }
  float det = 0.0f;
  for (int r = 0; r < 4; ++r) {
    float minor[9];
    int index = 0;
    for (int c = 1; c < 4; ++c)
      for (int rr = 0; rr < 4; ++rr)
        if (rr != r)
          minor[index++] = d[c * 4 + rr];
    const float term = Det3(minor[0], minor[1], minor[2], minor[3], minor[4],
                            minor[5], minor[6], minor[7], minor[8]);
    det += (r & 1 ? -1.0f : 1.0f) * d[r] * term;
  }
  return det;
}

bool Invert(const Value& m, Value* out) {
  const int n = m.type.rows();
  if (n < 2 || n > 4 || m.type.columns() != n)
    return false;
  double a[16];
  double inv[16] = {};
  float row_major[16];
  MatToRowMajor(m, n, n, row_major);
  for (int i = 0; i < 16; ++i)
    a[i] = row_major[i];
  for (int i = 0; i < n; ++i)
    inv[i * n + i] = 1.0;
  for (int i = 0; i < n; ++i) {
    int pivot = i;
    for (int r = i + 1; r < n; ++r) {
      if (std::fabs(a[r * n + i]) > std::fabs(a[pivot * n + i]))
        pivot = r;
    }
    if (std::fabs(a[pivot * n + i]) < 1e-30)
      return false;
    if (pivot != i) {
      for (int c = 0; c < n; ++c) {
        std::swap(a[i * n + c], a[pivot * n + c]);
        std::swap(inv[i * n + c], inv[pivot * n + c]);
      }
    }
    const double diagonal = a[i * n + i];
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
  for (int i = 0; i < 16; ++i)
    row_major[i] = static_cast<float>(inv[i]);
  *out = RowMajorToMat(row_major, n, n);
  for (int i = 0; i < out->size(); ++i)
    if (std::isnan(out->data[i]))
      out->data[i] = 0.0f;
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
  void set_attributes(const float* attributes,
                      const uint32_t* attribute_int,
                      int vertex_id,
                      int instance_id) {
    attributes_ = attributes;
    attribute_int_ = attribute_int;
    vertex_id_ = vertex_id;
    instance_id_ = instance_id;
  }
  void set_varyings(const float* in, float* out) {
    varyings_in_ = in;
    varyings_out_ = out;
  }
  void set_sampler(TextureSampler* sampler) { sampler_ = sampler; }
  void set_varying_derivatives(const float* dx, const float* dy) {
    varyings_dx_ = dx;
    varyings_dy_ = dy;
  }
  void set_fragment_coord(const float coord[4]) {
    fragment_coord_[0] = coord[0];
    fragment_coord_[1] = coord[1];
    fragment_coord_[2] = coord[2];
    fragment_coord_[3] = coord[3];
  }
  void set_front_facing(bool front) { front_facing_ = front; }
  void set_depth_range(float near_value, float far_value) {
    depth_range_[0] = near_value;
    depth_range_[1] = far_value;
    depth_range_[2] = far_value - near_value;
    if (getenv("GLANCEASY_DEBUG_DR"))
      fprintf(stderr, "[dr] set near=%.2f far=%.2f diff=%.2f\n", near_value,
              far_value, depth_range_[2]);
  }
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
  Value CallUserFunction(int function_index,
                         const std::vector<Value>& args,
                         std::vector<Value>* out_values = nullptr);
  bool CallFunction(const Function& function,
                    const std::vector<Value>& args,
                    Value* result,
                    std::vector<Value>* out_values = nullptr);

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
  const uint32_t* attribute_int_ = nullptr;
  int vertex_id_ = 0;
  int instance_id_ = 0;
  const float* varyings_in_ = nullptr;
  float* varyings_out_ = nullptr;
  const float* varyings_dx_ = nullptr;
  const float* varyings_dy_ = nullptr;
  TextureSampler* sampler_ = nullptr;
  float fragment_coord_[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  int sample_id_ = 0;
  uint32_t sample_mask_ = 0xFFFFFFFFu;
  bool front_facing_ = true;
  float depth_range_[3] = {0.0f, 1.0f, 1.0f};
  float point_coord_[2] = {0.0f, 0.0f};

  std::vector<Value> locals_;
  std::vector<Value> neighbor_x_;
  std::vector<Value> neighbor_y_;
  int neighbor_mode_ = 0;
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
  if (index >= 0 && index < static_cast<int>(ast_.builtin_slots.size()) &&
      ast_.builtin_slots[index] >= 0) {
    return ast_.builtin_slots[index];
  }
  for (const Variable& variable : ast_.globals) {
    if (variable.builtin == id)
      return variable.index;
  }
  return -1;
}

void Evaluator::StoreBuiltin(BuiltinId id, const Value& value) {
  const int slot = BuiltinSlot(id);
  if (slot < 0 || !globals_ || slot >= static_cast<int>(globals_->size()))
    return;
  (*globals_)[slot] = value;
}

void Evaluator::PrepareBuiltins() {
  Value depth_range(Type::Vec(3));
  depth_range.data[0] = depth_range_[0];
  depth_range.data[1] = depth_range_[1];
  depth_range.data[2] = depth_range_[2];
  StoreBuiltin(BuiltinId::kDepthRange, depth_range);
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
  if (getenv("GLANCEASY_DEBUG_DR"))
    fprintf(stderr, "[dr] prepare stage=%d slot=%d slots=%d/%d value=%.2f %.2f %.2f\n",
            (int)ast_.stage, BuiltinSlot(BuiltinId::kDepthRange),
            (int)ast_.builtin_slots.size(), (int)BuiltinId::kDepthRange,
            depth_range.data[0], depth_range.data[1], depth_range.data[2]);
  StoreBuiltin(BuiltinId::kDepthRange, depth_range);
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
                             Value* result,
                             std::vector<Value>* out_values) {
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
  neighbor_x_.resize(needed);
  neighbor_y_.resize(needed);
  for (size_t i = base; i < locals_.size(); ++i) {
    locals_[i] = Value();
    neighbor_x_[i] = Value();
    neighbor_y_[i] = Value();
  }
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
  if (getenv("GLANCEASY_DEBUG_INOUT") && function.name == "test") {
    for (size_t i = 0; i < function.parameters.size(); ++i)
      fprintf(stderr, "[inout] test-p%zu = %.1f %.1f %.1f %.1f\n", i,
              locals_[base + i].Data()[0], locals_[base + i].Data()[1],
              locals_[base + i].Data()[2], locals_[base + i].Data()[3]);
  }
  if (getenv("GLANCEASY_DEBUG_INOUT") && function.name == "test") {
    fprintf(stderr, "[inout] test-ret=%.1f type=%s\n", returned_value.Data()[0],
            returned_value.type.Name().c_str());
  }
  if (getenv("GLANCEASY_DEBUG_INOUT") && function.name == "get") {
    fprintf(stderr, "[inout] get-ret=%.1f %.1f %.1f %.1f\n",
            returned_value.Data()[0], returned_value.Data()[1],
            returned_value.Data()[2], returned_value.Data()[3]);
  }
  if (getenv("GLANCEASY_DEBUG_INOUT") && function.name == "set") {
    for (size_t i = 0; i < function.parameters.size(); ++i)
      fprintf(stderr, "[inout] set-p%zu type=%s vals=%.1f %.1f %.1f %.1f\n", i,
              function.parameters[i].type.Name().c_str(),
              locals_[base + i].Data()[0], locals_[base + i].Data()[1],
              locals_[base + i].Data()[2], locals_[base + i].Data()[3]);
  }
  if (getenv("GLANCEASY_DEBUG_INOUT") && function.name == "func") {
    for (size_t i = 0; i < args.size(); ++i)
      fprintf(stderr, "[inout] arg%zu = %.1f %.1f %.1f %.1f\n", i,
              args[i].Data()[0], args[i].Data()[1], args[i].Data()[2],
              args[i].Data()[3]);
  }
  if (getenv("GLANCEASY_DEBUG_INOUT") && (function.name == "func" ||
                                     function.name == "isEqual")) {
    for (size_t i = 0; i < function.parameters.size(); ++i)
      fprintf(stderr, "[inout] %s p%zu q=%d vals=%.1f %.1f %.1f %.1f\n",
              function.name.c_str(), i,
              function.parameters[i].parameter_qualifier,
              locals_[base + i].Data()[0], locals_[base + i].Data()[1],
              locals_[base + i].Data()[2], locals_[base + i].Data()[3]);
  }
  if (getenv("GLANCEASY_DEBUG_INOUT"))
    fprintf(stderr, "[inout] fn=%s ret=%.2f type=%s\n", function.name.c_str(),
            returned_value.Data()[0], returned_value.type.Name().c_str());
  if (getenv("GLANCEASY_DEBUG_INOUT"))
    fprintf(stderr, "[inout] fn=%s args=%zu out=%d first=%.2f\n",
            function.name.c_str(), args.size(), out_values ? 1 : 0,
            args.empty() ? 0.0f : args[0].Data()[0]);
  if (out_values) {
    out_values->clear();
    for (size_t i = 0; i < function.parameters.size(); ++i) {
      out_values->push_back(locals_[base + i]);
      if (getenv("GLANCEASY_DEBUG_INOUT"))
        fprintf(stderr, "[inout]   p%zu q=%d val0=%.2f\n", i,
                function.parameters[i].parameter_qualifier,
                locals_[base + i].Data()[0]);
    }
  }
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
  const auto EvalCondition = [this](const Statement& condition) {
    if (condition.kind == Statement::Kind::kDeclaration)
      Exec(condition);
    return condition.expression ? Eval(*condition.expression)
                                : MakeBool(false);
  };
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
        const int slot = local_base_ + declarator.variable.index;
        if (slot >= 0 && slot < static_cast<int>(neighbor_x_.size())) {
          neighbor_x_[slot] = value;
          neighbor_y_[slot] = value;
          if (declarator.initializer &&
              !HasSideEffects(*declarator.initializer)) {
            const int saved_mode = neighbor_mode_;
            const float* saved_varyings = varyings_in_;
            neighbor_mode_ = 1;
            varyings_in_ = varyings_dx_;
            neighbor_x_[slot] =
                Convert(Eval(*declarator.initializer), declarator.variable.type);
            neighbor_mode_ = 2;
            varyings_in_ = varyings_dy_;
            neighbor_y_[slot] =
                Convert(Eval(*declarator.initializer), declarator.variable.type);
            neighbor_mode_ = saved_mode;
            varyings_in_ = saved_varyings;
          }
        }
      }
      return;
    }
    case Statement::Kind::kExpression:
      if (statement.expression)
        Eval(*statement.expression);
      return;
    case Statement::Kind::kIf: {
      Value condition = MakeBool(false);
      if (statement.condition) {
        if (statement.condition->kind == Statement::Kind::kDeclaration)
          Exec(*statement.condition);
        if (statement.condition->expression)
          condition = Eval(*statement.condition->expression);
      }
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
          if (!IsTrue(EvalCondition(*statement.condition)))
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
        if (!IsTrue(EvalCondition(*statement.condition)))
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
        if (!IsTrue(EvalCondition(*statement.condition)))
          return;
      }
    }
    case Statement::Kind::kSwitch: {
      const Value selector = Eval(*statement.expression);
      const int key = static_cast<int>(ComponentAt(selector, 0));
      int start = -1;
      int default_index = -1;
      for (size_t i = 0; i < statement.cases.size(); ++i) {
        const Statement::SwitchCase& entry = statement.cases[i];
        if (!entry.label) {
          default_index = static_cast<int>(i);
          continue;
        }
        const Value label = Eval(*entry.label);
        const int label_value = static_cast<int>(ComponentAt(label, 0));
        if (label_value == key) {
          start = static_cast<int>(i);
          break;
        }
      }
      if (start < 0)
        start = default_index;
      if (start >= 0) {
        for (size_t i = static_cast<size_t>(start); i < statement.cases.size();
             ++i) {
          for (const StmtPtr& child : statement.cases[i].statements) {
            Exec(*child);
            if (returned_ || broke_ || continued_ || discarded_ || HasError())
              break;
          }
          if (returned_ || broke_ || continued_ || discarded_ || HasError())
            break;
        }
      }
      broke_ = false;
      return;
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
    case Storage::kLocal: {
      const int slot = local_base_ + expr.index;
      if (expr.index < 0 || slot >= static_cast<int>(locals_.size()))
        return Value(expr.type);
      if (neighbor_mode_ == 1 && slot < static_cast<int>(neighbor_x_.size()))
        return Convert(neighbor_x_[slot], expr.type);
      if (neighbor_mode_ == 2 && slot < static_cast<int>(neighbor_y_.size()))
        return Convert(neighbor_y_[slot], expr.type);
      return Convert(locals_[slot], expr.type);
    }
    case Storage::kGlobal:
      if (globals_ && expr.index >= 0 &&
          expr.index < static_cast<int>(globals_->size())) {
        if (getenv("GLANCEASY_DEBUG_IDX") && expr.text == "sideEffectCounter")
          fprintf(stderr, "[idx] read global counter=%d int=%u\n",
                  (int)(*globals_)[expr.index].Data()[0],
                  (*globals_)[expr.index].IntData()[0]);
        return Convert((*globals_)[expr.index], expr.type);
      }
      return Value(expr.type);
    case Storage::kUniform: {
      if (getenv("GLANCEASY_DEBUG_UNIFORM")) {
        const Value uv = uniforms_ && expr.index >= 0
                             ? uniforms_[expr.index]
                             : Value(expr.type);
        fprintf(stderr, "[uniform] %s idx=%d %.3f %.3f %.3f %.3f\n",
                expr.text.empty() ? "?" : expr.text.c_str(), expr.index,
                uv.Data()[0], uv.Data()[1], uv.Data()[2], uv.Data()[3]);
      }
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
          const bool integer = expr.type.base() == BaseType::kInt ||
                               expr.type.base() == BaseType::kUInt;
          for (int i = 0; i < std::min(4, expr.type.components()); ++i) {
            value.data[i] = attributes_[expr.index * 4 + i];
            if (integer && attribute_int_)
              value.int_data[i] = attribute_int_[expr.index * 4 + i];
          }
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
      if (getenv("GLANCEASY_DEBUG_VIEWPORT")) {
        static int vd = 0;
        if (vd < 8) {
          fprintf(stderr, "[vineval] %s idx=%d size=%zu off=%d ptr=%p v0=%f\n",
                  expr.text.c_str(), expr.index, ast_.varyings.size(), offset,
                  (const void*)varyings_in_, varyings_in_ ? varyings_in_[0] : -1.0f);
          ++vd;
        }
      }
      if (varyings_in_ && offset >= 0) {
        const bool integer = expr.type.base() == BaseType::kInt ||
                             expr.type.base() == BaseType::kUInt;
        for (int i = 0; i < std::min(static_cast<int>(kValueFloats), expr.type.components()); ++i) {
          const float raw = varyings_in_[offset + i];
          if (integer) {
            memcpy(&value.IntData()[i], &raw, sizeof(raw));
            value.Data()[i] =
                expr.type.base() == BaseType::kInt
                    ? static_cast<float>(
                          *reinterpret_cast<const int32_t*>(&raw))
                    : static_cast<float>(
                          *reinterpret_cast<const uint32_t*>(&raw));
          } else {
            value.Data()[i] = raw;
          }
        }
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
        const bool integer = expr.type.base() == BaseType::kInt ||
                             expr.type.base() == BaseType::kUInt;
        for (int i = 0; i < std::min(static_cast<int>(kValueFloats), expr.type.components()); ++i) {
          const float raw = varyings_out_[offset + i];
          value.Data()[i] = raw;
          if (integer)
            memcpy(&value.IntData()[i], &raw, sizeof(raw));
        }
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
      if (expr.index >= 0 && expr.index < static_cast<int>(locals_.size())) {
        const int slot = local_base_ + expr.index;
        if (getenv("GLANCEASY_DEBUG_IDX") && expr.text == "sideEffectCounter")
          fprintf(stderr, "[idx] write local counter=%d int=%u\n",
                  (int)converted.Data()[0], converted.IntData()[0]);
        locals_[slot] = converted;
        if (converted.type.is_integer() && slot >= 0 &&
            slot < static_cast<int>(neighbor_x_.size())) {
          neighbor_x_[slot] = converted;
          neighbor_y_[slot] = converted;
        }
      }
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
        const bool integer = expr.type.base() == BaseType::kInt ||
                             expr.type.base() == BaseType::kUInt;
        for (int i = 0; i < std::min(static_cast<int>(kValueFloats), expr.type.components()); ++i) {
          if (integer) {
            memcpy(&varyings_out_[offset + i], &converted.IntData()[i],
                   sizeof(uint32_t));
          } else {
            varyings_out_[offset + i] = converted.Data()[i];
          }
        }
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
    if (getenv("GLANCEASY_DEBUG_INOUT") && target.text == "arr")
      fprintf(stderr, "[inout] assign arr idx=%d base=%d now=%.1f %.1f %.1f %.1f\n",
              target.index, local_base_, locals_[local_base_ + target.index].Data()[0],
              locals_[local_base_ + target.index].Data()[1],
              locals_[local_base_ + target.index].Data()[2],
              locals_[local_base_ + target.index].Data()[3]);
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
    if (getenv("GLANCEASY_DEBUG_ASSIGN"))
      fprintf(stderr, "[assignfield] text=%s len=%d base=%s b0=%.2f b1=%.2f b2=%.2f v0=%.2f v1=%.2f\n",
              target.text.c_str(), length, base.type.Name().c_str(), base.data[0],
              base.data[1], base.data[2], value.data[0], value.data[1]);
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
        const Value converted =
            Convert(MakeFloat(source), base.type.Element());
        base.data[component] = converted.data[0];
        if (base.type.base() == BaseType::kInt ||
            base.type.base() == BaseType::kUInt) {
          base.IntData()[component] = value.type.is_scalar()
                                          ? value.IntData()[0]
                                          : value.IntData()[i];
        }
        if (getenv("GLANCEASY_DEBUG_ASSIGN"))
          fprintf(stderr, "[assignw] i=%d comp=%d src=%.2f now=%.2f %.2f %.2f\n",
                  i, component, source, base.data[0], base.data[1], base.data[2]);
      }
    }
    if (getenv("GLANCEASY_DEBUG_ASSIGN"))
      fprintf(stderr, "[assignend] b0=%.2f b1=%.2f b2=%.2f\n", base.data[0],
              base.data[1], base.data[2]);
    Assign(*target.base, base);
    if (getenv("GLANCEASY_DEBUG_ASSIGN") && target.base->text == "color")
      fprintf(stderr, "[assignpost] color now=%.2f %.2f %.2f %.2f\n",
              Eval(*target.base).data[0], Eval(*target.base).data[1],
              Eval(*target.base).data[2], Eval(*target.base).data[3]);
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
      if (getenv("GLANCEASY_DEBUG_VARY"))
        fprintf(stderr, "[vary] idx=%d elem=%d base0=%.2f val0=%.2f\n", index,
                count, base.Data()[0], converted.Data()[0]);
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
      if (base.type.base() == BaseType::kInt ||
          base.type.base() == BaseType::kUInt)
        base.IntData()[index] = value.IntData()[0];
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
    out.data[i] = component < kValueFloats ? base.Data()[component] : 0.0f;
    out.IntData()[i] =
        component < kValueFloats ? base.IntData()[component] : 0u;
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
      for (int r = 0; r < rows && r < kValueFloats; ++r) {
        column.data[r] = base.Data()[index * rows + r];
        column.IntData()[r] = base.IntData()[index * rows + r];
      }
    }
    return column;
  }
  Value element(expr.type);
  if (index >= 0 && index < base.type.components()) {
    element.data[0] = base.Data()[index];
    element.IntData()[0] = base.IntData()[index];
  }
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
  if (expr.type.is_matrix()) {
    for (const ExprPtr& argument : expr.args) {
      const Value source = Eval(*argument);
      if (getenv("GLANCEASY_DEBUG_MAT"))
        fprintf(stderr, "[mat] dst=%dx%d src=%dx%d srcmat=%d\n",
                expr.type.columns(), expr.type.rows(), source.type.columns(),
                source.type.rows(), (int)source.type.is_matrix());
      if (!source.type.is_matrix())
        continue;
      Value out(expr.type);
      const int dst_rows = expr.type.rows();
      const int dst_cols = expr.type.columns();
      const int src_rows = source.type.rows();
      const int src_cols = source.type.columns();
      for (int c = 0; c < dst_cols; ++c) {
        for (int r = 0; r < dst_rows; ++r) {
          out.data[c * dst_rows + r] =
              (c < src_cols && r < src_rows)
                  ? source.Data()[c * src_rows + r]
                  : ((c == r) ? 1.0f : 0.0f);
        }
      }
      return out;
    }
  }
  float components[kValueFloats] = {0.0f};
  uint32_t int_components[kValueFloats] = {};
  int count = 0;
  const BaseType destination = expr.type.base();
  const auto append = [&](const Value& value, int component) {
    if (count >= kValueFloats)
      return;
    const BaseType source = value.type.base();
    const float raw = value.Data()[component];
    if (getenv("GLANCEASY_DEBUG_RES") && expr.type.Name() == "vec2" && count < 2)
      fprintf(stderr, "[vec2raw] cnt=%d src=%s raw=%.1f int=%u\n", count,
              value.type.Name().c_str(), raw, value.IntData()[component]);
    components[count] = ConvertComponent(source, destination, raw);
    if ((destination == BaseType::kInt || destination == BaseType::kUInt) &&
        (source == BaseType::kInt || source == BaseType::kUInt)) {
      int_components[count] = value.IntData()[component];
    } else if (destination == BaseType::kInt ||
               destination == BaseType::kUInt) {
      // Convert through the signed type for kInt so that negative values keep
      // their two's-complement bit pattern.  Casting a negative float straight
      // to uint32_t saturates to zero on some targets (VFP on ARM).
      int_components[count] =
          destination == BaseType::kInt
              ? static_cast<uint32_t>(static_cast<int32_t>(components[count]))
              : static_cast<uint32_t>(components[count]);
    }
    ++count;
  };
  for (const ExprPtr& argument : expr.args) {
    const Value value = Eval(*argument);
    if (value.type.is_matrix()) {
      const int total = value.type.components();
      for (int i = 0; i < total && count < kValueFloats; ++i)
        append(value, i);
      continue;
    }
    const int total = value.type.is_scalar() ? 1 : value.type.components();
    for (int i = 0; i < total && count < kValueFloats; ++i)
      append(value, value.type.is_scalar() ? 0 : i);
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
    for (int i = 0; i < expr.type.components(); ++i) {
      out.data[i] = components[0];
      out.IntData()[i] = int_components[0];
    }
    return out;
  }
  if (getenv("GLANCEASY_DEBUG_CTOR"))
    fprintf(stderr, "[ctor] type=%s count=%d c0=%.2f c1=%.2f\n",
            expr.type.Name().c_str(), count, components[0],
            count > 1 ? components[1] : 0.0f);
  const int total = expr.type.is_scalar() ? 1 : expr.type.components();
  for (int i = 0; i < total; ++i) {
    if (i < count) {
      out.data[i] = components[i];
      out.IntData()[i] = int_components[i];
    } else {
      out.data[i] = (i == 3) ? 1.0f : 0.0f;
      out.IntData()[i] = (i == 3) ? 1u : 0u;
    }
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
      if (expr.type.base() == BaseType::kInt) {
        const int32_t raw = static_cast<int32_t>(expr.integer_literal);
        value.data[0] = static_cast<float>(raw);
        value.int_data[0] = static_cast<uint32_t>(raw);
      } else if (expr.type.base() == BaseType::kUInt) {
        value.data[0] = static_cast<float>(expr.integer_literal);
        value.int_data[0] = static_cast<uint32_t>(expr.integer_literal);
      }
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
      struct IndexLValue {
        bool valid = false;
        Value base;
        int offset = 0;
        int count = 0;
      };
      std::vector<IndexLValue> index_lvalues(expr.args.size());
      const Function& call_callee = ast_.functions[expr.function_index];
      for (size_t a = 0; a < expr.args.size(); ++a) {
        const Expr& argument = *expr.args[a];
        if (a < call_callee.parameters.size() &&
            call_callee.parameters[a].parameter_qualifier != 0 &&
            argument.kind == Expr::Kind::kIndex) {
          IndexLValue& cache = index_lvalues[a];
          cache.base = Eval(*argument.base);
          const Value index_value = Eval(*argument.args[0]);
          const int index = static_cast<int>(ComponentAt(index_value, 0));
          cache.offset = index;
          cache.count = argument.type.components();
          if (cache.base.type.is_array())
            cache.offset = index * cache.base.type.array_element().components();
          else if (cache.base.type.is_matrix())
            cache.offset = index * cache.base.type.rows();
          Value value(argument.type);
          for (int i = 0; i < cache.count && cache.offset + i < cache.base.size();
               ++i)
            value.Data()[i] = cache.base.Data()[cache.offset + i];
          args.push_back(value);
          cache.valid = true;
          continue;
        }
        args.push_back(Eval(argument));
      }
      std::vector<Value> out_values;
      Value result =
          CallUserFunction(expr.function_index, args, &out_values);
      const Function& callee = ast_.functions[expr.function_index];
      for (size_t i = 0; i < out_values.size() &&
                      i < expr.args.size() && i < callee.parameters.size();
           ++i) {
      if (getenv("GLANCEASY_DEBUG_INOUT") && expr.text == "func")
        fprintf(stderr, "[inout] out%zu target=%s type=%s val=%.1f %.1f %.1f %.1f\n",
                i, expr.args[i]->text.c_str(), expr.args[i]->type.Name().c_str(),
                out_values[i].Data()[0], out_values[i].Data()[1],
                out_values[i].Data()[2], out_values[i].Data()[3]);
        if (callee.parameters[i].parameter_qualifier != 0) {
          if (index_lvalues[i].valid) {
            WriteSlice(&index_lvalues[i].base, index_lvalues[i].offset,
                       out_values[i], index_lvalues[i].count);
            Assign(*expr.args[i]->base, index_lvalues[i].base);
          } else {
            Assign(*expr.args[i], out_values[i]);
          }
        }
      }
      return result;
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
                                  const std::vector<Value>& args,
                                  std::vector<Value>* out_values) {
  if (function_index < 0 ||
      function_index >= static_cast<int>(ast_.functions.size())) {
    Fail("invalid function index");
    return Value();
  }
  if (getenv("GLANCEASY_DEBUG_INOUT") && ast_.functions[function_index].name == "test")
    for (size_t i = 0; i < args.size(); ++i)
      fprintf(stderr, "[inout] test-arg%zu = %.1f %.1f %.1f %.1f\n", i,
              args[i].Data()[0], args[i].Data()[1], args[i].Data()[2],
              args[i].Data()[3]);
  Value result;
  CallFunction(ast_.functions[function_index], args, &result, out_values);
  return result;
}

Value Evaluator::EvalUnary(const Expr& expr) {
  const Op op = expr.op;
  if (op == Op::kPreIncrement || op == Op::kPreDecrement ||
      op == Op::kPostIncrement || op == Op::kPostDecrement) {
    const Expr& target = *expr.args[0];
    if (target.kind == Expr::Kind::kIndex) {
      Value base = Eval(*target.base);
      const Value index_value = Eval(*target.args[0]);
      const int index = static_cast<int>(ComponentAt(index_value, 0));
      if (getenv("GLANCEASY_DEBUG_IDX"))
        fprintf(stderr, "[idx] ++ target=%s base=%s index=%d\n",
                target.base->text.c_str(), target.base->type.Name().c_str(),
                index);
      int offset = index;
      int count = target.type.components();
      if (base.type.is_array())
        offset = index * base.type.array_element().components();
      else if (base.type.is_matrix())
        offset = index * base.type.rows();
      Value old(target.type);
      for (int i = 0; i < count && offset + i < base.size(); ++i)
        old.Data()[i] = base.Data()[offset + i];
      Value next(expr.type);
      const bool increment =
          op == Op::kPreIncrement || op == Op::kPostIncrement;
      for (int i = 0; i < expr.type.components(); ++i) {
        next.Data()[i] = old.Data()[i] + (increment ? 1.0f : -1.0f);
        if (expr.type.is_integer())
          next.IntData()[i] = old.IntData()[i] +
                              (increment ? 1u : static_cast<uint32_t>(-1));
      }
      WriteSlice(&base, offset, next, count);
      Assign(*target.base, base);
      return (op == Op::kPostIncrement || op == Op::kPostDecrement) ? old
                                                                    : next;
    }
    const Value old = Eval(target);
    Value next(expr.type);
    const bool increment = op == Op::kPreIncrement || op == Op::kPostIncrement;
    for (int i = 0; i < expr.type.components(); ++i) {
      next.data[i] = old.data[i] + (increment ? 1.0f : -1.0f);
      if (expr.type.is_integer())
        next.IntData()[i] =
            old.IntData()[i] + (increment ? 1u : static_cast<uint32_t>(-1));
    }
    Assign(target, next);
    return (op == Op::kPostIncrement || op == Op::kPostDecrement) ? old : next;
  }

  const Value operand = Eval(*expr.args[0]);
  if (op == Op::kNot)
    return MakeBool(!IsTrue(operand));
  if (op == Op::kBitNot) {
    Value out(expr.type);
    const int count = expr.type.components();
    const bool is_uint = expr.type.base() == BaseType::kUInt;
    for (int i = 0; i < count; ++i) {
      const uint32_t v = ~operand.int_data[i];
      out.int_data[i] = v;
      out.data[i] = is_uint ? static_cast<float>(v)
                            : static_cast<float>(static_cast<int32_t>(v));
    }
    return out;
  }
  if (op == Op::kNegate) {
    Value out(expr.type);
    const int count = expr.type.components();
    const BaseType base = expr.type.base();
    for (int i = 0; i < count; ++i) {
      if (base == BaseType::kInt) {
        const uint32_t v =
            static_cast<uint32_t>(-static_cast<int32_t>(operand.int_data[i]));
        out.int_data[i] = v;
        out.data[i] = static_cast<float>(static_cast<int32_t>(v));
      } else if (base == BaseType::kUInt) {
        const uint32_t v = 0u - operand.int_data[i];
        out.int_data[i] = v;
        out.data[i] = static_cast<float>(v);
      } else {
        out.data[i] = -operand.data[i];
      }
    }
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
    if (right.type.is_scalar()) {
      Value out(left.type);
      for (int i = 0; i < left.size(); ++i)
        out.data[i] = left.data[i] * right.data[0];
      return out;
    }
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
    if (left.type.is_scalar()) {
      Value out(right.type);
      for (int i = 0; i < right.size(); ++i)
        out.data[i] = left.data[0] * right.data[i];
      return out;
    }
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
      if (is_int) {
        return MapIntPairs(left, right, result_type,
                           [](uint32_t a, uint32_t b, bool) { return a + b; });
      }
      return MapPairs(left, right, result_type,
                      [](float a, float b) { return a + b; });
    case Op::kSubtract:
      if (is_int) {
        return MapIntPairs(left, right, result_type,
                           [](uint32_t a, uint32_t b, bool) { return a - b; });
      }
      return MapPairs(left, right, result_type,
                      [](float a, float b) { return a - b; });
    case Op::kMultiply:
      if (is_int) {
        return MapIntPairs(left, right, result_type,
                           [](uint32_t a, uint32_t b, bool) { return a * b; });
      }
      return Multiply(left, right, result_type);
    case Op::kDivide:
      if (is_int) {
        return MapIntPairs(left, right, result_type,
                           [](uint32_t a, uint32_t b, bool is_uint) {
                             if (is_uint)
                               return b == 0 ? 0u : a / b;
                             const int32_t divisor = static_cast<int32_t>(b);
                             return divisor == 0
                                        ? 0u
                                        : static_cast<uint32_t>(
                                              static_cast<int32_t>(a) /
                                              divisor);
                           });
      }
      return MapPairs(left, right, result_type,
                      [](float a, float b) { return b == 0.0f ? 0.0f : a / b; });
    case Op::kModulo:
      if (is_int) {
        return MapIntPairs(left, right, result_type,
                           [](uint32_t a, uint32_t b, bool is_uint) {
                             if (is_uint)
                               return b == 0 ? 0u : a % b;
                             const int32_t divisor = static_cast<int32_t>(b);
                             return divisor == 0
                                        ? 0u
                                        : static_cast<uint32_t>(
                                              static_cast<int32_t>(a) %
                                              divisor);
                           });
      }
      return MapPairs(left, right, result_type,
                      [](float a, float b) {
                        return a - b * std::floor(a / b);
                      });
    case Op::kShiftLeft:
    case Op::kShiftRight:
      if (getenv("GLANCEASY_DEBUG_SHIFT")) {
        fprintf(stderr, "[shift] op=%d lt=%s rt=%s a=%.1f %.1f b=%.1f %.1f isu=%d\n",
                (int)op, left.type.Name().c_str(), right.type.Name().c_str(),
                left.data[0], left.data[1], right.data[0], right.data[1],
                (int)is_uint);
      }
    case Op::kBitAnd:
    case Op::kBitXor:
    case Op::kBitOr:
      return MapIntPairs(left, right, result_type, [op, is_uint](
                                                      uint32_t x, uint32_t y_in,
                                                      bool) {
        const uint32_t y = y_in & 31u;
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
            value = x & y_in;
            break;
          case Op::kBitXor:
            value = x ^ y_in;
            break;
          case Op::kBitOr:
            value = x | y_in;
            break;
          default:
            break;
        }
        if (getenv("GLANCEASY_DEBUG_SHIFT"))
          fprintf(stderr, "[shiftout] x=%u y=%u v=%u isu=%d f=%.1f\n", x, y,
                  value, (int)is_uint,
                  is_uint ? static_cast<float>(value)
                          : static_cast<float>(static_cast<int32_t>(value)));
        return value;
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
  if (getenv("GLANCEASY_DEBUG_RES") && target.text == "res")
    fprintf(stderr, "[resassign] data=%.1f %.1f ints=%u %u\n", converted.data[0],
            converted.data[1], converted.IntData()[0], converted.IntData()[1]);
  Assign(target, converted);
  if (getenv("GLANCEASY_DEBUG_RES") && target.text == "res")
    fprintf(stderr, "[respost] data=%.1f %.1f ints=%u %u\n", Eval(target).data[0],
            Eval(target).data[1], Eval(target).IntData()[0], Eval(target).IntData()[1]);
  return converted;
}

namespace {

// Writes a sampled texel into |out| honouring the result base type: integer
// samplers keep an exact 32-bit payload next to the float mirror.
void StoreTextureResult(Value& out,
                        const float rgba[4],
                        bool int_result,
                        bool uint_result) {
  for (int i = 0; i < out.size() && i < 4; ++i) {
    if (int_result) {
      const int32_t value = static_cast<int32_t>(rgba[i]);
      out.int_data[i] = static_cast<uint32_t>(value);
      out.data[i] = static_cast<float>(value);
    } else if (uint_result) {
      const uint32_t value =
          static_cast<uint32_t>(rgba[i] < 0.0f ? 0.0f : rgba[i]);
      out.int_data[i] = value;
      out.data[i] = static_cast<float>(value);
    } else {
      out.data[i] = rgba[i];
    }
  }
}

bool IsIntegerSampler(const Type& type) {
  switch (type.base()) {
    case BaseType::kISampler2D:
    case BaseType::kISampler3D:
    case BaseType::kISamplerCube:
    case BaseType::kISampler2DArray:
      return true;
    default:
      return false;
  }
}

bool IsUnsignedSampler(const Type& type) {
  switch (type.base()) {
    case BaseType::kUSampler2D:
    case BaseType::kUSampler3D:
    case BaseType::kUSamplerCube:
    case BaseType::kUSampler2DArray:
      return true;
    default:
      return false;
  }
}

}  // namespace

Value Evaluator::BuiltinTexture(const Expr& expr) {
  std::vector<Value> args;
  args.reserve(expr.args.size());
  for (const ExprPtr& argument : expr.args)
    args.push_back(Eval(*argument));

  const BuiltinFunc function = expr.builtin_func;
  const BaseType base =
      args.empty() ? BaseType::kFloat : args[0].type.base();
  const bool int_result = !args.empty() && IsIntegerSampler(args[0].type);
  const bool uint_result = !args.empty() && IsUnsignedSampler(args[0].type);

  Value out(expr.type);
  for (int i = 0; i < out.size() && i < 4; ++i) {
    out.data[i] = 0.0f;
    out.int_data[i] = 0u;
  }
  if (out.size() > 3) {
    out.data[3] = 1.0f;
    out.int_data[3] = int_result || uint_result ? 1u : 0x3F800000u;
  }
  if (args.size() < 2 || !sampler_)
    return out;

  const int unit = static_cast<int>(args[0].data[0]);
  const bool cube = base == BaseType::kSamplerCube ||
                    base == BaseType::kISamplerCube ||
                    base == BaseType::kUSamplerCube ||
                    base == BaseType::kSamplerCubeShadow;
  const bool volume = base == BaseType::kSampler3D ||
                      base == BaseType::kISampler3D ||
                      base == BaseType::kUSampler3D;
  const bool array = base == BaseType::kSampler2DArray ||
                     base == BaseType::kISampler2DArray ||
                     base == BaseType::kUSampler2DArray ||
                     base == BaseType::kSampler2DArrayShadow;
  const bool shadow2d = base == BaseType::kSampler2DShadow;
  const bool shadow_cube = base == BaseType::kSamplerCubeShadow;
  const bool shadow_array = base == BaseType::kSampler2DArrayShadow;
  const bool proj = function == BuiltinFunc::kTexture2DProj ||
                    function == BuiltinFunc::kTextureProj ||
                    function == BuiltinFunc::kTextureProjLod ||
                    function == BuiltinFunc::kTextureProjOffset ||
                    function == BuiltinFunc::kTextureProjLodOffset ||
                    function == BuiltinFunc::kTextureProjGrad ||
                    function == BuiltinFunc::kTextureProjGradOffset;
  const bool has_offset = function == BuiltinFunc::kTextureOffset ||
                          function == BuiltinFunc::kTextureLodOffset ||
                          function == BuiltinFunc::kTextureGradOffset ||
                          function == BuiltinFunc::kTextureProjOffset ||
                          function == BuiltinFunc::kTextureProjLodOffset ||
                          function == BuiltinFunc::kTextureProjGradOffset ||
                          function == BuiltinFunc::kTexelFetchOffset;
  const bool has_grad = function == BuiltinFunc::kTextureGrad ||
                        function == BuiltinFunc::kTextureGradOffset ||
                        function == BuiltinFunc::kTextureProjGrad ||
                        function == BuiltinFunc::kTextureProjGradOffset;
  const bool explicit_lod = function == BuiltinFunc::kTextureLod ||
                            function == BuiltinFunc::kTextureLodOffset ||
                            function == BuiltinFunc::kTextureProjLod ||
                            function == BuiltinFunc::kTextureProjLodOffset;

  if (function == BuiltinFunc::kTexelFetch ||
      function == BuiltinFunc::kTexelFetchOffset) {
    const int dims = volume || array ? 3 : 2;
    int position[3] = {0, 0, 0};
    for (int i = 0; i < dims && i < 3; ++i)
      position[i] = static_cast<int32_t>(args[1].IntData()[i]);
    int level = 0;
    int offset[3] = {0, 0, 0};
    std::size_t next = 2;
    if (args.size() > next) {
      level = static_cast<int32_t>(args[next].IntData()[0]);
      ++next;
    }
    if (has_offset && args.size() > next) {
      const Value& delta = args[next];
      const int offset_dims = array ? 2 : dims;
      for (int i = 0; i < offset_dims && i < 3; ++i)
        offset[i] = static_cast<int32_t>(delta.IntData()[i]);
    }
    float rgba[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (volume)
      sampler_->Fetch3D(unit, position[0] + offset[0],
                        position[1] + offset[1], position[2] + offset[2],
                        level, rgba);
    else if (array)
      sampler_->Fetch2DArray(unit, position[0] + offset[0],
                             position[1] + offset[1], position[2], level,
                             rgba);
    else
      sampler_->Fetch2D(unit, position[0] + offset[0],
                        position[1] + offset[1], level, rgba);
    StoreTextureResult(out, rgba, int_result, uint_result);
    return out;
  }

  float coords[3] = {0.0f, 0.0f, 0.0f};
  coords[0] = args[1].data[0];
  coords[1] = args[1].data[1];
  if (args[1].type.components() > 2)
    coords[2] = args[1].data[2];

  if (proj) {
    // ES3 textureProj family: divide the coordinate by its last component;
    // the remaining lookup then follows the regular sampling path.  A shadow
    // lookup carries the reference value in the third component, which is
    // divided as well.
    const int ndim = volume ? 3 : 2;
    const int comps = args[1].type.components();
    const float w = comps > 0 ? args[1].data[comps - 1] : 1.0f;
    for (int i = 0; i < ndim && i < comps - 1; ++i)
      coords[i] = w != 0.0f ? coords[i] / w : 0.0f;
    if (shadow2d && comps > 3)
      coords[2] = w != 0.0f ? args[1].data[2] / w : 0.0f;
  }

  int size_base[3] = {1, 1, 1};
  sampler_->Size(unit, size_base);
  int texel_offset[3] = {0, 0, 0};
  int offset_index = -1;
  if (has_offset) {
    // The offset is an integer vector argument; with an implicit-LOD bias the
    // bias comes last, so scan instead of assuming a fixed position.
    for (int i = static_cast<int>(args.size()) - 1; i >= 2; --i) {
      const Type& type = args[i].type;
      if (!type.is_scalar() &&
          (type.base() == BaseType::kInt || type.base() == BaseType::kUInt)) {
        offset_index = i;
        break;
      }
    }
    if (offset_index >= 0) {
      const Value& delta = args[offset_index];
      const int offset_dims = volume ? 3 : (cube ? 0 : 2);
      for (int i = 0; i < offset_dims && i < 3; ++i)
        texel_offset[i] = static_cast<int32_t>(delta.IntData()[i]);
    }
  }
  float bias = 0.0f;
  if (!explicit_lod && !has_grad && args.size() > 2) {
    const Value& last = args.back();
    if (last.type.is_scalar() && last.type.base() == BaseType::kFloat)
      bias = last.data[0];
  }

  float lod = 0.0f;
  if (has_grad) {
    // Spec 3.00 section 8.8: lod = log2(max(||dPdx||, ||dPdy||)) in texel
    // units, the same magnitude the implicit-derivative path produces.
    const Value& dpx = args[2];
    const Value& dpy = args[3];
    const int dims = array || cube ? 2 : (volume ? 3 : 2);
    float rho_dx = 0.0f;
    float rho_dy = 0.0f;
    for (int i = 0; i < dims && i < 3; ++i) {
      const float scale = static_cast<float>(size_base[i]) *
                          (cube ? 0.5f : 1.0f);
      const float dx = dpx.data[i] * scale;
      const float dy = dpy.data[i] * scale;
      rho_dx += dx * dx;
      rho_dy += dy * dy;
    }
    const float rho = std::sqrt(std::max(rho_dx, rho_dy));
    if (rho > 0.0f)
      lod = std::log2(rho);
  } else if (explicit_lod) {
    const int index = 2;
    if (args.size() > index)
      lod = args[index].data[0];
  }

  if (getenv("GLANCEASY_DEBUG_LOD"))
    fprintf(stderr, "[lod2] explicit=%d dx=%p kind=%d args=%zu\n", (int)explicit_lod,
            (const void*)varyings_dx_, (int)expr.args[1]->kind, expr.args.size());
  if (!explicit_lod && !has_grad && varyings_dx_ && varyings_dy_) {
    const Expr& coord_expr = *expr.args[1];
    int varying_index = -1;
    int coordinate_dims = 0;
    if (coord_expr.kind == Expr::Kind::kVariable) {
      varying_index = coord_expr.index;
      coordinate_dims = coord_expr.type.components();
    } else if (coord_expr.kind == Expr::Kind::kCall &&
               coord_expr.is_constructor) {
      // Constructed coordinates such as vec3(v_coord, u_depth): the first
      // varying argument carries the screen-space derivatives.  The remaining
      // components (for example a uniform depth) are constant across the
      // primitive and contribute nothing to the LOD.
      for (const ExprPtr& argument : coord_expr.args) {
        if (argument->kind == Expr::Kind::kVariable &&
            argument->index >= 0 &&
            argument->index < static_cast<int>(ast_.varyings.size())) {
          varying_index = argument->index;
          coordinate_dims = argument->type.components();
          break;
        }
      }
    }
    if (varying_index >= 0 &&
        varying_index < static_cast<int>(ast_.varyings.size())) {
      const int offset = ast_.varyings[varying_index].varying_offset;
      if (cube) {
        size_base[1] = size_base[0];
        size_base[2] = size_base[0];
      }
      const int dims = array ? 2 : std::min(coordinate_dims, 3);
      // For a projective lookup the derivative must be taken on the projected
      // coordinate, so the neighbouring sample's varying is divided by its own
      // last component before differencing.
      const int comps = args[1].type.components();
      const float w_current = comps > 0 ? args[1].data[comps - 1] : 1.0f;
      const float w_dx = varyings_dx_[offset + comps - 1];
      const float w_dy = varyings_dy_[offset + comps - 1];
      const auto project = [&](float value, float w) {
        return (proj && w != 0.0f) ? value / w : value;
      };
      if (cube) {
        // Cube LOD is computed from derivatives of the projected face
        // coordinates, matching the Khronos reference sampler.  Differencing
        // the raw direction and summing all three components overestimates
        // the footprint once the ray leaves the base face.
        float c[3];
        float dcx[3];
        float dcy[3];
        for (int i = 0; i < 3; ++i) {
          c[i] = project(args[1].data[i], w_current);
          dcx[i] = project(varyings_dx_[offset + i], w_dx) - c[i];
          dcy[i] = project(varyings_dy_[offset + i], w_dy) - c[i];
        }
        const float ax = std::fabs(c[0]);
        const float ay = std::fabs(c[1]);
        const float az = std::fabs(c[2]);
        int ma_ndx = 0;
        int s_ndx = 2;
        int t_ndx = 1;
        if (ay < ax && az < ax) {
          ma_ndx = 0; s_ndx = 2; t_ndx = 1;
        } else if (ax < ay && az < ay) {
          ma_ndx = 1; s_ndx = 0; t_ndx = 2;
        } else if (ax < az && ay < az) {
          ma_ndx = 2; s_ndx = 0; t_ndx = 1;
        } else if (ax == ay) {
          if (ax < az) { ma_ndx = 2; s_ndx = 0; t_ndx = 1; }
          else { ma_ndx = 0; s_ndx = 2; t_ndx = 1; }
        } else if (ax == az) {
          if (az < ay) { ma_ndx = 1; s_ndx = 0; t_ndx = 2; }
          else { ma_ndx = 2; s_ndx = 0; t_ndx = 1; }
        } else {
          if (ay < ax) { ma_ndx = 0; s_ndx = 2; t_ndx = 1; }
          else { ma_ndx = 1; s_ndx = 0; t_ndx = 2; }
        }
        const float ma = std::max(1e-6f, std::fabs(c[ma_ndx]));
        const float face_size = static_cast<float>(size_base[0]);
        const float dudx = face_size * 0.5f *
                           (dcx[s_ndx] * ma - c[s_ndx] * dcx[ma_ndx]) /
                           (ma * ma);
        const float dvdx = face_size * 0.5f *
                           (dcx[t_ndx] * ma - c[t_ndx] * dcx[ma_ndx]) /
                           (ma * ma);
        const float dudy = face_size * 0.5f *
                           (dcy[s_ndx] * ma - c[s_ndx] * dcy[ma_ndx]) /
                           (ma * ma);
        const float dvdy = face_size * 0.5f *
                           (dcy[t_ndx] * ma - c[t_ndx] * dcy[ma_ndx]) /
                           (ma * ma);
        const float rho =
            std::max(std::sqrt(dudx * dudx + dvdx * dvdx),
                     std::sqrt(dudy * dudy + dvdy * dvdy));
        if (getenv("GLANCEASY_DEBUG_CUBE_LOD") && rho > 0.0f) {
          static int cld = 0;
          if (cld++ < 3)
            fprintf(stderr,
                    "[cubelod] c=%.3f,%.3f,%.3f dcx=%.5f,%.5f,%.5f dudx=%.5f "
                    "dvdx=%.5f rho=%.5f lod=%.4f\n",
                    c[0], c[1], c[2], dcx[0], dcx[1], dcx[2], dudx, dvdx,
                    rho, std::log2(rho));
        }
        if (rho > 0.0f)
          lod += std::log2(rho);
      } else {
        float rho_dx = 0.0f;
        float rho_dy = 0.0f;
        for (int i = 0; i < dims; ++i) {
          const float scale = static_cast<float>(size_base[i]);
          const float current_coord = project(args[1].data[i], w_current);
          const float dx =
              (project(varyings_dx_[offset + i], w_dx) - current_coord) * scale;
          const float dy =
              (project(varyings_dy_[offset + i], w_dy) - current_coord) * scale;
          if (getenv("GLANCEASY_DEBUG_DERIV")) {
            static int ddbg = 0;
            if (ddbg++ < 16)
              fprintf(stderr,
                      "[deriv] i=%d cur=%.5f nx=%.5f ny=%.5f dx=%.5f dy=%.5f "
                      "scale=%.1f\n",
                      i, current_coord, varyings_dx_[offset + i],
                      varyings_dy_[offset + i], dx, dy, scale);
          }
          rho_dx += dx * dx;
          rho_dy += dy * dy;
        }
        const float rho = std::sqrt(std::max(rho_dx, rho_dy));
        if (rho > 0.0f)
          lod += std::log2(rho);
      }
      if (getenv("GLANCEASY_NO_LOD"))
        lod = 0.0f;
      if (getenv("GLANCEASY_DEBUG_LOD"))
        fprintf(stderr,
                "[lod] var=%d off=%d dims=%d dx=%.6f,%.6f dy=%.6f,%.6f "
                "in=%.6f,%.6f cur=%.6f,%.6f lod=%.4f\n",
                varying_index, offset, dims, varyings_dx_[offset],
                varyings_dx_[offset + 1], varyings_dy_[offset],
                varyings_dy_[offset + 1], varyings_in_[offset],
                varyings_in_[offset + 1], args[1].data[0], args[1].data[1],
                lod);
    }
  }

  // Debug override: force the computed LOD before the bias is applied.
  if (const char* forced = getenv("GLANCEASY_LOD_FORCE"))
    lod = static_cast<float>(atof(forced));

  // Implicit-LOD bias, applied to the level chosen by the derivative path.
  if (getenv("GLANCEASY_NO_BIAS"))
    bias = 0.0f;
  lod += bias;
  if (getenv("GLANCEASY_DEBUG_LOD"))
    fprintf(stderr, "[bias] bias=%.4f final=%.4f\n", bias, lod);

  // Texel offsets were specified in base-level texels but the reference
  // sampler applies them in the sampled level's texel grid, so convert them
  // with the level that the mip selection will land on.
  const int offset_axes = volume ? 3 : (cube ? 0 : 2);
  if (has_offset) {
    int level = 0;
    int level_size[3] = {1, 1, 1};
    if (offset_axes > 0) {
      const uint32_t min_filter = sampler_->MinFilter(unit);
      const bool mipmapped = min_filter >= 0x2700u && min_filter <= 0x2703u;
      if (mipmapped) {
        level = static_cast<int>(std::ceil(lod + 0.5f)) - 1;
        level = std::max(0, level);
      }
      sampler_->SizeLod(unit, level, level_size);
      if (getenv("GLANCEASY_DEBUG_LOD"))
        fprintf(stderr, "[offlod] lod=%.3f level=%d size=%d,%d,%d\n", lod,
                level, level_size[0], level_size[1], level_size[2]);
      for (int i = 0; i < offset_axes; ++i) {
        float shift = static_cast<float>(texel_offset[i]);
        if (i == 1 && getenv("GLANCEASY_OFF_FLIP_Y"))
          shift = -shift;
        if (shift != 0.0f && level_size[i] > 0)
          coords[i] += shift / static_cast<float>(level_size[i]);
      }
    }
  }

  float rgba[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  if (shadow2d) {
    sampler_->Sample2DShadow(unit, coords[0], coords[1], coords[2], lod,
                             rgba);
  } else if (shadow_array) {
    sampler_->Sample2DArrayShadow(unit, coords[0], coords[1], args[1].data[2],
                                  args[1].data[3], lod, rgba);
  } else if (shadow_cube) {
    float direction[3] = {args[1].data[0], args[1].data[1], args[1].data[2]};
    sampler_->SampleCubeShadow(unit, direction, args[1].data[3], lod, rgba);
  } else if (cube) {
    float direction[3] = {coords[0], coords[1], coords[2]};
    sampler_->SampleCube(unit, direction, lod, rgba);
  } else if (volume) {
    // Volume offsets were already folded into |coords| above.
    float position[3] = {coords[0], coords[1], coords[2]};
    sampler_->Sample3D(unit, position, lod, rgba);
  } else if (array) {
    sampler_->Sample2DArray(unit, coords[0], coords[1], args[1].data[2], lod,
                            rgba);
  } else {
    sampler_->Sample2D(unit, coords[0], coords[1], lod, rgba);
    if (getenv("GLANCEASY_DEBUG_TEXOUT")) {
      static int od = 0;
      if (od++ < 3)
        fprintf(stderr, "[texout] unit=%d %.4f %.4f %.4f %.4f type=%s\n", unit,
                rgba[0], rgba[1], rgba[2], rgba[3],
                out.type.Name().c_str());
    }
  }
  StoreTextureResult(out, rgba, int_result, uint_result);
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
    case BuiltinFunc::kSinh:
      return MapScalars(a, type, [](float v) {
        const float e = std::exp(v);
        return (e - std::exp(-v)) * 0.5f;
      });
    case BuiltinFunc::kCosh:
      return MapScalars(a, type, [](float v) {
        const float e = std::exp(v);
        return (e + std::exp(-v)) * 0.5f;
      });
    case BuiltinFunc::kTanh:
      return MapScalars(a, type, [](float v) { return std::tanh(v); });
    case BuiltinFunc::kAsin:
      return MapScalars(a, type, [](float v) { return fastmath::Asin(v); });
    case BuiltinFunc::kAcos:
      return MapScalars(a, type, [](float v) { return fastmath::Acos(v); });
    case BuiltinFunc::kAsinh:
      return MapScalars(a, type, [](float v) { return std::asinh(v); });
    case BuiltinFunc::kAcosh:
      return MapScalars(a, type, [](float v) { return std::acosh(v); });
    case BuiltinFunc::kAtanh:
      return MapScalars(a, type, [](float v) { return std::atanh(v); });
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
    case BuiltinFunc::kSign: {
      const bool is_abs = function == BuiltinFunc::kAbs;
      if (a.type.is_integer()) {
        Value out(type);
        for (int i = 0; i < type.components(); ++i) {
          if (type.base() == BaseType::kUInt) {
            const uint32_t v = a.IntData()[i];
            out.IntData()[i] = is_abs ? v : (v != 0u ? 1u : 0u);
            out.data[i] = static_cast<float>(out.IntData()[i]);
          } else {
            const int32_t v = static_cast<int32_t>(a.IntData()[i]);
            const int32_t r = is_abs ? (v < 0 ? -v : v)
                                     : (v > 0 ? 1 : (v < 0 ? -1 : 0));
            out.IntData()[i] = static_cast<uint32_t>(r);
            out.data[i] = static_cast<float>(r);
          }
        }
        return out;
      }
      return MapScalars(a, type, [is_abs](float v) {
        return is_abs ? std::fabs(v)
                                                     : (v > 0.0f ? 1.0f
                                                        : (v < 0.0f ? -1.0f
                                                                    : 0.0f));
      });
    }
    case BuiltinFunc::kFloor:
      return MapScalars(a, type, [](float v) { return std::floor(v); });
    case BuiltinFunc::kCeil:
      return MapScalars(a, type, [](float v) { return std::ceil(v); });
    case BuiltinFunc::kTrunc:
      return MapScalars(a, type, [](float v) { return std::trunc(v); });
    case BuiltinFunc::kRound:
      return MapScalars(a, type, [](float v) { return std::round(v); });
    case BuiltinFunc::kRoundEven:
      return MapScalars(a, type, [](float v) { return std::nearbyint(v); });
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
      return MapPairs(args[0], args[1], args.back().type,
                      [](float edge, float x) {
                        return x < edge ? 0.0f : 1.0f;
                      });
    case BuiltinFunc::kSmoothStep: {
      const Type& result_type = args.back().type;
      Value out_smooth = MapTriples(args[0], args[1], args[2], result_type,
                        [](float edge0, float edge1, float x) {
                          const float denom = edge1 - edge0;
                          float t = denom == 0.0f ? 0.0f : (x - edge0) / denom;
                          t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
                          return t * t * (3.0f - 2.0f * t);
                        });
      return out_smooth;
    }
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
    case BuiltinFunc::kOuterProduct: {
      // GLSL: outerProduct(a, b) yields rows = size(a), cols = size(b) with
      // res[col][row] = a[row] * b[col] (b scales the columns of a).
      const int columns = args[1].type.rows();
      const int rows = args[0].type.rows();
      Value out(Type::Mat(columns, rows));
      for (int c = 0; c < columns; ++c) {
        for (int r = 0; r < rows; ++r) {
          const float value = args[0].data[r] * args[1].data[c];
          out.data[c * rows + r] = std::isnan(value) ? 0.0f : value;
        }
      }
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
      expr.builtin_func == BuiltinFunc::kTextureLod ||
      expr.builtin_func == BuiltinFunc::kTextureProj ||
      expr.builtin_func == BuiltinFunc::kTextureProjLod ||
      expr.builtin_func == BuiltinFunc::kTextureOffset ||
      expr.builtin_func == BuiltinFunc::kTextureLodOffset ||
      expr.builtin_func == BuiltinFunc::kTextureGrad ||
      expr.builtin_func == BuiltinFunc::kTextureGradOffset ||
      expr.builtin_func == BuiltinFunc::kTextureProjOffset ||
      expr.builtin_func == BuiltinFunc::kTextureProjLodOffset ||
      expr.builtin_func == BuiltinFunc::kTextureProjGrad ||
      expr.builtin_func == BuiltinFunc::kTextureProjGradOffset ||
      expr.builtin_func == BuiltinFunc::kTexelFetch ||
      expr.builtin_func == BuiltinFunc::kTexelFetchOffset) {
    return BuiltinTexture(expr);
  }

  if (expr.builtin_func == BuiltinFunc::kDfdx ||
      expr.builtin_func == BuiltinFunc::kDfdy ||
      expr.builtin_func == BuiltinFunc::kFwidth) {
    const Value current = Eval(*expr.args[0]);
    const float* neighbor = expr.builtin_func == BuiltinFunc::kDfdy
                                ? varyings_dy_
                                : varyings_dx_;
    Value out(expr.type);
    if (!neighbor)
      return out;
    const float* saved = varyings_in_;
    const int saved_mode = neighbor_mode_;
    varyings_in_ = neighbor;
    neighbor_mode_ = expr.builtin_func == BuiltinFunc::kDfdy ? 2 : 1;
    const Value adjacent = Eval(*expr.args[0]);
    neighbor_mode_ = saved_mode;
    varyings_in_ = saved;
    if (getenv("GLANCEASY_DEBUG_DER")) {
      static int dbg = 0;
      if (dbg < 12) {
        fprintf(stderr, "[der] %s cur=%.4f adj=%.4f\n",
                expr.args[0]->text.c_str(), current.Data()[0],
                adjacent.Data()[0]);
        ++dbg;
      }
    }
    for (int i = 0; i < expr.type.components(); ++i) {
      const float dx = adjacent.data[i] - current.data[i];
      if (expr.builtin_func == BuiltinFunc::kFwidth) {
        const float* other = varyings_dy_;
        varyings_in_ = other;
        neighbor_mode_ = 2;
        const Value adjacent_y = Eval(*expr.args[0]);
        neighbor_mode_ = saved_mode;
        varyings_in_ = saved;
        out.data[i] = std::fabs(dx) + std::fabs(adjacent_y.data[i] - current.data[i]);
      } else {
        out.data[i] = dx;
      }
    }
    return out;
  }

  std::vector<Value> args;
  args.reserve(expr.args.size());
  for (const ExprPtr& argument : expr.args)
    args.push_back(Eval(*argument));

  switch (expr.builtin_func) {
    case BuiltinFunc::kModf: {
      const Value x = Convert(args[0], expr.type);
      Value integral(expr.type);
      Value fraction(expr.type);
      for (int i = 0; i < expr.type.components(); ++i)
        fraction.data[i] = std::modf(x.data[i], &integral.data[i]);
      if (expr.args.size() > 1)
        Assign(*expr.args[1], Convert(integral, expr.args[1]->type));
      return fraction;
    }
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
      int level = 0;
      if (args.size() > 1)
        level = static_cast<int32_t>(args[1].IntData()[0]);
      if (sampler_)
        sampler_->SizeLod(unit, level, size);
      if (getenv("GLANCEASY_DEBUG_TEX")) {
        static int ts_dbg = 0;
        if (ts_dbg++ < 40)
          fprintf(stderr, "[tsize] unit=%d lod=%d out=%d,%d,%d comps=%d\n",
                  unit, level, size[0], size[1], size[2],
                  expr.type.components());
      }
      Value out(expr.type);
      for (int i = 0; i < expr.type.components() && i < 3; ++i) {
        out.data[i] = static_cast<float>(size[i]);
        out.int_data[i] = static_cast<uint32_t>(std::max(0, size[i]));
      }
      return out;
    }
    case BuiltinFunc::kFloatBitsToUint:
    case BuiltinFunc::kFloatBitsToInt:
    case BuiltinFunc::kUIntBitsToFloat:
    case BuiltinFunc::kIntBitsToFloat: {
      Value out(expr.type);
      for (int i = 0; i < expr.type.components(); ++i) {
        uint32_t bits;
        if (expr.builtin_func == BuiltinFunc::kFloatBitsToUint ||
            expr.builtin_func == BuiltinFunc::kFloatBitsToInt) {
          const float source = ComponentAt(args[0], i);
          memcpy(&bits, &source, sizeof(bits));
        } else {
          bits = args[0].IntData()[i];
        }
        const float converted = [&]() {
          float value;
          memcpy(&value, &bits, sizeof(value));
          return value;
        }();
        out.data[i] = converted;
        out.IntData()[i] = bits;
      }
      return out;
    }
    case BuiltinFunc::kPackUnorm2x16:
    case BuiltinFunc::kPackSnorm2x16:
    case BuiltinFunc::kUnpackUnorm2x16:
    case BuiltinFunc::kUnpackSnorm2x16: {
      if (expr.builtin_func == BuiltinFunc::kPackUnorm2x16 ||
          expr.builtin_func == BuiltinFunc::kPackSnorm2x16) {
        const bool snorm = expr.builtin_func == BuiltinFunc::kPackSnorm2x16;
        const float lo = snorm ? -1.0f : 0.0f;
        const float hi = 1.0f;
        const float scale = snorm ? 32767.0f : 65535.0f;
        uint32_t packed = 0;
        for (int i = 0; i < 2; ++i) {
          float v = ComponentAt(args[0], i);
          v = std::min(hi, std::max(lo, v));
          int32_t q = static_cast<int32_t>(std::round(v * scale));
          if (snorm) {
            if (q < -32768) q = -32768;
            if (q > 32767) q = 32767;
            packed |= (static_cast<uint32_t>(q) & 0xffffu) << (i * 16);
          } else {
            packed |= static_cast<uint32_t>(q) << (i * 16);
          }
        }
        Value out(expr.type);
        out.data[0] = static_cast<float>(packed);
        out.IntData()[0] = packed;
        return out;
      }
      if (expr.builtin_func == BuiltinFunc::kUnpackUnorm2x16 ||
          expr.builtin_func == BuiltinFunc::kUnpackSnorm2x16) {
        const bool snorm = expr.builtin_func == BuiltinFunc::kUnpackSnorm2x16;
        const uint32_t packed = args[0].IntData()[0];
        Value out(expr.type);
        for (int i = 0; i < 2; ++i) {
          const uint32_t raw = (packed >> (i * 16)) & 0xffffu;
          if (snorm) {
            const int32_t s = (raw & 0x8000u) ? static_cast<int32_t>(raw) - 65536
                                               : static_cast<int32_t>(raw);
            out.data[i] = std::max(-1.0f, static_cast<float>(s) / 32767.0f);
          } else {
            out.data[i] = static_cast<float>(raw) / 65535.0f;
          }
        }
        return out;
      }
    }
    case BuiltinFunc::kPackHalf2x16:
    case BuiltinFunc::kUnpackHalf2x16: {
      Value out(expr.type);
      if (expr.builtin_func == BuiltinFunc::kPackHalf2x16) {
        uint32_t packed = 0;
        for (int i = 0; i < 2; ++i) {
          const float v = ComponentAt(args[0], i);
          uint32_t bits;
          memcpy(&bits, &v, sizeof(bits));
          const uint32_t sign = (bits >> 31) & 1u;
          const int32_t exp = static_cast<int32_t>((bits >> 23) & 0xffu) - 127;
          uint32_t mant = bits & 0x7fffffu;
          uint32_t half;
          if (exp > 15) {
            half = 0x7c00u;  // Infinity
          } else if (exp < -24) {
            half = sign << 15;
          } else if (exp < -14) {
            mant |= 0x800000u;
            const int shift = -14 - exp;
            half = (mant >> (13 + shift)) | (sign << 15);
          } else {
            half = ((static_cast<uint32_t>(exp + 15) << 10) |
                    (mant >> 13)) | (sign << 15);
          }
          packed |= half << (i * 16);
        }
        out.data[0] = static_cast<float>(packed);
        out.IntData()[0] = packed;
      } else {
        const uint32_t packed = args[0].IntData()[0];
        for (int i = 0; i < 2; ++i) {
          const uint32_t half = (packed >> (i * 16)) & 0xffffu;
          const uint32_t sign = (half >> 15) & 1u;
          const uint32_t exp = (half >> 10) & 0x1fu;
          const uint32_t mant = half & 0x3ffu;
          float v;
          if (exp == 0) {
            v = (mant == 0) ? 0.0f : std::ldexp(static_cast<float>(mant), -24);
          } else if (exp == 31) {
            v = mant ? std::numeric_limits<float>::quiet_NaN()
                     : std::numeric_limits<float>::infinity();
          } else {
            v = std::ldexp(1.0f + static_cast<float>(mant) / 1024.0f,
                           static_cast<int>(exp) - 15);
          }
          out.data[i] = sign ? -v : v;
        }
      }
      return out;
    }
    case BuiltinFunc::kIsInf:
    case BuiltinFunc::kIsNan: {
      Value out(expr.type);
      for (int i = 0; i < expr.type.components(); ++i) {
        const float v = ComponentAt(args[0], i);
        out.data[i] = expr.builtin_func == BuiltinFunc::kIsInf
                          ? (std::isinf(v) ? 1.0f : 0.0f)
                          : (std::isnan(v) ? 1.0f : 0.0f);
      }
      return out;
    }
    default:
      break;
  }
  return EvaluateBuiltinMath(expr.builtin_func, args);
}

// ---- Program entry points ------------------------------------------------

namespace {
int BuiltinSlotFor(const ShaderAst& ast, BuiltinId id) {
  const int index = static_cast<int>(id);
  if (index >= 0 && index < static_cast<int>(ast.builtin_slots.size()) &&
      ast.builtin_slots[index] >= 0) {
    return ast.builtin_slots[index];
  }
  for (const Variable& variable : ast.globals) {
    if (variable.builtin == id)
      return variable.index;
  }
  return -1;
}
}  // namespace

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
  vertex_globals_ = vertex_global_initials_;
  if (getenv("GLANCEASY_DEBUG_DR")) {
    fprintf(stderr, "[dr] run globals=%d slots=%d b17=%d\n",
            (int)vertex_->globals.size(), (int)vertex_->builtin_slots.size(),
            (int)vertex_->builtin_slots[17]);
    for (const Variable& v : vertex_->globals)
      fprintf(stderr, "[dr] global %s idx=%d builtin=%d\n", v.name.c_str(),
              v.index, (int)v.builtin);
  }
  if (varying_floats_ > 0 && out_varyings)
    memset(out_varyings, 0, sizeof(float) * varying_floats_);

  Evaluator evaluator(*vertex_, &vertex_globals_, this);
  evaluator.set_uniforms(input.uniforms, vertex_uniform_map_.data());
  evaluator.set_attributes(input.attributes, input.attribute_int,
                           input.vertex_id, input.instance_id);
  evaluator.set_depth_range(input.depth_range[0], input.depth_range[1]);
  evaluator.set_varyings(nullptr, out_varyings);
  evaluator.set_sampler(input.sampler);

  bool used_jit = false;
#if defined(GLANCEASY_ENABLE_JIT)
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
      BuiltinSlotFor(*vertex_, BuiltinId::kPosition);
  if (position_slot >= 0 &&
      position_slot < static_cast<int>(vertex_globals_.size())) {
    const Value& value = vertex_globals_[position_slot];
    for (int i = 0; i < 4; ++i)
      out_position[i] = value.data[i];
  }
  if (out_point_size) {
    const int slot =
        BuiltinSlotFor(*vertex_, BuiltinId::kPointSize);
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
  fragment_globals_ = fragment_global_initials_;

  // Output arrays share one global slot.  A built-in declaration for that
  // slot may have reset it to vec4 during linking, so restore the aggregate
  // storage before the shader writes its elements.
  std::vector<int> output_elements(fragment_globals_.size(), 1);
  for (const FragmentOutputInfo& output : fragment_outputs_) {
    if (output.slot >= 0 &&
        output.slot < static_cast<int>(output_elements.size()) &&
        output.element + 1 > output_elements[output.slot]) {
      output_elements[output.slot] = output.element + 1;
    }
  }
  for (const FragmentOutputInfo& output : fragment_outputs_) {
    if (output.slot < 0 ||
        output.slot >= static_cast<int>(fragment_globals_.size()))
      continue;
    const int count = output_elements[output.slot];
    if (count > 1 &&
        fragment_globals_[output.slot].type.kind() !=
            glanceasy::glsl::TypeKind::kArray) {
      Type element(output.base, output.components, 1);
      fragment_globals_[output.slot].ResetType(
          Type::Array(element, count));
    }
  }

  if (getenv("GLANCEASY_DEBUG_VIEWPORT")) {
    static size_t last_uniform_count = 0;
    if (fragment_->uniforms.size() != last_uniform_count) {
      last_uniform_count = fragment_->uniforms.size();
      fprintf(stderr, "[uni] count=%zu\n", last_uniform_count);
      for (size_t i = 0; i < fragment_->uniforms.size() && i < 30; ++i) {
        const int slot = i < fragment_uniform_map_.size()
                             ? fragment_uniform_map_[i]
                             : -1;
        const Value* value = slot >= 0 ? &input.uniforms[slot] : nullptr;
        fprintf(stderr, "[uni] %s slot=%d v=%.6f,%.6f,%.6f,%.6f\n",
                fragment_->uniforms[i].name.c_str(), slot,
                value ? value->data[0] : -999.0f,
                value ? value->data[1] : -999.0f,
                value ? value->data[2] : -999.0f,
                value ? value->data[3] : -999.0f);
      }
    }
  }
  Evaluator evaluator(*fragment_, &fragment_globals_, this);
  evaluator.set_uniforms(input.uniforms, fragment_uniform_map_.data());
  evaluator.set_varyings(input.varyings, nullptr);
  evaluator.set_varying_derivatives(input.varyings_dx, input.varyings_dy);
  evaluator.set_sampler(input.sampler);
  evaluator.set_fragment_coord(input.frag_coord);
  evaluator.set_front_facing(input.front_facing);
  evaluator.set_depth_range(input.depth_range[0], input.depth_range[1]);
  evaluator.set_point_coord(input.point_coord);
  evaluator.set_sample_id(input.sample_id);
  evaluator.set_sample_mask(input.sample_mask);

  bool used_jit = false;
#if defined(GLANCEASY_ENABLE_JIT)
  if (fragment_jit_) {
    evaluator.PrepareBuiltins();
    JitRunInputs jit_inputs;
    jit_inputs.uniforms = input.uniforms;
    jit_inputs.uniform_map = fragment_uniform_map_.data();
    jit_inputs.varyings_in = input.varyings;
    jit_inputs.varyings_dx = input.varyings_dx;
    jit_inputs.varyings_dy = input.varyings_dy;
    jit_inputs.sampler = input.sampler;
    jit_inputs.globals = fragment_globals_.data();
    bool discarded = false;
    used_jit = fragment_jit_->Run(jit_inputs, &discarded);
    if (getenv("GLANCEASY_DEBUG_VIEWPORT")) {
      static int ed = 0;
      if (ed++ < 6) fprintf(stderr, "[exec] jit=%d\n", used_jit ? 1 : 0);
    }
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
    const int components = fragment_output.components;
    const float* data =
        value.Data() + static_cast<size_t>(fragment_output.element) * components;
    const uint32_t* int_data =
        value.IntData() +
        static_cast<size_t>(fragment_output.element) * components;
    for (int i = 0; i < 4; ++i) {
      output->color[fragment_output.location][i] =
          i < components ? data[i] : (i == 3 ? 1.0f : 0.0f);
      output->color_int[fragment_output.location][i] =
          i < components ? int_data[i] : (i == 3 ? 1u : 0u);
    }
    if (getenv("GLANCEASY_DEBUG_FRAGOUT"))
      fprintf(stderr, "[fragout] loc=%d comp=%d data=%.3f %.3f %.3f %.3f\n",
              fragment_output.location, components, data[0], data[1], data[2],
              data[3]);
    if (getenv("GLANCEASY_DEBUG_FRAGOUT"))
      fprintf(stderr, "[fragout] loc=%d base=%d data=%.3f %.3f %.3f %.3f\n",
              fragment_output.location, (int)fragment_output.base, data[0],
              data[1], data[2], data[3]);
    output->color_base[fragment_output.location] = fragment_output.base;
    output->written[fragment_output.location] = true;
    if (getenv("GLANCEASY_DEBUG_FRAGOUT") && fragment_output.base != BaseType::kFloat) {
      fprintf(stderr, "fragout loc=%d comps=%d ints=%u,%u,%u,%u floats=%.1f,%.1f,%.1f,%.1f\n",
              fragment_output.location, components, output->color_int[fragment_output.location][0],
              output->color_int[fragment_output.location][1], output->color_int[fragment_output.location][2],
              output->color_int[fragment_output.location][3], output->color[fragment_output.location][0],
              output->color[fragment_output.location][1], output->color[fragment_output.location][2],
              output->color[fragment_output.location][3]);
    }
  }
  if (uses_frag_depth_) {
    const int depth_slot =
        BuiltinSlotFor(*fragment_, BuiltinId::kFragDepth);
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
}  // namespace glanceasy
