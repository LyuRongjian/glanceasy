// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <vector>

#include "tests/zgl_test.h"

namespace zgltest {

namespace {

struct TestCase {
  const char* name = nullptr;
  TestFunction function = nullptr;
};

std::vector<TestCase>& Registry() {
  static std::vector<TestCase> registry;
  return registry;
}

int& FailureCount() {
  static int failures = 0;
  return failures;
}

}  // namespace

void Register(const char* name, TestFunction function) {
  Registry().push_back(TestCase{name, function});
}

bool ExpectTrue(bool condition,
                const char* expression,
                const char* file,
                int line) {
  if (condition)
    return true;
  FailureCount()++;
  fprintf(stderr, "%s:%d: expectation failed: %s\n", file, line, expression);
  return false;
}

bool ExpectEqual(long long actual,
                 long long expected,
                 const char* expression,
                 const char* file,
                 int line) {
  if (actual == expected)
    return true;
  FailureCount()++;
  fprintf(stderr, "%s:%d: expectation failed: %s (got %lld, want %lld)\n", file,
          line, expression, actual, expected);
  return false;
}

bool ExpectNear(double actual,
                double expected,
                double tolerance,
                const char* expression,
                const char* file,
                int line) {
  const double difference =
      actual > expected ? actual - expected : expected - actual;
  if (difference <= tolerance)
    return true;
  FailureCount()++;
  fprintf(stderr, "%s:%d: expectation failed: %s (got %f, want %f)\n", file,
          line, expression, actual, expected);
  return false;
}

int RunAllTests() {
  for (const TestCase& test : Registry()) {
    const int before = FailureCount();
    fprintf(stderr, "RUN %s\n", test.name);
    test.function();
    if (FailureCount() == before)
      printf("PASS %s\n", test.name);
    else
      printf("FAIL %s\n", test.name);
  }
  printf("%zu tests, %d failed checks\n", Registry().size(), FailureCount());
  return FailureCount();
}

}  // namespace zgltest

int main() {
  return zgltest::RunAllTests();
}
