// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// SLJIT backend for the GLSL ES subset.  This is deliberately a tiered
// compiler: constructs that are cheap to lower (float arithmetic, swizzles,
// constructors, branches, discard and texture sampling) become machine code;
// anything else is rejected at compile time and the AST evaluator runs the
// stage instead.  Keeping the JIT all-or-nothing for a stage means the two
// implementations can disagree on no shader.

#include "src/glsl/glsl_jit.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/glsl/glsl_builtins.h"
#include "src/glsl/glsl_program.h"
#include "src/glsl/glsl_types.h"
#include "zgl/zgl.h"
#include "sljitLir.h"

namespace zgl {
namespace glsl {

// Counters are process-wide because a shader can be linked without a context
// attached.  They are diagnostic only and deliberately not part of GL state.
struct JitCounters {
  uint64_t compile_attempts = 0;
  uint64_t stages_compiled = 0;
  uint64_t fallback_stages = 0;
  uint64_t dispatches = 0;
};
JitCounters g_jit_counters;

namespace {

constexpr int kMaxJitFrameSlots = 512;
constexpr int kMaxBuiltinArgs = 3;

// State passed from Program to the generated function.  The layout is part of
// the JIT's private ABI; generated code uses offsetof() constants on it.
struct JitRuntime {
  const Value* uniforms;
  const int* uniform_map;
  const float* attributes;
  const float* varyings_in;
  const float* varyings_dx;
  const float* varyings_dy;
  float* varyings_out;
  TextureSampler* sampler;
  Value* globals;
  float* locals;
  int discarded;
};

struct JitBuiltinCall {
  BuiltinFunc function = BuiltinFunc::kNone;
  int arg_count = 0;
  Type arg_types[kMaxBuiltinArgs];
  Type result_type;
};

struct JitTextureCall {
  BuiltinFunc function = BuiltinFunc::kNone;
  int arg_count = 0;
  Type arg_types[kMaxBuiltinArgs];
  // Offset of the coordinate varying in the interpolated varying array, or
  // -1 when the coordinate is not a plain varying input.  Used for implicit
  // LOD selection.
  int coord_offset = -1;
  // Number of coordinate components backed by the varying.  Constructed
  // coordinates may append uniform components (for example vec3(v_coord,
  // u_depth)); those must be excluded from derivative-based LOD selection.
  int coord_dims = 0;
};

float ComponentOf(const float* data, const Type& type, int index) {
  if (type.is_scalar())
    return data[0];
  if (index < 0 || index >= type.components() || index >= kValueFloats)
    return 0.0f;
  return data[index];
}

void CopyValueToFloats(const Value& value, float* out) {
  for (int i = 0; i < kValueFloats; ++i)
    out[i] = value.data[i];
}

// Called from generated code for built-ins implemented by the evaluator.
void JitBuiltin(const JitBuiltinCall* call, const float* args, float* out) {
  std::vector<Value> values(static_cast<size_t>(call->arg_count));
  for (int i = 0; i < call->arg_count; ++i) {
    values[i].type = call->arg_types[i];
    for (int j = 0; j < kValueFloats; ++j)
      values[i].data[j] = args[i * kValueFloats + j];
  }
  const Value result =
      EvaluateBuiltinMath(call->function, values);
  for (int i = 0; i < kValueFloats; ++i)
    out[i] = i < call->result_type.components() ? result.data[i] : 0.0f;
}

// Texture sampling is identical to Evaluator::BuiltinTexture().  Keeping the
// two copies close together is intentional: the evaluator remains the
// reference for the small amount of special-case code.
void JitTexture(const JitRuntime* runtime,
                const JitTextureCall* call,
                const float* args,
                float* out) {
  for (int i = 0; i < kValueFloats; ++i)
    out[i] = 0.0f;
  out[3] = 1.0f;
  if (call->arg_count < 2 || !runtime->sampler)
    return;

  const int unit = static_cast<int>(args[0]);
  const float* coord = args + kValueFloats;
  const bool projective = call->function == BuiltinFunc::kTextureProj ||
                          call->function == BuiltinFunc::kTextureProjLod;
  float coords[3] = {0.0f, 0.0f, 0.0f};
  float lod = 0.0f;

  if (call->function == BuiltinFunc::kTexture2DProj) {
    const int last = call->arg_types[1].components() - 1;
    const float w = last >= 1 ? coord[last] : 1.0f;
    coords[0] = w != 0.0f ? coord[0] / w : 0.0f;
    coords[1] = w != 0.0f ? coord[1] / w : 0.0f;
    runtime->sampler->Sample2D(unit, coords[0], coords[1], lod, out);
    return;
  }

  float proj_coord[kValueFloats] = {};
  if (call->function == BuiltinFunc::kTextureProj ||
      call->function == BuiltinFunc::kTextureProjLod) {
    // ES3 textureProj(): divide each coordinate by the last component.
    const BaseType sb = call->arg_types[0].base();
    const bool proj3d = sb == BaseType::kSampler3D ||
                        sb == BaseType::kISampler3D ||
                        sb == BaseType::kUSampler3D;
    const int ndim = proj3d ? 3 : 2;
    const int comps = call->arg_types[1].components();
    for (int i = 0; i < kValueFloats; ++i)
      proj_coord[i] = coord[i];
    const float w = comps > 0 ? coord[comps - 1] : 1.0f;
    for (int i = 0; i < ndim && i < comps - 1; ++i)
      proj_coord[i] = w != 0.0f ? coord[i] / w : 0.0f;
    // A shadow comparison carries the reference value in the third component,
    // which is divided by the projective component as well.
    if (sb == BaseType::kSampler2DShadow && comps > 2)
      proj_coord[2] = w != 0.0f ? coord[2] / w : 0.0f;
    coord = proj_coord;
  }

  const BaseType base = call->arg_types[0].base();
  const bool cube = call->function == BuiltinFunc::kTextureCube ||
                    base == BaseType::kSamplerCube ||
                    base == BaseType::kISamplerCube ||
                    base == BaseType::kUSamplerCube ||
                    base == BaseType::kSamplerCubeShadow;
  const bool volume = call->function == BuiltinFunc::kTexture3D ||
                      base == BaseType::kSampler3D ||
                      base == BaseType::kISampler3D ||
                      base == BaseType::kUSampler3D;
  const bool array = base == BaseType::kSampler2DArray ||
                     base == BaseType::kISampler2DArray ||
                     base == BaseType::kUSampler2DArray ||
                     base == BaseType::kSampler2DArrayShadow;
  const bool shadow2d = base == BaseType::kSampler2DShadow;
  const bool shadow_cube = base == BaseType::kSamplerCubeShadow;
  const bool shadow_array = base == BaseType::kSampler2DArrayShadow;
  const bool explicit_lod = call->function == BuiltinFunc::kTextureLod ||
                            call->function == BuiltinFunc::kTextureProjLod;
  if (explicit_lod && call->arg_count > 2)
    lod = args[2 * kValueFloats];
  else if (call->arg_count > 2)
    lod = args[2 * kValueFloats];
  if (!explicit_lod && call->coord_offset >= 0 && runtime->varyings_dx &&
      runtime->varyings_dy) {
    const int dims =
        array ? 2
              : (call->coord_dims > 0 ? call->coord_dims
                                      : call->arg_types[1].components());
    int size[3] = {1, 1, 1};
    runtime->sampler->Size(unit, size);
    if (cube) {
      size[1] = size[0];
      size[2] = size[0];
    }
    float rho_dx = 0.0f;
    float rho_dy = 0.0f;
    const int comps = call->arg_types[1].components();
    const auto project = [&](float value, float w) {
      return (projective && w != 0.0f) ? value / w : value;
    };
    if (cube) {
      float c[3];
      float dcx[3];
      float dcy[3];
      for (int i = 0; i < 3; ++i) {
        c[i] = coord[i];
        dcx[i] = runtime->varyings_dx[call->coord_offset + i] - c[i];
        dcy[i] = runtime->varyings_dy[call->coord_offset + i] - c[i];
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
      const float face_size = static_cast<float>(size[0]);
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
      if (rho > 0.0f)
        lod += std::log2(rho);
      if (getenv("ZGL_DEBUG_CUBE_LOD")) {
        static int cld = 0;
        if (cld++ < 3)
          fprintf(stderr,
                  "[jitcubelod] c=%.3f,%.3f,%.3f dcx=%.5f,%.5f,%.5f "
                  "dudx=%.5f dvdx=%.5f rho=%.5f lod=%.4f\n",
                  c[0], c[1], c[2], dcx[0], dcx[1], dcx[2], dudx, dvdx, rho,
                  std::log2(rho));
      }
    } else {
      for (int i = 0; i < dims; ++i) {
        const float scale = static_cast<float>(size[i]);
        const float current = coord[i];
        const float dx =
            (project(runtime->varyings_dx[call->coord_offset + i],
                     runtime->varyings_dx[call->coord_offset + comps - 1]) -
             current) *
            scale;
        const float dy =
            (project(runtime->varyings_dy[call->coord_offset + i],
                     runtime->varyings_dy[call->coord_offset + comps - 1]) -
             current) *
            scale;
        rho_dx += dx * dx;
        rho_dy += dy * dy;
      }
    }
    const float rho = std::sqrt(std::max(rho_dx, rho_dy));
    if (rho > 0.0f)
      lod += std::log2(rho);
    if (getenv("ZGL_DEBUG_LOD")) {
      static int lod_dbg = 0;
      if (lod_dbg < 20000) {
        fprintf(stderr, "[jitlod] rho=%.3f lod=%.3f dims=%d coord=%.3f %.3f %.3f dx=%.3f %.3f %.3f off=%d\n",
                rho, lod, dims, coord[0], coord[1], coord[2],
                runtime->varyings_dx[call->coord_offset],
                runtime->varyings_dx[call->coord_offset + 1],
                runtime->varyings_dx[call->coord_offset + 2],
                call->coord_offset);
        ++lod_dbg;
      }
    }
  }
  for (int i = 0; i < 3; ++i)
    coords[i] = coord[i];

  if (shadow2d)
    runtime->sampler->Sample2DShadow(unit, coords[0], coords[1], coord[2],
                                     lod, out);
  else if (shadow_array)
    runtime->sampler->Sample2DArrayShadow(unit, coords[0], coords[1],
                                          coords[2], coord[3], lod, out);
  else if (shadow_cube)
    runtime->sampler->SampleCubeShadow(unit, coords, coord[3], lod, out);
  else if (cube)
    runtime->sampler->SampleCube(unit, coords, lod, out);
  else if (volume)
    runtime->sampler->Sample3D(unit, coords, lod, out);
  else if (array)
    runtime->sampler->Sample2DArray(unit, coords[0], coords[1], coords[2],
                                   lod, out);
  else
    runtime->sampler->Sample2D(unit, coords[0], coords[1], lod, out);
}

}  // namespace

// ---------------------------------------------------------------------------
// Compiler.

class ShaderJit::Impl {
 public:
  Impl(const ShaderAst& ast);
  ~Impl();

