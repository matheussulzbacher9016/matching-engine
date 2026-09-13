#include "matching/cli.hpp"

#include <iostream>

int main() {
    try {
        return matching::run_session(std::cin, std::cout, std::cerr);
    } catch (const std::exception &error) {
        std::cerr << "Fatal: " << error.what() << '\n';
        return 2;
    }
}
