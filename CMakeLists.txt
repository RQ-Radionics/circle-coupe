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

# --- SimCoupe kernel (Circle bare-metal, no SDL) ---
add_subdirectory(simcoupe)