  bool Compile(std::string* reason);
  bool Run(const JitRunInputs& inputs, bool* discarded);
  int frame_slots() const { return frame_slots_; }

 private:
  struct Temp {
    int slot = -1;
    Type type;
  };

  bool Scan();
  bool ScanExpr(const Expr& expr);
  bool ScanStatement(const Statement& statement);

  bool EmitPrologue();
  bool EmitStatement(const Statement& statement);
  bool EmitBlock(const std::vector<StmtPtr>& body);
  bool EmitIf(const Statement& statement);

  Temp EmitExpr(const Expr& expr);
  Temp EmitLiteral(const Expr& expr);
  Temp EmitVariable(const Expr& expr);
  Temp EmitField(const Expr& expr);
  Temp EmitIndex(const Expr& expr);
  Temp EmitConstructor(const Expr& expr);
  Temp EmitCall(const Expr& expr);
  Temp EmitUnary(const Expr& expr);
  Temp EmitBinary(const Expr& expr);
  Temp EmitAssignment(const Expr& expr);
  Temp EmitTernary(const Expr& expr);

  Temp NewTemp(const Type& type);
  void Release(const Temp& temp);
  Temp ConvertTo(const Temp& source, const Type& target);
  bool EmitCopy(int dst, const Type& dst_type, int src, const Type& src_type);
  bool EmitStoreVariable(const Expr& target, int slot, const Type& type);
  bool EmitAssignTarget(const Expr& target, int slot, const Type& type);

  bool EmitLoadComponent(int slot, const Type& type, int index, int freg);
  bool EmitStoreComponent(int slot, int index, int freg);
  bool EmitMoveComponent(int dst, int dst_index, int src, int src_index);
  bool EmitSetComponent(int slot, int index, float value);
  bool EmitBinaryComponent(sljit_s32 op,
                           int dst,
                           int left,
                           int right,
                           int count);

  bool EmitBuiltinCall(const Expr& expr, int out, int arg_area);
  bool EmitTextureCall(const Expr& expr, int out, int arg_area);
  bool EmitMatrixMultiply(int out,
                          int left,
                          const Type& left_type,
                          int right,
                          const Type& right_type,
                          const Type& result_type);
  bool EmitF32Binary(sljit_s32 op, int dst, int left, int right);
  bool EmitF32Unary(sljit_s32 op, int dst, int src);
  sljit_jump* EmitJumpIfFalse(int slot, const Type& type);
  bool EmitBoolResult(int slot, bool value);
  bool EmitComparison(int dst,
                      const Expr& expr,
                      int left,
                      int right);
  bool EmitLogical(int dst,
                   Op op,
                   const Expr& expr,
                   int left,
                   int right);

  bool IsVariableWritable(const Expr& expr) const;
  int SourceSlot(const Expr& expr) const;
  bool IsSupportedType(const Type& type) const;
  bool IsSamplerType(const Type& type) const { return type.is_sampler(); }
  bool ConstantIndex(const Expr& expr, int* index) const;

  bool Check(sljit_s32 result, std::string* reason);
  bool Fail(const std::string& reason);
  int SlotOffset(int slot) const {
    return slot * kValueFloats * static_cast<int>(sizeof(float));
  }
  int ValueDataOffset(int index) const {
    return index * static_cast<int>(sizeof(Value)) +
           static_cast<int>(offsetof(Value, data));
  }

  const ShaderAst& ast_;
  const Function* main_ = nullptr;
  sljit_compiler* compiler_ = nullptr;
  std::vector<sljit_jump*> end_jumps_;
  sljit_label* end_label_ = nullptr;
  std::string failure_;

  int arg_area_ = -1;
  int next_temp_ = 0;
  int frame_slots_ = 0;
  std::vector<int> free_temps_;
  std::vector<std::unique_ptr<JitBuiltinCall>> builtin_calls_;
  std::vector<std::unique_ptr<JitTextureCall>> texture_calls_;

  void* code_ = nullptr;
  using Entry = void (*)(JitRuntime*);
  Entry entry_ = nullptr;

