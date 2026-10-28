cmake_minimum_required(VERSION 3.20)
project(cfd_mesher_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC-specific high-performance optimization flags
if(MSVC)
    add_compile_options(
        /std:c++20
        /O2             # Max speed optimization
        /Oi             # Enable intrinsic functions
        /Ot             # Favor fast code
        /fp:fast        # Fast floating point model
        /openmp         # Multi-threading for cell loops
        /W4             # High warning level
        /MP             # Multi-processor compilation
    )
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -DNOMINMAX -DCFD_MESHER_EXPORTS)
else()
    add_compile_options(-O3 -ffast-math -fopenmp -Wall -Wextra)
    add_definitions(-DCFD_MESHER_EXPORTS)
endif()

add_library(cfd_mesher_native SHARED
    src/cfd_mesher_native.cpp
    src/difvm_mesh.cpp
    src/distmesh.cpp
    src/inflation.cpp
    src/rcm.cpp
    src/cht_cyclic.cpp
)

target_include_directories(cfd_mesher_native PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Output library directly to backend/meshing/ for seamless ctypes discovery
set_target_properties(cfd_mesher_native PROPERTIES
    PREFIX ""
    OUTPUT_NAME "cfd_mesher_native"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/.."
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/.."
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/.."
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
