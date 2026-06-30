#pragma once

#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace bytecachedb::test {

class TestFailure : public std::runtime_error {
public:
    explicit TestFailure(const std::string& message) : std::runtime_error(message) {}
};

inline void fail(const char* file, int line, const std::string& message) {
    std::ostringstream out;
    out << file << ":" << line << ": " << message;
    throw TestFailure(out.str());
}

inline int run(const std::string& name, const std::function<void()>& test) {
    try {
        test();
        std::cout << "[PASS] " << name << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "[FAIL] " << name << ": " << ex.what() << "\n";
        return 1;
    }
}

} // namespace bytecachedb::test

#define ASSERT_TRUE(expr)                                                                          \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            bytecachedb::test::fail(__FILE__, __LINE__, "expected true: " #expr);                  \
        }                                                                                          \
    } while (false)

#define ASSERT_FALSE(expr)                                                                         \
    do {                                                                                           \
        if ((expr)) {                                                                              \
            bytecachedb::test::fail(__FILE__, __LINE__, "expected false: " #expr);                 \
        }                                                                                          \
    } while (false)

#define ASSERT_EQ(expected, actual)                                                                \
    do {                                                                                           \
        const auto expected_value = (expected);                                                     \
        const auto actual_value = (actual);                                                         \
        if (!(expected_value == actual_value)) {                                                    \
            bytecachedb::test::fail(__FILE__, __LINE__, #expected " != " #actual);                  \
        }                                                                                          \
    } while (false)
