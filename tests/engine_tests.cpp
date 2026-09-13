#include "check.hpp"
#include <iostream>
int main() {
    try {
        CHECK(1 + 1 == 2);
        std::cout << "All tests passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}