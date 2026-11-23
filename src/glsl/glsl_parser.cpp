// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/glsl/glsl_parser.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace zgl {
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
    {"gl_FragData", BuiltinId::kFragData, Type::Vec(4), false},
    {"gl_FragDepth", BuiltinId::kFragDepth, Type::Float(), false},
    {"__zgl_frag_output_0", BuiltinId::kFragOutput0, Type::Vec(4), false},
    {"__zgl_frag_output_1", BuiltinId::kFragOutput1, Type::Vec(4), false},
    {"__zgl_frag_output_2", BuiltinId::kFragOutput2, Type::Vec(4), false},
    {"__zgl_frag_output_3", BuiltinId::kFragOutput3, Type::Vec(4), false},
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
    {"asin", BuiltinFunc::kAsin},
    {"acos", BuiltinFunc::kAcos},
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
    {"fract", BuiltinFunc::kFract},
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
    {"textureLod", BuiltinFunc::kTextureLod},
    {"textureSize", BuiltinFunc::kTextureSize},
    {"dFdx", BuiltinFunc::kDfdx},
    {"dFdy", BuiltinFunc::kDfdy},
    {"fwidth", BuiltinFunc::kFwidth},
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

}  // namespace

Parser::Parser(Stage stage, const std::string& source)
    : stage_(stage), lexer_(source) {}

Parser::~Parser() = default;

