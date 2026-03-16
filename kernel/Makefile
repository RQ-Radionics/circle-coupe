#
# kernel/Makefile
#
# circle-coupe SDL3 test kernel for Raspberry Pi 3B (AArch32)
# Output: kernel8-32.img
#
# Build steps:
#   1. cmake -B ../build -DCMAKE_TOOLCHAIN_FILE=../cmake/circle-toolchain.cmake
#   2. cmake --build ../build --target SDL3-circle
#   3. make  (this file)
#

CIRCLEHOME = ../circle

# SDL3-Circle library (built by CMake in ../build/src/)
SDL3_LIB_DIR = ../build/src

OBJS = main.o kernel.o syscalls.o

# Circle libraries needed (fs for CPartitionManager pulled by libusb)
# SDL3_LIBS added to LIBS so Rules.mk --start-group includes them
LIBS =	$(SDL3_LIB_DIR)/libSDL3-circle.a \
	$(SDL3_LIB_DIR)/video/libSDL3-circle-video.a \
	$(SDL3_LIB_DIR)/audio/libSDL3-circle-audio.a \
	$(SDL3_LIB_DIR)/timer/libSDL3-circle-timer.a \
	$(SDL3_LIB_DIR)/thread/libSDL3-circle-thread.a \
	$(CIRCLEHOME)/lib/sched/libsched.a \
	$(CIRCLEHOME)/lib/usb/libusb.a \
	$(CIRCLEHOME)/lib/input/libinput.a \
	$(CIRCLEHOME)/lib/sound/libsound.a \
	$(CIRCLEHOME)/lib/fs/libfs.a \
	$(CIRCLEHOME)/lib/libcircle.a

# Include paths: SDL3 public headers + our SDL_build_config.h
INCLUDE += -I../SDL3/include -I../SDL3/include/SDL3 -I../src/include -I../SDL3/src

# Force no short-enums (SDL3 requirement)
CFLAGS  += -fno-short-enums

# SDL defines
CFLAGS  += -DSDL_VIDEO_DRIVER_CIRCLE=1 -DSDL_AUDIO_DRIVER_CIRCLE=1
CFLAGS  += -DSDL_TIMER_CIRCLE=1 -DSDL_THREADS_DISABLED=1
CFLAGS  += -DSDL_PLATFORM_CIRCLE=1
# SDL_DYNAMIC_API=0: SDL3/src/dynapi/SDL_dynapi.h patched by scripts/patch-sdl3.sh
# to allow this define without triggering #error.
CFLAGS  += -DSDL_DYNAMIC_API=0

include $(CIRCLEHOME)/Rules.mk

# Suppress enum-size mismatch warnings from newlib (thumb vs arm enum sizes)
# These are harmless: SDL3 uses -fno-short-enums, newlib is compiled with defaults.
LDFLAGS += --no-enum-size-warning

# SDL3 requires full newlib (libc/libm) - add after Rules.mk sets ARCHCPU
LIBC     = $(shell $(PREFIX)gcc $(ARCHCPU) -print-file-name=libc.a)
ifneq ($(strip $(LIBC)),libc.a)
EXTRALIBS += $(LIBC)
endif

-include $(DEPS)
