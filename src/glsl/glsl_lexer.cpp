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

int NewlinesBetween(const std::string& text, size_t begin, size_t end) {
  int count = 0;
  for (size_t i = begin; i < end && i < text.size(); ++i)
    if (text[i] == '\n')
      ++count;
  return count;
}

std::vector<std::pair<size_t, size_t>> SplitArgumentRanges(
    const std::string& text) {
  std::vector<std::pair<size_t, size_t>> ranges;
  int depth = 0;
  size_t begin = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '(') {
      ++depth;
    } else if (text[i] == ')') {
      --depth;
    } else if (text[i] == ',' && depth == 0) {
      size_t b = begin;
      size_t e = i;
      while (b < e && (text[b] == ' ' || text[b] == '\t' || text[b] == '\n'))
        ++b;
      while (e > b && (text[e - 1] == ' ' || text[e - 1] == '\t' ||
                       text[e - 1] == '\n'))
        --e;
      ranges.push_back({b, e});
      begin = i + 1;
    }
  }
  size_t b = begin;
  size_t e = text.size();
  while (b < e && (text[b] == ' ' || text[b] == '\t' || text[b] == '\n'))
    ++b;
  while (e > b && (text[e - 1] == ' ' || text[e - 1] == '\t' ||
                   text[e - 1] == '\n'))
    --e;
  ranges.push_back({b, e});
  return ranges;
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
  int current_line = line_number;
  while (i < line.size()) {
    if (!IsIdentStart(line[i])) {
      if (line[i] == '\n')
        ++current_line;
      out += line[i++];
      continue;
    }
    const size_t start = i;
    while (i < line.size() && IsIdentChar(line[i]))
      ++i;
    const std::string name = line.substr(start, i - start);
    if (start > 0 && (std::isdigit(static_cast<unsigned char>(line[start - 1])) ||
                      line[start - 1] == '.')) {
      out += name;
      continue;
    }
    if (name == "__LINE__") {
      out += std::to_string(current_line);
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
           (line[lookahead] == ' ' || line[lookahead] == '\t' ||
            line[lookahead] == '\n' || line[lookahead] == '\r'))
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
    const std::string argument_text = line.substr(open + 1, close - open - 1);
    const auto ranges = SplitArgumentRanges(argument_text);
    std::vector<std::string> args;
    args.reserve(ranges.size());
    for (const auto& range : ranges) {
      // Macro arguments are macro-expanded before substitution.  Each
      // argument expands at the physical line where it appears.
      const int argument_line =
          line_number + NewlinesBetween(line, 0, open + 1 + range.first);
      const std::string raw = argument_text.substr(range.first,
                                                   range.second - range.first);
      args.push_back(ExpandIdentifiers(raw, macros, depth + 1,
                                       argument_line, file_number));
    }
    if (args.size() == 1 && args[0].empty() && it->second.params.empty())
      args.clear();
    if (args.size() != it->second.params.size()) {
      out += line.substr(start, close - start + 1);
      i = close + 1;
      continue;
    }
    // The replacement list expands at the invocation's closing line.
    const int invocation_line =
        line_number + NewlinesBetween(line, 0, close);
    out += ExpandIdentifiers(
        Substitute(it->second.body, it->second.params, args), macros,
        depth + 1, invocation_line, file_number);
    i = close + 1;
  }
  return out;
}

std::string NormalizeMacroText(const std::string& text) {
  std::string out;
  bool pending_space = false;
  for (char c : text) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      pending_space = !out.empty();
    } else {
      if (pending_space)
        out += ' ';
      pending_space = false;
      out += c;
    }
  }
  return out;
}

std::string SpliceLines(const std::string& source) {
  std::string out;
  for (size_t i = 0; i < source.size(); ++i) {
    if (source[i] == '\\') {
      if (i + 1 < source.size() && source[i + 1] == '\n') {
        ++i;
        continue;
      }
      if (i + 2 < source.size() && source[i + 1] == '\r' &&
          source[i + 2] == '\n') {
        i += 2;
        continue;
      }
    }
    out += source[i];
  }
  return out;
}

