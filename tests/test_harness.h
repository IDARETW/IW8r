// Minimal shared test harness for iw8r_tests. Any tests/*.cpp file can register tests:
//   #include "test_harness.h"
//   TEST(my_test) { CHECK(1 + 1 == 2); }
// The runner (main) lives in core_tests.cpp. iw8r::test::tmpDir() is a scratch folder for files.
#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace iw8r::test {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& tests() {
    static std::vector<TestCase> t;
    return t;
}
inline int& checks() {
    static int n = 0;
    return n;
}
inline int& failures() {
    static int n = 0;
    return n;
}
// Tests that could not run because a fixture on this machine is missing (a stock dump, a built map). The summary
// line counts them: a suite that says "0 failures" with skips has NOT checked everything (2026-11-11: two fixture
// folders were deleted and nobody noticed, because skips were only lines in the middle of the output).
inline int& skips() {
    static int n = 0;
    return n;
}
template <typename... Args>
inline void skipf(const char* format, Args... args) {
    ++skips();
    std::printf(format, args...);
}
inline std::string& current() {
    static std::string s;
    return s;
}
inline std::string& tmpDir() {
    static std::string s;
    return s;
}
struct Reg {
    Reg(const char* n, std::function<void()> f) { tests().push_back({n, std::move(f)}); }
};

}  // namespace iw8r::test

#define CHECK(cond)                                                                                              \
    do {                                                                                                         \
        ++::iw8r::test::checks();                                                                                \
        if (!(cond)) {                                                                                           \
            ++::iw8r::test::failures();                                                                          \
            std::printf("  FAIL [%s] %s:%d: %s\n", ::iw8r::test::current().c_str(), __FILE__, __LINE__, #cond); \
        }                                                                                                        \
    } while (0)

#define TEST(name)                                                   \
    static void name();                                              \
    static ::iw8r::test::Reg reg_##name(#name, name);                \
    static void name()
