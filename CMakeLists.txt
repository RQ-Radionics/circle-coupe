cmake_minimum_required(VERSION 3.16)

# Toolchain must be set before project()
# Usage: cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/circle-toolchain.cmake

project(circle-coupe C CXX ASM)

# Verify we're cross-compiling for bare-metal
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Generic")
    message(FATAL_ERROR
        "This project requires the Circle toolchain file.\n"
        "Run: cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/circle-toolchain.cmake"
    )
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# --- Circle paths (set by toolchain, allow override) ---
if(NOT DEFINED CIRCLE_HOME)
    set(CIRCLE_HOME "${CMAKE_SOURCE_DIR}/circle")
endif()
set(SDL3_HOME "${CMAKE_SOURCE_DIR}/SDL3")

# --- SDL3 as a hand-rolled static library for Circle bare-metal ---
# We do NOT use add_subdirectory(SDL3) because SDL3's CMake requires a
# recognized OS platform and POSIX threads. Instead we compile the SDL3
# sources we need directly, with our Circle backends substituted in.
add_subdirectory(src)

# --- Test/stub kernel ---
if(EXISTS "${CMAKE_SOURCE_DIR}/kernel")
    add_subdirectory(kernel)
endif()

# --- SimCoupe kernel (added later) ---
if(EXISTS "${CMAKE_SOURCE_DIR}/simcoupe")
    add_subdirectory(simcoupe)
endif()
