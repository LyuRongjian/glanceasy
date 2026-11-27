// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GLANCEASY_SRC_GLSL_GLSL_LEXER_H_
#define GLANCEASY_SRC_GLSL_GLSL_LEXER_H_

#include <stdint.h>

#include <string>

namespace glanceasy {
namespace glsl {

struct Token {
  enum class Kind {
    kEnd,
    kIdentifier,
    kIntLiteral,
    kFloatLiteral,
    kPunct,
  };

  Kind kind = Kind::kEnd;
  std::string text;
  double number = 0.0;
  bool is_unsigned = false;  // Integer literal with a trailing 'u'.
  int line = 1;

  bool IsPunct(const char* p) const {
    return kind == Kind::kPunct && text == p;
  }
  bool IsIdentifier(const char* p) const {
    return kind == Kind::kIdentifier && text == p;
  }
};

// Splits GLSL ES source into tokens.  Preprocessor directives other than
// #version are skipped together with the line they occupy; see
// docs/GLSL_SUBSET.md for the supported language surface.
class Lexer {
 public:
  explicit Lexer(const std::string& source);

  Token Next();
  bool ok() const { return ok_; }
  const std::string& error() const { return error_; }
  int glsl_version() const { return glsl_version_; }

 private:
  void SkipSpaceAndComments();
  bool SkipDirective();
  Token ScanNumber();
  Token ScanIdentifier();
  Token ScanPunct();
  void Fail(const std::string& message);

  // Owned copy of the program text.  Callers frequently pass a temporary, so
  // the lexer cannot rely on the lifetime of its argument.
  std::string source_;
  size_t pos_ = 0;
  int line_ = 1;
  bool ok_ = true;
  std::string error_;
  int glsl_version_ = 100;
};

}  // namespace glsl
}  // namespace glanceasy

#endif  // GLANCEASY_SRC_GLSL_GLSL_LEXER_H_