  // Reused between dispatches.  The generated code zeroes the frame itself,
  // so the buffer is only resized, never cleared on the host side.
  std::vector<float> locals_;
  bool failed_ = false;
};

ShaderJit::Impl::Impl(const ShaderAst& ast) : ast_(ast) {}

ShaderJit::Impl::~Impl() {
  if (code_)
    sljit_free_code(code_, nullptr);
}

bool ShaderJit::Impl::Fail(const std::string& reason) {
  if (failure_.empty())
    failure_ = reason;
  failed_ = true;
  return false;
}

bool ShaderJit::Impl::Check(sljit_s32 result, std::string* reason) {
  if (result == SLJIT_SUCCESS)
    return true;
  if (reason)
    *reason = "SLJIT backend rejected the shader";
  return false;
}

bool ShaderJit::Impl::IsSupportedType(const Type& type) const {
  if (type.is_aggregate())
    return false;
  if (type.base() == BaseType::kInt || type.base() == BaseType::kUInt)
    return false;
  if (type.base() == BaseType::kVoid)
    return false;
  return true;
}

bool ShaderJit::Impl::ConstantIndex(const Expr& expr, int* index) const {
  if (expr.kind != Expr::Kind::kLiteral || expr.type.base() != BaseType::kInt)
    return false;
  if (index)
    *index = static_cast<int>(expr.literal);
  return true;
}

bool ShaderJit::Impl::ScanExpr(const Expr& expr) {
  if (!IsSupportedType(expr.type) && !IsSamplerType(expr.type))
    return false;
  if (expr.is_constructor && expr.type.base() == BaseType::kBool) {
    for (const ExprPtr& argument : expr.args) {
      if (argument->type.base() != BaseType::kBool)
        return false;
    }
  }
  if (expr.kind == Expr::Kind::kCall && !expr.is_constructor) {
    if (expr.builtin_func == BuiltinFunc::kNone)
      return false;
    if (expr.builtin_func == BuiltinFunc::kLessThan ||
        expr.builtin_func == BuiltinFunc::kLessThanEqual ||
        expr.builtin_func == BuiltinFunc::kGreaterThan ||
        expr.builtin_func == BuiltinFunc::kGreaterThanEqual ||
        expr.builtin_func == BuiltinFunc::kEqualFunc ||
        expr.builtin_func == BuiltinFunc::kNotEqualFunc ||
        expr.builtin_func == BuiltinFunc::kAny ||
        expr.builtin_func == BuiltinFunc::kAll ||
        expr.builtin_func == BuiltinFunc::kNot ||
        expr.builtin_func == BuiltinFunc::kDfdx ||
        expr.builtin_func == BuiltinFunc::kDfdy ||
        expr.builtin_func == BuiltinFunc::kFwidth) {
      return false;
    }
    if (expr.args.size() > static_cast<size_t>(kMaxBuiltinArgs))
      return false;
  }
  if (expr.kind == Expr::Kind::kIndex && !ConstantIndex(*expr.args[0], nullptr))
    return false;
  if (expr.base && !ScanExpr(*expr.base))
    return false;
  for (const ExprPtr& arg : expr.args) {
    if (arg && !ScanExpr(*arg))
      return false;
  }
  return true;
}

bool ShaderJit::Impl::ScanStatement(const Statement& statement) {
  switch (statement.kind) {
    case Statement::Kind::kFor:
    case Statement::Kind::kWhile:
    case Statement::Kind::kDoWhile:
    case Statement::Kind::kBreak:
    case Statement::Kind::kContinue:
      return false;
    default:
      break;
  }
  if (statement.init && !ScanStatement(*statement.init))
    return false;
  if (statement.condition && !ScanStatement(*statement.condition))
    return false;
  if (statement.step && !ScanStatement(*statement.step))
    return false;
  if (statement.then_branch && !ScanStatement(*statement.then_branch))
    return false;
  if (statement.else_branch && !ScanStatement(*statement.else_branch))
    return false;
  if (statement.expression && !ScanExpr(*statement.expression))
    return false;
  for (const StmtPtr& child : statement.statements) {
    if (child && !ScanStatement(*child))
      return false;
  }
  for (const auto& declarator : statement.declarators) {
    if (!IsSupportedType(declarator.variable.type))
      return false;
    if (declarator.initializer && !ScanExpr(*declarator.initializer))
      return false;
  }
  return true;
}

bool ShaderJit::Impl::Scan() {
  if (ast_.functions.size() != 1)
    return false;
  if (!ast_.main())
    return false;
  main_ = ast_.main();
  if (!main_->parameters.empty())
    return false;
  if (main_->slot_count > kMaxJitFrameSlots)
    return false;
  for (const Variable& global : ast_.globals) {
    if (!IsSupportedType(global.type) && !IsSamplerType(global.type))
      return false;
  }
  for (const Variable& uniform : ast_.uniforms) {
    if (!IsSupportedType(uniform.type) && !IsSamplerType(uniform.type))
      return false;
  }
  for (const Variable& attribute : ast_.attributes) {
    if (!IsSupportedType(attribute.type))
      return false;
  }
  for (const Variable& varying : ast_.varyings) {
    if (!IsSupportedType(varying.type))
      return false;
  }
  for (const StmtPtr& statement : main_->body) {
    if (!ScanStatement(*statement))
      return false;
  }
  return true;
}

bool ShaderJit::Impl::EmitPrologue() {
  sljit_emit_enter(compiler_, 0, SLJIT_ARGS1V(P),
                   4 | SLJIT_ENTER_FLOAT(4), 3, 0);
  // S0 = runtime, S1 = locals, S2 = globals.  Saved registers survive calls
  // to the built-in and texture helpers.
  sljit_emit_op1(compiler_, SLJIT_MOV, SLJIT_S1, 0, SLJIT_MEM1(SLJIT_S0),
                 offsetof(JitRuntime, locals));
  sljit_emit_op1(compiler_, SLJIT_MOV, SLJIT_S2, 0, SLJIT_MEM1(SLJIT_S0),
                 offsetof(JitRuntime, globals));

  // Locals are zero-initialised exactly like Evaluator::CallFunction().
  for (int slot = 0; slot < frame_slots_; ++slot) {
    for (int component = 0; component < kValueFloats; ++component) {
      if (!EmitSetComponent(slot, component, 0.0f))
        return false;
    }
  }
  return true;
}

int ShaderJit::Impl::SourceSlot(const Expr& expr) const {
  return expr.index;
}

ShaderJit::Impl::Temp ShaderJit::Impl::NewTemp(const Type& type) {
  int slot = -1;
  if (!free_temps_.empty()) {
    slot = free_temps_.back();
    free_temps_.pop_back();
  } else {
    slot = next_temp_++;
  }
  if (slot >= kMaxJitFrameSlots) {
    Fail("too many temporaries in shader");
    return Temp{};
  }
  frame_slots_ = std::max(frame_slots_, slot + 1);
  return Temp{slot, type};
}

void ShaderJit::Impl::Release(const Temp& temp) {
  if (temp.slot >= 0)
    free_temps_.push_back(temp.slot);
}

bool ShaderJit::Impl::EmitSetComponent(int slot, int index, float value) {
  const int offset = SlotOffset(slot) + index * static_cast<int>(sizeof(float));
  sljit_emit_fset32(compiler_, SLJIT_FR3, value);
  return Check(sljit_emit_fop1(compiler_, SLJIT_MOV_F32, SLJIT_MEM1(SLJIT_S1),
                               offset, SLJIT_FR3, 0),
               nullptr);
}

bool ShaderJit::Impl::EmitLoadComponent(int slot,
                                      const Type& type,
                                      int index,
                                      int freg) {
  const int source_index =
      type.is_scalar() ? 0 : std::min(index, kValueFloats - 1);
  const int offset =
      SlotOffset(slot) + source_index * static_cast<int>(sizeof(float));
  return Check(sljit_emit_fop1(compiler_, SLJIT_MOV_F32, freg, 0,
                               SLJIT_MEM1(SLJIT_S1), offset),
               nullptr);
}

bool ShaderJit::Impl::EmitStoreComponent(int slot, int index, int freg) {
  const int offset = SlotOffset(slot) + index * static_cast<int>(sizeof(float));
  return Check(sljit_emit_fop1(compiler_, SLJIT_MOV_F32, SLJIT_MEM1(SLJIT_S1),
                               offset, freg, 0),
               nullptr);
}

bool ShaderJit::Impl::EmitMoveComponent(int dst,
                                        int dst_index,
                                        int src,
                                        int src_index) {
  const int src_offset =
      SlotOffset(src) + src_index * static_cast<int>(sizeof(float));
  const int dst_offset =
      SlotOffset(dst) + dst_index * static_cast<int>(sizeof(float));
  sljit_emit_fop1(compiler_, SLJIT_MOV_F32, SLJIT_FR3, 0, SLJIT_MEM1(SLJIT_S1),
                  src_offset);
  return Check(sljit_emit_fop1(compiler_, SLJIT_MOV_F32, SLJIT_MEM1(SLJIT_S1),
                               dst_offset, SLJIT_FR3, 0),
               nullptr);
}

bool ShaderJit::Impl::EmitCopy(int dst,
                               const Type& dst_type,
                               int src,
                               const Type& src_type) {
  if (dst_type.is_matrix()) {
    if (src_type.is_matrix()) {
      const int rows = std::min(dst_type.rows(), src_type.rows());
      const int columns = std::min(dst_type.columns(), src_type.columns());
      for (int c = 0; c < dst_type.columns(); ++c) {
        for (int r = 0; r < dst_type.rows(); ++r) {
          if (c < columns && r < rows)
            EmitMoveComponent(dst, c * dst_type.rows() + r, src,
                              c * src_type.rows() + r);
          else
            EmitSetComponent(dst, c * dst_type.rows() + r,
                             (c == r) ? 1.0f : 0.0f);
        }
      }
      return true;
    }
    for (int c = 0; c < dst_type.columns(); ++c) {
      for (int r = 0; r < dst_type.rows(); ++r)
        EmitSetComponent(dst, c * dst_type.rows() + r, (c == r) ? 1.0f : 0.0f);
    }
    return true;
  }
  if (src_type.is_matrix()) {
    for (int i = 0; i < dst_type.components(); ++i)
      EmitSetComponent(dst, i, i == 0 ? 1.0f : 0.0f);
    return EmitMoveComponent(dst, 0, src, 0);
  }
  if (src_type.is_scalar()) {
    for (int i = 0; i < dst_type.components(); ++i)
      EmitMoveComponent(dst, i, src, 0);
    return true;
  }
  for (int i = 0; i < dst_type.components(); ++i) {
    if (i < src_type.components())
      EmitMoveComponent(dst, i, src, i);
    else
      EmitSetComponent(dst, i, (i == 3) ? 1.0f : 0.0f);
  }
  return true;
}

ShaderJit::Impl::Temp ShaderJit::Impl::ConvertTo(const Temp& source,
                                                 const Type& target) {
  if (source.slot < 0)
    return Temp{};
  if (source.type == target)
    return source;
  Temp result = NewTemp(target);
  if (result.slot < 0)
    return result;
  EmitCopy(result.slot, target, source.slot, source.type);
  return result;
}

ShaderJit::Impl::Temp ShaderJit::Impl::EmitLiteral(const Expr& expr) {
  Temp result = NewTemp(expr.type);
  if (result.slot < 0)
    return result;
  for (int i = 0; i < expr.type.components(); ++i)
    EmitSetComponent(result.slot, i, expr.literal);
  return result;
}

ShaderJit::Impl::Temp ShaderJit::Impl::EmitVariable(const Expr& expr) {
  if (!IsSupportedType(expr.type) && !IsSamplerType(expr.type))
    return Temp{};
  Temp result = NewTemp(expr.type);
  if (result.slot < 0)
    return result;
  const int components = expr.type.is_scalar() ? 1 : expr.type.components();
  switch (expr.storage) {
    case Storage::kLocal:
      if (expr.index < 0 || expr.index >= main_->slot_count) {
        Release(result);
        return Temp{};
      }
      for (int i = 0; i < components; ++i)
        EmitMoveComponent(result.slot, i, expr.index, i);
      return result;
    case Storage::kGlobal:
    case Storage::kBuiltin: {
      if (expr.index < 0) {
        Release(result);
        return Temp{};
      }
      const int offset = ValueDataOffset(expr.index);
      for (int i = 0; i < components; ++i) {
        const int component_offset =
            offset + i * static_cast<int>(sizeof(float));
        sljit_emit_fop1(compiler_, SLJIT_MOV_F32, SLJIT_FR3, 0,
                        SLJIT_MEM1(SLJIT_S2), component_offset);
        EmitStoreComponent(result.slot, i, SLJIT_FR3);
      }
      return result;
    }
    case Storage::kUniform: {
      if (expr.index < 0 || !main_) {
        Release(result);
        return Temp{};
      }
      sljit_emit_op1(compiler_, SLJIT_MOV, SLJIT_R0, 0,
                     SLJIT_MEM1(SLJIT_S0), offsetof(JitRuntime, uniforms));
      sljit_emit_op1(compiler_, SLJIT_MOV, SLJIT_R1, 0,
                     SLJIT_MEM1(SLJIT_S0), offsetof(JitRuntime, uniform_map));
      sljit_jump* missing_map =
          sljit_emit_cmp(compiler_, SLJIT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, 0);
      sljit_emit_op1(compiler_, SLJIT_MOV_U32, SLJIT_R2, 0,
                     SLJIT_MEM1(SLJIT_R1), expr.index * sizeof(int));
      sljit_emit_op2(compiler_, SLJIT_MUL, SLJIT_R2, 0, SLJIT_R2, 0,
                     SLJIT_IMM, static_cast<sljit_sw>(sizeof(Value)));
      sljit_emit_op2(compiler_, SLJIT_ADD, SLJIT_R2, 0, SLJIT_R0, 0, SLJIT_R2,
                     0);
      for (int i = 0; i < components; ++i) {
        const int component_offset =
            static_cast<int>(offsetof(Value, data)) +
            i * static_cast<int>(sizeof(float));
        sljit_emit_fop1(compiler_, SLJIT_MOV_F32, SLJIT_FR3, 0,
                        SLJIT_MEM1(SLJIT_R2), component_offset);
        EmitStoreComponent(result.slot, i, SLJIT_FR3);
      }
      sljit_label* ok = sljit_emit_label(compiler_);
      sljit_set_label(missing_map, ok);
      return result;
    }
    case Storage::kAttribute: {
      sljit_emit_op1(compiler_, SLJIT_MOV, SLJIT_R0, 0,
                     SLJIT_MEM1(SLJIT_S0), offsetof(JitRuntime, attributes));
      const int rows = expr.type.is_matrix() ? expr.type.rows() : components;
      for (int i = 0; i < components; ++i) {
        const int column = expr.type.is_matrix() ? i / rows : 0;
        const int row = expr.type.is_matrix() ? i % rows : i;
        const int offset = ((expr.index + column) * 4 + row) *
                           static_cast<int>(sizeof(float));
        sljit_emit_fop1(compiler_, SLJIT_MOV_F32, SLJIT_FR3, 0,
                        SLJIT_MEM1(SLJIT_R0), offset);
        EmitStoreComponent(result.slot, i, SLJIT_FR3);
      }
      return result;
    }
    case Storage::kVaryingIn:
    case Storage::kVaryingOut: {
      const int offset =
          expr.index >= 0 &&
                  expr.index < static_cast<int>(ast_.varyings.size())
              ? ast_.varyings[expr.index].varying_offset
              : -1;
      if (offset < 0) {
        Release(result);
        return Temp{};
      }
      const bool output = expr.storage == Storage::kVaryingOut;
      sljit_emit_op1(compiler_, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0),
                     output ? offsetof(JitRuntime, varyings_out)
                            : offsetof(JitRuntime, varyings_in));
      sljit_jump* missing =
          sljit_emit_cmp(compiler_, SLJIT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
      for (int i = 0; i < components; ++i) {
        const int component_offset =
            (offset + i) * static_cast<int>(sizeof(float));
        sljit_emit_fop1(compiler_, SLJIT_MOV_F32, SLJIT_FR3, 0,
                        SLJIT_MEM1(SLJIT_R0), component_offset);
        EmitStoreComponent(result.slot, i, SLJIT_FR3);
      }
      sljit_label* ok = sljit_emit_label(compiler_);
      sljit_set_label(missing, ok);
      return result;
    }
  }
  Release(result);
  return Temp{};
}

ShaderJit::Impl::Temp ShaderJit::Impl::EmitField(const Expr& expr) {
  Temp base = EmitExpr(*expr.base);
  if (base.slot < 0)
    return Temp{};
  Temp result = NewTemp(expr.type);
  if (result.slot < 0) {
    Release(base);
    return result;
  }
  const char* sets[] = {"xyzw", "rgba", "stpq"};
  for (size_t i = 0; i < expr.text.size(); ++i) {
    int component = 0;
    for (const char* set : sets) {
      const char* found = strchr(set, expr.text[i]);
      if (found) {
        component = static_cast<int>(found - set);
        break;
      }
    }
    if (component < base.type.components())
      EmitMoveComponent(result.slot, static_cast<int>(i), base.slot, component);
    else
      EmitSetComponent(result.slot, static_cast<int>(i), 0.0f);
  }
  Release(base);
  return result;
}

ShaderJit::Impl::Temp ShaderJit::Impl::EmitIndex(const Expr& expr) {
  int index = 0;
  if (!ConstantIndex(*expr.args[0], &index))
    return Temp{};
  Temp base = EmitExpr(*expr.base);
  if (base.slot < 0)
    return Temp{};
  Temp result = NewTemp(expr.type);
  if (result.slot < 0) {
    Release(base);
    return result;
  }
  if (base.type.is_matrix()) {
    if (index >= 0 && index < base.type.columns()) {
      for (int r = 0; r < expr.type.components(); ++r)
        EmitMoveComponent(result.slot, r, base.slot,
                          index * base.type.rows() + r);
    }
  } else {
    if (index >= 0 && index < base.type.components())
      EmitMoveComponent(result.slot, 0, base.slot, index);
    else
      EmitSetComponent(result.slot, 0, 0.0f);
  }
  Release(base);
  return result;
}

ShaderJit::Impl::Temp ShaderJit::Impl::EmitConstructor(const Expr& expr) {
  Temp result = NewTemp(expr.type);
  if (result.slot < 0)
    return result;
  std::vector<Temp> args;
  args.reserve(expr.args.size());
  for (const ExprPtr& argument : expr.args)
    args.push_back(EmitExpr(*argument));
  if (std::any_of(args.begin(), args.end(),
                  [](const Temp& t) { return t.slot < 0; })) {
    for (const Temp& t : args)
      Release(t);
    Release(result);
    return Temp{};
  }

  if (expr.type.is_matrix()) {
    if (args.size() == 1 && args[0].type.is_matrix()) {
      const int dst_rows = expr.type.rows();
      const int dst_columns = expr.type.columns();
      const int src_rows = args[0].type.rows();
      const int src_columns = args[0].type.columns();
      for (int c = 0; c < dst_columns; ++c) {
        for (int r = 0; r < dst_rows; ++r) {
          if (c < src_columns && r < src_rows)
            EmitMoveComponent(result.slot, c * dst_rows + r, args[0].slot,
                              c * src_rows + r);
          else
            EmitSetComponent(result.slot, c * dst_rows + r,
                             (c == r) ? 1.0f : 0.0f);
        }
      }
      for (const Temp& t : args)
        Release(t);
      return result;
    }
    if (args.size() == 1 && args[0].type.is_scalar()) {
      Temp scalar = args[0];
      for (int c = 0; c < expr.type.columns(); ++c) {
        for (int r = 0; r < expr.type.rows(); ++r) {
          if (c == r) {
            EmitLoadComponent(scalar.slot, Type::Float(), 0, SLJIT_FR0);
            EmitStoreComponent(result.slot, c * expr.type.rows() + r,
                               SLJIT_FR0);
          } else {
            EmitSetComponent(result.slot, c * expr.type.rows() + r, 0.0f);
          }
        }
      }
      for (const Temp& t : args)
        Release(t);
      return result;
    }
    int count = 0;
    for (const Temp& t : args) {
      if (t.type.is_matrix()) {
        for (int i = 0; i < t.type.components() && count < kValueFloats;
             ++i, ++count)
          EmitMoveComponent(result.slot, count, t.slot, i);
      } else {
        for (int i = 0; i < t.type.components() && count < kValueFloats;
             ++i, ++count)
          EmitMoveComponent(result.slot, count, t.slot, i);
      }
    }
    for (; count < expr.type.components(); ++count)
      EmitSetComponent(result.slot, count, 0.0f);
  } else if (args.size() == 1 && args[0].type.is_scalar()) {
    for (int i = 0; i < expr.type.components(); ++i)
      EmitMoveComponent(result.slot, i, args[0].slot, 0);
  } else {
    int count = 0;
    for (const Temp& t : args) {
      for (int i = 0; i < t.type.components() && count < expr.type.components();
           ++i, ++count) {
        EmitMoveComponent(result.slot, count, t.slot, i);
      }
    }
    for (; count < expr.type.components(); ++count)
      EmitSetComponent(result.slot, count, count == 3 ? 1.0f : 0.0f);
  }
  for (const Temp& t : args)
    Release(t);
  return result;
}

bool ShaderJit::Impl::EmitF32Binary(sljit_s32 op,
                                    int dst,
                                    int left,
                                    int right) {
  EmitLoadComponent(left, Type::Float(), 0, SLJIT_FR0);
  EmitLoadComponent(right, Type::Float(), 0, SLJIT_FR1);
  sljit_emit_fop2(compiler_, op, SLJIT_FR0, 0, SLJIT_FR0, 0, SLJIT_FR1, 0);
  return EmitStoreComponent(dst, 0, SLJIT_FR0);
}

bool ShaderJit::Impl::EmitF32Unary(sljit_s32 op, int dst, int src) {
  EmitLoadComponent(src, Type::Float(), 0, SLJIT_FR0);
  sljit_emit_fop1(compiler_, op, SLJIT_FR0, 0, SLJIT_FR0, 0);
  return EmitStoreComponent(dst, 0, SLJIT_FR0);
}

bool ShaderJit::Impl::EmitBinaryComponent(sljit_s32 op,
                                          int dst,
                                          int left,
                                          int right,
                                          int count) {
  for (int i = 0; i < count; ++i) {
    EmitLoadComponent(left, Type::Float(), i, SLJIT_FR0);
    EmitLoadComponent(right, Type::Float(), i, SLJIT_FR1);
    sljit_emit_fop2(compiler_, op, SLJIT_FR0, 0, SLJIT_FR0, 0, SLJIT_FR1, 0);
    EmitStoreComponent(dst, i, SLJIT_FR0);
  }
  return true;
}

bool ShaderJit::Impl::EmitMatrixMultiply(int out,
                                         int left,
                                         const Type& left_type,
                                         int right,
                                         const Type& right_type,
                                         const Type& result_type) {
  if (left_type.is_matrix() && right_type.is_matrix()) {
    if (left_type.columns() != right_type.rows())
      return false;
    for (int c = 0; c < result_type.columns(); ++c) {
      for (int r = 0; r < result_type.rows(); ++r) {
        EmitSetComponent(out, c * result_type.rows() + r, 0.0f);
        for (int k = 0; k < left_type.columns(); ++k) {
          EmitLoadComponent(left, left_type, k * left_type.rows() + r,
                            SLJIT_FR0);
          EmitLoadComponent(right, right_type, c * right_type.rows() + k,
                            SLJIT_FR1);
          sljit_emit_fop2(compiler_, SLJIT_MUL_F32, SLJIT_FR0, 0, SLJIT_FR0, 0,
                          SLJIT_FR1, 0);
          EmitLoadComponent(out, result_type, c * result_type.rows() + r,
                            SLJIT_FR2);
          sljit_emit_fop2(compiler_, SLJIT_ADD_F32, SLJIT_FR2, 0, SLJIT_FR2, 0,
                          SLJIT_FR0, 0);
          EmitStoreComponent(out, c * result_type.rows() + r, SLJIT_FR2);
        }
      }
    }
    return true;
  }
  if (left_type.is_matrix()) {
    for (int r = 0; r < result_type.components(); ++r) {
      EmitSetComponent(out, r, 0.0f);
      for (int c = 0; c < left_type.columns(); ++c) {
        EmitLoadComponent(left, left_type, c * left_type.rows() + r, SLJIT_FR0);
        EmitLoadComponent(right, right_type, c, SLJIT_FR1);
        sljit_emit_fop2(compiler_, SLJIT_MUL_F32, SLJIT_FR0, 0, SLJIT_FR0, 0,
                        SLJIT_FR1, 0);
        EmitLoadComponent(out, result_type, r, SLJIT_FR2);
        sljit_emit_fop2(compiler_, SLJIT_ADD_F32, SLJIT_FR2, 0, SLJIT_FR2, 0,
                        SLJIT_FR0, 0);
        EmitStoreComponent(out, r, SLJIT_FR2);
      }
    }
    return true;
  }
  if (right_type.is_matrix()) {
    for (int c = 0; c < result_type.components(); ++c) {
      EmitSetComponent(out, c, 0.0f);
      for (int r = 0; r < right_type.rows(); ++r) {
        EmitLoadComponent(left, left_type, r, SLJIT_FR0);
        EmitLoadComponent(right, right_type, c * right_type.rows() + r,
                          SLJIT_FR1);
        sljit_emit_fop2(compiler_, SLJIT_MUL_F32, SLJIT_FR0, 0, SLJIT_FR0, 0,
                        SLJIT_FR1, 0);
        EmitLoadComponent(out, result_type, c, SLJIT_FR2);
        sljit_emit_fop2(compiler_, SLJIT_ADD_F32, SLJIT_FR2, 0, SLJIT_FR2, 0,
                        SLJIT_FR0, 0);
        EmitStoreComponent(out, c, SLJIT_FR2);
      }
    }
    return true;
  }
  return false;
}

bool ShaderJit::Impl::EmitBoolResult(int slot, bool value) {
  return EmitSetComponent(slot, 0, value ? 1.0f : 0.0f);
}

sljit_jump* ShaderJit::Impl::EmitJumpIfFalse(int slot, const Type& type) {
  if (type.is_vector() || type.is_matrix())
    return nullptr;
  EmitLoadComponent(slot, type, 0, SLJIT_FR0);
  sljit_emit_fset32(compiler_, SLJIT_FR1, 0.0f);
  return sljit_emit_fcmp(compiler_, SLJIT_ORDERED_EQUAL | SLJIT_32, SLJIT_FR0,
                         0, SLJIT_FR1, 0);
}

bool ShaderJit::Impl::EmitComparison(int dst,
                                     const Expr& expr,
                                     int left,
                                     int right) {
  if (expr.args[0]->type.components() != 1 ||
      expr.args[1]->type.components() != 1)
    return false;
  EmitLoadComponent(left, Type::Float(), 0, SLJIT_FR0);
  EmitLoadComponent(right, Type::Float(), 0, SLJIT_FR1);
  sljit_s32 condition = SLJIT_ORDERED_EQUAL;
  switch (expr.op) {
    case Op::kLess:
      condition = SLJIT_ORDERED_LESS;
      break;
    case Op::kLessEqual:
      condition = SLJIT_ORDERED_LESS_EQUAL;
      break;
    case Op::kGreater:
      condition = SLJIT_ORDERED_GREATER;
      break;
    case Op::kGreaterEqual:
      condition = SLJIT_ORDERED_GREATER_EQUAL;
      break;
    case Op::kEqual:
      condition = SLJIT_ORDERED_EQUAL;
      break;
    case Op::kNotEqual:
      condition = SLJIT_UNORDERED_OR_NOT_EQUAL;
      break;
    default:
      return false;
  }
  sljit_jump* jump = sljit_emit_fcmp(compiler_, condition | SLJIT_32, SLJIT_FR0,
                                     0, SLJIT_FR1, 0);
  EmitBoolResult(dst, false);
  sljit_jump* end = sljit_emit_jump(compiler_, SLJIT_JUMP);
  sljit_label* true_label = sljit_emit_label(compiler_);
  sljit_set_label(jump, true_label);
  EmitBoolResult(dst, true);
  sljit_label* end_label = sljit_emit_label(compiler_);
  sljit_set_label(end, end_label);
  return true;
}

bool ShaderJit::Impl::EmitLogical(int dst,
                                  Op op,
                                  const Expr& expr,
                                  int left,
                                  int right) {
  if (expr.args[0]->type.components() != 1 ||
      expr.args[1]->type.components() != 1)
    return false;
  if (op == Op::kLogicalAnd) {
    EmitBoolResult(dst, false);
    sljit_jump* left_false = EmitJumpIfFalse(left, expr.args[0]->type);
    if (!left_false)
      return false;
    sljit_jump* right_false = EmitJumpIfFalse(right, expr.args[1]->type);
    if (!right_false)
      return false;
    EmitBoolResult(dst, true);
    sljit_label* end = sljit_emit_label(compiler_);
    sljit_set_label(left_false, end);
    sljit_set_label(right_false, end);
    return true;
  }
  return false;
}

ShaderJit::Impl::Temp ShaderJit::Impl::EmitUnary(const Expr& expr) {
  // Integer values live as floats in the scalar JIT, so any operation that
  // needs an exact 32-bit payload is left to the interpreter instead.
  if (expr.type.is_integer())
    return Temp{};
  if (expr.op == Op::kPreIncrement || expr.op == Op::kPreDecrement ||
      expr.op == Op::kPostIncrement || expr.op == Op::kPostDecrement) {
    if (expr.args[0]->kind != Expr::Kind::kVariable &&
        expr.args[0]->kind != Expr::Kind::kField &&
        expr.args[0]->kind != Expr::Kind::kIndex) {
      return Temp{};
    }
    Temp old = EmitExpr(*expr.args[0]);
    if (old.slot < 0)
      return Temp{};
    Temp one = NewTemp(Type::Float());
    EmitSetComponent(one.slot, 0, 1.0f);
    Temp result = NewTemp(expr.type);
    const bool increment = expr.op == Op::kPreIncrement ||
                           expr.op == Op::kPostIncrement;
    for (int i = 0; i < expr.type.components(); ++i) {
      EmitLoadComponent(old.slot, expr.type, i, SLJIT_FR0);
      EmitLoadComponent(one.slot, Type::Float(), 0, SLJIT_FR1);
      sljit_emit_fop2(compiler_, increment ? SLJIT_ADD_F32 : SLJIT_SUB_F32,
                      SLJIT_FR0, 0, SLJIT_FR0, 0, SLJIT_FR1, 0);
      EmitStoreComponent(result.slot, i, SLJIT_FR0);
    }
    const bool prefix = expr.op == Op::kPreIncrement ||
                        expr.op == Op::kPreDecrement;
    EmitAssignTarget(*expr.args[0], result.slot, expr.type);
    if (!prefix) {
      // The postfix form returns the old value.
      EmitCopy(result.slot, expr.type, old.slot, expr.type);
    }
    Release(one);
    Release(old);
    return result;
  }
  if (expr.op == Op::kNegate) {
    Temp arg = EmitExpr(*expr.args[0]);
    if (arg.slot < 0)
      return Temp{};
    Temp result = NewTemp(expr.type);
    for (int i = 0; i < expr.type.components(); ++i) {
      EmitLoadComponent(arg.slot, expr.args[0]->type, i, SLJIT_FR0);
      sljit_emit_fop1(compiler_, SLJIT_NEG_F32, SLJIT_FR0, 0, SLJIT_FR0, 0);
      EmitStoreComponent(result.slot, i, SLJIT_FR0);
    }
    Release(arg);
    return result;
  }
  if (expr.op == Op::kNot) {
    Temp arg = EmitExpr(*expr.args[0]);
    if (arg.slot < 0)
      return Temp{};
    Temp result = NewTemp(Type::Bool());
    EmitLoadComponent(arg.slot, expr.args[0]->type, 0, SLJIT_FR0);
    sljit_emit_fset32(compiler_, SLJIT_FR1, 0.0f);
    sljit_jump* jump = sljit_emit_fcmp(compiler_,
                                       SLJIT_ORDERED_EQUAL | SLJIT_32,
                                       SLJIT_FR0, 0, SLJIT_FR1, 0);
    EmitBoolResult(result.slot, false);
    sljit_jump* end = sljit_emit_jump(compiler_, SLJIT_JUMP);
    sljit_label* label = sljit_emit_label(compiler_);
    sljit_set_label(jump, label);
    EmitBoolResult(result.slot, true);
    sljit_label* end_label = sljit_emit_label(compiler_);
    sljit_set_label(end, end_label);
    Release(arg);
    return result;
  }
  return Temp{};
}

ShaderJit::Impl::Temp ShaderJit::Impl::EmitBinary(const Expr& expr) {
  if (expr.op == Op::kModulo || expr.op == Op::kShiftLeft ||
      expr.op == Op::kShiftRight || expr.op == Op::kBitAnd ||
      expr.op == Op::kBitXor || expr.op == Op::kBitOr)
    return Temp{};

  Temp left = EmitExpr(*expr.args[0]);
  Temp right = EmitExpr(*expr.args[1]);
  if (left.slot < 0 || right.slot < 0) {
    Release(left);
    Release(right);
    return Temp{};
  }

  Temp result = NewTemp(expr.type);
  if (result.slot < 0) {
    Release(left);
    Release(right);
    return result;
  }

  if (expr.op == Op::kLess || expr.op == Op::kLessEqual ||
      expr.op == Op::kGreater || expr.op == Op::kGreaterEqual ||
      expr.op == Op::kEqual || expr.op == Op::kNotEqual) {
    if (!EmitComparison(result.slot, expr, left.slot, right.slot)) {
      Release(result);
      result = Temp{};
    }
  } else if (expr.op == Op::kLogicalAnd || expr.op == Op::kLogicalOr ||
             expr.op == Op::kLogicalXor) {
    if (!EmitLogical(result.slot, expr.op, expr, left.slot, right.slot)) {
      Release(result);
      result = Temp{};
    }
  } else if (expr.op == Op::kMultiply &&
             (left.type.is_matrix() || right.type.is_matrix())) {
    if (!EmitMatrixMultiply(result.slot, left.slot, left.type, right.slot,
                            right.type, expr.type)) {
      Release(result);
      result = Temp{};
    }
  } else {
    sljit_s32 op = SLJIT_ADD_F32;
    switch (expr.op) {
      case Op::kAdd:
        op = SLJIT_ADD_F32;
        break;
      case Op::kSubtract:
        op = SLJIT_SUB_F32;
        break;
      case Op::kDivide:
        op = SLJIT_DIV_F32;
        break;
      case Op::kMultiply:
        op = SLJIT_MUL_F32;
        break;
      default:
        Release(result);
        result = Temp{};
        break;
    }
    if (result.slot >= 0) {
      const int count = std::max(expr.args[0]->type.components(),
                                 expr.args[1]->type.components());
      for (int i = 0; i < expr.type.components(); ++i) {
        EmitLoadComponent(left.slot, expr.args[0]->type,
                          expr.args[0]->type.is_scalar() ? 0 : i, SLJIT_FR0);
        EmitLoadComponent(right.slot, expr.args[1]->type,
                          expr.args[1]->type.is_scalar() ? 0
                                                        : std::min(i, count - 1),
                          SLJIT_FR1);
        sljit_emit_fop2(compiler_, op, SLJIT_FR0, 0, SLJIT_FR0, 0, SLJIT_FR1,
                        0);
        EmitStoreComponent(result.slot, i, SLJIT_FR0);
      }
    }
  }

  Release(left);
  Release(right);
  return result;
}

bool ShaderJit::Impl::EmitBuiltinCall(const Expr& expr,
                                       int out,
                                       int arg_area) {
  std::unique_ptr<JitBuiltinCall> call(new JitBuiltinCall());
  call->function = expr.builtin_func;
  call->result_type = expr.type;
  call->arg_count = static_cast<int>(expr.args.size());
  for (int i = 0; i < call->arg_count; ++i)
    call->arg_types[i] = expr.args[i]->type;

  std::vector<Temp> args;
  args.reserve(expr.args.size());
  for (const ExprPtr& arg : expr.args)
    args.push_back(EmitExpr(*arg));
  if (std::any_of(args.begin(), args.end(),
                  [](const Temp& t) { return t.slot < 0; })) {
    for (const Temp& t : args)
      Release(t);
    return false;
  }
  for (int i = 0; i < call->arg_count; ++i)
    for (int c = 0; c < kValueFloats; ++c)
      EmitMoveComponent(arg_area + i, c, args[i].slot, c);
  for (const Temp& t : args)
    Release(t);

  JitBuiltinCall* raw = call.get();
  builtin_calls_.push_back(std::move(call));
  sljit_emit_op1(compiler_, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM,
                 reinterpret_cast<sljit_sw>(raw));
  sljit_emit_op2(compiler_, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S1, 0, SLJIT_IMM,
                 SlotOffset(arg_area));
  sljit_emit_op2(compiler_, SLJIT_ADD, SLJIT_R2, 0, SLJIT_S1, 0, SLJIT_IMM,
                 SlotOffset(out));
  return Check(sljit_emit_icall(compiler_, SLJIT_CALL,
                                SLJIT_ARGS3V(P, P, P), SLJIT_IMM,
                                SLJIT_FUNC_ADDR(JitBuiltin)),
               nullptr);
}

bool ShaderJit::Impl::EmitTextureCall(const Expr& expr,
                                       int out,
                                       int arg_area) {
  if (expr.args.size() > 0 && expr.args[0]->type.base() ==
                                  BaseType::kSampler2DArrayShadow) {
    // Array shadow lookups go through the interpreter for now; the scalar
    // path does not model the per-layer depth comparison.
    return Fail("2D array shadow sampling is not supported by the JIT");
  }
  std::unique_ptr<JitTextureCall> call(new JitTextureCall());
  call->function = expr.builtin_func;
  call->arg_count = static_cast<int>(expr.args.size());
  for (int i = 0; i < call->arg_count; ++i)
    call->arg_types[i] = expr.args[i]->type;
  if (getenv("ZGL_DEBUG_CUBE")) {
    static int emb = 0;
    if (emb++ < 4)
      fprintf(stderr, "[emittex] func=%d coordoff=%d arg1kind=%d comps=%d idx=%d vary=%zu\n",
              (int)expr.builtin_func, call->coord_offset,
              expr.args.size() > 1 ? (int)expr.args[1]->kind : -1,
              expr.args.size() > 1 ? expr.args[1]->type.components() : 0,
              expr.args.size() > 1 ? expr.args[1]->index : -1,
              ast_.varyings.size());
  }
  if (expr.args.size() > 1) {
    const auto varying_offset = [&](int index) {
      int offset = ast_.varyings[index].varying_offset;
      if (offset < 0) {
        offset = 0;
        for (int i = 0; i < index; ++i)
          offset += ast_.varyings[i].type.components();
      }
      return offset;
    };
    const Expr* coord_expr = expr.args[1].get();
    int first_component = -1;
    if (coord_expr->kind == Expr::Kind::kVariable) {
      const int varying_index = coord_expr->index;
      if (varying_index >= 0 &&
          varying_index < static_cast<int>(ast_.varyings.size()))
        call->coord_offset = varying_offset(varying_index);
    } else if (coord_expr->kind == Expr::Kind::kCall &&
               coord_expr->is_constructor) {
      for (const ExprPtr& argument : coord_expr->args) {
        if (argument->kind == Expr::Kind::kVariable &&
            argument->index >= 0 &&
            argument->index < static_cast<int>(ast_.varyings.size())) {
          call->coord_offset = varying_offset(argument->index);
          call->coord_dims = argument->type.components();
          break;
        }
      }
    } else if (coord_expr->kind == Expr::Kind::kField &&
               coord_expr->base &&
               coord_expr->base->kind == Expr::Kind::kVariable &&
               coord_expr->base->index >= 0 &&
               coord_expr->base->index <
                   static_cast<int>(ast_.varyings.size())) {
      const std::string& swizzle = coord_expr->text;
      bool contiguous = true;
      for (size_t i = 0; i < swizzle.size(); ++i) {
        int component = -1;
        switch (swizzle[i]) {
          case 'x': component = 0; break;
          case 'y': component = 1; break;
          case 'z': component = 2; break;
          case 'w': component = 3; break;
          default: contiguous = false; break;
        }
        if (component < 0) {
          contiguous = false;
          break;
        }
        if (i == 0)
          first_component = component;
        else if (component != first_component + static_cast<int>(i))
          contiguous = false;
      }
      if (contiguous && first_component >= 0 &&
          first_component + static_cast<int>(swizzle.size()) <= 4) {
        call->coord_offset =
            varying_offset(coord_expr->base->index) + first_component;
      }
    }
  }

  std::vector<Temp> args;
  args.reserve(expr.args.size());
  for (const ExprPtr& arg : expr.args)
    args.push_back(EmitExpr(*arg));
  if (std::any_of(args.begin(), args.end(),
                  [](const Temp& t) { return t.slot < 0; })) {
    for (const Temp& t : args)
      Release(t);
    return false;
  }
  for (int i = 0; i < call->arg_count; ++i)
    for (int c = 0; c < kValueFloats; ++c)
      EmitMoveComponent(arg_area + i, c, args[i].slot, c);
  for (const Temp& t : args)
    Release(t);

  JitTextureCall* raw = call.get();
  texture_calls_.push_back(std::move(call));
  sljit_emit_op1(compiler_, SLJIT_MOV, SLJIT_R0, 0, SLJIT_S0, 0);
  sljit_emit_op1(compiler_, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM,
                 reinterpret_cast<sljit_sw>(raw));
  sljit_emit_op2(compiler_, SLJIT_ADD, SLJIT_R2, 0, SLJIT_S1, 0, SLJIT_IMM,
                 SlotOffset(arg_area));
  sljit_emit_op2(compiler_, SLJIT_ADD, SLJIT_R3, 0, SLJIT_S1, 0, SLJIT_IMM,
                 SlotOffset(out));
  return Check(sljit_emit_icall(compiler_, SLJIT_CALL,
                                SLJIT_ARGS4V(P, P, P, P), SLJIT_IMM,
                                SLJIT_FUNC_ADDR(JitTexture)),
               nullptr);
}

ShaderJit::Impl::Temp ShaderJit::Impl::EmitCall(const Expr& expr) {
  if (expr.is_constructor)
    return EmitConstructor(expr);
  switch (expr.builtin_func) {
    case BuiltinFunc::kTextureSize:
    case BuiltinFunc::kTextureOffset:
    case BuiltinFunc::kTextureLodOffset:
    case BuiltinFunc::kTextureGrad:
    case BuiltinFunc::kTextureGradOffset:
    case BuiltinFunc::kTextureProjOffset:
    case BuiltinFunc::kTextureProjLodOffset:
    case BuiltinFunc::kTextureProjGrad:
    case BuiltinFunc::kTextureProjGradOffset:
    case BuiltinFunc::kTexelFetch:
    case BuiltinFunc::kTexelFetchOffset:
      // Served by the interpreter: these need per-level fetches, integer
      // payloads and offset vectors the scalar JIT does not model.
      Fail("texture lookup is not supported by the JIT");
      return Temp{};
    default:
      break;
  }
  if (expr.builtin_func == BuiltinFunc::kTexture2D ||
      expr.builtin_func == BuiltinFunc::kTexture2DProj ||
      expr.builtin_func == BuiltinFunc::kTextureCube ||
      expr.builtin_func == BuiltinFunc::kTexture3D ||
      expr.builtin_func == BuiltinFunc::kTexture ||
      expr.builtin_func == BuiltinFunc::kTextureProj ||
      expr.builtin_func == BuiltinFunc::kTextureProjLod ||
      expr.builtin_func == BuiltinFunc::kTextureLod) {
    Temp result = NewTemp(expr.type);
    if (result.slot < 0)
      return result;
    if (!EmitTextureCall(expr, result.slot, arg_area_)) {
      Release(result);
      return Temp{};
    }
    return result;
  }
  if (expr.builtin_func == BuiltinFunc::kDfdx ||
      expr.builtin_func == BuiltinFunc::kDfdy ||
      expr.builtin_func == BuiltinFunc::kFwidth) {
    Temp result = NewTemp(expr.type);
    for (int i = 0; i < expr.type.components(); ++i)
      EmitSetComponent(result.slot, i, 0.0f);
    return result;
  }
  Temp result = NewTemp(expr.type);
  if (result.slot < 0)
    return result;
  if (!EmitBuiltinCall(expr, result.slot, arg_area_)) {
    Release(result);
    return Temp{};
  }
  return result;
}

ShaderJit::Impl::Temp ShaderJit::Impl::EmitAssignment(const Expr& expr) {
  Temp value = EmitExpr(*expr.args[1]);
  if (value.slot < 0)
    return Temp{};
  if (expr.op != Op::kAssign) {
    Temp current = EmitExpr(*expr.args[0]);
    if (current.slot < 0) {
      Release(value);
      return Temp{};
    }
    Op op = Op::kAdd;
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
      default:
        Release(current);
        Release(value);
        return Temp{};
    }
    Temp combined = NewTemp(expr.args[0]->type);
    if (combined.slot < 0) {
      Release(current);
      Release(value);
      return combined;
    }
    if (op == Op::kMultiply) {
      if (!EmitMatrixMultiply(combined.slot, current.slot, current.type,
                              value.slot, value.type, combined.type)) {
        Release(combined);
        Release(current);
        Release(value);
        return Temp{};
      }
    } else {
      const sljit_s32 slop =
          op == Op::kAdd ? SLJIT_ADD_F32
                         : (op == Op::kSubtract ? SLJIT_SUB_F32
                                                : SLJIT_DIV_F32);
      for (int i = 0; i < combined.type.components(); ++i) {
        EmitLoadComponent(current.slot, current.type,
                          current.type.is_scalar() ? 0 : i, SLJIT_FR0);
        EmitLoadComponent(value.slot, value.type,
                          value.type.is_scalar() ? 0 : i, SLJIT_FR1);
        sljit_emit_fop2(compiler_, slop, SLJIT_FR0, 0, SLJIT_FR0, 0, SLJIT_FR1,
                        0);
        EmitStoreComponent(combined.slot, i, SLJIT_FR0);
      }
    }
    Release(current);
    Release(value);
    value = combined;
  }
  Temp converted = ConvertTo(value, expr.args[0]->type);
  if (value.slot != converted.slot)
    Release(value);
  if (converted.slot < 0)
    return converted;
  if (!EmitAssignTarget(*expr.args[0], converted.slot, expr.args[0]->type)) {
    Release(converted);
    return Temp{};
  }
  return converted;
}

ShaderJit::Impl::Temp ShaderJit::Impl::EmitTernary(const Expr& expr) {
  Temp result = NewTemp(expr.type);
  if (result.slot < 0)
    return result;
  Temp condition = EmitExpr(*expr.args[0]);
  if (condition.slot < 0) {
    Release(result);
    return Temp{};
  }
  sljit_jump* condition_false =
      EmitJumpIfFalse(condition.slot, expr.args[0]->type);
  if (!condition_false) {
    Release(condition);
    Release(result);
    return Temp{};
  }
  Temp then_value = EmitExpr(*expr.args[1]);
  if (then_value.slot < 0) {
    Release(condition);
    Release(result);
    return Temp{};
  }
  EmitCopy(result.slot, expr.type, then_value.slot, then_value.type);
  Release(then_value);
  sljit_jump* end = sljit_emit_jump(compiler_, SLJIT_JUMP);
  sljit_label* else_label = sljit_emit_label(compiler_);
  sljit_set_label(condition_false, else_label);
  Temp else_value = EmitExpr(*expr.args[2]);
  if (else_value.slot < 0) {
    Release(condition);
    Release(result);
    return Temp{};
  }
  EmitCopy(result.slot, expr.type, else_value.slot, else_value.type);
  Release(else_value);
  sljit_label* end_label = sljit_emit_label(compiler_);
  sljit_set_label(end, end_label);
  Release(condition);
  return result;
}

ShaderJit::Impl::Temp ShaderJit::Impl::EmitExpr(const Expr& expr) {
  if (failed_)
    return Temp{};
  switch (expr.kind) {
    case Expr::Kind::kLiteral:
      return EmitLiteral(expr);
    case Expr::Kind::kVariable:
      return EmitVariable(expr);
    case Expr::Kind::kField:
      return EmitField(expr);
    case Expr::Kind::kIndex:
      return EmitIndex(expr);
    case Expr::Kind::kCall:
      return EmitCall(expr);
    case Expr::Kind::kUnary:
      return EmitUnary(expr);
    case Expr::Kind::kBinary:
      return EmitBinary(expr);
    case Expr::Kind::kAssign:
      return EmitAssignment(expr);
    case Expr::Kind::kTernary:
      return EmitTernary(expr);
    case Expr::Kind::kComma:
      return Temp{};
  }
  return Temp{};
}

bool ShaderJit::Impl::IsVariableWritable(const Expr& expr) const {
  return expr.kind == Expr::Kind::kVariable &&
         expr.storage != Storage::kUniform &&
         expr.storage != Storage::kAttribute &&
         expr.storage != Storage::kVaryingIn;
}

bool ShaderJit::Impl::EmitStoreVariable(const Expr& target,
                                        int slot,
                                        const Type& type) {
  switch (target.storage) {
    case Storage::kLocal:
      for (int i = 0; i < type.components(); ++i)
        EmitMoveComponent(target.index, i, slot, i);
      return true;
    case Storage::kGlobal:
    case Storage::kBuiltin: {
      for (int i = 0; i < type.components(); ++i) {
        EmitLoadComponent(slot, type, i, SLJIT_FR3);
        const int offset =
            ValueDataOffset(target.index) + i * static_cast<int>(sizeof(float));
        sljit_emit_fop1(compiler_, SLJIT_MOV_F32, SLJIT_MEM1(SLJIT_S2), offset,
                        SLJIT_FR3, 0);
      }
      return true;
    }
    case Storage::kVaryingOut: {
      const int offset =
          target.index >= 0 &&
                  target.index < static_cast<int>(ast_.varyings.size())
              ? ast_.varyings[target.index].varying_offset
              : -1;
      if (offset < 0)
        return false;
      sljit_emit_op1(compiler_, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0),
                     offsetof(JitRuntime, varyings_out));
      sljit_jump* missing =
          sljit_emit_cmp(compiler_, SLJIT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
      for (int i = 0; i < type.components(); ++i) {
        EmitLoadComponent(slot, type, i, SLJIT_FR3);
        const int component_offset =
            (offset + i) * static_cast<int>(sizeof(float));
        sljit_emit_fop1(compiler_, SLJIT_MOV_F32, SLJIT_MEM1(SLJIT_R0),
                        component_offset, SLJIT_FR3, 0);
      }
      sljit_label* ok = sljit_emit_label(compiler_);
      sljit_set_label(missing, ok);
      return true;
    }
    default:
      return false;
  }
}

bool ShaderJit::Impl::EmitAssignTarget(const Expr& target,
                                       int slot,
                                       const Type& type) {
  if (target.kind == Expr::Kind::kVariable)
    return EmitStoreVariable(target, slot, type);
  if (target.kind == Expr::Kind::kField) {
    Temp base = EmitExpr(*target.base);
    if (base.slot < 0)
      return false;
    const char* sets[] = {"xyzw", "rgba", "stpq"};
    for (size_t i = 0; i < target.text.size(); ++i) {
      int component = 0;
      for (const char* set : sets) {
        const char* found = strchr(set, target.text[i]);
        if (found) {
          component = static_cast<int>(found - set);
          break;
        }
      }
      if (component < base.type.components()) {
        const int source_index =
            type.is_scalar() ? 0 : static_cast<int>(i);
        EmitMoveComponent(base.slot, component, slot, source_index);
      }
    }
    const bool stored = EmitAssignTarget(*target.base, base.slot, base.type);
    Release(base);
    return stored;
  }
  if (target.kind == Expr::Kind::kIndex) {
    int index = 0;
    if (!ConstantIndex(*target.args[0], &index))
      return false;
    Temp base = EmitExpr(*target.base);
    if (base.slot < 0)
      return false;
    if (base.type.is_matrix()) {
      if (index >= 0 && index < base.type.columns()) {
        for (int r = 0; r < base.type.rows(); ++r)
          EmitMoveComponent(base.slot, index * base.type.rows() + r, slot, r);
      }
    } else if (index >= 0 && index < base.type.components()) {
      EmitMoveComponent(base.slot, index, slot, 0);
    }
    const bool stored = EmitAssignTarget(*target.base, base.slot, base.type);
    Release(base);
    return stored;
  }
  return false;
}

bool ShaderJit::Impl::EmitStatement(const Statement& statement) {
  if (failed_)
    return false;
  switch (statement.kind) {
    case Statement::Kind::kBlock:
      return EmitBlock(statement.statements);
    case Statement::Kind::kDeclaration: {
      for (const auto& declarator : statement.declarators) {
        const Variable& variable = declarator.variable;
        if (declarator.initializer) {
          Temp value = EmitExpr(*declarator.initializer);
          if (value.slot < 0)
            return false;
          Temp converted = ConvertTo(value, variable.type);
          if (value.slot != converted.slot)
            Release(value);
          if (converted.slot < 0)
            return false;
          Expr target;
          target.kind = Expr::Kind::kVariable;
          target.storage = variable.storage;
          target.index = variable.index;
          target.type = variable.type;
          if (!EmitStoreVariable(target, converted.slot, variable.type)) {
            Release(converted);
            return false;
          }
          Release(converted);
        } else {
          Expr target;
          target.kind = Expr::Kind::kVariable;
          target.storage = variable.storage;
          target.index = variable.index;
          target.type = variable.type;
          for (int i = 0; i < variable.type.components(); ++i)
            EmitSetComponent(target.index, i, 0.0f);
        }
      }
      return true;
    }
    case Statement::Kind::kExpression: {
      if (!statement.expression)
        return true;
      Temp value = EmitExpr(*statement.expression);
      if (value.slot < 0)
        return false;
      Release(value);
      return true;
    }
    case Statement::Kind::kIf:
      return EmitIf(statement);
    case Statement::Kind::kReturn:
      if (statement.expression) {
        Temp value = EmitExpr(*statement.expression);
        if (value.slot < 0)
          return false;
        Release(value);
      }
      end_jumps_.push_back(sljit_emit_jump(compiler_, SLJIT_JUMP));
      return true;
    case Statement::Kind::kDiscard:
      sljit_emit_op1(compiler_, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, 1);
      sljit_emit_op1(compiler_, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0),
                     offsetof(JitRuntime, discarded), SLJIT_R0, 0);
      end_jumps_.push_back(sljit_emit_jump(compiler_, SLJIT_JUMP));
      return true;
    case Statement::Kind::kFor:
    case Statement::Kind::kWhile:
    case Statement::Kind::kDoWhile:
    case Statement::Kind::kBreak:
    case Statement::Kind::kContinue:
      return Fail("unsupported control flow");
  }
  return false;
}

bool ShaderJit::Impl::EmitBlock(const std::vector<StmtPtr>& body) {
  for (const StmtPtr& statement : body) {
    if (statement && !EmitStatement(*statement))
      return false;
  }
  return true;
}

bool ShaderJit::Impl::EmitIf(const Statement& statement) {
  if (!statement.condition || !statement.condition->expression)
    return false;
  Temp condition = EmitExpr(*statement.condition->expression);
  if (condition.slot < 0)
    return false;
  sljit_jump* condition_false =
      EmitJumpIfFalse(condition.slot, statement.condition->expression->type);
  if (!condition_false) {
    Release(condition);
    return false;
  }
  Release(condition);
  if (statement.then_branch &&
      !EmitStatement(*statement.then_branch))
    return false;
  if (statement.else_branch) {
    sljit_jump* end = sljit_emit_jump(compiler_, SLJIT_JUMP);
    sljit_label* else_label = sljit_emit_label(compiler_);
    sljit_set_label(condition_false, else_label);
    if (!EmitStatement(*statement.else_branch))
      return false;
    sljit_label* end_label = sljit_emit_label(compiler_);
    sljit_set_label(end, end_label);
  } else {
    sljit_label* end_label = sljit_emit_label(compiler_);
    sljit_set_label(condition_false, end_label);
  }
  return true;
}

bool ShaderJit::Impl::Compile(std::string* reason) {
  g_jit_counters.compile_attempts++;
  if (ast_.uses_depth_range) {
    if (reason)
      *reason = "gl_DepthRange requires the interpreter";
    g_jit_counters.fallback_stages++;
    return false;
  }
  if (ast_.uses_out_params) {
    if (reason)
      *reason = "out and inout parameters require the interpreter";
    g_jit_counters.fallback_stages++;
    return false;
  }
  if (ast_.uses_switch) {
    if (reason)
      *reason = "switch statements require the interpreter";
    g_jit_counters.fallback_stages++;
    return false;
  }
  if (getenv("ZGL_JIT_TRACE"))
    fprintf(stderr, "JIT compile start stage=%d\n", static_cast<int>(ast_.stage));
  if (!Scan()) {
    g_jit_counters.fallback_stages++;
    if (reason)
      *reason = "shader uses a construct outside the SLJIT fast path";
    return false;
  }
  if (getenv("ZGL_JIT_TRACE"))
    fprintf(stderr, "JIT scan ok stage=%d\n", static_cast<int>(ast_.stage));
  compiler_ = sljit_create_compiler(nullptr);
  if (compiler_ && getenv("ZGL_JIT_VERBOSE"))
    sljit_compiler_verbose(compiler_, stderr);
  if (!compiler_) {
    g_jit_counters.fallback_stages++;
    if (reason)
      *reason = "could not allocate SLJIT compiler";
    return false;
  }

  arg_area_ = main_->slot_count;
  next_temp_ = arg_area_ + kMaxBuiltinArgs;
  frame_slots_ = next_temp_;
  if (frame_slots_ >= kMaxJitFrameSlots) {
    if (reason)
      *reason = "too many local slots";
    g_jit_counters.fallback_stages++;
    return false;
  }

  if (!EmitPrologue() || !EmitBlock(main_->body)) {
    if (reason)
      *reason = failure_.empty() ? "could not lower shader" : failure_;
    g_jit_counters.fallback_stages++;
    return false;
  }
  if (getenv("ZGL_JIT_TRACE"))
    fprintf(stderr, "JIT lowered stage=%d\n", static_cast<int>(ast_.stage));
  end_label_ = sljit_emit_label(compiler_);
  for (sljit_jump* jump : end_jumps_)
    sljit_set_label(jump, end_label_);
  sljit_emit_return_void(compiler_);

  void* code = sljit_generate_code(compiler_, 0, nullptr);
  if (getenv("ZGL_JIT_TRACE"))
    fprintf(stderr, "JIT generated stage=%d code=%p\n",
            static_cast<int>(ast_.stage), code);
  sljit_free_compiler(compiler_);
  compiler_ = nullptr;
  if (!code) {
    g_jit_counters.fallback_stages++;
    if (reason)
      *reason = "SLJIT code generation failed";
    return false;
  }
  code_ = code;
  entry_ = reinterpret_cast<Entry>(code);
  locals_.assign(static_cast<size_t>(frame_slots_) * kValueFloats, 0.0f);
  if (getenv("ZGL_JIT_TRACE")) {
    fprintf(stderr, "JIT compiled stage=%d slots=%d code=%p\n",
            static_cast<int>(ast_.stage), frame_slots_, code);
  }
  g_jit_counters.stages_compiled++;
  return true;
}

bool ShaderJit::Impl::Run(const JitRunInputs& inputs, bool* discarded) {
  if (!entry_)
    return false;
  if (getenv("ZGL_JIT_TRACE"))
    fprintf(stderr, "JIT run stage=%d entry=%p\n", static_cast<int>(ast_.stage),
            reinterpret_cast<void*>(entry_));
  JitRuntime runtime = {};
  runtime.uniforms = inputs.uniforms;
  runtime.uniform_map = inputs.uniform_map;
  runtime.attributes = inputs.attributes;
  runtime.varyings_in = inputs.varyings_in;
  runtime.varyings_dx = inputs.varyings_dx;
  runtime.varyings_dy = inputs.varyings_dy;
  runtime.varyings_out = inputs.varyings_out;
  runtime.sampler = inputs.sampler;
  runtime.globals = inputs.globals;
  runtime.locals = locals_.data();
  runtime.discarded = 0;
  entry_(&runtime);
  g_jit_counters.dispatches++;
  if (discarded)
    *discarded = runtime.discarded != 0;
  return true;
}

// ---------------------------------------------------------------------------
// Public facade.

ShaderJit::ShaderJit(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)), frame_slots_(impl_->frame_slots()) {}

