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

# Black-box probe of the AUTHORS' sPGM (G2 prep). Compiles their wrapper header as-is,
# with the harness's flags (-ffast-math, OpenMP), so it behaves as it does inside BASIL.
# We do not read that code; see ours/blackbox/spgm_probe.cc for what is measured.
find_package(OpenMP REQUIRED)
find_package(Threads REQUIRED)
add_executable(spgm_probe blackbox/spgm_probe.cc)
target_include_directories(spgm_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_options(spgm_probe PRIVATE -ffast-math -march=native -w)
target_link_libraries(spgm_probe PRIVATE OpenMP::OpenMP_CXX Threads::Threads)
