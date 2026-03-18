#
# simcoupe/Makefile
#
# SimCoupe Circle bare-metal kernel for Raspberry Pi 3B (AArch32)
# Output: kernel8-32.img
#
# Build order:
#   1. cmake -B ../build -DCMAKE_TOOLCHAIN_FILE=../cmake/circle-toolchain.cmake
#   2. cmake --build ../build              (builds SDL3-circle + libsimcoupe.a)
#   3. make -C ../circle/addon/fatfs       (if not already built)
#   4. make -C ../circle/addon/SDCard      (if not already built)
#   5. make                                (this file: links kernel8-32.img)
#
# Strategy: SimCoupe is built as libsimcoupe.a by CMake (with all
# FetchContent deps, SDL3 migration, FatFs FILE shim). This Makefile
# only compiles the Circle entry points (main.o, kernel.o, posix_stubs.o)
# and links everything into kernel8-32.img via Circle's Rules.mk.
#

CIRCLEHOME = ../circle

# ---- Object files (Circle entry points only) ----
OBJS = main.o kernel.o posix_stubs.o fatfs_posix.o ../kernel/syscalls.o

# ---- CMake build output ----
CMAKE_BUILD = ../build
SDL3_LIB_DIR = $(CMAKE_BUILD)/src
SIMCOUPE_LIB = $(CMAKE_BUILD)/simcoupe/libsimcoupe.a

# ---- Libraries to link ----
LIBS = \
	$(SIMCOUPE_LIB) \
	$(CMAKE_BUILD)/simcoupe/libwhereami.a \
	$(CMAKE_BUILD)/_deps/saasound-build/libSAASound.a \
	$(CMAKE_BUILD)/_deps/fmt-build/libfmt.a \
	$(CMAKE_BUILD)/_deps/resid-build/libresid.a \
	$(CMAKE_BUILD)/_deps/spectrum-build/libspectrum.a \
	$(SDL3_LIB_DIR)/libSDL3-circle.a \
	$(SDL3_LIB_DIR)/video/libSDL3-circle-video.a \
	$(SDL3_LIB_DIR)/audio/libSDL3-circle-audio.a \
	$(SDL3_LIB_DIR)/timer/libSDL3-circle-timer.a \
	$(SDL3_LIB_DIR)/thread/libSDL3-circle-thread.a \
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
	-I../SDL3/include \
	-I../SDL3/include/SDL3 \
	-I../src/include \
	-I../circle/addon/fatfs \
	-I../circle/addon/SDCard

# ---- Defines ----
CFLAGS += -fno-short-enums

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

# ---- Rebuild libsimcoupe.a (and SDL3 libs) via CMake whenever sources change ----
# This ensures 'make' picks up edits to Frame.cpp, SDL20.cpp, Input.cpp, etc.
# without needing a manual 'cmake --build' first.
CMAKE_SOURCES := $(shell find ../simcoupe/Base ../simcoupe/SDL ../src -name '*.cpp' -o -name '*.c' -o -name '*.h' 2>/dev/null)

$(SIMCOUPE_LIB): $(CMAKE_SOURCES)
	@echo "  CMAKE $(CMAKE_BUILD)"
	cmake --build $(CMAKE_BUILD) --parallel $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Make the kernel depend on the CMake lib so linking re-runs after cmake rebuild
$(TARGET).img: $(SIMCOUPE_LIB)

# ---- posix_stubs.cpp: needs Circle types FIRST to avoid time_t conflict ----
posix_stubs.o: posix_stubs.cpp
	@echo "  CPP   $@"
	$(CPP) $(CPPFLAGS) $(INCLUDE) -c $< -o $@

# ---- fatfs_posix.cpp: FatFs POSIX shim (mount, fopen/fread via FatFs) ----
fatfs_posix.o: ../src/fatfs_posix.cpp
	@echo "  CPP   $@"
	$(CPP) $(CPPFLAGS) $(INCLUDE) -c $< -o $@

# ---- SD card image assembly ----
SDCARD_DIR = sdcard
FIRMWARE_DIR = $(CIRCLEHOME)/boot

.PHONY: sdcard firmware-check
firmware-check:
	@test -f $(FIRMWARE_DIR)/bootcode.bin || { echo "ERROR: RPi firmware not found. Run: make -C $(FIRMWARE_DIR) firmware"; exit 1; }

sdcard: all firmware-check
	@echo "=== Assembling SD card in $(SDCARD_DIR)/ ==="
	@mkdir -p $(SDCARD_DIR)/simcoupe
	cp -f $(FIRMWARE_DIR)/bootcode.bin $(SDCARD_DIR)/
	cp -f $(FIRMWARE_DIR)/start.elf    $(SDCARD_DIR)/
	cp -f $(FIRMWARE_DIR)/fixup.dat    $(SDCARD_DIR)/
	cp -f kernel8-32.img $(SDCARD_DIR)/
	@echo ""
	@echo "SD card layout ready in $(SDCARD_DIR)/:"
	@ls -1 $(SDCARD_DIR)/
	@echo ""
	@echo "Next steps:"
	@echo "  1. Copy $(SDCARD_DIR)/* to a FAT32-formatted SD card"
	@echo "  2. Add samcoupe.rom to the simcoupe/ folder on the SD card"
	@echo "  3. Optionally add .dsk/.mgt disk images to simcoupe/"

-include $(DEPS)