ShaderJit::~ShaderJit() = default;

std::unique_ptr<ShaderJit> ShaderJit::Compile(const ShaderAst& ast,
                                              std::string* reason) {
  if (getenv("ZGL_DISABLE_JIT")) {
    zgl::glsl::g_jit_counters.fallback_stages++;
    if (reason)
      *reason = "disabled by ZGL_DISABLE_JIT";
    return nullptr;
  }
  std::unique_ptr<Impl> impl(new Impl(ast));
  if (!impl->Compile(reason))
    return nullptr;
  return std::unique_ptr<ShaderJit>(new ShaderJit(std::move(impl)));
}

bool ShaderJit::Run(const JitRunInputs& inputs, bool* discarded) {
  return impl_->Run(inputs, discarded);
}

}  // namespace glsl
}  // namespace zgl

extern "C" void zglGetJitStats(ZglJitStats* stats) {
  if (!stats)
    return;
  stats->compile_attempts = zgl::glsl::g_jit_counters.compile_attempts;
  stats->stages_compiled = zgl::glsl::g_jit_counters.stages_compiled;
  stats->fallback_stages = zgl::glsl::g_jit_counters.fallback_stages;
  stats->dispatches = zgl::glsl::g_jit_counters.dispatches;
}

extern "C" void zglResetJitStats(void) {
  zgl::glsl::g_jit_counters = zgl::glsl::JitCounters();
}
