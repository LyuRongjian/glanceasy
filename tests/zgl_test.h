// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZGL_TESTS_ZGL_TEST_H_
#define ZGL_TESTS_ZGL_TEST_H_

namespace zgltest {

using TestFunction = void (*)();

void Register(const char* name, TestFunction function);

bool ExpectTrue(bool condition,
                const char* expression,
                const char* file,
                int line);
bool ExpectEqual(long long actual,
                 long long expected,
                 const char* expression,
                 const char* file,
                 int line);
bool ExpectNear(double actual,
                double expected,
                double tolerance,
                const char* expression,
                const char* file,
                int line);

// Runs every registered test and prints a summary.  Returns the number of
// failed checks so that the process exit code is meaningful.
int RunAllTests();

struct Registrar {
  Registrar(const char* name, TestFunction function) {
    Register(name, function);
  }
};

}  // namespace zgltest

#define TEST(Name)                                           \
  static void Name();                                        \
  static ::zgltest::Registrar Name##_registrar(#Name, Name); \
  static void Name()

#define EXPECT_TRUE(condition) \
  ::zgltest::ExpectTrue((condition), #condition, __FILE__, __LINE__)

#define EXPECT_FALSE(condition) \
  ::zgltest::ExpectTrue(!(condition), "!(" #condition ")", __FILE__, __LINE__)

#define EXPECT_EQ(actual, expected)                                  \
  ::zgltest::ExpectEqual((long long)(actual), (long long)(expected), \
                         #actual " == " #expected, __FILE__, __LINE__)

#define EXPECT_NEAR(actual, expected, tolerance)           \
  ::zgltest::ExpectNear((actual), (expected), (tolerance), \
                        #actual " ~ " #expected, __FILE__, __LINE__)

#endif  // ZGL_TESTS_ZGL_TEST_H_