void Parser::Advance() {
  current_ = lexer_.Next();
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

bool Parser::ParseQualifiers(Qualifiers* qualifiers) {
  for (;;) {
    if (current_.kind != Token::Kind::kIdentifier)
      return true;
    const std::string& text = current_.text;
    if (text == "const") {
      qualifiers->is_const = true;
    } else if (text == "uniform") {
      qualifiers->is_uniform = true;
    } else if (text == "attribute") {
      qualifiers->is_attribute = true;
    } else if (text == "varying") {
      qualifiers->is_varying = true;
    } else if (text == "in") {
      qualifiers->is_in = true;
    } else if (text == "out") {
      qualifiers->is_out = true;
    } else if (text == "invariant") {
      qualifiers->is_invariant = true;
    } else if (text == "flat") {
      qualifiers->flat = true;
    } else if (text == "smooth" || text == "centroid" || text == "lowp" ||
               text == "mediump" || text == "highp") {
      // Interpolation and precision qualifiers are accepted and ignored.
    } else if (text == "layout") {
      Advance();
      if (!ExpectPunct("("))
        return false;
      for (;;) {
        if (CheckIdent("location")) {
          Advance();
          if (!ExpectPunct("="))
            return false;
          if (current_.kind != Token::Kind::kIntLiteral)
            return Fail("expected an integer location");
          qualifiers->location = static_cast<int>(current_.number);
          Advance();
        } else if (CheckIdent("std140") || CheckIdent("depth_any") ||
                   CheckIdent("index")) {
          if (CheckIdent("std140"))
            qualifiers->is_std140 = true;
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

bool Parser::ParseConstantInt(int* value) {
  if (current_.kind != Token::Kind::kIntLiteral) {
    Fail("expected a constant integer expression");
    return false;
  }
  *value = static_cast<int>(current_.number);
  Advance();
  if (*value <= 0) {
    Fail("array size must be positive");
    return false;
  }
  return true;
}

bool Parser::ParseArraySuffix(Type* type) {
  while (AcceptPunct("[")) {
    int count = 0;
    if (!ParseConstantInt(&count))
      return false;
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
  if (HasName(global_names_, name, nullptr) ||
      HasName(uniform_names_, name, nullptr) ||
      HasName(attribute_names_, name, nullptr) ||
      HasName(varying_names_, name, nullptr)) {
    return FailAt(line, "redefinition of '" + name + "'");
  }
  if (type.is_void())
    return FailAt(line, "variable '" + name + "' cannot have type void");
  if (initializer && !qualifiers.is_const)
    return FailAt(line, "global initialisers require 'const'");

  Variable variable;
  variable.name = name;
  variable.type = type;

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
    const bool valid_output =
        type.columns() == 1 && type.rows() == 4 &&
        (type.base() == BaseType::kFloat || type.base() == BaseType::kInt ||
         type.base() == BaseType::kUInt);
    if (!valid_output)
      return FailAt(line, "fragment output '" + name +
                             "' must be a vec4, ivec4 or uvec4");
    const bool is_array = type.is_array();
    const int array_length = is_array ? type.array_size() : 1;
    const Type element_type = is_array ? type.array_element() : type;
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
    ast_->uses_frag_color = true;
    return true;
  }

  const bool wants_varying_out =
      qualifiers.is_varying || (qualifiers.is_out && stage_ == Stage::kVertex);
  const bool wants_varying_in = qualifiers.is_in && stage_ == Stage::kFragment;
  if (wants_varying_out || wants_varying_in) {
    if (type.is_sampler())
      return FailAt(line, "varying '" + name + "' cannot be a sampler");
    if (type.is_matrix())
      return FailAt(line, "matrix varyings are not supported");
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
                              int line,
                              StmtPtr* statement) {
  for (const auto& scope : scopes_) {
    for (const auto& entry : scope) {
      if (entry.first == name)
        return FailAt(line, "redefinition of '" + name + "'");
    }
  }
  if (type.is_void())
    return FailAt(line, "variable '" + name + "' cannot have type void");
  if (type.is_sampler())
    return FailAt(line, "samplers must be declared as uniforms");
  Variable variable;
  variable.name = name;
  variable.type = type;
  variable.storage = Storage::kLocal;
  variable.index = slot_count_++;
  scopes_.back().push_back({name, variable.index});
  if (static_cast<int>(slot_types_.size()) <= variable.index)
    slot_types_.resize(variable.index + 1, Type::Float());
  slot_types_[variable.index] = type;
  max_slot_count_ = std::max(max_slot_count_, slot_count_);

  Statement::Declarator declarator;
  declarator.variable = variable;
  declarator.initializer = std::move(initializer);
  (*statement)->declarators.push_back(std::move(declarator));
  return true;
}

bool Parser::RegisterFunction(Function&& function, bool prototype) {
  const std::string name = function.name;
  int existing = -1;
  if (HasName(function_names_, name, &existing)) {
    Function& previous = ast_->functions[existing];
    if (previous.defined || !prototype)
      return FailAt(function.line, "redefinition of function '" + name + "'");
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
    if (!ParseQualifiers(&qualifiers))
      return false;
    if (qualifiers.is_out || qualifiers.is_inout)
      return Fail("'out' and 'inout' parameters are not supported");
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
    if (current_.kind != Token::Kind::kIdentifier)
      return Fail("expected a parameter name");
    const std::string name = current_.text;
    Advance();
    if (!ParseArraySuffix(&type))
      return false;
    Variable variable;
    variable.name = name;
    variable.type = type;
    variable.storage = Storage::kLocal;
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
  (void)qualifiers;
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
  slot_count_ = static_cast<int>(function.parameters.size());
  max_slot_count_ = slot_count_;
  slot_types_.clear();
  slot_types_.reserve(16);
  loop_depth_ = 0;
  scopes_.push_back({});
  for (const Variable& parameter : function.parameters) {
    scopes_.back().push_back({parameter.name, parameter.index});
    slot_types_.push_back(parameter.type);
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
  slot_count_ = 0;
  return RegisterFunction(std::move(function), false);
}

bool Parser::ParseStructDefinition() {
  const int line = current_.line;
  Advance();  // 'struct'
  if (current_.kind != Token::Kind::kIdentifier)
    return Fail("expected a structure name");
  const std::string name = current_.text;
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
    Type member_type;
    if (!ParseTypeSpec(&member_type))
      return false;
    if (member_type.is_void())
      return FailAt(line, "a structure member cannot have type void");
    if (member_type.is_sampler())
      return FailAt(line, "a structure member cannot be a sampler");
    if (current_.kind != Token::Kind::kIdentifier)
      return FailAt(line, "expected a structure member name");
    const std::string member_name = current_.text;
    Advance();
    if (!ParseArraySuffix(&member_type))
      return false;
    for (const StructMember& member : info->members) {
      if (member.name == member_name)
        return FailAt(line, "duplicate structure member '" + member_name + "'");
    }
    const int alignment = Std140Alignment(member_type);
    byte_offset = (byte_offset + alignment - 1) / alignment * alignment;
    StructMember member;
    member.name = member_name;
    member.type = member_type;
    member.component_offset = component_offset;
    member.byte_offset = byte_offset;
    info->members.push_back(member);
    component_offset += member_type.components();
    byte_offset += Std140Size(member_type);
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
  if (qualifiers.is_invariant && !IsKnownTypeName(current_.text)) {
    // "invariant gl_Position;" is accepted and ignored.
    Advance();
    return ExpectPunct(";");
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

bool Parser::ParseUniformBlock(const Qualifiers& qualifiers, int line) {
  (void)qualifiers;
  const std::string block_name = current_.text;
  Advance();
  for (const std::string& existing : block_names_) {
    if (existing == block_name)
      return FailAt(line, "duplicate uniform block '" + block_name + "'");
  }

  if (!ExpectPunct("{"))
    return false;

  UniformBlock block;
  block.name = block_name;
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
    if (member_qualifiers.is_uniform || member_qualifiers.is_attribute ||
        member_qualifiers.is_varying || member_qualifiers.is_in ||
        member_qualifiers.is_out)
      return FailAt(line, "invalid qualifier on a uniform block member");
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

    offset += Std140Size(member_type);
    if (!ExpectPunct(";"))
      return false;
  }

  if (!ExpectPunct("}"))
    return false;

  if (current_.kind == Token::Kind::kIdentifier) {
    block.instance_name = current_.text;
    Advance();
    if (CheckPunct("["))
      return FailAt(line, "uniform block instance arrays are not supported");
    for (const auto& existing : block_instances_) {
      if (existing.first == block.instance_name)
        return FailAt(line, "duplicate uniform block instance '" +
                               block.instance_name + "'");
    }
    block_instances_.push_back({block.instance_name, block_index});
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

StmtPtr Parser::ParseDeclarationStatement() {
  Qualifiers qualifiers;
  if (!ParseQualifiers(&qualifiers))
    return nullptr;
  if (qualifiers.is_uniform || qualifiers.is_attribute ||
      qualifiers.is_varying || qualifiers.is_in || qualifiers.is_out) {
    Fail("uniform/attribute/varying declarations must be global");
    return nullptr;
  }
  if (CheckIdent("struct")) {
    Fail("local struct definitions are not supported");
    return nullptr;
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
    if (!AddLocalVariable(name, declaration_type, std::move(initializer), line,
                          &statement)) {
      return nullptr;
    }
    if (!AcceptPunct(","))
      break;
  }
  if (!ExpectPunct(";"))
    return nullptr;
  return statement;
}

bool Parser::IsDeclarationStart() const {
  if (current_.kind != Token::Kind::kIdentifier)
    return false;
  const std::string& text = current_.text;
  return IsKnownTypeName(text) || IsQualifierKeyword(text) || text == "struct" ||
         text == "precision";
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
  ExprPtr condition = ParseExpression(false);
  if (!condition || !ExpectPunct(")"))
    return nullptr;
  StmtPtr condition_statement(new Statement());
  condition_statement->kind = Statement::Kind::kExpression;
  condition_statement->line = statement->line;
  condition_statement->expression = std::move(condition);
  statement->condition = std::move(condition_statement);
  statement->then_branch = ParseStatement();
  if (!statement->then_branch)
    return nullptr;
  if (AcceptIdent("else")) {
    statement->else_branch = ParseStatement();
    if (!statement->else_branch)
      return nullptr;
  }
  return statement;
}

StmtPtr Parser::ParseFor() {
  StmtPtr statement(new Statement());
  statement->kind = Statement::Kind::kFor;
  statement->line = current_.line;
  Advance();  // for
  if (!ExpectPunct("("))
    return nullptr;

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
    ExprPtr condition = ParseExpression(false);
    if (!condition)
      return nullptr;
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
  statement->then_branch = ParseStatement();
  --loop_depth_;
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
  ExprPtr condition = ParseExpression(false);
  if (!condition || !ExpectPunct(")"))
    return nullptr;
  StmtPtr condition_statement(new Statement());
  condition_statement->kind = Statement::Kind::kExpression;
  condition_statement->line = statement->line;
  condition_statement->expression = std::move(condition);
  statement->condition = std::move(condition_statement);
  ++loop_depth_;
  statement->then_branch = ParseStatement();
  --loop_depth_;
  if (!statement->then_branch)
    return nullptr;
  return statement;
}

StmtPtr Parser::ParseDoWhile() {
  StmtPtr statement(new Statement());
  statement->kind = Statement::Kind::kDoWhile;
  statement->line = current_.line;
  Advance();  // do
  ++loop_depth_;
  statement->then_branch = ParseStatement();
  --loop_depth_;
  if (!statement->then_branch)
    return nullptr;
  if (!AcceptIdent("while")) {
    Fail("expected 'while'");
    return nullptr;
  }
  if (!ExpectPunct("("))
    return nullptr;
  ExprPtr condition = ParseExpression(false);
  if (!condition || !ExpectPunct(")"))
    return nullptr;
  StmtPtr condition_statement(new Statement());
  condition_statement->kind = Statement::Kind::kExpression;
  condition_statement->line = statement->line;
  condition_statement->expression = std::move(condition);
  statement->condition = std::move(condition_statement);
  if (!ExpectPunct(";"))
    return nullptr;
  return statement;
}

StmtPtr Parser::ParseReturn() {
  StmtPtr statement(new Statement());
  statement->kind = Statement::Kind::kReturn;
  statement->line = current_.line;
  Advance();  // return
  if (!CheckPunct(";")) {
    statement->expression = ParseExpression(true);
    if (!statement->expression)
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
  if (CheckIdent("for"))
    return ParseFor();
  if (CheckIdent("while"))
    return ParseWhile();
  if (CheckIdent("do"))
    return ParseDoWhile();
  if (CheckIdent("return"))
    return ParseReturn();
  if (CheckIdent("break")) {
    if (loop_depth_ == 0) {
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
  if (CheckIdent("struct")) {
    Fail("struct is not supported");
    return nullptr;
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
      else if (op == Op::kBitNot)
        unary->type = operand->type.is_uint() ? Type::UInt() : Type::Int();
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

  int function_index = -1;
  if (!HasName(function_names_, name, &function_index)) {
    FailAt(line, "unknown function '" + name + "'");
    return nullptr;
  }
  const Function& callee = ast_->functions[function_index];
  ExprPtr call = MakeExpr(Expr::Kind::kCall, line);
  call->function_index = function_index;
  call->text = name;
  call->type = callee.return_type;
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
  if (call->args.size() != callee.parameters.size()) {
    FailAt(line, "function '" + name + "' expects " +
                     std::to_string(callee.parameters.size()) + " argument(s)");
    return nullptr;
  }
  return call;
}

ExprPtr Parser::ParseVariable(const std::string& name, int line) {
  for (size_t s = scopes_.size(); s > 0; --s) {
    const auto& scope = scopes_[s - 1];
    for (const auto& entry : scope) {
      if (entry.first != name)
        continue;
      ExprPtr expr = MakeExpr(Expr::Kind::kVariable, line);
      expr->storage = Storage::kLocal;
      expr->index = entry.second;
      expr->type = entry.second < static_cast<int>(slot_types_.size())
                       ? slot_types_[entry.second]
                       : Type::Float();
      return expr;
    }
  }
  int index = -1;
  if (HasName(block_instances_, name, &index)) {
    if (!CheckPunct(".")) {
      FailAt(line, "uniform block instance '" + name +
                       "' must be accessed through a member");
      return nullptr;
    }
    ExprPtr expr = MakeExpr(Expr::Kind::kVariable, line);
    expr->storage = Storage::kUniform;
    expr->index = -1;
    expr->ubo_block = index;
    expr->type = Type::Void();
    return expr;
  }
  if (HasName(global_names_, name, &index)) {
    ExprPtr expr = MakeExpr(Expr::Kind::kVariable, line);
    expr->storage = Storage::kGlobal;
    expr->index = index;
    for (const Variable& global : ast_->globals) {
      if (global.index == index) {
        expr->type = global.type;
        break;
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
    ExprPtr expr = MakeExpr(Expr::Kind::kVariable, line);
    expr->storage =
        stage_ == Stage::kVertex ? Storage::kVaryingOut : Storage::kVaryingIn;
    expr->index = index;
    expr->type = ast_->varyings[index].type;
    return expr;
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
    if (current_.kind == Token::Kind::kFloatLiteral) {
      literal->type = Type::Float();
    } else {
      literal->type = current_.is_unsigned ? Type::UInt() : Type::Int();
    }
    Advance();
    return literal;
  }
  if (AcceptPunct("(")) {
    ExprPtr expression = ParseExpression(false);
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
      if (current_.kind != Token::Kind::kIntLiteral ||
          current_.number < 0 || current_.number >= kMaxFragmentOutputs) {
        FailAt(line, "gl_FragData index is out of range");
        return nullptr;
      }
      const int location = static_cast<int>(current_.number);
      Advance();
      if (!ExpectPunct("]"))
        return nullptr;
      BuiltinId id = static_cast<BuiltinId>(
          static_cast<int>(BuiltinId::kFragOutput0) + location);
      int slot = -1;
      for (const FragmentOut& output : ast_->outputs) {
        if (output.location == location) {
          slot = output.slot;
          break;
        }
      }
      if (slot < 0) {
        slot = EnsureBuiltin(id);
        FragmentOut output;
        output.name = "gl_FragData[" + std::to_string(location) + "]";
        output.location = location;
        output.slot = slot;
        ast_->outputs.push_back(output);
      }
      ExprPtr expr = MakeExpr(Expr::Kind::kVariable, line);
      expr->storage = Storage::kGlobal;
      expr->index = slot;
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
    case Op::kBitOr:
    case Op::kShiftLeft:
    case Op::kShiftRight: {
      const int rows = std::max(left.rows(), right.rows());
      const BaseType base = (left.base() == BaseType::kUInt ||
                             right.base() == BaseType::kUInt)
                                ? BaseType::kUInt
                                : BaseType::kInt;
      return Type(base, rows, 1);
    }
    case Op::kMultiply: {
      if (left.is_matrix() && right.is_matrix())
        return Type::Mat(left.columns(), right.rows());
      if (left.is_matrix())
        return Type::Vec(left.rows());
      if (right.is_matrix())
        return Type::Vec(right.columns());
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
    case BuiltinFunc::kTexture2D:
    case BuiltinFunc::kTexture2DProj:
    case BuiltinFunc::kTextureCube:
    case BuiltinFunc::kTexture3D:
    case BuiltinFunc::kTexture:
    case BuiltinFunc::kTextureLod:
      return Type::Vec(4);
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
  return std::move(ast_);
}

}  // namespace glsl
}  // namespace zgl
