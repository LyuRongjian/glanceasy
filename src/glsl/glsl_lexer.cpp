// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/glsl/glsl_lexer.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace zgl {
namespace glsl {

namespace {

bool IsIdentStart(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool IsIdentChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// Longest first so that "<<" wins over "<" and "<<=" over "<<".
const char* const kPuncts[] = {
    "<<=", ">>=", "++", "--", "<=", ">=", "==", "!=", "&&", "||", "^^", "+=",
    "-=",  "*=",  "/=", "%=", "&=", "|=", "^=", "<<", ">>", ";",  ",",  ".",
    "(",   ")",   "[",  "]",  "{",  "}",  "+",  "-",  "*",  "/",  "%",  "<",
    ">",   "=",   "!",  "~",  "&",  "|",  "^",  "?",  ":",
};

}  // namespace

Lexer::Lexer(const std::string& source) : source_(source) {}

void Lexer::Fail(const std::string& message) {
  if (!ok_)
    return;
  ok_ = false;
  error_ = "line " + std::to_string(line_) + ": " + message;
}

void Lexer::SkipSpaceAndComments() {
  while (pos_ < source_.size()) {
    char c = source_[pos_];
    if (c == '\n') {
      ++line_;
      ++pos_;
    } else if (c == ' ' || c == '\t' || c == '\r') {
      ++pos_;
    } else if (c == '/' && pos_ + 1 < source_.size() &&
               source_[pos_ + 1] == '/') {
      while (pos_ < source_.size() && source_[pos_] != '\n')
        ++pos_;
    } else if (c == '/' && pos_ + 1 < source_.size() &&
               source_[pos_ + 1] == '*') {
      pos_ += 2;
      while (pos_ + 1 < source_.size() &&
             !(source_[pos_] == '*' && source_[pos_ + 1] == '/')) {
        if (source_[pos_] == '\n')
          ++line_;
        ++pos_;
      }
      if (pos_ + 1 >= source_.size()) {
        Fail("unterminated block comment");
        return;
      }
      pos_ += 2;
    } else {
      break;
    }
  }
}

// Handles "#..." lines.  #version is parsed for the version number; every
// other directive is ignored up to the end of the line.
bool Lexer::SkipDirective() {
  const size_t start = pos_;
  while (pos_ < source_.size() && source_[pos_] != '\n')
    ++pos_;
  const std::string directive = source_.substr(start, pos_ - start);
  if (directive.compare(0, 8, "#version") == 0) {
    const char* p = directive.c_str() + 8;
    while (*p == ' ' || *p == '\t')
      ++p;
    const int version = std::atoi(p);
    if (version != 100 && version != 300) {
      Fail("unsupported GLSL version '" + std::to_string(version) +
           "' (expected 100 or 300)");
      return false;
    }
    glsl_version_ = version;
  }
  return true;
}

Token Lexer::ScanNumber() {
  Token token;
  token.line = line_;
  const size_t start = pos_;
  if (source_[pos_] == '0' && pos_ + 1 < source_.size() &&
      (source_[pos_ + 1] == 'x' || source_[pos_ + 1] == 'X')) {
    pos_ += 2;
    while (pos_ < source_.size() &&
           std::isxdigit(static_cast<unsigned char>(source_[pos_])))
      ++pos_;
    token.kind = Token::Kind::kIntLiteral;
    token.text = source_.substr(start, pos_ - start);
    token.number =
        static_cast<double>(std::strtol(token.text.c_str(), nullptr, 16));
    if (pos_ < source_.size() &&
        (source_[pos_] == 'u' || source_[pos_] == 'U')) {
      token.is_unsigned = true;
      ++pos_;
    }
    return token;
  }
  bool is_float = false;
  while (pos_ < source_.size() &&
         std::isdigit(static_cast<unsigned char>(source_[pos_])))
    ++pos_;
  if (pos_ < source_.size() && source_[pos_] == '.') {
    is_float = true;
    ++pos_;
    while (pos_ < source_.size() &&
           std::isdigit(static_cast<unsigned char>(source_[pos_])))
      ++pos_;
  }
  if (pos_ < source_.size() && (source_[pos_] == 'e' || source_[pos_] == 'E')) {
    size_t lookahead = pos_ + 1;
    if (lookahead < source_.size() &&
        (source_[lookahead] == '+' || source_[lookahead] == '-'))
      ++lookahead;
    if (lookahead < source_.size() &&
        std::isdigit(static_cast<unsigned char>(source_[lookahead]))) {
      is_float = true;
      pos_ = lookahead;
      while (pos_ < source_.size() &&
             std::isdigit(static_cast<unsigned char>(source_[pos_])))
        ++pos_;
    }
  }
  if (pos_ < source_.size() && (source_[pos_] == 'f' || source_[pos_] == 'F')) {
    is_float = true;
    ++pos_;
  }
  bool is_unsigned = false;
  if (!is_float && pos_ < source_.size() &&
      (source_[pos_] == 'u' || source_[pos_] == 'U')) {
    is_unsigned = true;
    ++pos_;
  }
  token.text = source_.substr(start, pos_ - start);
  token.kind = is_float ? Token::Kind::kFloatLiteral : Token::Kind::kIntLiteral;
  token.is_unsigned = is_unsigned;
  token.number = std::strtod(token.text.c_str(), nullptr);
  return token;
}

Token Lexer::ScanIdentifier() {
  Token token;
  token.line = line_;
  const size_t start = pos_;
  while (pos_ < source_.size() && IsIdentChar(source_[pos_]))
    ++pos_;
  token.kind = Token::Kind::kIdentifier;
  token.text = source_.substr(start, pos_ - start);
  return token;
}

Token Lexer::ScanPunct() {
  Token token;
  token.line = line_;
  token.kind = Token::Kind::kPunct;
  for (const char* punct : kPuncts) {
    const size_t length = std::strlen(punct);
    if (source_.compare(pos_, length, punct) == 0) {
      token.text = punct;
      pos_ += length;
      return token;
    }
  }
  token.text = std::string(1, source_[pos_]);
  Fail("unexpected character '" + token.text + "'");
  ++pos_;
  return token;
}

Token Lexer::Next() {
  SkipSpaceAndComments();
  if (!ok_)
    return Token();
  for (;;) {
    if (pos_ >= source_.size())
      return Token();
    if (source_[pos_] != '#')
      break;
    if (!SkipDirective())
      return Token();
    SkipSpaceAndComments();
  }
  const char c = source_[pos_];
  if (IsIdentStart(c))
    return ScanIdentifier();
  if (std::isdigit(static_cast<unsigned char>(c)))
    return ScanNumber();
  if (c == '.' && pos_ + 1 < source_.size() &&
      std::isdigit(static_cast<unsigned char>(source_[pos_ + 1])))
    return ScanNumber();
  return ScanPunct();
}

}  // namespace glsl
}  // namespace zgl
