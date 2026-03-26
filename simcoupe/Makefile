#
# simcoupe/Makefile
#
# SimCoupe Circle bare-metal kernel — multi-platform build
#
# Supported targets:
#   make                  → Pi3 (default, kernel8-32.img)
#   make RASPPI=3         → Pi3 (kernel8-32.img, Cortex-A53)
#   make RASPPI=2         → Pi2B (kernel7.img, Cortex-A7)
#
# Full clean rebuild for a given platform:
#   make clean && make RASPPI=2
#
# Build order (handled automatically by this Makefile):
#   1. cmake -B ../build-pi<N> -DCMAKE_TOOLCHAIN_FILE=../cmake/circle-toolchain-pi<N>.cmake
#   2. cmake --build ../build-pi<N>        (builds SDL3-circle + libsimcoupe.a)
#   3. make -C ../circle/lib RASPPI=<N>    (Circle libs)
#   4. make -C ../circle/addon/fatfs RASPPI=<N>
#   5. make -C ../circle/addon/SDCard RASPPI=<N>
#   6. make RASPPI=<N>                     (this file: links kernel)
#

CIRCLEHOME = ../circle

# ---- Platform selection (default: Pi3) ----
RASPPI ?= 3

 ifeq ($(strip $(RASPPI)),2)
   TOOLCHAIN_FILE  = ../cmake/circle-toolchain-pi2.cmake
   CMAKE_BUILD     = ../build-pi2
 else ifeq ($(strip $(RASPPI)),3)
   TOOLCHAIN_FILE  = ../cmake/circle-toolchain.cmake
   CMAKE_BUILD     = ../build
 else ifeq ($(strip $(RASPPI)),4)
   TOOLCHAIN_FILE  = ../cmake/circle-toolchain-pi4.cmake
   CMAKE_BUILD     = ../build-pi4
 else
   $(error RASPPI must be 2, 3 or 4)
 endif

# ---- Object files (Circle entry points only) ----
OBJS = main.o kernel.o posix_stubs.o fatfs_posix.o syscalls.o circle_fb.o

# ---- CMake build output ----
SDL3_LIB_DIR = $(CMAKE_BUILD)/src
SIMCOUPE_LIB = $(CMAKE_BUILD)/simcoupe/libsimcoupe.a

# ---- Libraries to link ----
# No SDL3 — direct Circle port
LIBS = \
	$(SIMCOUPE_LIB) \
	$(CMAKE_BUILD)/simcoupe/libwhereami.a \
	$(CMAKE_BUILD)/_deps/saasound-build/libSAASound.a \
	$(CMAKE_BUILD)/_deps/fmt-build/libfmt.a \
	$(CMAKE_BUILD)/_deps/resid-build/libresid.a \
	$(CMAKE_BUILD)/_deps/spectrum-build/libspectrum.a \
	$(CIRCLEHOME)/addon/SDCard/libsdcard.a \
	$(CIRCLEHOME)/addon/fatfs/libfatfs.a \
	$(CIRCLEHOME)/lib/sched/libsched.a \
	$(CIRCLEHOME)/lib/usb/libusb.a \
	$(CIRCLEHOME)/lib/input/libinput.a \
	$(CIRCLEHOME)/lib/sound/libsound.a \
	$(CIRCLEHOME)/lib/fs/libfs.a \
	$(CIRCLEHOME)/lib/libcircle.a

# ---- Include paths for Circle entry points ----
INCLUDE += \
	-I. \
	-I./Base \
	-I./SDL \
	-I../src/video/circle \
	-I../src/include \
	-I../circle/addon/fatfs \
	-I../circle/addon/SDCard \
	-I../circle/addon/vc4 \
	-I$(CMAKE_BUILD)/simcoupe

# ---- Defines ----
CFLAGS += -fno-short-enums -DNO_PHYSICAL_COUNTER

# Disable dependency generation
CHECK_DEPS = 0

include $(CIRCLEHOME)/Rules.mk

# ---- Newlib libc (needed for hosted C++ in SimCoupe) ----
LIBC = $(shell $(PREFIX)gcc $(ARCHCPU) -print-file-name=libc.a)
ifneq ($(strip $(LIBC)),libc.a)
EXTRALIBS += $(LIBC)
endif

LIBM = $(shell $(PREFIX)gcc $(ARCHCPU) -print-file-name=libm.a)
ifneq ($(strip $(LIBM)),libm.a)
EXTRALIBS += $(LIBM)
endif