// Replace block comments with a single space. Newlines inside the comment are
// deferred until the end of the logical line so that a multi-line comment does
// not terminate a preprocessing directive, while line numbering is preserved.
std::string StripBlockComments(const std::string& source) {
  std::string out;
  size_t pending_newlines = 0;
  for (size_t i = 0; i < source.size(); ++i) {
    if (i + 1 < source.size() && source[i] == '/' && source[i + 1] == '*') {
      i += 2;
      while (i + 1 < source.size() &&
             !(source[i] == '*' && source[i + 1] == '/')) {
        if (source[i] == '\n')
          ++pending_newlines;
        ++i;
      }
      if (i + 1 < source.size())
        i += 1;
      out += ' ';
    } else if (source[i] == '\n') {
      out += '\n';
      while (pending_newlines > 0) {
        out += '\n';
        --pending_newlines;
      }
    } else {
      out += source[i];
    }
  }
  while (pending_newlines > 0) {
    out += '\n';
    --pending_newlines;
  }
  return out;
}

bool IsPreprocessorDefined(const std::map<std::string, Macro>& macros,
                           const std::string& name) {
  return macros.count(name) != 0 || name == "__LINE__" || name == "__FILE__" ||
         name == "__VERSION__" || name == "GL_ES";
}

// Replace defined(NAME) and defined NAME before macro expansion so that the
// operand is not expanded as an ordinary identifier.
std::string ResolveDefined(const std::string& expr,
                           const std::map<std::string, Macro>& macros) {
  std::string out;
  size_t i = 0;
  while (i < expr.size()) {
    if (IsIdentStart(expr[i])) {
      const size_t start = i;
      while (i < expr.size() && IsIdentChar(expr[i]))
        ++i;
      const std::string name = expr.substr(start, i - start);
      if (name != "defined") {
        out += name;
        continue;
      }
      size_t p = i;
      while (p < expr.size() && (expr[p] == ' ' || expr[p] == '\t'))
        ++p;
      bool parenthesised = false;
      if (p < expr.size() && expr[p] == '(') {
        parenthesised = true;
        ++p;
        while (p < expr.size() && (expr[p] == ' ' || expr[p] == '\t'))
          ++p;
      }
      const size_t name_start = p;
      while (p < expr.size() && IsIdentChar(expr[p]))
        ++p;
      const std::string operand = expr.substr(name_start, p - name_start);
      bool closed = false;
      if (parenthesised) {
        while (p < expr.size() && (expr[p] == ' ' || expr[p] == '\t'))
          ++p;
        if (p < expr.size() && expr[p] == ')') {
          ++p;
          closed = true;
        }
      }
      if (operand.empty() || (parenthesised && !closed)) {
        out += "__zgl_invalid_defined_operand__";
      } else {
        out += IsPreprocessorDefined(macros, operand) ? "1" : "0";
      }
      i = p;
    } else {
      out += expr[i++];
    }
  }
  return out;
}

class ExpressionParser {
 public:
  explicit ExpressionParser(const std::string& text) : text_(text) {}

  bool Parse(long long* value) {
    *value = ParseConditional();
    SkipSpace();
    return ok_ && pos_ == text_.size();
  }

 private:
  void SkipSpace() {
    while (pos_ < text_.size() &&
           (text_[pos_] == ' ' || text_[pos_] == '\t'))
      ++pos_;
  }

  bool Match(const char* token) {
    SkipSpace();
    const size_t length = std::strlen(token);
    if (text_.compare(pos_, length, token) != 0)
      return false;
    if (length == 1) {
      const char next = pos_ + 1 < text_.size() ? text_[pos_ + 1] : '\0';
      const char c = token[0];
      if ((c == '&' && next == '&') || (c == '|' && next == '|') ||
          (c == '<' && (next == '<' || next == '=')) ||
          (c == '>' && (next == '>' || next == '=')) ||
          ((c == '+' || c == '-') && next == c) ||
          ((c == '+' || c == '-' || c == '*' || c == '/' || c == '%' ||
            c == '!' || c == '^' || c == '=') &&
           next == '='))
        return false;
    }
    pos_ += length;
    return true;
  }

