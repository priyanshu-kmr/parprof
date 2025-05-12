cmake_minimum_required(VERSION 3.20)
project(parprof LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra -Wpedantic)

# The tool
add_library(parprof_core STATIC
  src/analysis.cpp
  src/gpu.cpp
  src/launcher.cpp
  src/perf_counters.cpp
  src/procfs.cpp
  src/profiler.cpp
  src/report.cpp)
target_include_directories(parprof_core PUBLIC src)
target_link_libraries(parprof_core PUBLIC ${CMAKE_DL_LIBS})

add_executable(parprof src/main.cpp)
target_link_libraries(parprof PRIVATE parprof_core)

# Demo programs, each with a buggy default and a --fixed mode
find_package(OpenMP REQUIRED)
foreach(demo false_sharing imbalance contention)
  add_executable(${demo} demos/${demo}.cpp)
  target_link_libraries(${demo} PRIVATE OpenMP::OpenMP_CXX)
endforeach()

# GPU demo for --gpu, built only if the CUDA toolkit is installed
find_package(CUDAToolkit QUIET)
if(CUDAToolkit_FOUND)
  add_executable(gpu_busy demos/gpu_busy.cpp)
  target_link_libraries(gpu_busy PRIVATE CUDA::cuda_driver CUDA::nvrtc)
endif()

# Tests: `ctest --test-dir build --output-on-failure`
enable_testing()
add_executable(unit_tests tests/unit_tests.cpp)
target_link_libraries(unit_tests PRIVATE parprof_core)
add_test(NAME unit COMMAND unit_tests)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME integration
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_parprof.py)
  set_tests_properties(integration PROPERTIES ENVIRONMENT "PARPROF_BUILD=${CMAKE_BINARY_DIR}")
endif()