LIBSTDCXX = $(shell $(PREFIX)gcc $(ARCHCPU) -print-file-name=libstdc++.a)
ifneq ($(strip $(LIBSTDCXX)),libstdc++.a)
EXTRALIBS += $(LIBSTDCXX)
endif

LDFLAGS += --no-enum-size-warning
# Allow multiple definitions (Circle and newlib both define strtoul, __cxa_guard, etc.)
LDFLAGS += --allow-multiple-definition

# ---- Rebuild Circle libs for the correct RASPPI ----
# Circle libs embed CPU-specific code (-mcpu=cortex-aX). Since the Circle
# build system compiles objects in-place (no per-RASPPI output dirs), we
# must clean and rebuild whenever RASPPI changes to avoid mixing A53/A7 objects.
#
# We detect the current RASPPI by checking the CPU flags inside libcircle.a.
# If they don't match, we clean all Circle libs first, then rebuild.
CIRCLE_CURRENT_CPU := $(shell strings $(CIRCLEHOME)/lib/libcircle.a 2>/dev/null | grep -o 'mcpu=cortex-a[0-9]*' | head -1)
 ifeq ($(strip $(RASPPI)),2)
   CIRCLE_EXPECTED_CPU = mcpu=cortex-a7
 else ifeq ($(strip $(RASPPI)),3)
   CIRCLE_EXPECTED_CPU = mcpu=cortex-a53
 else ifeq ($(strip $(RASPPI)),4)
   CIRCLE_EXPECTED_CPU = mcpu=cortex-a72
 endif

circle-libs:
	@if [ -n "$(CIRCLE_CURRENT_CPU)" ] && [ "$(CIRCLE_CURRENT_CPU)" != "$(CIRCLE_EXPECTED_CPU)" ]; then \
		echo "  CLEAN Circle libs + local objects (was $(CIRCLE_CURRENT_CPU), need $(CIRCLE_EXPECTED_CPU))"; \
		$(MAKE) -C $(CIRCLEHOME)/lib          RASPPI=$(RASPPI) clean; \
		$(MAKE) -C $(CIRCLEHOME)/lib/usb      RASPPI=$(RASPPI) clean; \
		$(MAKE) -C $(CIRCLEHOME)/lib/input    RASPPI=$(RASPPI) clean; \
		$(MAKE) -C $(CIRCLEHOME)/lib/sound    RASPPI=$(RASPPI) clean; \
		$(MAKE) -C $(CIRCLEHOME)/lib/sched    RASPPI=$(RASPPI) clean; \
		$(MAKE) -C $(CIRCLEHOME)/lib/fs       RASPPI=$(RASPPI) clean; \
		$(MAKE) -C $(CIRCLEHOME)/addon/fatfs  RASPPI=$(RASPPI) clean; \
		$(MAKE) -C $(CIRCLEHOME)/addon/SDCard RASPPI=$(RASPPI) clean; \
		rm -f *.o syscalls.o; \
	fi
	$(MAKE) -C $(CIRCLEHOME)/lib          RASPPI=$(RASPPI)
	$(MAKE) -C $(CIRCLEHOME)/lib/usb      RASPPI=$(RASPPI)
	$(MAKE) -C $(CIRCLEHOME)/lib/input    RASPPI=$(RASPPI)
	$(MAKE) -C $(CIRCLEHOME)/lib/sound    RASPPI=$(RASPPI)
	$(MAKE) -C $(CIRCLEHOME)/lib/sched    RASPPI=$(RASPPI)
	$(MAKE) -C $(CIRCLEHOME)/lib/fs       RASPPI=$(RASPPI)
	$(MAKE) -C $(CIRCLEHOME)/addon/fatfs  RASPPI=$(RASPPI)
	$(MAKE) -C $(CIRCLEHOME)/addon/SDCard RASPPI=$(RASPPI)

# ---- Rebuild libsimcoupe.a (and SDL3 libs) via CMake whenever sources change ----
CMAKE_SOURCES := $(shell find ../simcoupe/Base ../simcoupe/Circle ../simcoupe/SDL ../src/video/circle -name '*.cpp' -o -name '*.c' -o -name '*.h' 2>/dev/null)

