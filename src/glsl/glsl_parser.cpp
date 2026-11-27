// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/glsl/glsl_parser.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <cstring>
#include <utility>

namespace glanceasy {
namespace glsl {

namespace {

struct TypeNameEntry {
  const char* name;
  Type type;
};

const TypeNameEntry kTypeNames[] = {
    {"void", Type::Void()},
    {"float", Type::Float()},
    {"int", Type::Int()},
    {"uint", Type::UInt()},
    {"bool", Type::Bool()},
    {"vec2", Type::Vec(2)},
    {"vec3", Type::Vec(3)},
    {"vec4", Type::Vec(4)},
    {"ivec2", Type::IVec(2)},
    {"ivec3", Type::IVec(3)},
    {"ivec4", Type::IVec(4)},
    {"uvec2", Type::UVec(2)},
    {"uvec3", Type::UVec(3)},
    {"uvec4", Type::UVec(4)},
    {"bvec2", Type::BVec(2)},
    {"bvec3", Type::BVec(3)},
    {"bvec4", Type::BVec(4)},
    {"mat2", Type::Mat(2, 2)},
    {"mat3", Type::Mat(3, 3)},
    {"mat4", Type::Mat(4, 4)},
    {"mat2x2", Type::Mat(2, 2)},
    {"mat2x3", Type::Mat(2, 3)},
    {"mat2x4", Type::Mat(2, 4)},
    {"mat3x2", Type::Mat(3, 2)},
    {"mat3x3", Type::Mat(3, 3)},
    {"mat3x4", Type::Mat(3, 4)},
    {"mat4x2", Type::Mat(4, 2)},
    {"mat4x3", Type::Mat(4, 3)},
    {"mat4x4", Type::Mat(4, 4)},
    {"sampler2D", Type(BaseType::kSampler2D, 1, 1)},
    {"samplerCube", Type(BaseType::kSamplerCube, 1, 1)},
    {"sampler3D", Type(BaseType::kSampler3D, 1, 1)},
    {"sampler2DShadow", Type(BaseType::kSampler2DShadow, 1, 1)},
    {"sampler2DArray", Type(BaseType::kSampler2DArray, 1, 1)},
    {"sampler2DArrayShadow", Type(BaseType::kSampler2DArrayShadow, 1, 1)},
    {"samplerCubeShadow", Type(BaseType::kSamplerCubeShadow, 1, 1)},
    {"isampler2D", Type(BaseType::kISampler2D, 1, 1)},
    {"isampler3D", Type(BaseType::kISampler3D, 1, 1)},
    {"isamplerCube", Type(BaseType::kISamplerCube, 1, 1)},
    {"isampler2DArray", Type(BaseType::kISampler2DArray, 1, 1)},
    {"usampler2D", Type(BaseType::kUSampler2D, 1, 1)},
    {"usampler3D", Type(BaseType::kUSampler3D, 1, 1)},
    {"usamplerCube", Type(BaseType::kUSamplerCube, 1, 1)},
    {"usampler2DArray", Type(BaseType::kUSampler2DArray, 1, 1)},
};

bool LookupTypeName(const std::string& name, Type* type) {
  for (const TypeNameEntry& entry : kTypeNames) {
    if (name == entry.name) {
      *type = entry.type;
      return true;
    }
  }
  return false;
}


bool IsQualifierKeyword(const std::string& text) {
  return text == "const" || text == "uniform" || text == "attribute" ||
         text == "varying" || text == "in" || text == "out" ||
         text == "inout" || text == "flat" || text == "smooth" ||
         text == "centroid" || text == "invariant" || text == "lowp" ||
         text == "mediump" || text == "highp" || text == "layout";
}

struct BuiltinVarEntry {
  const char* name;
  BuiltinId id;
  Type type;
  bool vertex_stage;
};

const BuiltinVarEntry kBuiltinVars[] = {
    {"gl_Position", BuiltinId::kPosition, Type::Vec(4), true},
    {"gl_PointSize", BuiltinId::kPointSize, Type::Float(), true},
    {"gl_VertexID", BuiltinId::kVertexId, Type::Int(), true},
    {"gl_InstanceID", BuiltinId::kInstanceId, Type::Int(), true},
    {"gl_SampleID", BuiltinId::kSampleId, Type::Int(), false},
    {"gl_SampleMaskIn", BuiltinId::kSampleMaskIn, Type::Int(), false},
    {"gl_FragCoord", BuiltinId::kFragCoord, Type::Vec(4), false},
    {"gl_FrontFacing", BuiltinId::kFrontFacing, Type::Bool(), false},
    {"gl_PointCoord", BuiltinId::kPointCoord, Type::Vec(2), false},
    {"gl_FragColor", BuiltinId::kFragColor, Type::Vec(4), false},
    {"gl_FragData", BuiltinId::kFragData,
     Type::Array(Type::Vec(4), kMaxFragmentOutputs), false},
    {"gl_FragDepth", BuiltinId::kFragDepth, Type::Float(), false},
    {"__glanceasy_frag_output_0", BuiltinId::kFragOutput0, Type::Vec(4), false},
    {"__glanceasy_frag_output_1", BuiltinId::kFragOutput1, Type::Vec(4), false},
    {"__glanceasy_frag_output_2", BuiltinId::kFragOutput2, Type::Vec(4), false},
    {"__glanceasy_frag_output_3", BuiltinId::kFragOutput3, Type::Vec(4), false},
    {"gl_DepthRange", BuiltinId::kDepthRange, Type::Vec(3), true},
};

struct BuiltinConstantEntry {
  const char* name;
  int value;
};

const BuiltinConstantEntry kBuiltinConstants[] = {
    {"gl_MaxVertexAttribs", 16},
    {"gl_MaxVertexUniformVectors", 256},
    {"gl_MaxVertexTextureImageUnits", 16},
    {"gl_MaxCombinedTextureImageUnits", 32},
    {"gl_MaxTextureImageUnits", 16},
    {"gl_MaxFragmentUniformVectors", 256},
    {"gl_MaxDrawBuffers", 4},
    {"gl_MaxVertexOutputVectors", 64},
    {"gl_MaxFragmentInputVectors", 64},
    {"gl_MinProgramTexelOffset", -8},
    {"gl_MaxProgramTexelOffset", 7},
};

struct BuiltinFuncEntry {
  const char* name;
  BuiltinFunc function;
};

const BuiltinFuncEntry kBuiltinFuncs[] = {
    {"radians", BuiltinFunc::kRadians},
    {"degrees", BuiltinFunc::kDegrees},
    {"sin", BuiltinFunc::kSin},
    {"cos", BuiltinFunc::kCos},
    {"tan", BuiltinFunc::kTan},
    {"sinh", BuiltinFunc::kSinh},
    {"cosh", BuiltinFunc::kCosh},
    {"tanh", BuiltinFunc::kTanh},
    {"asin", BuiltinFunc::kAsin},
    {"acos", BuiltinFunc::kAcos},
    {"asinh", BuiltinFunc::kAsinh},
    {"acosh", BuiltinFunc::kAcosh},
    {"atanh", BuiltinFunc::kAtanh},
    {"atan", BuiltinFunc::kAtan},
    {"pow", BuiltinFunc::kPow},
    {"exp", BuiltinFunc::kExp},
    {"log", BuiltinFunc::kLog},
    {"exp2", BuiltinFunc::kExp2},
    {"log2", BuiltinFunc::kLog2},
    {"sqrt", BuiltinFunc::kSqrt},
    {"inversesqrt", BuiltinFunc::kInverseSqrt},
    {"abs", BuiltinFunc::kAbs},
    {"sign", BuiltinFunc::kSign},
    {"floor", BuiltinFunc::kFloor},
    {"ceil", BuiltinFunc::kCeil},
    {"trunc", BuiltinFunc::kTrunc},
    {"round", BuiltinFunc::kRound},
    {"roundEven", BuiltinFunc::kRoundEven},
    {"fract", BuiltinFunc::kFract},
    {"modf", BuiltinFunc::kModf},
    {"mod", BuiltinFunc::kMod},
    {"min", BuiltinFunc::kMin},
    {"max", BuiltinFunc::kMax},
    {"clamp", BuiltinFunc::kClamp},
    {"mix", BuiltinFunc::kMix},
    {"step", BuiltinFunc::kStep},
    {"smoothstep", BuiltinFunc::kSmoothStep},
    {"length", BuiltinFunc::kLength},
    {"distance", BuiltinFunc::kDistance},
    {"dot", BuiltinFunc::kDot},
    {"cross", BuiltinFunc::kCross},
    {"normalize", BuiltinFunc::kNormalize},
    {"faceforward", BuiltinFunc::kFaceForward},
    {"reflect", BuiltinFunc::kReflect},
    {"refract", BuiltinFunc::kRefract},
    {"matrixCompMult", BuiltinFunc::kMatrixCompMult},
    {"outerProduct", BuiltinFunc::kOuterProduct},
    {"transpose", BuiltinFunc::kTranspose},
    {"determinant", BuiltinFunc::kDeterminant},
    {"inverse", BuiltinFunc::kInverse},
    {"lessThan", BuiltinFunc::kLessThan},
    {"lessThanEqual", BuiltinFunc::kLessThanEqual},
    {"greaterThan", BuiltinFunc::kGreaterThan},
    {"greaterThanEqual", BuiltinFunc::kGreaterThanEqual},
    {"equal", BuiltinFunc::kEqualFunc},
    {"notEqual", BuiltinFunc::kNotEqualFunc},
    {"any", BuiltinFunc::kAny},
    {"all", BuiltinFunc::kAll},
    {"not", BuiltinFunc::kNot},
    {"texture2D", BuiltinFunc::kTexture2D},
    {"texture2DProj", BuiltinFunc::kTexture2DProj},
    {"textureCube", BuiltinFunc::kTextureCube},
    {"texture3D", BuiltinFunc::kTexture3D},
    {"texture", BuiltinFunc::kTexture},
    {"textureProj", BuiltinFunc::kTextureProj},
    {"textureProjLod", BuiltinFunc::kTextureProjLod},
    {"textureLod", BuiltinFunc::kTextureLod},
    {"textureSize", BuiltinFunc::kTextureSize},
    {"textureOffset", BuiltinFunc::kTextureOffset},
    {"textureLodOffset", BuiltinFunc::kTextureLodOffset},
    {"textureGrad", BuiltinFunc::kTextureGrad},
    {"textureGradOffset", BuiltinFunc::kTextureGradOffset},
    {"textureProjOffset", BuiltinFunc::kTextureProjOffset},
    {"textureProjLodOffset", BuiltinFunc::kTextureProjLodOffset},
    {"textureProjGrad", BuiltinFunc::kTextureProjGrad},
    {"textureProjGradOffset", BuiltinFunc::kTextureProjGradOffset},
    {"texelFetch", BuiltinFunc::kTexelFetch},
    {"texelFetchOffset", BuiltinFunc::kTexelFetchOffset},
    {"dFdx", BuiltinFunc::kDfdx},
    {"dFdy", BuiltinFunc::kDfdy},
    {"fwidth", BuiltinFunc::kFwidth},
    {"floatBitsToUint", BuiltinFunc::kFloatBitsToUint},
    {"floatBitsToInt", BuiltinFunc::kFloatBitsToInt},
    {"uintBitsToFloat", BuiltinFunc::kUIntBitsToFloat},
    {"intBitsToFloat", BuiltinFunc::kIntBitsToFloat},
    {"isinf", BuiltinFunc::kIsInf},
    {"isnan", BuiltinFunc::kIsNan},
    {"packUnorm2x16", BuiltinFunc::kPackUnorm2x16},
    {"packSnorm2x16", BuiltinFunc::kPackSnorm2x16},
    {"packHalf2x16", BuiltinFunc::kPackHalf2x16},
    {"unpackUnorm2x16", BuiltinFunc::kUnpackUnorm2x16},
    {"unpackSnorm2x16", BuiltinFunc::kUnpackSnorm2x16},
    {"unpackHalf2x16", BuiltinFunc::kUnpackHalf2x16},
};

int PrecedenceOfPunct(const std::string& punct) {
  if (punct == "||")
    return 1;
  if (punct == "^^")
    return 2;
  if (punct == "&&")
    return 3;
  if (punct == "|")
    return 4;
  if (punct == "^")
    return 5;
  if (punct == "&")
    return 6;
  if (punct == "==" || punct == "!=")
    return 7;
  if (punct == "<" || punct == ">" || punct == "<=" || punct == ">=")
    return 8;
  if (punct == "<<" || punct == ">>")
    return 9;
  if (punct == "+" || punct == "-")
    return 10;
  if (punct == "*" || punct == "/" || punct == "%")
    return 11;
  return 0;
}

Op OpOfPunct(const std::string& punct) {
  if (punct == "+")
    return Op::kAdd;
  if (punct == "-")
    return Op::kSubtract;
  if (punct == "*")
    return Op::kMultiply;
  if (punct == "/")
    return Op::kDivide;
  if (punct == "%")
    return Op::kModulo;
  if (punct == "<<")
    return Op::kShiftLeft;
  if (punct == ">>")
    return Op::kShiftRight;
  if (punct == "<")
    return Op::kLess;
  if (punct == "<=")
    return Op::kLessEqual;
  if (punct == ">")
    return Op::kGreater;
  if (punct == ">=")
    return Op::kGreaterEqual;
  if (punct == "==")
    return Op::kEqual;
  if (punct == "!=")
    return Op::kNotEqual;
  if (punct == "&")
    return Op::kBitAnd;
  if (punct == "^")
    return Op::kBitXor;
  if (punct == "|")
    return Op::kBitOr;
  if (punct == "&&")
    return Op::kLogicalAnd;
  if (punct == "^^")
    return Op::kLogicalXor;
  if (punct == "||")
    return Op::kLogicalOr;
  return Op::kNone;
}

Op AssignmentOpOfPunct(const std::string& punct) {
  if (punct == "=")
    return Op::kAssign;
  if (punct == "+=")
    return Op::kAddAssign;
  if (punct == "-=")
    return Op::kSubtractAssign;
  if (punct == "*=")
    return Op::kMultiplyAssign;
  if (punct == "/=")
    return Op::kDivideAssign;
  if (punct == "%=")
    return Op::kModuloAssign;
  if (punct == "<<=")
    return Op::kShiftLeftAssign;
  if (punct == ">>=")
    return Op::kShiftRightAssign;
  if (punct == "&=")
    return Op::kBitAndAssign;
  if (punct == "^=")
    return Op::kBitXorAssign;
  if (punct == "|=")
    return Op::kBitOrAssign;
  return Op::kNone;
}

ExprPtr MakeExpr(Expr::Kind kind, int line) {
  ExprPtr expr(new Expr());
  expr->kind = kind;
  expr->line = line;
  return expr;
}

bool HasName(const std::vector<std::pair<std::string, int>>& names,
             const std::string& name,
             int* index) {
  for (const auto& entry : names) {
    if (entry.first == name) {
      if (index)
        *index = entry.second;
      return true;
    }
  }
  return false;
}

bool IsSwizzleChar(char c, int* set) {
  const char* sets[] = {"xyzw", "rgba", "stpq"};
  for (int i = 0; i < 3; ++i) {
    if (std::strchr(sets[i], c)) {
      *set = i;
      return true;
    }
  }
  return false;
}

// Identifiers that GLSL ES reserves for the language, and the maximum
// identifier length guaranteed by the specification.  These may not be used
// for user variables, functions, structures, fields or parameters.
bool IsReservedIdentifier(const std::string& name) {
  if (name.size() > 1024)
    return true;
  if (name.compare(0, 3, "gl_") == 0)
    return true;
  static const char* const kReserved[] = {
      "active", "asm", "atomic_uint", "attribute", "bool", "break", "bvec2",
      "bvec3", "bvec4", "case",
      "cast", "centroid", "class", "coherent", "common", "const", "continue",
      "default", "discard", "do", "double", "dvec2", "dvec3", "dvec4", "else",
      "enum", "extern", "external", "false", "filter", "fixed", "flat", "float",
      "for", "fvec2", "fvec3", "fvec4", "goto", "half", "highp", "hvec2",
      "hvec3", "hvec4", "if", "iimage1D", "iimage1DArray", "iimage2D",
      "iimage2DArray", "iimage3D", "iimageBuffer", "iimageCube", "image1D",
      "image1DArray", "image2D", "image2DArray", "image3D", "imageBuffer",
      "imageCube", "in", "inline", "inout", "input", "int", "interface",
      "invariant", "isampler1D", "isampler1DArray", "isampler2D",
      "isampler2DArray", "isampler2DMS", "isampler2DMSArray", "isampler2DRect",
      "isampler3D", "isamplerBuffer", "isamplerCube", "ivec2", "ivec3",
      "ivec4", "layout", "long", "lowp", "mat2", "mat2x2", "mat2x3", "mat2x4",
      "mat3", "mat3x2", "mat3x3", "mat3x4", "mat4", "mat4x2", "mat4x3",
      "mat4x4", "mediump", "namespace", "noinline", "noperspective", "out",
      "output", "partition", "patch", "precision", "public", "readonly",
      "resource", "restrict", "return", "sample", "sampler1D",
      "sampler1DArray", "sampler1DArrayShadow", "sampler1DShadow", "sampler2D",
      "sampler2DArray", "sampler2DArrayShadow", "sampler2DMS",
      "sampler2DMSArray", "sampler2DRect", "sampler2DRectShadow",
      "sampler2DShadow", "sampler3D", "sampler3DRect", "samplerBuffer",
      "samplerCube", "samplerCubeShadow", "short", "sizeof", "smooth",
      "static", "struct", "subroutine", "superp", "switch", "template", "this",
      "true", "typedef", "uimage1D", "uimage1DArray", "uimage2D",
      "uimage2DArray", "uimage3D", "uimageBuffer", "uimageCube", "uint",
      "uniform", "union", "unsigned", "usampler1D", "usampler1DArray",
      "usampler2D", "usampler2DArray", "usampler2DMS", "usampler2DMSArray",
      "usampler2DRect", "usampler3D", "usamplerBuffer", "usamplerCube",
      "using", "uvec2", "uvec3", "uvec4", "varying", "vec2", "vec3", "vec4",
      "void", "volatile", "while", "writeonly"};
  for (const char* reserved : kReserved) {
    if (name == reserved)
      return true;
  }
  return false;
}

}  // namespace

Parser::Parser(Stage stage, const std::string& source)
    : stage_(stage), lexer_(source) {}

Parser::~Parser() = default;

void Parser::Advance() {
  if (has_lookahead_) {
    current_ = lookahead_;
    has_lookahead_ = false;
  } else {
    current_ = lexer_.Next();
  }
}

const Token& Parser::Peek() {
  if (!has_lookahead_) {
    lookahead_ = lexer_.Next();
    has_lookahead_ = true;
  }
  return lookahead_;
}

bool Parser::CheckPunct(const char* text) const {
  return current_.kind == Token::Kind::kPunct && current_.text == text;
}

bool Parser::CheckIdent(const char* text) const {
  return current_.kind == Token::Kind::kIdentifier && current_.text == text;
}

bool Parser::AcceptPunct(const char* text) {
  if (!CheckPunct(text))
    return false;
  Advance();
  return true;
}

bool Parser::AcceptIdent(const char* text) {
  if (!CheckIdent(text))
    return false;
  Advance();
  return true;
}

bool Parser::ExpectPunct(const char* text) {
  if (!CheckPunct(text))
    return Fail(std::string("expected '") + text + "'");
  Advance();
  return true;
}

bool Parser::Fail(const std::string& message) {
  return FailAt(current_.line, message);
}

bool Parser::FailAt(int line, const std::string& message) {
  if (error_.empty())
    error_ = "line " + std::to_string(line) + ": " + message;
  return false;
}

bool Parser::ParseQualifiers(Qualifiers* qualifiers, bool parameter) {
  int last_rank = -1;
  const auto accept_rank = [&](int rank) -> bool {
    if (rank < last_rank)
      return Fail("qualifiers are in an invalid order");
    last_rank = rank;
    return true;
  };
  for (;;) {
    if (current_.kind != Token::Kind::kIdentifier)
      return true;
    const std::string& text = current_.text;
    if (text == "attribute") {
      if (!accept_rank(2))
        return false;
      qualifiers->is_attribute = true;
    } else if (text == "varying") {
      if (!accept_rank(2))
        return false;
      qualifiers->is_varying = true;
    } else if (text == "in") {
      if (!accept_rank(parameter ? 1 : 2))
        return false;
      qualifiers->is_in = true;
    } else if (text == "out") {
      if (!accept_rank(parameter ? 1 : 2))
        return false;
      qualifiers->is_out = true;
    } else if (text == "inout") {
      if (!accept_rank(parameter ? 1 : 2))
        return false;
      qualifiers->is_inout = true;
    } else if (text == "invariant") {
      if (parameter)
        return Fail("invariant is not a parameter qualifier");
      if (!accept_rank(0))
        return false;
      qualifiers->is_invariant = true;
    } else if (text == "flat") {
      if (!accept_rank(1))
        return false;
      qualifiers->flat = true;
    } else if (text == "smooth" || text == "centroid") {
      if (!accept_rank(1))
        return false;
    } else if (text == "lowp" || text == "mediump" || text == "highp") {
      if (!accept_rank(parameter ? 2 : 3))
        return false;
    } else if (text == "const") {
      if (!accept_rank(parameter ? 0 : 2))
        return false;
      qualifiers->is_const = true;
    } else if (text == "uniform") {
      if (!accept_rank(2))
        return false;
      qualifiers->is_uniform = true;
    } else if (text == "layout") {
      Advance();
      if (!ExpectPunct("("))
        return false;
      for (;;) {
        if (CheckIdent("location")) {
          qualifiers->has_layout = true;
          Advance();
          if (!ExpectPunct("="))
            return false;
          if (current_.kind != Token::Kind::kIntLiteral)
            return Fail("expected an integer location");
          qualifiers->location = static_cast<int>(current_.number);
          Advance();
        } else if (CheckIdent("row_major") ||
                   CheckIdent("column_major")) {
          qualifiers->matrix_layout =
              CheckIdent("row_major") ? 1 : 0;
          Advance();
        } else if (CheckIdent("std140") || CheckIdent("shared") ||
                   CheckIdent("packed") || CheckIdent("depth_any") ||
                   CheckIdent("index")) {
          qualifiers->has_layout = true;
          if (CheckIdent("std140"))
            qualifiers->is_std140 = true;
          if (CheckIdent("packed"))
            qualifiers->is_packed = true;
          Advance();
        } else if (current_.kind == Token::Kind::kIntLiteral) {
          Advance();
        } else {
          return Fail("unsupported layout qualifier");
        }
        if (!AcceptPunct(","))
          break;
      }
      if (!ExpectPunct(")"))
        return false;
      continue;
    } else {
      return true;
    }
    Advance();
  }
}

bool Parser::LookupType(const std::string& name, Type* type) const {
  if (LookupTypeName(name, type))
    return true;
  for (const auto& entry : struct_types_) {
    if (entry.first == name) {
      *type = entry.second;
      return true;
    }
  }
  return false;
}

bool Parser::IsKnownTypeName(const std::string& name) const {
  Type unused;
  return LookupType(name, &unused);
}

bool Parser::EvalConstNumber(const ExprPtr& expr, double* value) {
  if (!expr)
    return false;
  if (expr->kind == Expr::Kind::kLiteral) {
    if (expr->type.base() == BaseType::kInt ||
        expr->type.base() == BaseType::kUInt) {
      *value = static_cast<double>(expr->integer_literal);
      return true;
    }
    *value = static_cast<double>(expr->literal);
    return true;
  }
  if (expr->kind == Expr::Kind::kVariable) {
    for (const auto& entry : const_ints_) {
      if (entry.first == expr->text) {
        *value = static_cast<double>(entry.second);
        return true;
      }
    }
    for (const BuiltinConstantEntry& entry : kBuiltinConstants) {
      if (expr->text == entry.name) {
        *value = static_cast<double>(entry.value);
        return true;
      }
    }
    return false;
  }
  if (expr->kind == Expr::Kind::kUnary) {
    double inner = 0.0;
    if (expr->args.empty() || !EvalConstNumber(expr->args[0], &inner))
      return false;
    switch (expr->op) {
      case Op::kNegate: *value = -inner; return true;
      case Op::kNot: *value = inner == 0.0 ? 1.0 : 0.0; return true;
      case Op::kBitNot: *value = static_cast<double>(~static_cast<int>(inner)); return true;
      default: return false;
    }
  }
  if (expr->kind == Expr::Kind::kBinary) {
    double left = 0.0;
    double right = 0.0;
    if (expr->args.size() < 2 ||
        !EvalConstNumber(expr->args[0], &left) ||
        !EvalConstNumber(expr->args[1], &right))
      return false;
    switch (expr->op) {
      case Op::kAdd: *value = left + right; return true;
      case Op::kSubtract: *value = left - right; return true;
      case Op::kMultiply: *value = left * right; return true;
      case Op::kDivide: if (right == 0.0) return false; *value = left / right; return true;
      case Op::kModulo:
        if (right == 0.0) return false;
        *value = static_cast<double>(static_cast<int>(left) % static_cast<int>(right));
        return true;
      case Op::kShiftLeft:
        *value = static_cast<double>(static_cast<int>(left) << static_cast<int>(right));
        return true;
      case Op::kShiftRight:
        *value = static_cast<double>(static_cast<int>(left) >> static_cast<int>(right));
        return true;
      case Op::kLess: *value = left < right ? 1.0 : 0.0; return true;
      case Op::kLessEqual: *value = left <= right ? 1.0 : 0.0; return true;
      case Op::kGreater: *value = left > right ? 1.0 : 0.0; return true;
      case Op::kGreaterEqual: *value = left >= right ? 1.0 : 0.0; return true;
      case Op::kEqual: *value = left == right ? 1.0 : 0.0; return true;
      case Op::kNotEqual: *value = left != right ? 1.0 : 0.0; return true;
      case Op::kBitAnd:
        *value = static_cast<double>(static_cast<int>(left) & static_cast<int>(right));
        return true;
      case Op::kBitXor:
        *value = static_cast<double>(static_cast<int>(left) ^ static_cast<int>(right));
        return true;
      case Op::kBitOr:
        *value = static_cast<double>(static_cast<int>(left) | static_cast<int>(right));
        return true;
      case Op::kLogicalAnd:
        *value = (left != 0.0 && right != 0.0) ? 1.0 : 0.0;
        return true;
      case Op::kLogicalXor:
        *value = ((left != 0.0) != (right != 0.0)) ? 1.0 : 0.0;
        return true;
      case Op::kLogicalOr:
        *value = (left != 0.0 || right != 0.0) ? 1.0 : 0.0;
        return true;
      default: return false;
    }
  }
  if (expr->kind == Expr::Kind::kField ||
      expr->kind == Expr::Kind::kIndex) {
    int component = 0;
    if (expr->kind == Expr::Kind::kIndex) {
      if (expr->args.empty() ||
          !EvalConstNumber(expr->args[0], value))
        return false;
      component = static_cast<int>(*value);
    } else {
      const std::string& field = expr->text;
      if (field.size() != 1)
        return false;
      const char axis = field[0];
      component = axis == 'x' ? 0 : axis == 'y' ? 1 : axis == 'z' ? 2
                    : axis == 'w' ? 3 : -1;
      if (component < 0)
        return false;
    }
    const Expr* source = expr->base.get();
    const Expr* initializer = nullptr;
    if (source && source->kind == Expr::Kind::kVariable) {
      for (const auto& entry : const_exprs_) {
        if (entry.first == source->text) {
          initializer = entry.second;
          break;
        }
      }
      source = initializer;
    }
    if (!source || source->kind != Expr::Kind::kCall ||
        !source->is_constructor ||
        component < 0 ||
        component >= static_cast<int>(source->args.size()))
      return false;
    return EvalConstNumber(source->args[component], value);
  }
  if (expr->kind == Expr::Kind::kCall) {
    if (expr->is_constructor) {
      if (expr->args.size() == 1) {
        double inner = 0.0;
        if (!EvalConstNumber(expr->args[0], &inner))
          return false;
        switch (expr->type.base()) {
          case BaseType::kFloat: *value = inner; return true;
          case BaseType::kInt:
          case BaseType::kUInt: *value = static_cast<double>(static_cast<int>(inner)); return true;
          case BaseType::kBool: *value = inner != 0.0 ? 1.0 : 0.0; return true;
          default: return false;
        }
      }
      return false;
    }
    std::vector<double> args;
    for (const ExprPtr& argument : expr->args) {
      double number = 0.0;
      if (!EvalConstNumber(argument, &number))
        return false;
      args.push_back(number);
    }
    switch (expr->builtin_func) {
      case BuiltinFunc::kSin: if (args.size() != 1) return false; *value = std::sin(args[0]); return true;
      case BuiltinFunc::kCos: if (args.size() != 1) return false; *value = std::cos(args[0]); return true;
      case BuiltinFunc::kAbs: if (args.size() != 1) return false; *value = std::fabs(args[0]); return true;
      case BuiltinFunc::kFloor: if (args.size() != 1) return false; *value = std::floor(args[0]); return true;
      case BuiltinFunc::kCeil: if (args.size() != 1) return false; *value = std::ceil(args[0]); return true;
      case BuiltinFunc::kTrunc: if (args.size() != 1) return false; *value = std::trunc(args[0]); return true;
      case BuiltinFunc::kRound: if (args.size() != 1) return false; *value = std::round(args[0]); return true;
      case BuiltinFunc::kMin:
        if (args.size() != 2) return false;
        *value = args[0] < args[1] ? args[0] : args[1];
        return true;
      case BuiltinFunc::kMax:
        if (args.size() != 2) return false;
        *value = args[0] > args[1] ? args[0] : args[1];
        return true;
      case BuiltinFunc::kClamp:
        if (args.size() != 3) return false;
        *value = args[0] < args[1] ? args[1]
                 : (args[0] > args[2] ? args[2] : args[0]);
        return true;
      default: return false;
    }
  }
  return false;
}

bool Parser::EvalConstIntExpr(const ExprPtr& expr, int* value) {
  if (!expr)
    return false;
  double number = 0.0;
  if (EvalConstNumber(expr, &number)) {
    *value = static_cast<int>(number);
    return true;
  }
  if (expr->kind == Expr::Kind::kLiteral &&
      (expr->type.base() == BaseType::kInt ||
       expr->type.base() == BaseType::kUInt)) {
    *value = static_cast<int>(expr->integer_literal);
    return true;
  }
  if (expr->kind == Expr::Kind::kVariable) {
    for (const auto& entry : const_ints_) {
      if (entry.first == expr->text) {
        *value = entry.second;
        return true;
      }
    }
    return false;
  }
  if (expr->kind == Expr::Kind::kUnary && expr->op == Op::kNegate) {
    int inner = 0;
    if (expr->args.empty() || !EvalConstIntExpr(expr->args[0], &inner))
      return false;
    *value = -inner;
    return true;
  }
  if (expr->kind == Expr::Kind::kCall &&
      (expr->builtin_func == BuiltinFunc::kMax ||
       expr->builtin_func == BuiltinFunc::kMin ||
       expr->builtin_func == BuiltinFunc::kClamp) &&
      expr->args.size() >= 2) {
    int first = 0;
    int second = 0;
    if (!EvalConstIntExpr(expr->args[0], &first) ||
        !EvalConstIntExpr(expr->args[1], &second))
      return false;
    if (expr->builtin_func == BuiltinFunc::kMax) {
      *value = first > second ? first : second;
      return true;
    }
    if (expr->builtin_func == BuiltinFunc::kMin) {
      *value = first < second ? first : second;
      return true;
    }
    int low = 0;
    int high = 0;
    if (expr->args.size() < 3 ||
        !EvalConstIntExpr(expr->args[1], &low) ||
        !EvalConstIntExpr(expr->args[2], &high))
      return false;
    *value = first < low ? low : (first > high ? high : first);
    return true;
  }
  if (expr->kind == Expr::Kind::kBinary) {
    int left = 0;
    int right = 0;
    if (expr->args.size() < 2 ||
        !EvalConstIntExpr(expr->args[0], &left) ||
        !EvalConstIntExpr(expr->args[1], &right))
      return false;
    switch (expr->op) {
      case Op::kAdd: *value = left + right; return true;
      case Op::kSubtract: *value = left - right; return true;
      case Op::kMultiply: *value = left * right; return true;
      case Op::kDivide: if (right == 0) return false; *value = left / right; return true;
      case Op::kModulo: if (right == 0) return false; *value = left % right; return true;
      default: return false;
    }
  }
  return false;
}

bool Parser::IsConstantExpr(const ExprPtr& expr) const {
  if (!expr)
    return false;
  switch (expr->kind) {
    case Expr::Kind::kLiteral:
      return true;
    case Expr::Kind::kVariable:
      return expr->is_const && !expr->is_parameter;
    case Expr::Kind::kUnary:
      return expr->args.empty() ? false : IsConstantExpr(expr->args[0]);
    case Expr::Kind::kBinary:
    case Expr::Kind::kComma:
      return expr->args.size() >= 2 && IsConstantExpr(expr->args[0]) &&
             IsConstantExpr(expr->args[1]);
    case Expr::Kind::kTernary:
      return expr->args.size() >= 3 && IsConstantExpr(expr->args[0]) &&
             IsConstantExpr(expr->args[1]) && IsConstantExpr(expr->args[2]);
    case Expr::Kind::kField:
    case Expr::Kind::kIndex:
      return IsConstantExpr(expr->base);
    case Expr::Kind::kCall:
      if (expr->is_constructor) {
        for (const ExprPtr& argument : expr->args) {
          if (!IsConstantExpr(argument))
            return false;
        }
        return true;
      }
      if (expr->builtin_func == BuiltinFunc::kNone)
        return false;
      for (const ExprPtr& argument : expr->args) {
        if (!IsConstantExpr(argument))
          return false;
      }
      return true;
    default:
      return false;
  }
}

bool Parser::ParseConstAtom(int* value) {
  if (AcceptPunct("-")) {
    if (!ParseConstAtom(value))
      return false;
    *value = -*value;
    return true;
  }
  if (AcceptPunct("+"))
    return ParseConstAtom(value);
  if (AcceptPunct("(")) {
    if (!ParseConstantInt(value))
      return false;
    return ExpectPunct(")");
  }
  if (current_.kind == Token::Kind::kIntLiteral) {
    *value = static_cast<int>(current_.number);
    Advance();
    return true;
  }
  if (current_.kind == Token::Kind::kIdentifier) {
    const std::string name = current_.text;
    Advance();
    if (AcceptPunct("(")) {
      int first = 0;
      int second = 0;
      if (!ParseConstantInt(&first) || !ExpectPunct(",") ||
          !ParseConstantInt(&second))
        return false;
      if (name == "max" || name == "min") {
        if (!ExpectPunct(")"))
          return false;
        *value = name == "max" ? (first > second ? first : second)
                               : (first < second ? first : second);
        return true;
      }
      if (name == "clamp") {
        int high = 0;
        if (!ExpectPunct(",") || !ParseConstantInt(&high) ||
            !ExpectPunct(")"))
          return false;
        const int low = second;
        *value = first < low ? low : (first > high ? high : first);
        return true;
      }
      return Fail("unsupported function in array size");
    }
    if (AcceptPunct(".")) {
      if (!AcceptIdent("length") || !ExpectPunct("(") || !ExpectPunct(")"))
        return Fail("invalid array length expression");
      for (const auto& entry : array_sizes_) {
        if (entry.first == name) {
          *value = entry.second;
          if (*value <= 0)
            return Fail("array length is not a constant");
          return true;
        }
      }
      return Fail("unknown array '" + name + "'");
    }
    for (const auto& entry : const_ints_) {
      if (entry.first == name) {
        *value = entry.second;
        return true;
      }
    }
    return Fail("array size must be a constant integer expression");
  }
  return Fail("expected a constant integer expression");
}

bool Parser::ParseConstantInt(int* value) {
  if (!ParseConstAtom(value))
    return false;
  for (;;) {
    Op op = Op::kNone;
    if (AcceptPunct("+")) op = Op::kAdd;
    else if (AcceptPunct("-")) op = Op::kSubtract;
    else if (AcceptPunct("*")) op = Op::kMultiply;
    else if (AcceptPunct("/")) op = Op::kDivide;
    else if (AcceptPunct("%")) op = Op::kModulo;
    else break;
    int rhs = 0;
    if (!ParseConstAtom(&rhs))
      return false;
    switch (op) {
      case Op::kAdd: *value += rhs; break;
      case Op::kSubtract: *value -= rhs; break;
      case Op::kMultiply: *value *= rhs; break;
      case Op::kDivide:
        if (rhs == 0) return Fail("division by zero in array size");
        *value /= rhs; break;
      case Op::kModulo:
        if (rhs == 0) return Fail("division by zero in array size");
        *value %= rhs; break;
      default: break;
    }
  }
  return true;
}

bool Parser::ParseArraySuffix(Type* type) {
  while (AcceptPunct("[")) {
    if (type->is_array())
      return Fail("multidimensional arrays are not supported");
    int count = 0;
    if (CheckPunct("]")) {
      // An empty dimension is resolved from the initializer or constructor.
      count = 0;
    } else {
      if (!ParseConstantInt(&count))
        return false;
      if (count <= 0)
        return Fail("array size must be positive");
    }
    if (!ExpectPunct("]"))
      return false;
    *type = Type::Array(*type, count);
  }
  return true;
}

bool Parser::ParseTypeSpec(Type* type) {
  if (current_.kind != Token::Kind::kIdentifier)
    return Fail("expected a type");
  if (!LookupType(current_.text, type))
    return Fail("unknown type '" + current_.text + "'");
  Advance();
  return ParseArraySuffix(type);
}

bool Parser::AddGlobalVariable(const std::string& name,
                               const Type& type,
                               const Qualifiers& qualifiers,
                               ExprPtr initializer,
                               int line) {
  if (IsReservedIdentifier(name))
    return FailAt(line, "reserved identifier '" + name + "'");
  if (IsUniformBlockName(name))
    return FailAt(line, "redefinition of uniform block '" + name + "'");
  if (IsBlockInstanceName(name))
    return FailAt(line, "redefinition of uniform block instance '" + name + "'");
  if (HasName(function_names_, name, nullptr))
    return FailAt(line, "redefinition of function '" + name + "'");
  if (HasName(global_names_, name, nullptr) ||
      HasName(uniform_names_, name, nullptr) ||
      HasName(attribute_names_, name, nullptr) ||
      HasName(varying_names_, name, nullptr)) {
    return FailAt(line, "redefinition of '" + name + "'");
  }
  if (type.is_void())
    return FailAt(line, "variable '" + name + "' cannot have type void");

  Variable variable;
  variable.name = name;
  variable.type = type;
  variable.is_const = qualifiers.is_const;

  if (qualifiers.is_uniform) {
    if (initializer)
      return FailAt(line, "uniform '" + name + "' cannot be initialised");
    variable.storage = Storage::kUniform;
    variable.index = static_cast<int>(ast_->uniforms.size());
    uniform_names_.push_back({name, variable.index});
    ast_->uniforms.push_back(variable);
    return true;
  }

  const bool wants_attribute =
      qualifiers.is_attribute || (qualifiers.is_in && stage_ == Stage::kVertex);
  if (wants_attribute) {
    if (stage_ != Stage::kVertex) {
      return FailAt(
          line, "attribute '" + name + "' is only valid in a vertex shader");
    }
    if (type.is_sampler())
      return FailAt(line, "attribute '" + name + "' cannot be a sampler");
    variable.storage = Storage::kAttribute;
    variable.explicit_location = qualifiers.location >= 0;
    const int slots = type.is_matrix() ? type.columns()
                     : type.is_array() ? type.array_size()
                                       : 1;
    int location = qualifiers.location;
    if (location < 0) {
      location = next_attribute_location_;
      next_attribute_location_ = location + slots;
    } else if (location + slots > kMaxVertexAttribs) {
      return FailAt(line, "attribute location out of range");
    }
    variable.index = location;
    for (int slot = 0; slot < slots; ++slot)
      attribute_names_.push_back({name, location + slot});
    ast_->attributes.push_back(variable);
    return true;
  }

  if (stage_ == Stage::kFragment && qualifiers.is_out) {
    const bool is_array = type.is_array();
    const int array_length = is_array ? type.array_size() : 1;
    const Type element_type = is_array ? type.array_element() : type;
    const bool valid_output =
        element_type.columns() == 1 && element_type.rows() >= 1 &&
        element_type.rows() <= 4 &&
        (element_type.base() == BaseType::kFloat ||
         element_type.base() == BaseType::kInt ||
         element_type.base() == BaseType::kUInt);
    if (!valid_output)
      return FailAt(line, "fragment output '" + name +
                             "' must be a scalar or vector of floats, ints or "
                             "uints");
    int location = qualifiers.location;
    if (location < 0) {
      for (int candidate = 0;
           candidate + array_length <= kMaxFragmentOutputs; ++candidate) {
        bool used = false;
        for (const FragmentOut& output : ast_->outputs) {
          if (output.location >= candidate &&
              output.location < candidate + array_length) {
            used = true;
            break;
          }
        }
        if (!used) {
          location = candidate;
          break;
        }
      }
    }
    if (location < 0 || location + array_length > kMaxFragmentOutputs)
      return FailAt(line, "fragment output location is out of range");
    for (const FragmentOut& output : ast_->outputs) {
      if (output.location >= location &&
          output.location < location + array_length)
        return FailAt(line, "duplicate fragment output location");
    }
    const int slot = EnsureBuiltin(static_cast<BuiltinId>(
        static_cast<int>(BuiltinId::kFragOutput0) + location));
    if (slot < 0)
      return FailAt(line, "could not allocate a fragment output");
    for (int element = 0; element < array_length; ++element) {
      FragmentOut output;
      output.name = name;
      output.location = location + element;
      output.slot = slot;
      output.element = element;
      output.components = element_type.components();
      output.base = element_type.base();
      ast_->outputs.push_back(output);
    }
    global_names_.push_back({name, slot});
    Variable output_global;
    output_global.name = name;
    output_global.type = type;
    output_global.storage = Storage::kGlobal;
    output_global.index = slot;
    ast_->globals.push_back(output_global);
    ast_->uses_frag_color = true;
    return true;
  }

  const bool wants_varying_out =
      qualifiers.is_varying || (qualifiers.is_out && stage_ == Stage::kVertex);
  const bool wants_varying_in = qualifiers.is_in && stage_ == Stage::kFragment;
  if (wants_varying_out || wants_varying_in) {
    if (type.is_sampler())
      return FailAt(line, "varying '" + name + "' cannot be a sampler");
    if (type.base() == BaseType::kBool)
      return FailAt(line, "varying '" + name + "' cannot have a boolean type");
    variable.storage =
        wants_varying_out ? Storage::kVaryingOut : Storage::kVaryingIn;
    variable.flat = qualifiers.flat;
    variable.index = static_cast<int>(ast_->varyings.size());
    varying_names_.push_back({name, variable.index});
    ast_->varyings.push_back(variable);
    return true;
  }

  variable.storage = Storage::kGlobal;
  variable.index = ast_->global_slots++;
  global_names_.push_back({name, variable.index});
  ast_->globals.push_back(variable);
  if (initializer) {
    GlobalInit init;
    init.slot = variable.index;
    init.expression = std::move(initializer);
    ast_->global_initializers.push_back(std::move(init));
  }
  return true;
}

bool Parser::AddLocalVariable(const std::string& name,
                              const Type& type,
                              ExprPtr initializer,
                              bool is_const,
                              int line,
                              StmtPtr* statement) {
  if (IsReservedIdentifier(name))
    return FailAt(line, "reserved identifier '" + name + "'");
  for (const auto& entry : scopes_.back()) {
    if (entry.first == name)
      return FailAt(line, "redefinition of '" + name + "'");
  }
  if (type.is_void())
    return FailAt(line, "variable '" + name + "' cannot have type void");
  if (type.is_sampler())
    return FailAt(line, "samplers must be declared as uniforms");
  Variable variable;
  variable.name = name;
  variable.type = type;
  variable.storage = Storage::kLocal;
  variable.is_const = is_const;
  variable.index = slot_count_++;
  scopes_.back().push_back({name, variable.index});
  if (static_cast<int>(slot_types_.size()) <= variable.index)
    slot_types_.resize(variable.index + 1, Type::Float());
  slot_types_[variable.index] = type;
  if (static_cast<int>(slot_is_const_.size()) <= variable.index)
    slot_is_const_.resize(variable.index + 1, false);
  slot_is_const_[variable.index] = is_const;
  max_slot_count_ = std::max(max_slot_count_, slot_count_);

  Statement::Declarator declarator;
  declarator.variable = variable;
  declarator.initializer = std::move(initializer);
  (*statement)->declarators.push_back(std::move(declarator));
  return true;
}

bool Parser::RegisterFunction(Function&& function, bool prototype) {
  const std::string name = function.name;
  if (IsReservedIdentifier(name))
    return FailAt(function.line, "reserved identifier '" + name + "'");
  if (IsUniformBlockName(name))
    return FailAt(function.line, "redefinition of uniform block '" + name + "'");
  if (IsBlockInstanceName(name))
    return FailAt(function.line, "redefinition of uniform block instance '" +
                                   name + "'");
  for (const BuiltinFuncEntry& entry : kBuiltinFuncs) {
    if (entry.name == name)
      return FailAt(function.line, "cannot redefine built-in function '" +
                                      name + "'");
  }
  if (HasName(global_names_, name, nullptr) ||
      HasName(uniform_names_, name, nullptr) ||
      HasName(attribute_names_, name, nullptr) ||
      HasName(varying_names_, name, nullptr))
    return FailAt(function.line, "redefinition of '" + name + "'");
  int existing = -1;
  for (size_t i = 0; i < ast_->functions.size(); ++i) {
    const Function& candidate = ast_->functions[i];
    if (candidate.name != name ||
        candidate.parameters.size() != function.parameters.size()) {
      continue;
    }
    bool same = true;
    for (size_t p = 0; p < function.parameters.size(); ++p) {
      if (!(candidate.parameters[p].type == function.parameters[p].type)) {
        same = false;
        break;
      }
    }
    if (same) {
      existing = static_cast<int>(i);
      break;
    }
  }
  if (existing >= 0) {
    Function& previous = ast_->functions[existing];
    if (previous.return_type != function.return_type) {
      return FailAt(function.line, "function '" + name +
                                      "' prototype return type mismatch");
    }
    for (size_t p = 0; p < function.parameters.size() &&
                      p < previous.parameters.size(); ++p) {
      if (previous.parameters[p].is_const != function.parameters[p].is_const ||
          previous.parameters[p].parameter_qualifier !=
              function.parameters[p].parameter_qualifier) {
        return FailAt(function.line, "function '" + name +
                                        "' prototype parameter qualifier mismatch");
      }
    }
    if (prototype)
      return true;
    if (previous.defined)
      return FailAt(function.line, "redefinition of function '" + name + "'");
    function.defined = true;
    ast_->functions[existing] = std::move(function);
    return true;
  }
  function.defined = !prototype;
  function_names_.push_back({name, static_cast<int>(ast_->functions.size())});
  if (name == "main")
    ast_->main_function = static_cast<int>(ast_->functions.size());
  ast_->functions.push_back(std::move(function));
  return true;
}

bool Parser::ParseParameters(Function* function) {
  if (CheckPunct(")"))
    return true;
  for (;;) {
    Qualifiers qualifiers;
    if (!ParseQualifiers(&qualifiers, true))
      return false;
    if (qualifiers.is_uniform || qualifiers.is_attribute ||
        qualifiers.is_varying)
      return Fail("invalid parameter qualifier");
    Type type;
    if (!ParseTypeSpec(&type))
      return false;
    if (type.is_void()) {
      // "(void)" is the GLSL spelling for an empty parameter list.
      if (CheckPunct(")"))
        return true;
      return Fail("a parameter cannot have type void");
    }
    std::string name;
    if (current_.kind == Token::Kind::kIdentifier) {
      name = current_.text;
      if (IsReservedIdentifier(name))
        return FailAt(current_.line, "reserved identifier '" + name + "'");
      Advance();
      if (!ParseArraySuffix(&type))
        return false;
    } else if (!CheckPunct(",") && !CheckPunct(")")) {
      return Fail("expected a parameter name");
    }
    Variable variable;
    variable.name = name;
    variable.type = type;
    variable.storage = Storage::kLocal;
    variable.is_const = qualifiers.is_const;
    variable.parameter_qualifier =
        qualifiers.is_inout ? 2 : (qualifiers.is_out ? 1 : 0);
    if (variable.parameter_qualifier != 0 && ast_)
      ast_->uses_out_params = true;
    variable.index = static_cast<int>(function->parameters.size());
    function->parameters.push_back(variable);
    if (!AcceptPunct(","))
      break;
  }
  return true;
}

bool Parser::ParseFunction(const std::string& name,
                           const Type& return_type,
                           const Qualifiers& qualifiers,
                           int line) {
  if (qualifiers.is_in || qualifiers.is_out || qualifiers.is_inout ||
      qualifiers.is_uniform || qualifiers.is_attribute ||
      qualifiers.is_varying) {
    return FailAt(line, "invalid qualifier on a function return type");
  }
  if (in_function_)
    return FailAt(line, "nested functions are not supported");
  Function function;
  function.name = name;
  function.return_type = return_type;
  function.line = line;

  Advance();  // '('
  if (!CheckPunct(")")) {
    if (!ParseParameters(&function))
      return false;
  }
  if (!ExpectPunct(")"))
    return false;

  if (AcceptPunct(";"))
    return RegisterFunction(std::move(function), true);

  if (!ExpectPunct("{"))
    return false;

  in_function_ = true;
  current_function_ = name;
  current_return_type_ = return_type;
  slot_count_ = static_cast<int>(function.parameters.size());
  current_parameter_count_ = slot_count_;
  max_slot_count_ = slot_count_;
  slot_types_.clear();
  slot_types_.reserve(16);
  slot_is_const_.clear();
  slot_is_const_.reserve(16);
  loop_depth_ = 0;
  scopes_.push_back({});
  for (const Variable& parameter : function.parameters) {
    scopes_.back().push_back({parameter.name, parameter.index});
    slot_types_.push_back(parameter.type);
    slot_is_const_.push_back(parameter.is_const);
  }

  while (!CheckPunct("}")) {
    if (current_.kind == Token::Kind::kEnd) {
      Fail("unexpected end of shader inside function '" + name + "'");
      break;
    }
    StmtPtr statement = ParseStatement();
    if (!statement)
      break;
    function.body.push_back(std::move(statement));
  }
  if (!error_.empty())
    return false;
  Advance();  // '}'
  scopes_.pop_back();
  function.slot_count = max_slot_count_;
  in_function_ = false;
  current_function_.clear();
  current_return_type_ = Type::Void();
  current_parameter_count_ = 0;
  slot_count_ = 0;
  return RegisterFunction(std::move(function), false);
}

bool Parser::ParseStructDefinition() {
  const int line = current_.line;
  Advance();  // 'struct'
  if (current_.kind != Token::Kind::kIdentifier)
    return Fail("expected a structure name");
  const std::string name = current_.text;
  if (IsReservedIdentifier(name))
    return FailAt(line, "reserved identifier '" + name + "'");
  Advance();
  for (const auto& entry : struct_types_) {
    if (entry.first == name)
      return FailAt(line, "redefinition of struct '" + name + "'");
  }
  if (!ExpectPunct("{"))
    return false;

  std::shared_ptr<StructInfo> info = std::make_shared<StructInfo>();
  info->name = name;
  int component_offset = 0;
  int byte_offset = 0;
  while (!CheckPunct("}")) {
    if (current_.kind == Token::Kind::kEnd) {
      FailAt(line, "unexpected end of shader inside struct '" + name + "'");
      return false;
    }
    while (CheckIdent("lowp") || CheckIdent("mediump") ||
           CheckIdent("highp")) {
      Advance();
    }
    Type member_type;
    if (!ParseTypeSpec(&member_type))
      return false;
    if (member_type.is_void())
      return FailAt(line, "a structure member cannot have type void");
    for (;;) {
      if (current_.kind != Token::Kind::kIdentifier)
        return FailAt(line, "expected a structure member name");
      const std::string member_name = current_.text;
      if (IsReservedIdentifier(member_name))
        return FailAt(line, "reserved identifier '" + member_name + "'");
      Advance();
      Type member_type_with_array = member_type;
      if (!ParseArraySuffix(&member_type_with_array))
        return false;
      for (const StructMember& member : info->members) {
        if (member.name == member_name)
          return FailAt(line, "duplicate structure member '" + member_name + "'");
      }
      const int alignment = Std140Alignment(member_type_with_array);
      byte_offset = (byte_offset + alignment - 1) / alignment * alignment;
      StructMember member;
      member.name = member_name;
      member.type = member_type_with_array;
      member.component_offset = component_offset;
      member.byte_offset = byte_offset;
      info->members.push_back(member);
      component_offset += member_type_with_array.components();
      byte_offset += Std140Size(member_type_with_array);
      if (!AcceptPunct(","))
        break;
    }
    if (!ExpectPunct(";"))
      return false;
  }
  if (!ExpectPunct("}"))
    return false;
  info->components = component_offset;
  const int alignment = Std140Alignment(Type::Struct(info));
  info->byte_size = (byte_offset + alignment - 1) / alignment * alignment;

  const Type struct_type = Type::Struct(info);
  struct_types_.push_back({name, struct_type});

  // "struct S { ... } instance;" declares a variable of the new type.  A bare
  // definition simply introduces the type name.
  if (current_.kind == Token::Kind::kIdentifier) {
    for (;;) {
      const std::string instance_name = current_.text;
      Advance();
      Type instance_type = struct_type;
      if (!ParseArraySuffix(&instance_type))
        return false;
      Qualifiers qualifiers;
      ExprPtr initializer;
      if (AcceptPunct("=")) {
        initializer = ParseAssignment();
        if (!initializer)
          return false;
      }
      if (!AddGlobalVariable(instance_name, instance_type, qualifiers,
                             std::move(initializer), line)) {
        return false;
      }
      if (!AcceptPunct(","))
        break;
      if (current_.kind != Token::Kind::kIdentifier)
        return Fail("expected an identifier");
    }
  }
  return ExpectPunct(";");
}

bool Parser::ParseGlobalDeclaration() {
  Qualifiers qualifiers;
  if (!ParseQualifiers(&qualifiers))
    return false;
  if (CheckIdent("struct"))
    return ParseStructDefinition();
  // "layout(...) uniform;" (and the in/out spelling) is a global layout
  // declaration without a type; it only declares default qualifiers.
  if ((qualifiers.is_uniform || qualifiers.is_in || qualifiers.is_out) &&
      CheckPunct(";")) {
    Advance();
    return true;
  }
  if (qualifiers.is_invariant && !IsKnownTypeName(current_.text)) {
    // "invariant gl_Position;" is accepted and ignored.
    Advance();
    return ExpectPunct(";");
  }
  if (qualifiers.is_invariant &&
      (qualifiers.is_in || qualifiers.is_attribute ||
       qualifiers.is_uniform)) {
    return FailAt(current_.line,
                  "invariant is only valid on vertex outputs and fragment "
                  "inputs");
  }
  if (qualifiers.is_uniform && current_.kind == Token::Kind::kIdentifier &&
      !IsKnownTypeName(current_.text) && !CheckIdent("true") &&
      !CheckIdent("false")) {
    return ParseUniformBlock(qualifiers, current_.line);
  }
  Type type;
  if (!ParseTypeSpec(&type))
    return false;
  const int line = current_.line;
  if (current_.kind != Token::Kind::kIdentifier)
    return Fail("expected an identifier");
  std::string name = current_.text;
  if (name == "gl_FragDepth")
    return Fail("gl_FragDepth cannot be redeclared");
  Advance();
  if (CheckPunct("("))
    return ParseFunction(name, type, qualifiers, line);

  for (;;) {
    Type declaration_type = type;
    if (!ParseArraySuffix(&declaration_type))
      return false;
    ExprPtr initializer;
    if (AcceptPunct("=")) {
      initializer = ParseAssignment();
      if (!initializer)
        return false;
    }
    if (declaration_type.is_array() && declaration_type.array_size() == 0 &&
        initializer && initializer->type.is_array())
      declaration_type = initializer->type;
    if (declaration_type.is_array() && declaration_type.array_size() == 0 &&
        !initializer)
      return Fail("array size must be specified");
    if (initializer && initializer->type != declaration_type)
      return Fail("implicit conversion in variable initializer");
    if (qualifiers.is_const) {
      if (!initializer)
        return Fail("const variable requires an initializer");
      if (initializer->type != declaration_type)
        return Fail("implicit conversion in constant initializer");
      if (!IsConstantExpr(initializer))
        return Fail("initializer is not a constant expression");
    }
    if (qualifiers.is_const && initializer)
      const_exprs_.push_back({name, initializer.get()});
    if (qualifiers.is_const && declaration_type == Type::Int() && initializer) {
      int constant = 0;
      if (EvalConstIntExpr(initializer, &constant)) {
        const_ints_.push_back({name, constant});
      }
    }
    if (declaration_type.is_array() && declaration_type.array_size() > 0)
      array_sizes_.push_back({name, declaration_type.array_size()});
    if (!AddGlobalVariable(name, declaration_type, qualifiers,
                           std::move(initializer), line)) {
      return false;
    }
    if (!AcceptPunct(","))
      break;
    if (current_.kind != Token::Kind::kIdentifier)
      return Fail("expected an identifier");
    name = current_.text;
    Advance();
  }
  return ExpectPunct(";");
}

bool Parser::IsUniformBlockName(const std::string& name) const {
  for (const std::string& block_name : block_names_) {
    if (block_name == name)
      return true;
  }
  return false;
}

bool Parser::IsBlockInstanceName(const std::string& name) const {
  for (const auto& instance : block_instances_) {
    if (instance.first == name)
      return true;
  }
  return false;
}

bool Parser::ParseUniformBlock(const Qualifiers& qualifiers, int line) {
  const std::string block_name = current_.text;
  Advance();
  for (const std::string& existing : block_names_) {
    if (existing == block_name)
      return FailAt(line, "duplicate uniform block '" + block_name + "'");
  }
  if (HasName(global_names_, block_name, nullptr) ||
      HasName(uniform_names_, block_name, nullptr) ||
      HasName(attribute_names_, block_name, nullptr) ||
      HasName(varying_names_, block_name, nullptr) ||
      HasName(function_names_, block_name, nullptr)) {
    return FailAt(line, "redefinition of '" + block_name + "'");
  }

  if (!ExpectPunct("{"))
    return false;

  UniformBlock block;
  block.name = block_name;
  block.std140 = qualifiers.is_std140;
  block.packed = qualifiers.is_packed;
  block.matrix_layout =
      qualifiers.matrix_layout >= 0 ? qualifiers.matrix_layout : 0;
  const int block_index = static_cast<int>(ast_->uniform_blocks.size());
  int offset = 0;
  while (!CheckPunct("}")) {
    if (current_.kind == Token::Kind::kEnd) {
      FailAt(line, "unexpected end of shader inside uniform block");
      return false;
    }
    Qualifiers member_qualifiers;
    if (!ParseQualifiers(&member_qualifiers))
      return false;
    if (member_qualifiers.is_attribute ||
        member_qualifiers.is_varying || member_qualifiers.is_in ||
        member_qualifiers.is_out || member_qualifiers.is_invariant ||
        member_qualifiers.is_const)
      return FailAt(line, "invalid qualifier on a uniform block member");
    if (member_qualifiers.has_layout)
      return FailAt(line, "layout qualifiers are not allowed on block members");
    Type member_type;
    if (!ParseTypeSpec(&member_type))
      return false;
    if (member_type.is_void())
      return FailAt(line, "a uniform block member cannot have type void");
    if (member_type.is_sampler())
      return FailAt(line, "a uniform block member cannot be a sampler");
    if (current_.kind != Token::Kind::kIdentifier)
      return FailAt(line, "expected a uniform block member name");
    const std::string member_name = current_.text;
    Advance();
    if (!ParseArraySuffix(&member_type))
      return false;
    if (AcceptPunct("="))
      return FailAt(line, "a uniform block member cannot be initialised");
    for (const UniformBlockMember& member : block.members) {
      if (member.name == member_name)
        return FailAt(line, "duplicate uniform block member '" + member_name +
                               "'");
    }

    const int alignment = Std140Alignment(member_type);
    offset = (offset + alignment - 1) / alignment * alignment;
    UniformBlockMember member;
    member.name = member_name;
    member.type = member_type;
    member.offset = offset;
    member.matrix_layout = member_qualifiers.matrix_layout >= 0
                               ? member_qualifiers.matrix_layout
                               : block.matrix_layout;
    member.uniform_index = static_cast<int>(ast_->uniforms.size());

    Variable variable;
    variable.name = member_name;
    variable.type = member_type;
    variable.storage = Storage::kUniform;
    variable.index = member.uniform_index;
    variable.ubo_block = block_index;
    variable.ubo_offset = offset;
    ast_->uniforms.push_back(variable);
    block.members.push_back(member);

    offset += Std140Size(member_type, member.matrix_layout);
    if (!ExpectPunct(";"))
      return false;
  }

  if (!ExpectPunct("}"))
    return false;

  if (block.members.empty())
    return FailAt(line, "a uniform block must declare at least one member");

  if (current_.kind == Token::Kind::kIdentifier) {
    block.instance_name = current_.text;
    Advance();
    if (AcceptPunct("[")) {
      block.instance_array = true;
      int count = 0;
      if (!ParseConstantInt(&count))
        return false;
      if (count <= 0)
        return FailAt(line, "uniform block instance array size must be positive");
      if (!ExpectPunct("]"))
        return false;
      block.instance_array_size = count;
    }
    if (block.instance_name == block_name)
      return FailAt(line, "uniform block instance cannot reuse the block name");
    for (const auto& existing : block_instances_) {
      if (existing.first == block.instance_name)
        return FailAt(line, "duplicate uniform block instance '" +
                               block.instance_name + "'");
    }
    if (HasName(global_names_, block.instance_name, nullptr) ||
        HasName(uniform_names_, block.instance_name, nullptr) ||
        HasName(attribute_names_, block.instance_name, nullptr) ||
        HasName(varying_names_, block.instance_name, nullptr) ||
        HasName(function_names_, block.instance_name, nullptr)) {
      return FailAt(line, "redefinition of '" + block.instance_name + "'");
    }
    block_instances_.push_back({block.instance_name, block_index});
    if (!block.instance_name.empty()) {
      for (UniformBlockMember& member : block.members) {
        const std::string uniform_name = block.name + "." + member.name;
        if (block.instance_array_size > 1) {
          if (member.uniform_index >= 0 &&
              member.uniform_index < static_cast<int>(ast_->uniforms.size()))
            ast_->uniforms[member.uniform_index].name = uniform_name;
          member.instance_uniform_indices.push_back(member.uniform_index);
          for (int instance = 1; instance < block.instance_array_size;
               ++instance) {
            Variable variable;
            variable.name = uniform_name;
            variable.type = member.type;
            variable.storage = Storage::kUniform;
            variable.ubo_block = block_index;
            variable.ubo_instance = instance;
            variable.ubo_offset = member.offset;
            variable.index = static_cast<int>(ast_->uniforms.size());
            ast_->uniforms.push_back(variable);
            member.instance_uniform_indices.push_back(variable.index);
          }
        } else if (member.uniform_index >= 0 &&
                   member.uniform_index <
                       static_cast<int>(ast_->uniforms.size())) {
          ast_->uniforms[member.uniform_index].name = uniform_name;
        }
      }
    }
  } else {
    // Members of an unnamed block live in the global uniform namespace.
    for (const UniformBlockMember& member : block.members) {
      int existing = -1;
      if (HasName(uniform_names_, member.name, &existing))
        return FailAt(line, "duplicate uniform block member '" + member.name +
                               "'");
      uniform_names_.push_back({member.name, member.uniform_index});
    }
  }

  block.size = (offset + 15) / 16 * 16;
  block_names_.push_back(block.name);
  ast_->uniform_blocks.push_back(block);
  return ExpectPunct(";");
}

StmtPtr Parser::ParseDeclarationStatement(bool in_condition) {
  Qualifiers qualifiers;
  if (!ParseQualifiers(&qualifiers))
    return nullptr;
  if (qualifiers.is_uniform || qualifiers.is_attribute ||
      qualifiers.is_varying || qualifiers.is_in || qualifiers.is_out ||
      qualifiers.is_inout) {
    Fail("uniform/attribute/varying declarations must be global");
    return nullptr;
  }
  if (CheckIdent("struct")) {
    // GLSL ES 3.00 allows structure definitions in a local declaration.
    // Register the type and consume the trailing semicolon; the instance
    // declaration, if any, is parsed as the following statement.
    StmtPtr statement(new Statement());
    statement->kind = Statement::Kind::kBlock;
    statement->line = current_.line;
    if (!ParseStructDefinition())
      return nullptr;
    return statement;
  }
  Type type;
  if (!ParseTypeSpec(&type))
    return nullptr;
  const int line = current_.line;

  StmtPtr statement(new Statement());
  statement->kind = Statement::Kind::kDeclaration;
  statement->line = line;
  for (;;) {
    if (current_.kind != Token::Kind::kIdentifier) {
      Fail("expected an identifier");
      return nullptr;
    }
    const std::string name = current_.text;
    if (name == "gl_FragDepth") {
      Fail("gl_FragDepth cannot be redeclared");
      return nullptr;
    }
    Advance();
    Type declaration_type = type;
    if (!ParseArraySuffix(&declaration_type))
      return nullptr;
    ExprPtr initializer;
    if (AcceptPunct("=")) {
      initializer = ParseAssignment();
      if (!initializer)
        return nullptr;
    }
    if (declaration_type.is_array() && declaration_type.array_size() == 0 &&
        initializer && initializer->type.is_array())
      declaration_type = initializer->type;
    if (declaration_type.is_array() && declaration_type.array_size() == 0 &&
        !initializer) {
      Fail("array size must be specified");
      return nullptr;
    }
    if (initializer && initializer->type != declaration_type) {
      Fail("implicit conversion in variable initializer (" +
           initializer->type.Name() + " to " + declaration_type.Name() + ")");
      return nullptr;
    }
    if (qualifiers.is_const) {
      if (!initializer) {
        Fail("const variable requires an initializer");
        return nullptr;
      }
      if (initializer->type != declaration_type) {
        Fail("implicit conversion in constant initializer (" +
             initializer->type.Name() + " to " + declaration_type.Name() + ")");
        return nullptr;
      }
      if (!IsConstantExpr(initializer)) {
        Fail("initializer is not a constant expression");
        return nullptr;
      }
    }
    if (qualifiers.is_const && initializer)
      const_exprs_.push_back({name, initializer.get()});
    if (qualifiers.is_const && declaration_type == Type::Int() && initializer) {
      int constant = 0;
      if (EvalConstIntExpr(initializer, &constant)) {
        const_ints_.push_back({name, constant});
      }
    }
    if (declaration_type.is_array() && declaration_type.array_size() > 0)
      array_sizes_.push_back({name, declaration_type.array_size()});
    if (!AddLocalVariable(name, declaration_type, std::move(initializer),
                          qualifiers.is_const, line, &statement)) {
      return nullptr;
    }
    if (!AcceptPunct(","))
      break;
  }
  if (!in_condition && !ExpectPunct(";"))
    return nullptr;
  return statement;
}

bool Parser::IsDeclarationStart() const {
  if (current_.kind != Token::Kind::kIdentifier)
    return false;
  const std::string& text = current_.text;
  const bool keyword = IsKnownTypeName(text) || IsQualifierKeyword(text) ||
                       text == "struct" || text == "precision";
  // A declaration starts with a type name followed by an identifier, or by
  // an array suffix (`float[4] x`).  A type name followed by '(' is a
  // constructor call used as an expression statement, not a declaration.
  const Token& next = const_cast<Parser*>(this)->Peek();
  return keyword && (next.kind == Token::Kind::kIdentifier ||
                     next.IsPunct("["));
}

StmtPtr Parser::ParseBlock() {
  StmtPtr block(new Statement());
  block->kind = Statement::Kind::kBlock;
  block->line = current_.line;
  ExpectPunct("{");
  scopes_.push_back({});
  while (!CheckPunct("}")) {
    if (current_.kind == Token::Kind::kEnd) {
      Fail("unexpected end of shader inside a block");
      return nullptr;
    }
    StmtPtr statement = ParseStatement();
    if (!statement)
      return nullptr;
    block->statements.push_back(std::move(statement));
  }
  Advance();  // '}'
  scopes_.pop_back();
  return block;
}

StmtPtr Parser::ParseIf() {
  StmtPtr statement(new Statement());
  statement->kind = Statement::Kind::kIf;
  statement->line = current_.line;
  Advance();  // if
  if (!ExpectPunct("("))
    return nullptr;
  scopes_.emplace_back();
  StmtPtr condition_statement;
  if (IsDeclarationStart()) {
    condition_statement = ParseDeclarationStatement(true);
    if (!condition_statement) {
      scopes_.pop_back();
      return nullptr;
    }
    if (condition_statement->declarators.size() != 1) {
      Fail("if condition must declare exactly one variable");
      scopes_.pop_back();
      return nullptr;
    }
    const Variable& variable = condition_statement->declarators[0].variable;
    if (variable.type.base() != BaseType::kBool) {
      Fail("if condition must have type bool");
      scopes_.pop_back();
      return nullptr;
    }
    ExprPtr read = MakeExpr(Expr::Kind::kVariable, statement->line);
    read->text = variable.name;
    read->storage = variable.storage;
    read->index = variable.index;
    read->type = variable.type;
    condition_statement->expression = std::move(read);
  } else {
    ExprPtr condition = ParseExpression(true);
    if (!condition) {
      scopes_.pop_back();
      return nullptr;
    }
    if (condition->type.base() != BaseType::kBool) {
      Fail("if condition must have type bool");
      scopes_.pop_back();
      return nullptr;
    }
    condition_statement.reset(new Statement());
    condition_statement->kind = Statement::Kind::kExpression;
    condition_statement->line = statement->line;
    condition_statement->expression = std::move(condition);
  }
  if (!ExpectPunct(")")) {
    scopes_.pop_back();
    return nullptr;
  }
  statement->condition = std::move(condition_statement);
  scopes_.emplace_back();
  statement->then_branch = ParseStatement();
  scopes_.pop_back();
  if (!statement->then_branch) {
    scopes_.pop_back();
    return nullptr;
  }
  if (AcceptIdent("else")) {
    scopes_.emplace_back();
    statement->else_branch = ParseStatement();
    scopes_.pop_back();
    if (!statement->else_branch) {
      scopes_.pop_back();
      return nullptr;
    }
  }
  scopes_.pop_back();
  return statement;
}

StmtPtr Parser::ParseFor() {
  StmtPtr statement(new Statement());
  statement->kind = Statement::Kind::kFor;
  statement->line = current_.line;
  Advance();  // for
  if (!ExpectPunct("("))
    return nullptr;
  scopes_.emplace_back();

  if (CheckPunct(";")) {
    Advance();
  } else if (IsDeclarationStart()) {
    StmtPtr declaration = ParseDeclarationStatement();
    if (!declaration)
      return nullptr;
    statement->init = std::move(declaration);
  } else {
    ExprPtr init = ParseExpression(true);
    if (!init || !ExpectPunct(";"))
      return nullptr;
    StmtPtr init_statement(new Statement());
    init_statement->kind = Statement::Kind::kExpression;
    init_statement->line = statement->line;
    init_statement->expression = std::move(init);
    statement->init = std::move(init_statement);
  }

  if (!CheckPunct(";")) {
    ExprPtr condition = ParseExpression(true);
    if (!condition)
      return nullptr;
    if (condition->type.base() != BaseType::kBool) {
      Fail("for condition must have type bool");
      return nullptr;
    }
    StmtPtr condition_statement(new Statement());
    condition_statement->kind = Statement::Kind::kExpression;
    condition_statement->line = statement->line;
    condition_statement->expression = std::move(condition);
    statement->condition = std::move(condition_statement);
  }
  if (!ExpectPunct(";"))
    return nullptr;

  if (!CheckPunct(")")) {
    ExprPtr step = ParseExpression(true);
    if (!step)
      return nullptr;
    StmtPtr step_statement(new Statement());
    step_statement->kind = Statement::Kind::kExpression;
    step_statement->line = statement->line;
    step_statement->expression = std::move(step);
    statement->step = std::move(step_statement);
  }
  if (!ExpectPunct(")"))
    return nullptr;

  ++loop_depth_;
  scopes_.emplace_back();
  statement->then_branch = ParseStatement();
  scopes_.pop_back();
  --loop_depth_;
  scopes_.pop_back();
  if (!statement->then_branch)
    return nullptr;
  return statement;
}

StmtPtr Parser::ParseWhile() {
  StmtPtr statement(new Statement());
  statement->kind = Statement::Kind::kWhile;
  statement->line = current_.line;
  Advance();  // while
  if (!ExpectPunct("("))
    return nullptr;
  scopes_.emplace_back();
  StmtPtr condition_statement;
  if (IsDeclarationStart()) {
    condition_statement = ParseDeclarationStatement(true);
    if (!condition_statement) {
      scopes_.pop_back();
      return nullptr;
    }
    if (condition_statement->declarators.size() != 1) {
      Fail("while condition must declare exactly one variable");
      scopes_.pop_back();
      return nullptr;
    }
    const Variable& variable = condition_statement->declarators[0].variable;
    if (variable.type.base() != BaseType::kBool) {
      Fail("while condition must have type bool");
      scopes_.pop_back();
      return nullptr;
    }
    ExprPtr read = MakeExpr(Expr::Kind::kVariable, statement->line);
    read->text = variable.name;
    read->storage = variable.storage;
    read->index = variable.index;
    read->type = variable.type;
    condition_statement->expression = std::move(read);
  } else {
    ExprPtr condition = ParseExpression(true);
    if (!condition) {
      scopes_.pop_back();
      return nullptr;
    }
    if (condition->type.base() != BaseType::kBool) {
      Fail("while condition must have type bool");
      scopes_.pop_back();
      return nullptr;
    }
    condition_statement.reset(new Statement());
    condition_statement->kind = Statement::Kind::kExpression;
    condition_statement->line = statement->line;
    condition_statement->expression = std::move(condition);
  }
  if (!ExpectPunct(")")) {
    scopes_.pop_back();
    return nullptr;
  }
  statement->condition = std::move(condition_statement);
  ++loop_depth_;
  scopes_.emplace_back();
  statement->then_branch = ParseStatement();
  scopes_.pop_back();
  --loop_depth_;
  if (!statement->then_branch) {
    scopes_.pop_back();
    return nullptr;
  }
  scopes_.pop_back();
  return statement;
}

StmtPtr Parser::ParseDoWhile() {
  StmtPtr statement(new Statement());
  statement->kind = Statement::Kind::kDoWhile;
  statement->line = current_.line;
  Advance();  // do
  ++loop_depth_;
  scopes_.emplace_back();
  statement->then_branch = ParseStatement();
  scopes_.pop_back();
  --loop_depth_;
  if (!statement->then_branch)
    return nullptr;
  if (!AcceptIdent("while")) {
    Fail("expected 'while'");
    return nullptr;
  }
  if (!ExpectPunct("("))
    return nullptr;
  ExprPtr condition = ParseExpression(true);
  if (!condition || !ExpectPunct(")"))
    return nullptr;
  if (condition->type.base() != BaseType::kBool) {
    Fail("do-while condition must have type bool");
    return nullptr;
  }
  StmtPtr condition_statement(new Statement());
  condition_statement->kind = Statement::Kind::kExpression;
  condition_statement->line = statement->line;
  condition_statement->expression = std::move(condition);
  statement->condition = std::move(condition_statement);
  if (!ExpectPunct(";"))
    return nullptr;
  return statement;
}

StmtPtr Parser::ParseSwitch() {
  StmtPtr statement(new Statement());
  statement->kind = Statement::Kind::kSwitch;
  statement->line = current_.line;
  Advance();  // switch
  if (!ExpectPunct("("))
    return nullptr;
  ExprPtr selector = ParseExpression(true);
  if (!selector || !ExpectPunct(")"))
    return nullptr;
  if (selector->type.base() != BaseType::kInt &&
      selector->type.base() != BaseType::kUInt) {
    Fail("switch selector must have type int or uint");
    return nullptr;
  }
  if (selector->type.is_vector() || selector->type.is_matrix()) {
    Fail("switch selector must be a scalar");
    return nullptr;
  }
  if (!ExpectPunct("{"))
    return nullptr;
  scopes_.emplace_back();
  ++switch_depth_;
  if (ast_)
    ast_->uses_switch = true;

  Statement::SwitchCase* current_case = nullptr;
  while (!CheckPunct("}")) {
    if (current_.kind == Token::Kind::kEnd) {
      Fail("unexpected end of shader inside switch");
      return nullptr;
    }
    if (CheckIdent("case")) {
      const int line = current_.line;
      Advance();
      ExprPtr label = ParseExpression(false);
      if (!label || !ExpectPunct(":"))
        return nullptr;
      if (label->type.base() != BaseType::kInt &&
          label->type.base() != BaseType::kUInt) {
        FailAt(line, "case label must have an integer type");
        return nullptr;
      }
      int new_value = 0;
      if (!EvalConstIntExpr(label, &new_value)) {
        FailAt(line, "case label must be an integral constant expression");
        return nullptr;
      }
      for (const Statement::SwitchCase& existing : statement->cases) {
        int existing_value = 0;
        if (existing.label &&
            EvalConstIntExpr(existing.label, &existing_value) &&
            existing_value == new_value) {
          FailAt(line, "duplicate case label");
          return nullptr;
        }
      }
      statement->cases.push_back(Statement::SwitchCase{std::move(label), {}});
      current_case = &statement->cases.back();
      continue;
    }
    if (CheckIdent("default")) {
      const int line = current_.line;
      Advance();
      if (!ExpectPunct(":"))
        return nullptr;
      for (const Statement::SwitchCase& existing : statement->cases) {
        if (!existing.label) {
          FailAt(line, "duplicate default label");
          return nullptr;
        }
      }
      statement->cases.push_back(Statement::SwitchCase{nullptr, {}});
      current_case = &statement->cases.back();
      continue;
    }
    if (!current_case) {
      Fail("statement outside a case label");
      return nullptr;
    }
    StmtPtr child = ParseStatement();
    if (!child)
      return nullptr;
    current_case->statements.push_back(std::move(child));
  }
  if (current_case && current_case->statements.empty()) {
    Fail("expected a statement after the previous case label");
    return nullptr;
  }
  Advance();  // '}'
  scopes_.pop_back();
  --switch_depth_;
  statement->expression = std::move(selector);
  return statement;
}

StmtPtr Parser::ParseReturn() {
  StmtPtr statement(new Statement());
  statement->kind = Statement::Kind::kReturn;
  statement->line = current_.line;
  Advance();  // return
  const bool returns_value = !CheckPunct(";");
  if (returns_value) {
    if (current_return_type_.is_void()) {
      Fail("a void function cannot return a value");
      return nullptr;
    }
    statement->expression = ParseExpression(true);
    if (!statement->expression)
      return nullptr;
  } else if (!current_return_type_.is_void()) {
    Fail("a non-void function must return a value");
    return nullptr;
  }
  if (!ExpectPunct(";"))
    return nullptr;
  return statement;
}

StmtPtr Parser::ParseJump(const char* keyword, Statement::Kind kind) {
  StmtPtr statement(new Statement());
  statement->kind = kind;
  statement->line = current_.line;
  if (!AcceptIdent(keyword)) {
    Fail(std::string("expected '") + keyword + "'");
    return nullptr;
  }
  if (!ExpectPunct(";"))
    return nullptr;
  return statement;
}

StmtPtr Parser::ParseExpressionStatement() {
  StmtPtr statement(new Statement());
  statement->kind = Statement::Kind::kExpression;
  statement->line = current_.line;
  statement->expression = ParseExpression(true);
  if (!statement->expression)
    return nullptr;
  if (!ExpectPunct(";"))
    return nullptr;
  return statement;
}

StmtPtr Parser::ParseStatement() {
  if (CheckPunct("{"))
    return ParseBlock();
  if (CheckIdent("if"))
    return ParseIf();
  if (CheckIdent("switch"))
    return ParseSwitch();
  if (CheckIdent("for"))
    return ParseFor();
  if (CheckIdent("while"))
    return ParseWhile();
  if (CheckIdent("do"))
    return ParseDoWhile();
  if (CheckIdent("return"))
    return ParseReturn();
  if (CheckIdent("break")) {
    if (loop_depth_ == 0 && switch_depth_ == 0) {
      Fail("'break' outside a loop");
      return nullptr;
    }
    return ParseJump("break", Statement::Kind::kBreak);
  }
  if (CheckIdent("continue")) {
    if (loop_depth_ == 0) {
      Fail("'continue' outside a loop");
      return nullptr;
    }
    return ParseJump("continue", Statement::Kind::kContinue);
  }
  if (CheckIdent("discard")) {
    if (stage_ != Stage::kFragment) {
      Fail("'discard' is only valid in a fragment shader");
      return nullptr;
    }
    ast_->uses_discard = true;
    return ParseJump("discard", Statement::Kind::kDiscard);
  }
  if (CheckIdent("precision")) {
    while (current_.kind != Token::Kind::kEnd && !CheckPunct(";"))
      Advance();
    if (!ExpectPunct(";"))
      return nullptr;
    StmtPtr empty(new Statement());
    empty->kind = Statement::Kind::kBlock;
    empty->line = current_.line;
    return empty;
  }
  if (IsDeclarationStart())
    return ParseDeclarationStatement();
  return ParseExpressionStatement();
}

ExprPtr Parser::ParseExpression(bool allow_comma) {
  ExprPtr first = ParseAssignment();
  if (!first)
    return nullptr;
  if (!allow_comma || !CheckPunct(","))
    return first;
  ExprPtr comma = MakeExpr(Expr::Kind::kComma, first->line);
  comma->args.push_back(std::move(first));
  while (AcceptPunct(",")) {
    ExprPtr next = ParseAssignment();
    if (!next)
      return nullptr;
    comma->args.push_back(std::move(next));
  }
  comma->type = comma->args.back()->type;
  return comma;
}

ExprPtr Parser::ParseAssignment() {
  ExprPtr left = ParseConditional();
  if (!left)
    return nullptr;
  if (current_.kind == Token::Kind::kPunct) {
    const Op op = AssignmentOpOfPunct(current_.text);
    if (op != Op::kNone) {
      if (left->kind != Expr::Kind::kVariable &&
          left->kind != Expr::Kind::kField &&
          left->kind != Expr::Kind::kIndex) {
        Fail("left side of an assignment is not assignable");
        return nullptr;
      }
      if (left->is_const) {
        Fail("cannot assign to a const variable");
        return nullptr;
      }
      if (left->kind == Expr::Kind::kVariable &&
          (left->storage == Storage::kAttribute ||
           left->storage == Storage::kUniform ||
           left->storage == Storage::kVaryingIn)) {
        Fail("cannot assign to a read-only variable");
        return nullptr;
      }
      const int line = current_.line;
      Advance();
      ExprPtr right = ParseAssignment();
      if (!right)
        return nullptr;
      ExprPtr assign = MakeExpr(Expr::Kind::kAssign, line);
      assign->op = op;
      assign->type = left->type;
      assign->args.push_back(std::move(left));
      assign->args.push_back(std::move(right));
      return assign;
    }
  }
  return left;
}

ExprPtr Parser::ParseConditional() {
  ExprPtr condition = ParseBinary(1);
  if (!condition)
    return nullptr;
  if (!AcceptPunct("?"))
    return condition;
  const int line = current_.line;
  ExprPtr then_value = ParseAssignment();
  if (!then_value || !ExpectPunct(":"))
    return nullptr;
  ExprPtr else_value = ParseAssignment();
  if (!else_value)
    return nullptr;
  ExprPtr ternary = MakeExpr(Expr::Kind::kTernary, line);
  ternary->type = then_value->type;
  ternary->args.push_back(std::move(condition));
  ternary->args.push_back(std::move(then_value));
  ternary->args.push_back(std::move(else_value));
  return ternary;
}

ExprPtr Parser::ParseBinary(int min_precedence) {
  ExprPtr left = ParseUnary();
  if (!left)
    return nullptr;
  for (;;) {
    if (current_.kind != Token::Kind::kPunct)
      break;
    const int precedence = PrecedenceOfPunct(current_.text);
    if (precedence < min_precedence)
      break;
    const Op op = OpOfPunct(current_.text);
    if (op == Op::kNone)
      break;
    const int line = current_.line;
    Advance();
    ExprPtr right = ParseBinary(precedence + 1);
    if (!right)
      return nullptr;
    ExprPtr binary = MakeExpr(Expr::Kind::kBinary, line);
    binary->op = op;
    binary->type = BinaryResultType(op, left->type, right->type);
    binary->args.push_back(std::move(left));
    binary->args.push_back(std::move(right));
    left = std::move(binary);
  }
  return left;
}

ExprPtr Parser::ParseUnary() {
  if (current_.kind == Token::Kind::kPunct) {
    const std::string& punct = current_.text;
    if (punct == "+") {
      Advance();
      return ParseUnary();
    }
    Op op = Op::kNone;
    if (punct == "-")
      op = Op::kNegate;
    else if (punct == "!")
      op = Op::kNot;
    else if (punct == "~")
      op = Op::kBitNot;
    else if (punct == "++")
      op = Op::kPreIncrement;
    else if (punct == "--")
      op = Op::kPreDecrement;
    if (op != Op::kNone) {
      const int line = current_.line;
      Advance();
      ExprPtr operand = ParseUnary();
      if (!operand)
        return nullptr;
      ExprPtr unary = MakeExpr(Expr::Kind::kUnary, line);
      unary->op = op;
      if (op == Op::kNot)
        unary->type = Type::Bool();
      else
        unary->type = operand->type;
      unary->args.push_back(std::move(operand));
      return unary;
    }
  }
  return ParsePostfix();
}

ExprPtr Parser::ParsePostfix() {
  ExprPtr expression = ParsePrimary();
  if (!expression)
    return nullptr;
  for (;;) {
    if (AcceptPunct("[")) {
      const int line = current_.line;
      ExprPtr index = ParseExpression(false);
      if (!index || !ExpectPunct("]"))
        return nullptr;
      ExprPtr indexed = MakeExpr(Expr::Kind::kIndex, line);
      const Type& base = expression->type;
      if (base.is_array())
        indexed->type = base.array_element();
      else if (base.is_matrix())
        indexed->type = base.Column();
      else if (base.is_vector() || base.is_scalar())
        indexed->type = base.Element();
      else
        indexed->type = Type::Float();
      indexed->is_const = expression->is_const;
      indexed->base = std::move(expression);
      indexed->args.push_back(std::move(index));
      expression = std::move(indexed);
      continue;
    }
    if (AcceptPunct(".")) {
      const int line = current_.line;
      if (current_.kind != Token::Kind::kIdentifier) {
        Fail("expected a field name");
        return nullptr;
      }
      const std::string field = current_.text;
      Advance();
      if (field == "length" && expression->type.is_array() &&
          CheckPunct("(")) {
        Advance();
        if (!ExpectPunct(")"))
          return nullptr;
        ExprPtr length = MakeExpr(Expr::Kind::kLiteral, line);
        length->type = Type::Int();
        length->literal = static_cast<float>(expression->type.array_size());
        length->integer_literal = static_cast<uint64_t>(
            expression->type.array_size());
        expression = std::move(length);
        continue;
      }
      if (expression->kind == Expr::Kind::kVariable &&
          expression->ubo_block >= 0) {
        const UniformBlock& block =
            ast_->uniform_blocks[expression->ubo_block];
        const UniformBlockMember* found = nullptr;
        for (const UniformBlockMember& member : block.members) {
          if (member.name == field) {
            found = &member;
            break;
          }
        }
        if (!found) {
          Fail("uniform block '" + block.name + "' has no member '" + field +
               "'");
          return nullptr;
        }
        ExprPtr variable = MakeExpr(Expr::Kind::kVariable, line);
        variable->storage = Storage::kUniform;
        variable->index = found->uniform_index;
        if (expression->ubo_instance <
                static_cast<int>(found->instance_uniform_indices.size()) &&
            expression->ubo_instance >= 0)
          variable->index =
              found->instance_uniform_indices[expression->ubo_instance];
        if (variable->index >= 0 &&
            variable->index < static_cast<int>(ast_->uniforms.size()))
          ast_->uniforms[variable->index].used = true;
        variable->type = found->type;
        expression = std::move(variable);
        continue;
      }
      if (expression->type.is_struct()) {
        const StructInfo* info = expression->type.struct_info();
        const StructMember* found = nullptr;
        if (info) {
          for (const StructMember& member : info->members) {
            if (member.name == field) {
              found = &member;
              break;
            }
          }
        }
        if (!found) {
          Fail("structure '" + expression->type.Name() + "' has no member '" +
               field + "'");
          return nullptr;
        }
        ExprPtr member = MakeExpr(Expr::Kind::kField, line);
        member->text = field;
        member->type = found->type;
        member->field_offset = found->component_offset;
        member->is_const = expression->is_const;
        member->base = std::move(expression);
        expression = std::move(member);
        continue;
      }
      if (expression->builtin == BuiltinId::kDepthRange) {
        ExprPtr member = MakeExpr(Expr::Kind::kField, line);
        member->text = field == "near" ? "x" : field == "far" ? "y"
                       : field == "diff" ? "z" : "";
        if (member->text.empty()) {
          Fail("unknown gl_DepthRange member '" + field + "'");
          return nullptr;
        }
        member->type = Type::Float();
        member->base = std::move(expression);
        expression = std::move(member);
        continue;
      }
      int swizzle_set = -1;
      for (char c : field) {
        int set = -1;
        if (!IsSwizzleChar(c, &set)) {
          Fail("invalid swizzle '" + field + "'");
          return nullptr;
        }
        if (swizzle_set < 0)
          swizzle_set = set;
        else if (swizzle_set != set) {
          Fail("mixed swizzle sets in '" + field + "'");
          return nullptr;
        }
      }
      if (field.empty() || field.size() > 4) {
        Fail("invalid swizzle '" + field + "'");
        return nullptr;
      }
      ExprPtr member = MakeExpr(Expr::Kind::kField, line);
      member->text = field;
      member->type = field.size() == 1
                         ? expression->type.Element()
                         : Type(expression->type.base(),
                                static_cast<int>(field.size()), 1);
      if (!expression->type.is_vector() && !expression->type.is_scalar() &&
          !expression->type.is_matrix()) {
        Fail("field access on a non-vector value");
        return nullptr;
      }
      member->base = std::move(expression);
      expression = std::move(member);
      continue;
    }
    if (CheckPunct("++") || CheckPunct("--")) {
      const bool increment = CheckPunct("++");
      const int line = current_.line;
      Advance();
      ExprPtr unary = MakeExpr(Expr::Kind::kUnary, line);
      unary->op = increment ? Op::kPostIncrement : Op::kPostDecrement;
      unary->type = expression->type;
      unary->args.push_back(std::move(expression));
      expression = std::move(unary);
      continue;
    }
    break;
  }
  return expression;
}

ExprPtr Parser::ParseConstructor(const Type& type, int line) {
  ExprPtr call = MakeExpr(Expr::Kind::kCall, line);
  call->is_constructor = true;
  call->type = type;
  call->text = type.Name();
  if (!ExpectPunct("("))
    return nullptr;
  if (!CheckPunct(")")) {
    for (;;) {
      ExprPtr argument = ParseAssignment();
      if (!argument)
        return nullptr;
      call->args.push_back(std::move(argument));
      if (!AcceptPunct(","))
        break;
    }
  }
  if (!ExpectPunct(")"))
    return nullptr;
  if (call->args.empty()) {
    FailAt(line, "a constructor requires an argument");
    return nullptr;
  }
  if (call->type.is_vector()) {
    const bool scalar_splat =
        call->args.size() == 1 && call->args[0]->type.is_scalar();
    // A single vector argument may be truncated to build a shorter vector,
    // converting base types component-wise (e.g. vec2(ivec4), bvec2(vec4)).
    const bool truncating_vector =
        call->args.size() == 1 && call->args[0]->type.is_vector() &&
        call->args[0]->type.components() >= call->type.components();
    const bool truncating_matrix =
        call->args.size() == 1 && call->args[0]->type.is_matrix() &&
        call->args[0]->type.base() == call->type.base() &&
        call->args[0]->type.components() >= call->type.components();
    if (!scalar_splat && !truncating_vector && !truncating_matrix) {
      int components = 0;
      for (const ExprPtr& argument : call->args)
        components += argument->type.is_scalar() ? 1
                                                 : argument->type.components();
      if (components < call->type.components()) {
        FailAt(line, "constructor component count mismatch");
        return nullptr;
      }
    }
  }
  if (call->type.is_array() && call->type.array_size() == 0) {
    if (call->args.size() != 1 &&
        call->args[0]->type != call->type.array_element()) {
      FailAt(line, "array constructor element type mismatch");
      return nullptr;
    }
    call->type = Type::Array(call->type.array_element(),
                             static_cast<int>(call->args.size()));
  }
  return call;
}

ExprPtr Parser::ParseCall(const std::string& name, int line) {
  Type constructed;
  if (LookupType(name, &constructed) && !constructed.is_void())
    return ParseConstructor(constructed, line);

  BuiltinFunc function = BuiltinFunc::kNone;
  for (const BuiltinFuncEntry& entry : kBuiltinFuncs) {
    if (name == entry.name) {
      function = entry.function;
      break;
    }
  }
  if (function != BuiltinFunc::kNone) {
    ExprPtr call = MakeExpr(Expr::Kind::kCall, line);
    call->builtin_func = function;
    call->text = name;
    if (!ExpectPunct("("))
      return nullptr;
    if (!CheckPunct(")")) {
      for (;;) {
        ExprPtr argument = ParseAssignment();
        if (!argument)
          return nullptr;
        call->args.push_back(std::move(argument));
        if (!AcceptPunct(","))
          break;
      }
    }
    if (!ExpectPunct(")"))
      return nullptr;
    call->type = BuiltinResultType(function, call->args, line);
    if (!error_.empty())
      return nullptr;
    return call;
  }

  ExprPtr call = MakeExpr(Expr::Kind::kCall, line);
  call->text = name;
  if (!ExpectPunct("("))
    return nullptr;
  if (!CheckPunct(")")) {
    for (;;) {
      ExprPtr argument = ParseAssignment();
      if (!argument)
        return nullptr;
      call->args.push_back(std::move(argument));
      if (!AcceptPunct(","))
        break;
    }
  }
  if (!ExpectPunct(")"))
    return nullptr;

  int function_index = -1;
  for (size_t i = 0; i < ast_->functions.size(); ++i) {
    const Function& candidate = ast_->functions[i];
    if (candidate.name != name ||
        candidate.parameters.size() != call->args.size()) {
      continue;
    }
    bool matches = true;
    for (size_t p = 0; p < call->args.size(); ++p) {
      if (!(candidate.parameters[p].type == call->args[p]->type)) {
        matches = false;
        break;
      }
    }
    if (matches) {
      function_index = static_cast<int>(i);
      break;
    }
  }
  if (function_index < 0) {
    FailAt(line, "no matching overload for '" + name + "'");
    return nullptr;
  }
  const Function& callee = ast_->functions[function_index];
  for (size_t i = 0; i < call->args.size(); ++i) {
    const Expr& argument = *call->args[i];
    if (callee.parameters[i].parameter_qualifier != 0 &&
        argument.storage == Storage::kUniform) {
      FailAt(line, "out and inout arguments must be l-values");
      return nullptr;
    }
    if (callee.parameters[i].parameter_qualifier != 0 &&
        argument.is_const) {
      FailAt(line, "out and inout arguments cannot be const");
      return nullptr;
    }
  }
  call->function_index = function_index;
  call->type = callee.return_type;
  if (!current_function_.empty())
    calls_.push_back({current_function_, callee.name});
  return call;
}

ExprPtr Parser::ParseVariable(const std::string& name, int line) {
  for (size_t s = scopes_.size(); s > 0; --s) {
    const auto& scope = scopes_[s - 1];
    for (const auto& entry : scope) {
      if (entry.first != name)
        continue;
      ExprPtr expr = MakeExpr(Expr::Kind::kVariable, line);
      expr->text = entry.first;
      expr->storage = Storage::kLocal;
      expr->index = entry.second;
      expr->is_const = entry.second >= 0 &&
                               entry.second < static_cast<int>(slot_is_const_.size())
                           ? slot_is_const_[entry.second]
                           : false;
      expr->is_parameter = entry.second >= 0 &&
                           entry.second < current_parameter_count_;
      expr->type = entry.second < static_cast<int>(slot_types_.size())
                       ? slot_types_[entry.second]
                       : Type::Float();
      return expr;
    }
  }
  int index = -1;
  if (HasName(block_instances_, name, &index)) {
    int instance = 0;
    if (AcceptPunct("[")) {
      if (!ParseConstantInt(&instance) || instance < 0) {
        FailAt(line, "invalid uniform block instance index");
        return nullptr;
      }
      if (!ExpectPunct("]"))
        return nullptr;
      if (index >= 0 && index < static_cast<int>(ast_->uniform_blocks.size()) &&
          instance >= ast_->uniform_blocks[index].instance_array_size) {
        FailAt(line, "uniform block instance index out of range");
        return nullptr;
      }
    }
    if (!CheckPunct(".")) {
      FailAt(line, "uniform block instance '" + name +
                       "' must be accessed through a member");
      return nullptr;
    }
    ExprPtr expr = MakeExpr(Expr::Kind::kVariable, line);
    expr->storage = Storage::kUniform;
    expr->index = -1;
    expr->ubo_block = index;
    expr->ubo_instance = instance;
    expr->type = Type::Void();
    return expr;
  }
  if (HasName(global_names_, name, &index)) {
    ExprPtr expr = MakeExpr(Expr::Kind::kVariable, line);
    expr->text = name;
    expr->storage = Storage::kGlobal;
    expr->index = index;
    expr->is_const = false;
    for (const Variable& global : ast_->globals) {
      if (global.name == name) {
        expr->type = global.type;
        expr->is_const = global.is_const;
        break;
      }
    }
    if (expr->type.is_void()) {
      for (const Variable& global : ast_->globals) {
        if (global.index == index) {
          expr->type = global.type;
          break;
        }
      }
    }
    return expr;
  }
  if (HasName(uniform_names_, name, &index)) {
    ExprPtr expr = MakeExpr(Expr::Kind::kVariable, line);
    expr->storage = Storage::kUniform;
    expr->index = index;
    expr->type = ast_->uniforms[index].type;
    return expr;
  }
  if (HasName(attribute_names_, name, &index)) {
    ExprPtr expr = MakeExpr(Expr::Kind::kVariable, line);
    expr->storage = Storage::kAttribute;
    expr->index = index;
    for (const Variable& attribute : ast_->attributes) {
      if (attribute.name == name) {
        expr->type = attribute.type;
        break;
      }
    }
    return expr;
  }
  if (HasName(varying_names_, name, &index)) {
    if (index >= 0 && index < static_cast<int>(ast_->varyings.size()))
      ast_->varyings[index].used = true;
    ExprPtr expr = MakeExpr(Expr::Kind::kVariable, line);
    expr->storage =
        stage_ == Stage::kVertex ? Storage::kVaryingOut : Storage::kVaryingIn;
    expr->index = index;
    expr->type = ast_->varyings[index].type;
    return expr;
  }
  if (name == "gl_DepthRange") {
    ExprPtr expr = MakeExpr(Expr::Kind::kVariable, line);
    expr->storage = Storage::kGlobal;
    expr->builtin = BuiltinId::kDepthRange;
    expr->index = EnsureBuiltin(BuiltinId::kDepthRange);
    expr->type = Type::Vec(3);
    if (GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_DR"))
      fprintf(stderr, "[dr] parse depth range slot=%d\n", expr->index);
    if (ast_)
      ast_->uses_depth_range = true;
    return expr;
  }
  for (const BuiltinConstantEntry& constant : kBuiltinConstants) {
    if (name != constant.name)
      continue;
    ExprPtr literal = MakeExpr(Expr::Kind::kLiteral, line);
    literal->type = Type::Int();
    literal->literal = static_cast<float>(constant.value);
    literal->integer_literal =
        static_cast<uint64_t>(static_cast<uint32_t>(constant.value));
    return literal;
  }
  for (const BuiltinVarEntry& builtin : kBuiltinVars) {
    if (name != builtin.name)
      continue;
    const bool stage_matches =
        stage_ == Stage::kVertex ? builtin.vertex_stage : !builtin.vertex_stage;
    if (!stage_matches) {
      FailAt(line, "'" + name + "' is not available in this stage");
      return nullptr;
    }
    const int slot = EnsureBuiltin(builtin.id);
    ExprPtr expr = MakeExpr(Expr::Kind::kVariable, line);
    expr->storage = Storage::kGlobal;
    expr->index = slot;
    expr->type = builtin.type;
    return expr;
  }
  FailAt(line, "undeclared identifier '" + name + "'");
  return nullptr;
}

ExprPtr Parser::ParsePrimary() {
  const int line = current_.line;
  if (current_.kind == Token::Kind::kFloatLiteral ||
      current_.kind == Token::Kind::kIntLiteral) {
    ExprPtr literal = MakeExpr(Expr::Kind::kLiteral, line);
    literal->literal = static_cast<float>(current_.number);
    literal->integer_literal = static_cast<uint64_t>(current_.number);
    if (current_.kind == Token::Kind::kFloatLiteral) {
      literal->type = Type::Float();
    } else {
      literal->type = current_.is_unsigned ? Type::UInt() : Type::Int();
    }
    Advance();
    return literal;
  }
  if (AcceptPunct("(")) {
    ExprPtr expression = ParseExpression(true);
    if (!expression || !ExpectPunct(")"))
      return nullptr;
    return expression;
  }
  if (current_.kind == Token::Kind::kIdentifier) {
    const std::string name = current_.text;
    if (name == "true" || name == "false") {
      ExprPtr literal = MakeExpr(Expr::Kind::kLiteral, line);
      literal->type = Type::Bool();
      literal->literal = name == "true" ? 1.0f : 0.0f;
      Advance();
      return literal;
    }
    Advance();
    if (IsKnownTypeName(name) && CheckPunct("[")) {
      Type constructed;
      LookupType(name, &constructed);
      if (!ParseArraySuffix(&constructed) || !CheckPunct("(")) {
        FailAt(line, "expected a constructor call after an array type");
        return nullptr;
      }
      return ParseConstructor(constructed, line);
    }
    if (CheckPunct("("))
      return ParseCall(name, line);
    if (name == "gl_FragData") {
      if (!CheckPunct("[")) {
        FailAt(line, "expected an index after gl_FragData");
        return nullptr;
      }
      Advance();
      ExprPtr index = ParseExpression(false);
      if (!index)
        return nullptr;
      if (!ExpectPunct("]"))
        return nullptr;
      const int array_slot = EnsureBuiltin(BuiltinId::kFragData);
      if (array_slot < 0) {
        FailAt(line, "could not allocate gl_FragData");
        return nullptr;
      }
      ast_->uses_frag_data = true;
      // gl_FragData acts as one vec4[4] array; expose every location so a
      // dynamically indexed write reaches the corresponding draw buffer.
      for (int location = 0; location < kMaxFragmentOutputs; ++location) {
        bool exists = false;
        for (const FragmentOut& output : ast_->outputs)
          if (output.location == location)
            exists = true;
        if (!exists) {
          FragmentOut output;
          output.name = "gl_FragData[" + std::to_string(location) + "]";
          output.location = location;
          output.slot = array_slot;
          output.element = location;
          output.components = 4;
          output.base = BaseType::kFloat;
          ast_->outputs.push_back(output);
        }
      }
      ExprPtr base = MakeExpr(Expr::Kind::kVariable, line);
      base->text = "gl_FragData";
      base->storage = Storage::kGlobal;
      base->index = array_slot;
      base->type = Type::Array(Type::Vec(4), kMaxFragmentOutputs);
      ExprPtr expr = MakeExpr(Expr::Kind::kIndex, line);
      expr->base = std::move(base);
      expr->args.push_back(std::move(index));
      expr->type = Type::Vec(4);
      return expr;
    }
    return ParseVariable(name, line);
  }
  Fail("expected an expression");
  return nullptr;
}

int Parser::EnsureBuiltin(BuiltinId id) {
  const int id_index = static_cast<int>(id);
  if (id_index >= 0 &&
      id_index < static_cast<int>(ast_->builtin_slots.size()) &&
      ast_->builtin_slots[id_index] >= 0) {
    return ast_->builtin_slots[id_index];
  }
  for (const BuiltinVarEntry& entry : kBuiltinVars) {
    if (entry.id != id)
      continue;
    if (id == BuiltinId::kDepthRange && GLANCEASY_DEBUG_FLAG("GLANCEASY_DEBUG_DR"))
      fprintf(stderr, "[dr] ensure idx=%d size=%d entry=%s\n", id_index,
              (int)ast_->builtin_slots.size(), entry.name);
    Variable variable;
    variable.name = entry.name;
    variable.type = entry.type;
    variable.storage = Storage::kGlobal;
    variable.index = ast_->global_slots++;
    variable.builtin = id;
    ast_->globals.push_back(variable);
    ast_->builtin_slots[id_index] = variable.index;
    switch (id) {
      case BuiltinId::kPosition:
        ast_->declares_position = true;
        break;
      case BuiltinId::kPointSize:
        ast_->uses_point_size = true;
        break;
      case BuiltinId::kFragCoord:
        ast_->uses_frag_coord = true;
        break;
      case BuiltinId::kFrontFacing:
        ast_->uses_front_facing = true;
        break;
      case BuiltinId::kPointCoord:
        ast_->uses_point_coord = true;
        break;
      case BuiltinId::kFragColor:
        ast_->uses_frag_color = true;
        break;
      case BuiltinId::kFragData:
        ast_->uses_frag_data = true;
        break;
      case BuiltinId::kFragOutput0:
      case BuiltinId::kFragOutput1:
      case BuiltinId::kFragOutput2:
      case BuiltinId::kFragOutput3:
        ast_->uses_frag_color = true;
        break;
      case BuiltinId::kFragDepth:
        ast_->uses_frag_depth = true;
        break;
      default:
        break;
    }
    return variable.index;
  }
  return -1;
}

Type Parser::BinaryResultType(Op op, const Type& left, const Type& right) {
  switch (op) {
    case Op::kLess:
    case Op::kLessEqual:
    case Op::kGreater:
    case Op::kGreaterEqual:
    case Op::kEqual:
    case Op::kNotEqual:
    case Op::kLogicalAnd:
    case Op::kLogicalXor:
    case Op::kLogicalOr:
      return Type::Bool();
    case Op::kBitAnd:
    case Op::kBitXor:
    case Op::kBitOr: {
      const int rows = std::max(left.rows(), right.rows());
      const BaseType base = (left.base() == BaseType::kUInt ||
                             right.base() == BaseType::kUInt)
                                ? BaseType::kUInt
                                : BaseType::kInt;
      return Type(base, rows, 1);
    }
    case Op::kShiftLeft:
    case Op::kShiftRight:
      // GLSL ES 3.0: the result of a shift has the type of the left operand.
      return Type(left.base() == BaseType::kUInt ? BaseType::kUInt
                                                 : BaseType::kInt,
                  std::max(left.rows(), 1), 1);
    case Op::kMultiply: {
      if (left.is_matrix() && right.is_matrix())
        return Type::Mat(right.columns(), left.rows());
      if (left.is_matrix())
        return right.is_scalar() ? left : Type::Vec(left.rows());
      if (right.is_matrix())
        return left.is_scalar() ? right : Type::Vec(right.columns());
      break;
    }
    default:
      break;
  }
  if (left.is_matrix() || right.is_matrix())
    return left.is_matrix() ? left : right;
  const int rows = std::max(left.rows(), right.rows());
  BaseType base = BaseType::kFloat;
  if (left.base() == BaseType::kUInt || right.base() == BaseType::kUInt)
    base = BaseType::kUInt;
  else if (left.base() == BaseType::kInt && right.base() == BaseType::kInt)
    base = BaseType::kInt;
  return Type(base, std::max(rows, 1), 1);
}

Type Parser::BuiltinResultType(BuiltinFunc function,
                               const std::vector<ExprPtr>& args,
                               int line) {
  const Type first = args.empty() ? Type::Float() : args[0]->type;
  switch (function) {
    case BuiltinFunc::kLength:
    case BuiltinFunc::kDistance:
    case BuiltinFunc::kDot:
    case BuiltinFunc::kDeterminant:
      return Type::Float();
    case BuiltinFunc::kCross:
      if (args.size() != 2 || !(first == Type::Vec(3))) {
        FailAt(line, "cross() expects two vec3 arguments");
        return Type::Float();
      }
      return Type::Vec(3);
    case BuiltinFunc::kTranspose:
      return first.is_matrix() ? Type::Mat(first.rows(), first.columns())
                               : first;
    case BuiltinFunc::kInverse:
    case BuiltinFunc::kMatrixCompMult:
      return first;
    case BuiltinFunc::kOuterProduct:
      if (args.size() != 2) {
        FailAt(line, "outerProduct() expects two vector arguments");
        return Type::Float();
      }
      return Type::Mat(args[1]->type.rows(), args[0]->type.rows());
    case BuiltinFunc::kLessThan:
    case BuiltinFunc::kLessThanEqual:
    case BuiltinFunc::kGreaterThan:
    case BuiltinFunc::kGreaterThanEqual:
    case BuiltinFunc::kEqualFunc:
    case BuiltinFunc::kNotEqualFunc:
    case BuiltinFunc::kNot:
      return Type::BVec(std::max(2, first.rows()));
    case BuiltinFunc::kAny:
    case BuiltinFunc::kAll:
      return Type::Bool();
    case BuiltinFunc::kFloatBitsToUint:
      return Type::UVec(std::max(1, first.rows()));
    case BuiltinFunc::kFloatBitsToInt:
      return Type::IVec(std::max(1, first.rows()));
    case BuiltinFunc::kIsInf:
    case BuiltinFunc::kIsNan:
      return Type::BVec(std::max(1, first.rows()));
    case BuiltinFunc::kPackUnorm2x16:
    case BuiltinFunc::kPackSnorm2x16:
    case BuiltinFunc::kPackHalf2x16:
      return Type::UInt();
    case BuiltinFunc::kUnpackUnorm2x16:
    case BuiltinFunc::kUnpackSnorm2x16:
    case BuiltinFunc::kUnpackHalf2x16:
      return Type::Vec(2);
    case BuiltinFunc::kUIntBitsToFloat:
    case BuiltinFunc::kIntBitsToFloat:
      return Type::Vec(std::max(1, first.rows()));
    case BuiltinFunc::kStep:
    case BuiltinFunc::kSmoothStep:
      // The result has the type of the last argument (the value being
      // compared), not the type of the edge argument.
      return args.empty() ? first : args.back()->type;
    case BuiltinFunc::kTexture2D:
    case BuiltinFunc::kTexture2DProj:
    case BuiltinFunc::kTextureCube:
    case BuiltinFunc::kTexture3D:
    case BuiltinFunc::kTexture:
    case BuiltinFunc::kTextureProj:
    case BuiltinFunc::kTextureProjLod:
    case BuiltinFunc::kTextureProjOffset:
    case BuiltinFunc::kTextureProjLodOffset:
    case BuiltinFunc::kTextureProjGrad:
    case BuiltinFunc::kTextureProjGradOffset:
    case BuiltinFunc::kTextureLod:
    case BuiltinFunc::kTextureOffset:
    case BuiltinFunc::kTextureLodOffset:
    case BuiltinFunc::kTextureGrad:
    case BuiltinFunc::kTextureGradOffset:
    case BuiltinFunc::kTexelFetch:
    case BuiltinFunc::kTexelFetchOffset: {
      // ES 3.00 has no LOD-explicit lookup on the shadow cube / shadow array
      // samplers; rejecting them keeps the invalid-usage cases honest.
      if ((function == BuiltinFunc::kTextureLod ||
           function == BuiltinFunc::kTextureLodOffset) &&
          (first.base() == BaseType::kSamplerCubeShadow ||
           first.base() == BaseType::kSampler2DArrayShadow))
        FailAt(line, "textureLod() is not defined for this sampler type");
      if ((function == BuiltinFunc::kTexelFetch ||
           function == BuiltinFunc::kTexelFetchOffset) &&
          (first.base() == BaseType::kSamplerCube ||
           first.base() == BaseType::kISamplerCube ||
           first.base() == BaseType::kUSamplerCube ||
           first.base() == BaseType::kSamplerCubeShadow ||
           first.base() == BaseType::kSampler2DShadow ||
           first.base() == BaseType::kSampler2DArrayShadow))
        FailAt(line, "texelFetch() is not defined for this sampler type");
      // Projective lookups exist only for 2D, 3D and 2D shadow samplers.
      if ((function == BuiltinFunc::kTextureProj ||
           function == BuiltinFunc::kTextureProjLod ||
           function == BuiltinFunc::kTextureProjOffset ||
           function == BuiltinFunc::kTextureProjLodOffset ||
           function == BuiltinFunc::kTextureProjGrad ||
           function == BuiltinFunc::kTextureProjGradOffset) &&
          (first.base() == BaseType::kSamplerCube ||
           first.base() == BaseType::kISamplerCube ||
           first.base() == BaseType::kUSamplerCube ||
           first.base() == BaseType::kSamplerCubeShadow ||
           first.base() == BaseType::kSampler2DArray ||
           first.base() == BaseType::kISampler2DArray ||
           first.base() == BaseType::kUSampler2DArray ||
           first.base() == BaseType::kSampler2DArrayShadow))
        FailAt(line, "textureProj() is not defined for this sampler type");
      // Offset lookups are not defined for cube samplers.  textureLodOffset is
      // additionally unavailable on the shadow array sampler; textureOffset and
      // textureGradOffset are valid there.
      const bool offset_function =
          function == BuiltinFunc::kTextureOffset ||
          function == BuiltinFunc::kTextureLodOffset ||
          function == BuiltinFunc::kTextureGradOffset;
      const bool cube_sampler =
          first.base() == BaseType::kSamplerCube ||
          first.base() == BaseType::kISamplerCube ||
          first.base() == BaseType::kUSamplerCube ||
          first.base() == BaseType::kSamplerCubeShadow;
      if (offset_function && cube_sampler)
        FailAt(line, "offset lookups are not defined for this sampler type");
      if (function == BuiltinFunc::kTextureLodOffset &&
          first.base() == BaseType::kSampler2DArrayShadow)
        FailAt(line, "offset lookups are not defined for this sampler type");
      if (function == BuiltinFunc::kTextureOffset &&
          first.base() == BaseType::kSampler2DArrayShadow)
        FailAt(line, "offset lookups are not defined for this sampler type");
      if (function == BuiltinFunc::kTextureGrad &&
          first.base() == BaseType::kSamplerCubeShadow)
        FailAt(line, "textureGrad() is not defined for this sampler type");
      // Gradient vectors always carry the base texture dimensions (2 for 2D
      // and array textures, 3 for 3D and cube textures).
      const bool has_grad =
          function == BuiltinFunc::kTextureGrad ||
          function == BuiltinFunc::kTextureGradOffset ||
          function == BuiltinFunc::kTextureProjGrad ||
          function == BuiltinFunc::kTextureProjGradOffset;
      if (has_grad && args.size() >= 4) {
        const int expected =
            (first.base() == BaseType::kSampler3D ||
             first.base() == BaseType::kISampler3D ||
             first.base() == BaseType::kUSampler3D ||
             first.base() == BaseType::kSamplerCube ||
             first.base() == BaseType::kISamplerCube ||
             first.base() == BaseType::kUSamplerCube)
                ? 3
                : 2;
        if (args[2]->type.components() != expected ||
            args[3]->type.components() != expected)
          FailAt(line, "textureGrad() gradient vectors have the wrong size");
      }
      // Offsets are two-dimensional for 2D/2D-array lookups and three-
      // dimensional for 3D lookups.
      if (function == BuiltinFunc::kTextureOffset ||
          function == BuiltinFunc::kTextureLodOffset ||
          function == BuiltinFunc::kTextureGradOffset ||
          function == BuiltinFunc::kTextureProjOffset ||
          function == BuiltinFunc::kTextureProjLodOffset ||
          function == BuiltinFunc::kTextureProjGradOffset) {
        const int expected =
            (first.base() == BaseType::kSampler3D ||
             first.base() == BaseType::kISampler3D ||
             first.base() == BaseType::kUSampler3D ||
             first.base() == BaseType::kSamplerCube)
                ? 3
                : 2;
        for (size_t i = 1; i < args.size(); ++i) {
          const BaseType base = args[i]->type.base();
          if ((base == BaseType::kInt || base == BaseType::kUInt) &&
              args[i]->type.components() >= 2 &&
              args[i]->type.components() != expected)
            FailAt(line, "texture offset has the wrong size");
        }
      }
      if (first.base() == BaseType::kISampler2D ||
          first.base() == BaseType::kISampler3D ||
          first.base() == BaseType::kISamplerCube ||
          first.base() == BaseType::kISampler2DArray)
        return Type::IVec(4);
      if (first.base() == BaseType::kUSampler2D ||
          first.base() == BaseType::kUSampler3D ||
          first.base() == BaseType::kUSamplerCube ||
          first.base() == BaseType::kUSampler2DArray)
        return Type::UVec(4);
      return Type::Vec(4);
    }
    case BuiltinFunc::kTextureSize: {
      if (args.size() != 2 || !first.is_sampler()) {
        FailAt(line, "textureSize() expects a sampler and a level");
        return Type::Int();
      }
      switch (first.base()) {
        case BaseType::kSampler3D:
        case BaseType::kSampler2DArray:
        case BaseType::kISampler3D:
        case BaseType::kISampler2DArray:
        case BaseType::kUSampler3D:
        case BaseType::kUSampler2DArray:
        case BaseType::kSampler2DArrayShadow:
          return Type::IVec(3);
        default:
          return Type::IVec(2);
      }
    }
    default:
      break;
  }
  if (args.empty()) {
    FailAt(line, "a built-in function requires an argument");
    return Type::Float();
  }
  return first;
}

std::unique_ptr<ShaderAst> Parser::Parse() {
  ast_.reset(new ShaderAst());
  ast_->stage = stage_;
  ast_->builtin_slots.assign(static_cast<int>(BuiltinId::kCount), -1);
  Advance();
  if (!lexer_.ok()) {
    Fail(lexer_.error());
    return nullptr;
  }
  ast_->version = lexer_.glsl_version();

  while (current_.kind != Token::Kind::kEnd) {
    if (CheckIdent("precision")) {
      while (current_.kind != Token::Kind::kEnd && !CheckPunct(";"))
        Advance();
      if (!ExpectPunct(";"))
        return nullptr;
      continue;
    }
    if (AcceptPunct(";"))
      continue;
    if (CheckIdent("struct")) {
      if (!ParseStructDefinition())
        return nullptr;
      continue;
    }
    if (!ParseGlobalDeclaration())
      return nullptr;
  }
  if (!error_.empty())
    return nullptr;
  if (ast_->uses_frag_data &&
      ast_->builtin_slots[static_cast<int>(BuiltinId::kFragColor)] >= 0) {
    Fail("gl_FragColor and gl_FragData cannot be used together");
    return nullptr;
  }
  if (ast_->main_function < 0) {
    Fail("missing void main()");
    return nullptr;
  }
  const Function& main = ast_->functions[ast_->main_function];
  if (!main.defined) {
    Fail("main() is declared but never defined");
    return nullptr;
  }
  if (!main.return_type.is_void()) {
    Fail("main() must return void");
    return nullptr;
  }
  if (!main.parameters.empty()) {
    Fail("main() cannot take parameters");
    return nullptr;
  }
  for (const Function& function : ast_->functions) {
    if (!function.defined) {
      FailAt(function.line,
             "function '" + function.name + "' is declared but never defined");
      return nullptr;
    }
  }
  {
    std::map<std::string, std::vector<std::string>> graph;
    for (const auto& call : calls_)
      graph[call.first].push_back(call.second);
    std::map<std::string, int> state;
    const auto visit = [&](const auto& self, const std::string& name) -> bool {
      state[name] = 1;
      for (const std::string& next : graph[name]) {
        if (state[next] == 1)
          return false;
        if (state[next] == 0 && !self(self, next))
          return false;
      }
      state[name] = 2;
      return true;
    };
    for (const Function& function : ast_->functions) {
      if (state[function.name] == 0 && !visit(visit, function.name)) {
        FailAt(function.line, "recursive function call is not allowed");
        return nullptr;
      }
    }
  }
  return std::move(ast_);
}

}  // namespace glsl
}  // namespace glanceasy
