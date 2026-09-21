#pragma once

#include <exception>
#include <iostream>
#include <string_view>

namespace test_support {

inline int failures = 0;

inline void check(const bool condition, const std::string_view expression,
                  const std::string_view file, const int line) {
    if (!condition) {
        std::cerr << file << ':' << line << ": check failed: " << expression << '\n';
        ++failures;
    }
}

template <typename Exception, typename Function>
void expect_throw(Function&& function, const std::string_view expression,
                  const std::string_view file, const int line) {
    try {
        function();
    } catch (const Exception&) {
        return;
    } catch (const std::exception& error) {
        std::cerr << file << ':' << line << ": " << expression
                  << " threw unexpected exception: " << error.what() << '\n';
        ++failures;
        return;
    }
    std::cerr << file << ':' << line << ": expected exception from " << expression << '\n';
    ++failures;
}

}  // namespace test_support

#define CHECK(expression) \
    ::test_support::check(static_cast<bool>(expression), #expression, __FILE__, __LINE__)

#define EXPECT_THROW(expression, exception_type)                                      \
    ::test_support::expect_throw<exception_type>([&]() { static_cast<void>(expression); }, \
                                                  #expression, __FILE__, __LINE__)
