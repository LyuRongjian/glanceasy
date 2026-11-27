// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GLANCEASY_SRC_GLSL_GLSL_PARSER_H_
#define GLANCEASY_SRC_GLSL_GLSL_PARSER_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/glsl/glsl_ast.h"
#include "src/glsl/glsl_lexer.h"

namespace glanceasy {
namespace glsl {

// Recursive descent parser for the GLSL ES 1.00 / 3.00 subset documented in
// docs/GLSL_SUBSET.md.  Identifiers are resolved to slots while the AST is
// built, so the interpreter never performs name lookups.
class Parser {
 public:
  Parser(Stage stage, const std::string& source);
  ~Parser();

  // Returns the AST, or nullptr with |error()| describing the first failure.
  std::unique_ptr<ShaderAst> Parse();
  const std::string& error() const { return error_; }

 private:
  struct Qualifiers {
    bool is_const = false;
    bool is_uniform = false;
    bool is_attribute = false;
    bool is_varying = false;
    bool is_in = false;
    bool is_out = false;
    bool is_inout = false;
    bool is_invariant = false;
    bool is_std140 = false;
    bool is_packed = false;
    bool has_layout = false;
    bool flat = false;
    int matrix_layout = -1;  // -1 = unspecified, 0 = column, 1 = row.
    int location = -1;
  };

  // Token helpers.
  void Advance();
  const Token& Peek();
  bool CheckPunct(const char* text) const;
  bool CheckIdent(const char* text) const;
  bool AcceptPunct(const char* text);
  bool AcceptIdent(const char* text);
  bool ExpectPunct(const char* text);
  bool Fail(const std::string& message);
  bool FailAt(int line, const std::string& message);

  // Declarations.
  bool ParseQualifiers(Qualifiers* qualifiers, bool parameter = false);
  bool ParseTypeSpec(Type* type);
  bool ParseArraySuffix(Type* type);
  bool ParseStructDefinition();
  bool LookupType(const std::string& name, Type* type) const;
  bool IsKnownTypeName(const std::string& name) const;
  bool ParseConstantInt(int* value);
  bool ParseConstAtom(int* value);
  bool EvalConstIntExpr(const ExprPtr& expr, int* value);
  bool EvalConstNumber(const ExprPtr& expr, double* value);
  bool IsConstantExpr(const ExprPtr& expr) const;
  bool ParseGlobalDeclaration();
  bool ParseUniformBlock(const Qualifiers& qualifiers, int line);
  bool IsUniformBlockName(const std::string& name) const;
  bool IsBlockInstanceName(const std::string& name) const;
  bool ParseFunction(const std::string& name,
                     const Type& return_type,
                     const Qualifiers& qualifiers,
                     int line);
  bool ParseParameters(Function* function);
  StmtPtr ParseDeclarationStatement(bool in_condition = false);
  bool AddGlobalVariable(const std::string& name,
                         const Type& type,
                         const Qualifiers& qualifiers,
                         ExprPtr initializer,
                         int line);
  bool AddLocalVariable(const std::string& name,
                        const Type& type,
                        ExprPtr initializer,
                        bool is_const,
                        int line,
                        StmtPtr* statement);
  bool RegisterFunction(Function&& function, bool prototype);

  // Statements.
  StmtPtr ParseStatement();
  StmtPtr ParseBlock();
  StmtPtr ParseIf();
  StmtPtr ParseFor();
  StmtPtr ParseSwitch();
  StmtPtr ParseWhile();
  StmtPtr ParseDoWhile();
  StmtPtr ParseReturn();
  StmtPtr ParseJump(const char* keyword, Statement::Kind kind);
  StmtPtr ParseExpressionStatement();
  bool IsDeclarationStart() const;

  // Expressions.
  ExprPtr ParseExpression(bool allow_comma);
  ExprPtr ParseAssignment();
  ExprPtr ParseConditional();
  ExprPtr ParseBinary(int min_precedence);
  ExprPtr ParseUnary();
  ExprPtr ParsePostfix();
  ExprPtr ParsePrimary();
  ExprPtr ParseCall(const std::string& name, int line);
  ExprPtr ParseConstructor(const Type& type, int line);
  ExprPtr ParseVariable(const std::string& name, int line);

  // Name resolution and types.
  int EnsureBuiltin(BuiltinId id);
  Type BuiltinResultType(BuiltinFunc function,
                         const std::vector<ExprPtr>& args,
                         int line);
  Type BinaryResultType(Op op, const Type& left, const Type& right);

  Stage stage_;
  Lexer lexer_;
  Token current_;
  Token lookahead_;
  bool has_lookahead_ = false;
  std::unique_ptr<ShaderAst> ast_;
  std::string error_;

  // Local scopes; the innermost scope is the current block.
  std::vector<std::vector<std::pair<std::string, int>>> scopes_;
  std::vector<Type> slot_types_;  // Declared type of each local slot.
  int slot_count_ = 0;
  int max_slot_count_ = 0;
  int loop_depth_ = 0;
  int switch_depth_ = 0;
  bool in_function_ = false;
  std::string current_function_;
  Type current_return_type_;
  std::vector<std::pair<std::string, std::string>> calls_;
  int current_parameter_count_ = 0;

  // Name maps for the translation unit.
  std::vector<std::pair<std::string, int>> global_names_;
  std::vector<std::pair<std::string, int>> uniform_names_;
  std::vector<std::pair<std::string, int>> attribute_names_;
  std::vector<std::pair<std::string, int>> varying_names_;
  std::vector<std::pair<std::string, int>> function_names_;
  std::vector<std::pair<std::string, int>> block_instances_;
  std::vector<std::string> block_names_;
  std::vector<std::pair<std::string, Type>> struct_types_;
  std::vector<std::pair<std::string, int>> const_ints_;
  std::vector<std::pair<std::string, const Expr*>> const_exprs_;
  std::vector<std::pair<std::string, int>> array_sizes_;
  std::vector<bool> slot_is_const_;
  int next_attribute_location_ = 0;
};

}  // namespace glsl
}  // namespace glanceasy

#endif  // GLANCEASY_SRC_GLSL_GLSL_PARSER_H_
