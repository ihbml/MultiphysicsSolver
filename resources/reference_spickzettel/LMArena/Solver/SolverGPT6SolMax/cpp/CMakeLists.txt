cmake_minimum_required(VERSION 3.20)
project(fieldline_multiphysics VERSION 0.1 LANGUAGES CXX)

add_library(fieldline INTERFACE)
target_compile_features(fieldline INTERFACE cxx_std_20)
target_include_directories(fieldline INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

option(FIELDLINE_USE_OPENMP "Parallelize large sparse operations with OpenMP" ON)
if(FIELDLINE_USE_OPENMP)
  find_package(OpenMP QUIET)
  if(OpenMP_CXX_FOUND)
    target_link_libraries(fieldline INTERFACE OpenMP::OpenMP_CXX)
  endif()
endif()

add_executable(fieldline_cantilever examples/cantilever.cpp)
target_link_libraries(fieldline_cantilever PRIVATE fieldline)

enable_testing()
add_executable(fieldline_tests tests/solver_tests.cpp)
target_link_libraries(fieldline_tests PRIVATE fieldline)
add_test(NAME fieldline_tests COMMAND fieldline_tests)