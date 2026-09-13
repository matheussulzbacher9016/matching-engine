cmake_minimum_required(VERSION 3.20)
project(matching_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow)

add_executable(matching src/main.cpp)
target_include_directories(matching PRIVATE include)

enable_testing()

add_executable(engine_tests tests/engine_tests.cpp)
target_include_directories(engine_tests PRIVATE include tests)
add_test(NAME engine_tests COMMAND engine_tests)

add_executable(cli_tests tests/cli_tests.cpp)
target_include_directories(cli_tests PRIVATE include tests)
add_test(NAME cli_tests COMMAND cli_tests)