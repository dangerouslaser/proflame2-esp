# Standalone host build of the ProFlame 2 decoder + tests. No ESPHome
# dependency, no toolchain — runs anywhere with a C++17 compiler. Build:
#
#     cd components/proflame2/test
#     cmake -B build
#     cmake --build build
#     ./build/test_decode
#
# CI can invoke `ctest --test-dir build` after build to get a friendlier
# pass/fail report; the executable also returns non-zero on failure.

cmake_minimum_required(VERSION 3.16)
project(proflame2_decode_test CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion)

add_executable(test_decode
  ${CMAKE_CURRENT_LIST_DIR}/../proflame2_decode.cpp
  ${CMAKE_CURRENT_LIST_DIR}/test_decode.cpp
)
target_include_directories(test_decode PRIVATE ${CMAKE_CURRENT_LIST_DIR}/..)

enable_testing()
add_test(NAME decoder COMMAND test_decode)
