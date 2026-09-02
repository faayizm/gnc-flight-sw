// ============================================================================
//  tests/framework.hpp -- a minimal, dependency-free test framework.
//
//  Why not GoogleTest: this project's central claim is that the flight code
//  builds anywhere. A test suite that needs a network fetch and a C++ package
//  to run would quietly undermine that. Everything here is one header and
//  works on any C++17 compiler.
//
//  Usage:
//      TEST(suite_name, what_it_should_do) {
//          CHECK(condition);
//          CHECK_EQ(actual, expected);
//          CHECK_NEAR(actual, expected, tolerance);
//      }
//
//  Tests self-register at static initialisation time; main.cpp just runs the
//  registry. A failing check reports file, line and both values, then aborts
//  that test and moves to the next, so one broken thing does not hide ten more.
// ============================================================================
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

namespace testing {

struct TestCase {
    const char* suite;
    const char* name;
    void (*fn)();
};

// Function-local static, so registration works regardless of the order in
// which translation units are initialised.
inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

// Set when a check fails inside the currently running test.
inline bool& current_failed() {
    static bool failed = false;
    return failed;
}

struct Registrar {
    Registrar(const char* suite, const char* name, void (*fn)()) {
        registry().push_back(TestCase{suite, name, fn});
    }
};

inline void report_failure(const char* file, int line, const std::string& message) {
    std::printf("    FAIL %s:%d\n      %s\n", file, line, message.c_str());
    current_failed() = true;
}

// Stringify for the failure message. Overloaded rather than templated on
// std::to_string so that pointers and bools print usefully too.
template <typename T>
inline std::string show(const T& v) { return std::to_string(v); }
inline std::string show(bool v) { return v ? "true" : "false"; }
inline std::string show(const char* v) { return v != nullptr ? std::string(v) : "(null)"; }
inline std::string show(const std::string& v) { return v; }
inline std::string show(std::nullptr_t) { return "nullptr"; }

inline int run_all() {
    int passed = 0;
    int failed = 0;
    const char* last_suite = nullptr;

    for (const TestCase& t : registry()) {
        if (last_suite == nullptr || std::string(last_suite) != t.suite) {
            std::printf("\n[%s]\n", t.suite);
            last_suite = t.suite;
        }
        current_failed() = false;
        t.fn();
        if (current_failed()) {
            std::printf("  x  %s\n", t.name);
            ++failed;
        } else {
            std::printf("  .  %s\n", t.name);
            ++passed;
        }
    }

    std::printf("\n%d passed, %d failed, %d total\n",
                passed, failed, passed + failed);
    return failed == 0 ? 0 : 1;
}

}  // namespace testing

// ---------------------------------------------------------------------------
// Macros
// ---------------------------------------------------------------------------

#define FSW_CONCAT_INNER(a, b) a##b
#define FSW_CONCAT(a, b) FSW_CONCAT_INNER(a, b)

#define TEST(suite, name)                                                      \
    static void FSW_CONCAT(fsw_test_, __LINE__)();                             \
    static ::testing::Registrar FSW_CONCAT(fsw_reg_, __LINE__)(                \
        #suite, #name, &FSW_CONCAT(fsw_test_, __LINE__));                      \
    static void FSW_CONCAT(fsw_test_, __LINE__)()

// `return` on failure rather than continuing: once an assumption is violated
// the rest of the test is usually meaningless and often crashes.
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ::testing::report_failure(__FILE__, __LINE__,                       \
                std::string("expected: ") + #cond);                            \
            return;                                                            \
        }                                                                      \
    } while (false)

#define CHECK_EQ(actual, expected)                                             \
    do {                                                                       \
        const auto fsw_a = (actual);                                           \
        const auto fsw_e = (expected);                                         \
        if (!(fsw_a == fsw_e)) {                                               \
            ::testing::report_failure(__FILE__, __LINE__,                       \
                std::string(#actual) + " == " + #expected +                    \
                "\n      actual:   " + ::testing::show(fsw_a) +                \
                "\n      expected: " + ::testing::show(fsw_e));                \
            return;                                                            \
        }                                                                      \
    } while (false)

#define CHECK_NE(actual, unexpected)                                           \
    do {                                                                       \
        if ((actual) == (unexpected)) {                                        \
            ::testing::report_failure(__FILE__, __LINE__,                       \
                std::string(#actual) + " should not equal " + #unexpected);    \
            return;                                                            \
        }                                                                      \
    } while (false)

#define CHECK_NEAR(actual, expected, tolerance)                                \
    do {                                                                       \
        const double fsw_a = static_cast<double>(actual);                      \
        const double fsw_e = static_cast<double>(expected);                    \
        if (std::fabs(fsw_a - fsw_e) > (tolerance)) {                          \
            ::testing::report_failure(__FILE__, __LINE__,                       \
                std::string(#actual) + " ~= " + #expected +                    \
                "\n      actual:   " + ::testing::show(fsw_a) +                \
                "\n      expected: " + ::testing::show(fsw_e) +                \
                "\n      tolerance:" + ::testing::show(static_cast<double>(tolerance))); \
            return;                                                            \
        }                                                                      \
    } while (false)
