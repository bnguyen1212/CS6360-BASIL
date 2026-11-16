# Standalone build for our code; independent of the harness's top-level CMakeLists.txt.
#   CC=clang CXX=clang++ cmake -S ours -B build/ours -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/ours -j4 && ./build/ours/test_sample_eb_pla [data/history_200M_uint64]
cmake_minimum_required(VERSION 3.10)
project(ours CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(test_sample_eb_pla tests/test_sample_eb_pla.cc)
target_compile_options(test_sample_eb_pla PRIVATE -Wall -Wextra -march=native)

enable_testing()
add_test(NAME test_sample_eb_pla COMMAND test_sample_eb_pla)

# Build-time + exhaustive-error benchmark (G1). See ours/bench/pla_bench.cc for usage.
add_executable(pla_bench bench/pla_bench.cc)
target_compile_options(pla_bench PRIVATE -Wall -Wextra -march=native)
