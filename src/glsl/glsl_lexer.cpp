// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/glsl/glsl_lexer.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <map>
#include <string>
#include <vector>

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

// Minimal GLSL ES preprocessor.  Object-like and simple function-like
// macros are expanded; conditional directives are handled for the common
// #ifdef/#ifndef/#if 0/#if 1 forms.
struct Macro {
  bool function_like = false;
  std::vector<std::string> params;
  std::string body;
};

std::string Trim(const std::string& value) {
  size_t begin = value.find_first_not_of(" \t");
  if (begin == std::string::npos)
    return std::string();
  size_t end = value.find_last_not_of(" \t");
  return value.substr(begin, end - begin + 1);
}

std::vector<std::string> SplitArguments(const std::string& text) {
  std::vector<std::string> args;
  std::string current;
  int depth = 0;
  for (char c : text) {
    if (c == '(') {
      ++depth;
    } else if (c == ')') {
      --depth;
    }
    if (c == ',' && depth == 0) {
      args.push_back(Trim(current));
      current.clear();
    } else {
      current += c;
    }
  }
  if (!current.empty() || !args.empty())
    args.push_back(Trim(current));
  return args;
}

std::string Substitute(const std::string& body,
                       const std::vector<std::string>& params,
                       const std::vector<std::string>& args) {
  std::string out;
  size_t i = 0;
  while (i < body.size()) {
    if (IsIdentStart(body[i])) {
      const size_t start = i;
      while (i < body.size() && IsIdentChar(body[i]))
        ++i;
      const std::string name = body.substr(start, i - start);
      bool replaced = false;
      for (size_t p = 0; p < params.size(); ++p) {
        if (params[p] == name && p < args.size()) {
          out += args[p];
          replaced = true;
          break;
        }
      }
      if (!replaced)
        out += name;
    } else {
      out += body[i++];
    }
  }
  return out;
}

std::string ExpandIdentifiers(const std::string& line,
                              const std::map<std::string, Macro>& macros,
                              int depth,
                              int line_number,
                              int file_number) {
  if (depth > 16)
    return line;
  std::string out;
  size_t i = 0;
  while (i < line.size()) {
    if (!IsIdentStart(line[i])) {
      out += line[i++];
      continue;
    }
    const size_t start = i;
    while (i < line.size() && IsIdentChar(line[i]))
      ++i;
    const std::string name = line.substr(start, i - start);
    if (name == "__LINE__") {
      out += std::to_string(line_number);
      continue;
    }
    if (name == "__FILE__") {
      out += std::to_string(file_number);
      continue;
    }
    if (name == "__VERSION__") {
      out += "300";
      continue;
    }
    if (name == "GL_ES") {
      out += "1";
      continue;
    }
    const auto it = macros.find(name);
    if (it == macros.end()) {
      out += name;
      continue;
    }
    if (!it->second.function_like) {
      out += ExpandIdentifiers(it->second.body, macros, depth + 1,
                               line_number, file_number);
      continue;
    }
    size_t lookahead = i;
    while (lookahead < line.size() &&
           (line[lookahead] == ' ' || line[lookahead] == '\t'))
      ++lookahead;
    if (lookahead >= line.size() || line[lookahead] != '(') {
      out += name;
      continue;
    }
    size_t open = lookahead;
    int nesting = 0;
    size_t close = std::string::npos;
    for (size_t p = open; p < line.size(); ++p) {
      if (line[p] == '(') {
        ++nesting;
      } else if (line[p] == ')') {
        --nesting;
        if (nesting == 0) {
          close = p;
          break;
        }
      }
    }
    if (close == std::string::npos) {
      out += name;
      continue;
    }
    std::vector<std::string> args =
        SplitArguments(line.substr(open + 1, close - open - 1));
    if (args.size() == 1 && args[0].empty() && it->second.params.empty())
      args.clear();
    if (args.size() != it->second.params.size()) {
      out += line.substr(start, close - start + 1);
      i = close + 1;
      continue;
    }
    out += ExpandIdentifiers(
        Substitute(it->second.body, it->second.params, args), macros,
        depth + 1, line_number, file_number);
    i = close + 1;
  }
  return out;
}