  long long ParseConditional() {
    long long condition = ParseLogicalOr();
    if (Match("?")) {
      long long true_value = ParseConditional();
      if (!Match(":")) {
        ok_ = false;
        return 0;
      }
      long long false_value = ParseConditional();
      return condition ? true_value : false_value;
    }
    return condition;
  }

  long long ParseLogicalOr() {
    long long value = ParseLogicalAnd();
    while (true) {
      if (!Match("||"))
        return value;
      const bool lhs_true = value != 0;
      const bool saved_ok = ok_;
      const long long rhs = ParseLogicalAnd();
      if (lhs_true)
        ok_ = saved_ok;
      value = (lhs_true || rhs != 0) ? 1 : 0;
    }
  }

  long long ParseLogicalAnd() {
    long long value = ParseBitOr();
    while (true) {
      if (!Match("&&"))
        return value;
      const bool lhs_false = value == 0;
      const bool saved_ok = ok_;
      const long long rhs = ParseBitOr();
      if (lhs_false)
        ok_ = saved_ok;
      value = (!lhs_false && rhs != 0) ? 1 : 0;
    }
  }

  long long ParseBitOr() {
    long long value = ParseBitXor();
    while (true) {
      if (!Match("|"))
        return value;
      value |= ParseBitXor();
    }
  }

  long long ParseBitXor() {
    long long value = ParseBitAnd();
    while (true) {
      if (!Match("^"))
        return value;
      value ^= ParseBitAnd();
    }
  }

  long long ParseBitAnd() {
    long long value = ParseEquality();
    while (true) {
      if (!Match("&"))
        return value;
      value &= ParseEquality();
    }
  }

  long long ParseEquality() {
    long long value = ParseRelational();
    while (true) {
      if (Match("==")) {
        value = value == ParseRelational();
      } else if (Match("!=")) {
        value = value != ParseRelational();
      } else {
        return value;
      }
    }
  }

  long long ParseRelational() {
    long long value = ParseShift();
    while (true) {
      if (Match("<=")) {
        value = value <= ParseShift();
      } else if (Match(">=")) {
        value = value >= ParseShift();
      } else if (Match("<")) {
        value = value < ParseShift();
      } else if (Match(">")) {
        value = value > ParseShift();
      } else {
        return value;
      }
    }
  }

  long long ParseShift() {
    long long value = ParseAdditive();
    while (true) {
      if (Match("<<")) {
        value = value << ParseAdditive();
      } else if (Match(">>")) {
        value = value >> ParseAdditive();
      } else {
        return value;
      }
    }
  }

  long long ParseAdditive() {
    long long value = ParseMultiplicative();
    while (true) {
      if (Match("+")) {
        value += ParseMultiplicative();
      } else if (Match("-")) {
        value -= ParseMultiplicative();
      } else {
        return value;
      }
    }
  }

  long long ParseMultiplicative() {
    long long value = ParseUnary();
    while (true) {
      if (Match("*")) {
        value *= ParseUnary();
      } else if (Match("/")) {
        const long long rhs = ParseUnary();
        if (rhs == 0) {
          ok_ = false;
          return 0;
        }
        value /= rhs;
      } else if (Match("%")) {
        const long long rhs = ParseUnary();
        if (rhs == 0) {
          ok_ = false;
          return 0;
        }
        value %= rhs;
      } else {
        return value;
      }
    }
  }

  long long ParseUnary() {
    if (Match("+"))
      return ParseUnary();
    if (Match("-"))
      return -ParseUnary();
    if (Match("!"))
      return ParseUnary() == 0;
    if (Match("~"))
      return ~ParseUnary();
    return ParsePrimary();
  }

