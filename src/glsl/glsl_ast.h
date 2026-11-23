// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZGL_SRC_GLSL_GLSL_AST_H_
#define ZGL_SRC_GLSL_GLSL_AST_H_

#include <memory>
#include <string>
#include <vector>

#include "src/glsl/glsl_types.h"

namespace zgl {
namespace glsl {

enum class Stage { kVertex, kFragment };

// Where a variable lives at run time.  The interpreter switches on this to
// read and write the right backing array.
enum class Storage {
  kLocal,
  kGlobal,
  kUniform,
  kAttribute,
  kVaryingIn,
  kVaryingOut,
  kBuiltin,
};

enum class BuiltinId {
  kNone,
  kPosition,     // gl_Position, vec4
  kPointSize,    // gl_PointSize, float
  kFragCoord,    // gl_FragCoord, vec4
  kFrontFacing,  // gl_FrontFacing, bool
  kPointCoord,   // gl_PointCoord, vec2
  kVertexId,     // gl_VertexID, int
  kInstanceId,   // gl_InstanceID, int
  kFragColor,    // gl_FragColor, vec4
  kFragData,     // gl_FragData[0], vec4
  kFragDepth,    // gl_FragDepth, float
};

// Operator codes, indexed by precedence level in the parser.
enum class Op {
  kNone,
  // Unary.
  kNegate,
  kNot,
  kBitNot,
  kPreIncrement,
  kPreDecrement,
  kPostIncrement,
  kPostDecrement,
  // Binary.
  kAdd,
  kSubtract,
  kMultiply,
  kDivide,
  kModulo,
  kShiftLeft,
  kShiftRight,
  kLess,
  kLessEqual,
  kGreater,
  kGreaterEqual,
  kEqual,
  kNotEqual,
  kBitAnd,
  kBitXor,
  kBitOr,
  kLogicalAnd,
  kLogicalXor,
  kLogicalOr,
  // Assignment.
  kAssign,
  kAddAssign,
  kSubtractAssign,
  kMultiplyAssign,
  kDivideAssign,
  kModuloAssign,
  kShiftLeftAssign,
  kShiftRightAssign,
  kBitAndAssign,
  kBitXorAssign,
  kBitOrAssign,
};

// Built-in function identifiers.  Unknown names stay kNone and are resolved
// against user functions at parse time.
enum class BuiltinFunc {
  kNone,
  kRadians,
  kDegrees,
  kSin,
  kCos,
  kTan,
  kAsin,
  kAcos,
  kAtan,
  kPow,
  kExp,
  kLog,
  kExp2,
  kLog2,
  kSqrt,
  kInverseSqrt,
  kAbs,
  kSign,
  kFloor,
  kCeil,
  kFract,
  kMod,
  kMin,
  kMax,
  kClamp,
  kMix,
  kStep,
  kSmoothStep,
  kLength,
  kDistance,
  kDot,
  kCross,
  kNormalize,
  kFaceForward,
  kReflect,
  kRefract,
  kMatrixCompMult,
  kTranspose,
  kDeterminant,
  kInverse,
  kLessThan,
  kLessThanEqual,
  kGreaterThan,
  kGreaterThanEqual,
  kEqualFunc,
  kNotEqualFunc,
  kAny,
  kAll,
  kNot,
  kTexture2D,
  kTexture2DProj,
  kTextureCube,
  kTexture3D,
  kTexture,
  kTextureLod,
  kDfdx,
  kDfdy,
  kFwidth,
};

struct Variable {
  std::string name;
  Type type;
  Storage storage = Storage::kLocal;
  int index = -1;           // Slot / attribute location / uniform index.
  int varying_offset = -1;  // Float offset inside the varying array.
  BuiltinId builtin = BuiltinId::kNone;
  bool read_only = false;
};

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct Expr {
  enum class Kind {
    kLiteral,
    kVariable,
    kField,
    kIndex,
    kCall,
    kUnary,
    kBinary,
    kAssign,
    kTernary,
    kComma,
  };

  Kind kind = Kind::kLiteral;
  int line = 0;
  Type type;
  float literal = 0.0f;

  // kVariable: |text| is the source name, |storage| and |index| tell the
  // interpreter where to read from.
  Storage storage = Storage::kLocal;
  int index = -1;
  BuiltinId builtin = BuiltinId::kNone;

  // kField: |text| is the swizzle or field name.
  // kCall: |text| is the function name; |builtin_func| is kNone for user
  // functions, in which case |function_index| points into ShaderAst.
  // Constructors are kCall nodes with |is_constructor| set and |type| holding
  // the constructed type.
  std::string text;
  BuiltinFunc builtin_func = BuiltinFunc::kNone;
  int function_index = -1;
  bool is_constructor = false;

  Op op = Op::kNone;

  ExprPtr base;
  std::vector<ExprPtr> args;
};

struct Statement;
using StmtPtr = std::unique_ptr<Statement>;

struct Statement {
  enum class Kind {
    kBlock,
    kDeclaration,
    kExpression,
    kIf,
    kFor,
    kWhile,
    kDoWhile,
    kReturn,
    kBreak,
    kContinue,
    kDiscard,
  };

  Kind kind = Kind::kBlock;
  int line = 0;
  std::vector<StmtPtr> statements;  // Block body / declaration list.
  StmtPtr init;                     // For-loop initialiser.
  StmtPtr condition;                // If/for/while/do condition.
  StmtPtr step;                     // For-loop step.
  StmtPtr then_branch;              // If / loop body.
  StmtPtr else_branch;
  ExprPtr expression;  // Expression statement / return value.

  // kDeclaration entries.
  struct Declarator {
    Variable variable;
    ExprPtr initializer;
  };
  std::vector<Declarator> declarators;
};

struct Function {
  std::string name;
  Type return_type;
  std::vector<Variable> parameters;
  std::vector<Variable> locals;
  std::vector<StmtPtr> body;
  int slot_count = 0;  // Parameters and locals share one slot array.
  int line = 0;
  bool defined = false;
};

// Initialiser of a global variable, evaluated once at link time.
struct GlobalInit {
  int slot = -1;
  ExprPtr expression;
};

// One compiled stage.  The linker fills in varying offsets shared with the
// other stage and assigns uniform indices.
struct ShaderAst {
  Stage stage = Stage::kVertex;
  int version = 100;

  std::vector<Variable> globals;  // Storage::kGlobal and built-ins.
  std::vector<Variable> uniforms;
  std::vector<Variable> attributes;
  std::vector<Variable> varyings;  // Outputs for VS, inputs for FS.
  std::vector<Function> functions;
  std::vector<GlobalInit> global_initializers;

  int global_slots = 0;
  int main_function = -1;

  // Global slot of each built-in variable, or -1 when it is not referenced.
  // Indexed by the BuiltinId enum value.
  std::vector<int> builtin_slots;

  bool uses_discard = false;
  bool uses_frag_depth = false;
  bool uses_point_size = false;
  bool uses_frag_coord = false;
  bool uses_front_facing = false;
  bool uses_point_coord = false;
  bool uses_frag_color = false;
  bool uses_frag_data = false;
  bool declares_position = false;

  Function* main() {
    return main_function >= 0 ? &functions[main_function] : nullptr;
  }
  const Function* main() const {
    return main_function >= 0 ? &functions[main_function] : nullptr;
  }
};

}  // namespace glsl
}  // namespace zgl

#endif  // ZGL_SRC_GLSL_GLSL_AST_H_
