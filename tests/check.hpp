#pragma once

#include <stdexcept>
#include <string>

namespace test {

inline void check(bool condition, const char *expression, const char *file, int line) {
    if (!condition) {
        throw std::runtime_error(std::string(file) + ":" + std::to_string(line) + ": " +
                                 expression);
    }
}

} // namespace test

// Unlike assert(), this check remains active when NDEBUG is defined.
#define CHECK(condition) ::test::check(static_cast<bool>(condition), #condition, __FILE__, __LINE__)

template <class Function> void rejects(Function action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    CHECK(rejected);
}