  long long ParsePrimary() {
    SkipSpace();
    if (pos_ >= text_.size()) {
      ok_ = false;
      return 0;
    }
    if (text_[pos_] == '(') {
      ++pos_;
      const long long value = ParseConditional();
      if (!Match(")")) {
        ok_ = false;
        return 0;
      }
      return value;
    }
    if (IsIdentStart(text_[pos_])) {
      const size_t start = pos_;
      while (pos_ < text_.size() && IsIdentChar(text_[pos_]))
        ++pos_;
      // Undefined identifiers are an error in GLSL ES preprocessor
      // expressions.
      (void)start;
      ok_ = false;
      return 0;
    }
    if (std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
      const size_t start = pos_;
      if (text_.compare(pos_, 2, "0x") == 0 ||
          text_.compare(pos_, 2, "0X") == 0) {
        pos_ += 2;
        while (pos_ < text_.size() &&
               std::isxdigit(static_cast<unsigned char>(text_[pos_])))
          ++pos_;
      } else {
        while (pos_ < text_.size() &&
               std::isdigit(static_cast<unsigned char>(text_[pos_])))
          ++pos_;
      }
      const std::string number = text_.substr(start, pos_ - start);
      char* end = nullptr;
      const long long value = std::strtoll(number.c_str(), &end, 0);
      if (end == number.c_str()) {
        ok_ = false;
        return 0;
      }
      // Skip an integer suffix such as u, U, l or L.
      while (pos_ < text_.size() &&
             (text_[pos_] == 'u' || text_[pos_] == 'U' || text_[pos_] == 'l' ||
              text_[pos_] == 'L'))
        ++pos_;
      return value;
    }
    ok_ = false;
    return 0;
  }

  const std::string& text_;
  size_t pos_ = 0;
  bool ok_ = true;
};

bool EvaluateExpression(const std::string& expression,
                        const std::map<std::string, Macro>& macros,
                        long long* value) {
  const std::string resolved = ExpandIdentifiers(
      ResolveDefined(expression, macros), macros, 0, 1, 0);
  ExpressionParser parser(resolved);
  return parser.Parse(value);
}

