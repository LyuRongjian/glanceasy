// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GLANCEASY_TESTS_TEST_H_
#define GLANCEASY_TESTS_TEST_H_

namespace glanceasytest {

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

}  // namespace glanceasytest

#define TEST(Name)                                           \
  static void Name();                                        \
  static ::glanceasytest::Registrar Name##_registrar(#Name, Name); \
  static void Name()

#define EXPECT_TRUE(condition) \
  ::glanceasytest::ExpectTrue((condition), #condition, __FILE__, __LINE__)

#define EXPECT_FALSE(condition) \
  ::glanceasytest::ExpectTrue(!(condition), "!(" #condition ")", __FILE__, __LINE__)

#define EXPECT_EQ(actual, expected)                                  \
  ::glanceasytest::ExpectEqual((long long)(actual), (long long)(expected), \
                         #actual " == " #expected, __FILE__, __LINE__)

#define EXPECT_NEAR(actual, expected, tolerance)           \
  ::glanceasytest::ExpectNear((actual), (expected), (tolerance), \
                        #actual " ~ " #expected, __FILE__, __LINE__)

#endif  // GLANCEASY_TESTS_TEST_H_