std::string Preprocess(const std::string& source) {
  std::map<std::string, Macro> macros;
  struct Conditional {
    bool parent_active = true;
    bool active = true;
    bool taken = false;
  };
  std::vector<Conditional> conditionals;
  std::string out;
  int logical_line = 1;
  int logical_file = 0;
  size_t pos = 0;
  while (pos <= source.size()) {
    const size_t end = source.find('\n', pos);
    const size_t line_end = end == std::string::npos ? source.size() : end;
    std::string line = source.substr(pos, line_end - pos);
    const bool active =
        conditionals.empty() ? true : conditionals.back().active;
    size_t first = line.find_first_not_of(" \t");
    if (first != std::string::npos && line[first] == '#') {
      size_t p = first + 1;
      while (p < line.size() && (line[p] == ' ' || line[p] == '\t'))
        ++p;
      size_t name_end = p;
      while (name_end < line.size() &&
             (std::isalnum(static_cast<unsigned char>(line[name_end])) ||
              line[name_end] == '_'))
        ++name_end;
      const std::string directive = line.substr(p, name_end - p);
      size_t arg = name_end;
      while (arg < line.size() && (line[arg] == ' ' || line[arg] == '\t'))
        ++arg;
      if (directive == "version") {
        out += line;
      } else if (directive == "line") {
        std::string argument =
            ExpandIdentifiers(line.substr(arg), macros, 0, logical_line,
                              logical_file);
        const char* p = argument.c_str();
        while (*p == ' ' || *p == '\t')
          ++p;
        int value = 0;
        int file = -1;
        std::sscanf(p, "%d %d", &value, &file);
        if (value > 0)
          logical_line = value - 1;
        if (file >= 0)
          logical_file = file;
        out += line;
      } else if (directive == "define" && active) {
        size_t n = arg;
        while (n < line.size() &&
               (std::isalnum(static_cast<unsigned char>(line[n])) ||
                line[n] == '_'))
          ++n;
        const std::string name = line.substr(arg, n - arg);
        Macro macro;
        if (!name.empty()) {
          if (n < line.size() && line[n] == '(') {
            const size_t close = line.find(')', n + 1);
            if (close != std::string::npos) {
              macro.function_like = true;
              std::string params = line.substr(n + 1, close - n - 1);
              for (const std::string& value : SplitArguments(params)) {
                if (!value.empty())
                  macro.params.push_back(value);
              }
              macro.body = Trim(line.substr(close + 1));
            }
          } else {
            macro.body = Trim(line.substr(n));
          }
          macros[name] = macro;
        }
      } else if (directive == "undef" && active) {
        size_t n = arg;
        while (n < line.size() &&
               (std::isalnum(static_cast<unsigned char>(line[n])) ||
                line[n] == '_'))
          ++n;
        macros.erase(line.substr(arg, n - arg));
      } else if (directive == "ifdef" || directive == "ifndef") {
        Conditional c;
        c.parent_active = active;
        size_t n = arg;
        while (n < line.size() &&
               (std::isalnum(static_cast<unsigned char>(line[n])) ||
                line[n] == '_'))
          ++n;
        const bool defined = macros.count(line.substr(arg, n - arg)) != 0;
        c.active = active && (directive == "ifdef" ? defined : !defined);
        c.taken = c.active;
        conditionals.push_back(c);
      } else if (directive == "if") {
        Conditional c;
        c.parent_active = active;
        std::string expr = Trim(line.substr(arg));
        c.active = active && expr == "1";
        c.taken = c.active;
        conditionals.push_back(c);
      } else if (directive == "else") {
        if (!conditionals.empty()) {
          Conditional& c = conditionals.back();
          c.active = c.parent_active && !c.taken;
          c.taken = c.taken || c.active;
        }
      } else if (directive == "endif") {
        if (!conditionals.empty())
          conditionals.pop_back();
      } else if (directive == "elif") {
        if (!conditionals.empty())
          conditionals.back().active = false;
      }
      out += '\n';
    } else if (active) {
      out += ExpandIdentifiers(line, macros, 0, logical_line, logical_file);
      out += '\n';
    } else {
      out += '\n';
    }
    ++logical_line;
    if (end == std::string::npos)
      break;
    pos = end + 1;
  }
  return out;
}

}  // namespace

Lexer::Lexer(const std::string& source) : source_(Preprocess(source)) {}

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
