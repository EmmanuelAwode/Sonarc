#pragma once

// Minimal self-registering test harness (no external dependencies).

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace sonarc::test {

struct TestCase
{
    const char* name;
    std::function<void()> body;
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failureCount()
{
    static int failures = 0;
    return failures;
}

struct Registrar
{
    Registrar(const char* name, std::function<void()> body) { registry().push_back({ name, std::move(body) }); }
};

inline void reportFailure(const char* file, int line, const std::string& message)
{
    ++failureCount();
    std::printf("    FAILED %s:%d: %s\n", file, line, message.c_str());
}

} // namespace sonarc::test

#define SONARC_CONCAT_INNER(a, b) a##b
#define SONARC_CONCAT(a, b) SONARC_CONCAT_INNER(a, b)

#define TEST_CASE(name)                                                                             \
    static void SONARC_CONCAT(testBody_, __LINE__)();                                               \
    static const sonarc::test::Registrar SONARC_CONCAT(testRegistrar_, __LINE__)(name, &SONARC_CONCAT(testBody_, __LINE__)); \
    static void SONARC_CONCAT(testBody_, __LINE__)()

#define CHECK(condition)                                                                            \
    do {                                                                                            \
        if (! (condition))                                                                          \
            sonarc::test::reportFailure(__FILE__, __LINE__, #condition);                            \
    } while (false)

#define CHECK_NEAR(actual, expected, tolerance)                                                     \
    do {                                                                                            \
        const double a_ = static_cast<double>(actual);                                              \
        const double e_ = static_cast<double>(expected);                                            \
        if (! (std::abs(a_ - e_) <= static_cast<double>(tolerance)))                                \
            sonarc::test::reportFailure(__FILE__, __LINE__,                                         \
                std::string(#actual) + " = " + std::to_string(a_) + ", expected " + std::to_string(e_) \
                + " +/- " + std::to_string(static_cast<double>(tolerance)));                         \
    } while (false)
