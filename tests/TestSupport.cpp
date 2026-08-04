#include "TestSupport.hpp"

#include <iostream>

namespace meat2d_tests {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

} // namespace meat2d_tests