std::string Preprocess(const std::string& source) {
  const std::string input = StripBlockComments(SpliceLines(source));
  std::map<std::string, Macro> macros;
  struct Conditional {
    bool parent_active = true;
    bool active = true;
    bool taken = false;
    bool seen_else = false;
  };
  std::vector<Conditional> conditionals;
  std::string out;
  int logical_line = 1;
  int logical_file = 0;
  bool version_seen = false;
  bool code_seen = false;
  bool saw_statement = false;
  std::string pending_code;
  int pending_line = 1;
  auto flush_code = [&]() {
    if (!pending_code.empty()) {
      out += ExpandIdentifiers(pending_code, macros, 0, pending_line,
                               logical_file);
      pending_code.clear();
    }
  };
  size_t pos = 0;
  while (pos <= input.size()) {
    const size_t end = input.find('\n', pos);
    const size_t line_end = end == std::string::npos ? input.size() : end;
    std::string line = input.substr(pos, line_end - pos);
    const bool active =
        conditionals.empty() ? true : conditionals.back().active;
    size_t first = line.find_first_not_of(" \t");
    if (first != std::string::npos && line[first] == '#') {
      flush_code();
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
        const std::string version = Trim(line.substr(arg));
        if (saw_statement || (version != "300 es" && version != "100")) {
          out += "__zgl_invalid_version_directive__\n";
        } else {
          version_seen = true;
          out += line;
        }
      } else if (directive == "extension") {
        const std::string argument = Trim(line.substr(arg));
        bool valid = !code_seen;
        size_t q = 0;
        while (q < argument.size() &&
               (std::isalnum(static_cast<unsigned char>(argument[q])) ||
                argument[q] == '_'))
          ++q;
        const std::string name = argument.substr(0, q);
        if (name.empty() || !IsIdentStart(name[0]))
          valid = false;
        while (q < argument.size() &&
               (argument[q] == ' ' || argument[q] == '\t'))
          ++q;
        if (q >= argument.size() || argument[q] != ':') {
          valid = false;
        } else {
          ++q;
          while (q < argument.size() &&
                 (argument[q] == ' ' || argument[q] == '\t'))
            ++q;
          size_t behavior_end = q;
          while (behavior_end < argument.size() &&
                 (std::isalnum(static_cast<unsigned char>(argument[behavior_end])) ||
                  argument[behavior_end] == '_'))
            ++behavior_end;
          const std::string behavior = argument.substr(q, behavior_end - q);
          if (behavior != "require" && behavior != "enable" &&
              behavior != "warn" && behavior != "disable")
            valid = false;
          if (name == "all" && (behavior == "require" || behavior == "enable"))
            valid = false;
          q = behavior_end;
          while (q < argument.size() &&
                 (argument[q] == ' ' || argument[q] == '\t'))
            ++q;
          if (q != argument.size())
            valid = false;
        }
        if (argument.find("/*") != std::string::npos &&
            argument.find("*/") == std::string::npos)
          valid = false;
        if (!valid)
          out += "__zgl_invalid_extension_directive__\n";
        else
          out += line;
        saw_statement = true;
      } else if (directive == "error") {
        out += "__zgl_error_directive__\n";
        saw_statement = true;
      } else if (directive == "pragma") {
        out += line;
        saw_statement = true;
      } else if (directive == "line") {
        std::string argument =
            ExpandIdentifiers(line.substr(arg), macros, 0, logical_line,
                              logical_file);
        const char* p = argument.c_str();
        while (*p == ' ' || *p == '\t')
          ++p;
        int value = 0;
        int file = -1;
        int consumed = 0;
        const int fields = std::sscanf(p, "%d %d %n", &value, &file, &consumed);
        bool valid_line =
            fields >= 1 && value > 0 && !(fields >= 2 && file < 0);
        if (fields == 1)
          consumed = 0;
        if (valid_line && fields >= 1) {
          const char* tail = p;
          if (fields == 1) {
            // Locate the end of the single parsed integer.
            while (*tail == ' ' || *tail == '\t')
              ++tail;
            if (*tail == '-')
              ++tail;
            while (*tail >= '0' && *tail <= '9')
              ++tail;
          } else {
            tail = p + consumed;
          }
          while (*tail == ' ' || *tail == '\t')
            ++tail;
          if (*tail != '\0')
            valid_line = false;
        }
        if (!valid_line) {
          out += "__zgl_invalid_line_directive__\n";
        } else {
          logical_line = value - 1;
          if (file >= 0)
            logical_file = file;
          out += line;
        }
      } else if (directive == "define" && active) {
        size_t n = arg;
        while (n < line.size() &&
               (std::isalnum(static_cast<unsigned char>(line[n])) ||
                line[n] == '_'))
          ++n;
        const std::string name = line.substr(arg, n - arg);
        bool valid = !name.empty() && IsIdentStart(name[0]) &&
                     name.compare(0, 3, "GL_") != 0;
        Macro macro;
        if (valid && n < line.size() && line[n] == '(') {
          const size_t close = line.find(')', n + 1);
          if (close == std::string::npos) {
            valid = false;
          } else {
            macro.function_like = true;
            std::string params = line.substr(n + 1, close - n - 1);
            for (const std::string& value : SplitArguments(params)) {
              if (value.empty() || !IsIdentStart(value[0])) {
                valid = false;
                break;
              }
              for (char c : value) {
                if (!IsIdentChar(c))
                  valid = false;
              }
              for (const std::string& existing : macro.params) {
                if (existing == value)
                  valid = false;
              }
              macro.params.push_back(value);
            }
            macro.body = NormalizeMacroText(Trim(line.substr(close + 1)));
          }
        } else if (valid) {
          macro.body = NormalizeMacroText(Trim(line.substr(n)));
        }
        if (valid) {
          const auto existing = macros.find(name);
          if (existing != macros.end() &&
              (existing->second.function_like != macro.function_like ||
               existing->second.params != macro.params ||
               existing->second.body != macro.body)) {
            out += "__zgl_invalid_macro_redefinition__\n";
          } else {
            macros[name] = macro;
          }
        } else {
          out += "__zgl_invalid_define_directive__\n";
        }
        saw_statement = true;
      } else if (directive == "undef" && active) {
        const std::string argument = Trim(line.substr(arg));
        bool valid = !argument.empty() && IsIdentStart(argument[0]);
        for (char c : argument)
          if (!IsIdentChar(c))
            valid = false;
        if (argument == "__LINE__" || argument == "__FILE__" ||
            argument == "__VERSION__" || argument == "GL_ES" ||
            argument.compare(0, 3, "GL_") == 0)
          valid = false;
        if (valid)
          macros.erase(argument);
        else
          out += "__zgl_invalid_undef_directive__\n";
        saw_statement = true;
      } else if (directive == "ifdef" || directive == "ifndef") {
        Conditional c;
        c.parent_active = active;
        const std::string argument = Trim(line.substr(arg));
        bool valid = !argument.empty() && IsIdentStart(argument[0]);
        for (char ch : argument)
          if (!IsIdentChar(ch))
            valid = false;
        if (!valid)
          out += "__zgl_invalid_conditional__\n";
        const bool defined = macros.count(argument) != 0;
        c.active = active && (directive == "ifdef" ? defined : !defined);
        c.taken = c.active;
        conditionals.push_back(c);
        saw_statement = true;
      } else if (directive == "if") {
        Conditional c;
        c.parent_active = active;
        long long value = 0;
        if (!EvaluateExpression(line.substr(arg), macros, &value))
          out += "__zgl_invalid_conditional__\n";
        c.active = active && value != 0;
        c.taken = c.active;
        conditionals.push_back(c);
      } else if (directive == "else") {
        if (!conditionals.empty()) {
          Conditional& c = conditionals.back();
          if (c.seen_else || !Trim(line.substr(arg)).empty())
            out += "__zgl_invalid_conditional__\n";
          c.seen_else = true;
          c.active = c.parent_active && !c.taken;
          c.taken = c.taken || c.active;
        } else {
          out += "__zgl_invalid_conditional__\n";
        }
      } else if (directive == "endif") {
        if (conditionals.empty() || !Trim(line.substr(arg)).empty()) {
          out += "__zgl_invalid_conditional__\n";
        } else {
          conditionals.pop_back();
        }
      } else if (directive == "elif") {
        if (conditionals.empty()) {
          out += "__zgl_invalid_conditional__\n";
        } else {
          Conditional& c = conditionals.back();
          long long value = 0;
          if (!EvaluateExpression(line.substr(arg), macros, &value))
            out += "__zgl_invalid_conditional__\n";
          if (c.seen_else)
            out += "__zgl_invalid_conditional__\n";
          c.active = c.parent_active && !c.taken && value != 0;
          c.taken = c.taken || c.active;
        }
      }
      if (directive != "version" && directive != "line" &&
          directive != "define" && directive != "undef" &&
          directive != "ifdef" && directive != "ifndef" &&
          directive != "if" && directive != "else" && directive != "endif" &&
          directive != "elif" && directive != "extension" &&
          directive != "error" && directive != "pragma" && !directive.empty()) {
        out += "__zgl_unknown_directive__\n";
      }
      out += '\n';
    } else if (active) {
      if (line.find_first_not_of(" \t") != std::string::npos) {
        code_seen = true;
        saw_statement = true;
      }
      if (pending_code.empty())
        pending_line = logical_line;
      pending_code += line;
      pending_code += '\n';
    } else {
      flush_code();
      out += '\n';
    }
    ++logical_line;
    if (end == std::string::npos)
      break;
    pos = end + 1;
  }
  flush_code();
  if (!conditionals.empty())
    out += "__zgl_unterminated_conditional__\n";
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
  const size_t digits_start = pos_;
  while (pos_ < source_.size() &&
         std::isdigit(static_cast<unsigned char>(source_[pos_])))
    ++pos_;
  const bool octal = pos_ - digits_start > 1 && source_[digits_start] == '0';
  if (octal) {
    for (size_t i = digits_start + 1; i < pos_; ++i) {
      if (source_[i] == '8' || source_[i] == '9') {
        Fail("invalid digit in octal literal");
        return token;
      }
    }
  }
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
  if (is_float && pos_ < source_.size() &&
      (source_[pos_] == 'f' || source_[pos_] == 'F')) {
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
  if (!is_float && octal) {
    token.number = static_cast<double>(
        std::strtol(token.text.c_str(), nullptr, 8));
  } else {
    token.number = std::strtod(token.text.c_str(), nullptr);
  }
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
