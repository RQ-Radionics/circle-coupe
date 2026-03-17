# cmake/circle-toolchain.cmake
#
# CMake toolchain file for Circle bare-metal on Raspberry Pi 3B (AArch32)
# Target: ARM Cortex-A53, arm-none-eabi-gcc, kernel8-32.img
#
# Usage:
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/circle-toolchain.cmake
#

# --- System ---
set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Prevent CMake from testing the compiler with a full link (no OS = no crt0)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# --- Toolchain prefix ---
set(CROSS_PREFIX "arm-none-eabi-")

find_program(CMAKE_C_COMPILER   ${CROSS_PREFIX}gcc   REQUIRED)
find_program(CMAKE_CXX_COMPILER ${CROSS_PREFIX}g++   REQUIRED)
find_program(CMAKE_ASM_COMPILER ${CROSS_PREFIX}gcc   REQUIRED)
find_program(CMAKE_AR           ${CROSS_PREFIX}ar    REQUIRED)
find_program(CMAKE_LINKER       ${CROSS_PREFIX}ld    REQUIRED)
find_program(CMAKE_OBJCOPY      ${CROSS_PREFIX}objcopy)
find_program(CMAKE_OBJDUMP      ${CROSS_PREFIX}objdump)
find_program(CMAKE_SIZE         ${CROSS_PREFIX}size)

# --- CPU flags for RPi3 Cortex-A53 AArch32 ---
set(CIRCLE_CPU_FLAGS
    "-mcpu=cortex-a53 -marm -mfpu=neon-fp-armv8 -mfloat-abi=hard"
)

# --- Common bare-metal flags ---
# NOTE: -nostdinc is NOT set here because SDL3 sources use newlib headers
# (stdint.h, string.h, etc.) via arm-none-eabi-gcc's built-in include path.
# Circle kernel code that must avoid libc should add -nostdinc per-target.
# CIRCLE_BARE_FLAGS: Full bare-metal flags for per-target use (SDL3-circle, kernel).
# NOT used in CMAKE_CXX_FLAGS_INIT because -ffreestanding breaks hosted C++ headers
# (<string>, <fstream>, etc.) needed by SimCoupe dependencies (SAASound, resid, fmt).
set(CIRCLE_BARE_FLAGS
    "-ffreestanding -fno-exceptions -fno-rtti -fno-unwind-tables"
    " -fno-asynchronous-unwind-tables -fsigned-char -O2 -g"
    " -DAARCH=32 -DRASPPI=3 -DSTDLIB_SUPPORT=1 -D__circle__=500100"
    " -D__VCCOREVER__=0x04000000 -U__unix__ -U__linux__"
    " -DKERNEL_MAX_SIZE=0x800000 -DDEPTH=32"
)
string(REPLACE ";" " " CIRCLE_BARE_FLAGS "${CIRCLE_BARE_FLAGS}")

set(CMAKE_C_FLAGS_INIT   "${CIRCLE_CPU_FLAGS} -ffreestanding -fno-unwind-tables -fno-asynchronous-unwind-tables -fsigned-char -O2 -g -DAARCH=32 -DRASPPI=3 -DSTDLIB_SUPPORT=1 -D__circle__=500100 -D__VCCOREVER__=0x04000000 -U__unix__ -U__linux__ -DKERNEL_MAX_SIZE=0x800000 -DDEPTH=32")
# CXX: NO -ffreestanding here — hosted C++ headers required by SimCoupe deps.
# Targets that need freestanding (SDL3-circle, kernel) add it via target_compile_options.
set(CMAKE_CXX_FLAGS_INIT "${CIRCLE_CPU_FLAGS} -fno-exceptions -fno-rtti -fno-unwind-tables -fno-asynchronous-unwind-tables -fsigned-char -O2 -g -DAARCH=32 -DRASPPI=3 -DSTDLIB_SUPPORT=1 -D__circle__=500100 -D__VCCOREVER__=0x04000000 -U__unix__ -U__linux__ -DKERNEL_MAX_SIZE=0x800000 -DDEPTH=32 -std=c++17 -Wno-aligned-new")
set(CMAKE_ASM_FLAGS_INIT "${CIRCLE_CPU_FLAGS} -ffreestanding -O2 -g -DAARCH=32 -DRASPPI=3 -DSTDLIB_SUPPORT=1 -D__circle__=500100 -U__unix__ -U__linux__ -DKERNEL_MAX_SIZE=0x800000 -DDEPTH=32")

# No dynamic linking on bare-metal
set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS   "")
set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# --- Circle paths (from submodule) ---
set(CIRCLE_HOME "${CMAKE_SOURCE_DIR}/circle" CACHE PATH "Path to Circle submodule")

set(CIRCLE_INCLUDE_DIRS
    "${CIRCLE_HOME}/include"
    "${CIRCLE_HOME}/addon"
    "${CIRCLE_HOME}/app/lib"
    "${CIRCLE_HOME}/addon/vc4"
    "${CIRCLE_HOME}/addon/vc4/interface/khronos/include"
)

set(CIRCLE_LIBRARIES
    "${CIRCLE_HOME}/lib/libcircle.a"
    "${CIRCLE_HOME}/lib/usb/libusb.a"
    "${CIRCLE_HOME}/lib/input/libinput.a"
    "${CIRCLE_HOME}/lib/sound/libsound.a"
    "${CIRCLE_HOME}/lib/sched/libsched.a"
)

# libgcc is needed for soft-float helpers, __aeabi_* etc.
execute_process(
    COMMAND ${CROSS_PREFIX}gcc -mcpu=cortex-a53 -marm -mfpu=neon-fp-armv8
            -mfloat-abi=hard -print-file-name=libgcc.a
    OUTPUT_VARIABLE CIRCLE_LIBGCC
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
list(APPEND CIRCLE_LIBRARIES "${CIRCLE_LIBGCC}")

# Circle linker script
set(CIRCLE_LINKER_SCRIPT "${CIRCLE_HOME}/circle.ld" CACHE FILEPATH "Circle linker script")