$(SIMCOUPE_LIB): $(CMAKE_SOURCES)
	@echo "  CMAKE $(CMAKE_BUILD) [RASPPI=$(RASPPI)]"
	@if [ ! -f $(CMAKE_BUILD)/CMakeCache.txt ]; then \
		echo "  CMAKE configure $(CMAKE_BUILD)"; \
		cmake -B $(CMAKE_BUILD) -DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN_FILE) \
		      -DCMAKE_SOURCE_DIR=.. $(CURDIR)/.. ; \
	fi
	cmake --build $(CMAKE_BUILD) --parallel $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# All CMake-built libraries depend on the main simcoupe lib target
# (which triggers the full CMake build)
$(CMAKE_BUILD)/simcoupe/libwhereami.a \
$(CMAKE_BUILD)/_deps/saasound-build/libSAASound.a \
$(CMAKE_BUILD)/_deps/fmt-build/libfmt.a \
$(CMAKE_BUILD)/_deps/resid-build/libresid.a \
$(CMAKE_BUILD)/_deps/spectrum-build/libspectrum.a: $(SIMCOUPE_LIB)
	@:

# Make the kernel depend on the CMake lib and object files
# OBJS must be a hard dependency, circle-libs is order-only (|)
$(TARGET).img: $(OBJS) $(SIMCOUPE_LIB) | circle-libs

# Circle library files depend on circle-libs target (phony dependency)
# This tells make: "if any of these files are needed, run circle-libs first"
$(CIRCLEHOME)/addon/SDCard/libsdcard.a \
$(CIRCLEHOME)/addon/fatfs/libfatfs.a \
$(CIRCLEHOME)/lib/libcircle.a \
$(CIRCLEHOME)/lib/usb/libusb.a \
$(CIRCLEHOME)/lib/input/libinput.a \
$(CIRCLEHOME)/lib/sound/libsound.a \
$(CIRCLEHOME)/lib/sched/libsched.a \
$(CIRCLEHOME)/lib/fs/libfs.a: circle-libs
	@:

# ---- posix_stubs.cpp: needs Circle types FIRST to avoid time_t conflict ----
posix_stubs.o: posix_stubs.cpp
	@echo "  CPP   $@"
	$(CPP) $(CPPFLAGS) $(INCLUDE) -c $< -o $@

# ---- fatfs_posix.cpp: FatFs POSIX shim (mount, fopen/fread via FatFs) ----
fatfs_posix.o: ../src/fatfs_posix.cpp
	@echo "  CPP   $@"
	$(CPP) $(CPPFLAGS) $(INCLUDE) -c $< -o $@

# ---- circle_fb: framebuffer backend (single-buffered CBcmFrameBuffer) ----
circle_fb.o: ../src/video/circle/SDL_circlefb.cpp
	@echo "  CPP   $@"
	$(CPP) $(CPPFLAGS) $(INCLUDE) -c $< -o $@

# ---- SD card image assembly ----
SDCARD_DIR   = sdcard-pi$(RASPPI)/sdcard
CONFIG_DIR   = sdcard-pi$(RASPPI)
FIRMWARE_DIR = $(CIRCLEHOME)/boot

.PHONY: sdcard firmware-check circle-libs

firmware-check:
	@test -f $(FIRMWARE_DIR)/bootcode.bin || { echo "ERROR: RPi firmware not found. Run: make -C $(FIRMWARE_DIR) firmware"; exit 1; }

sdcard: all firmware-check
	@echo "=== Assembling SD card in $(SDCARD_DIR)/ [RASPPI=$(RASPPI)] ==="
	@mkdir -p $(SDCARD_DIR)/simcoupe
	cp -f $(FIRMWARE_DIR)/bootcode.bin $(SDCARD_DIR)/
	cp -f $(FIRMWARE_DIR)/start.elf    $(SDCARD_DIR)/
	cp -f $(FIRMWARE_DIR)/fixup.dat    $(SDCARD_DIR)/
	cp -f $(TARGET).img                $(SDCARD_DIR)/
	cp -f $(CONFIG_DIR)/config.txt     $(SDCARD_DIR)/
	@echo ""
	@echo "SD card layout ready in $(SDCARD_DIR)/:"
	@ls -1 $(SDCARD_DIR)/
	@echo ""
	@echo "Next steps:"
	@echo "  1. Copy $(SDCARD_DIR)/* to a FAT32-formatted SD card"
	@echo "  2. Add samcoupe.rom to the simcoupe/ folder on the SD card"
	@echo "  3. Optionally add .dsk/.mgt disk images to simcoupe/"

-include $(DEPS)
