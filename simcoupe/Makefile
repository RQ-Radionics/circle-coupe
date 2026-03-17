#
# simcoupe/Makefile
#
# SimCoupe Circle bare-metal kernel for Raspberry Pi 3B (AArch32)
# Output: kernel8-32.img
#
# Build order:
#   1. cmake -B ../build -DCMAKE_TOOLCHAIN_FILE=../cmake/circle-toolchain.cmake
#   2. cmake --build ../build --target SDL3-circle
#   3. make -C ../circle/addon/fatfs
#   4. make -C ../circle/addon/SDCard
#   5. make  (this file)
#

CIRCLEHOME = ../circle

# SDL3-Circle library
SDL3_LIB_DIR = ../build/src

# ---- Object files ----
OBJS = main.o kernel.o posix_stubs.o

# ---- SimCoupe Base/ sources ----
BASE_OBJS = \
	Base/Actions.o Base/ATA.o Base/AtaAdapter.o Base/Atom.o Base/AtomLite.o \
	Base/AVI.o Base/BlipBuffer.o Base/BlueAlpha.o Base/Breakpoint.o \
	Base/Clock.o Base/CPU.o Base/Debug.o Base/Disassem.o Base/Disk.o \
	Base/Drive.o Base/Events.o Base/Expr.o Base/Font.o Base/Frame.o \
	Base/FrameBuffer.o Base/GIF.o Base/GUI.o Base/GUIDlg.o Base/GUIIcons.o \
	Base/HardDisk.o Base/Joystick.o Base/Keyboard.o Base/Keyin.o Base/Main.o \
	Base/Memory.o Base/Mouse.o Base/Options.o Base/Parallel.o Base/Paula.o \
	Base/SAMIO.o Base/SAMVox.o Base/SavePNG.o Base/SDIDE.o Base/SID.o \
	Base/Sound.o Base/sp0256.o Base/SSX.o Base/Stream.o Base/Symbol.o \
	Base/Tape.o Base/Util.o Base/Video.o Base/VoiceBox.o Base/WAV.o

# ---- SimCoupe SDL/ sources (SDL2->SDL3 migrated, no OpenGL, no MIDI) ----
SDL_OBJS = \
	SDL/Audio.o SDL/IDEDisk.o SDL/Input.o \
	SDL/OSD.o SDL/SDL20.o SDL/UI.o
# SDL/MIDI.o excluded: no SDL_mixer on bare-metal
# SDL/SDL20_GL3.o excluded: no OpenGL on bare-metal

# ---- resid (SID chip emulator) ----
RESID_OBJS = \
	resid-src/envelope.o resid-src/extfilt.o resid-src/filter.o \
	resid-src/pot.o resid-src/sid.o resid-src/version.o \
	resid-src/voice.o resid-src/wave.o \
	resid-src/wave6581_PST.o resid-src/wave6581_PS_.o \
	resid-src/wave6581_P_T.o resid-src/wave6581__ST.o \
	resid-src/wave8580_PST.o resid-src/wave8580_PS_.o \
	resid-src/wave8580_P_T.o resid-src/wave8580__ST.o

# ---- fmt (header-only - just needs the single .cc compiled) ----
FMT_OBJS = Extern/fmt/src/format.o Extern/fmt/src/os.o

# ---- libspectrum (tape/snapshot support) ----
LIBSPECTRUM_OBJS = \
	Extern/libspectrum/libspectrum.o \
	Extern/libspectrum/buffer.o \
	Extern/libspectrum/creator.o \
	Extern/libspectrum/crypto.o \
	Extern/libspectrum/csw.o \
	Extern/libspectrum/tape.o \
	Extern/libspectrum/tape_block.o \
	Extern/libspectrum/tap.o \
	Extern/libspectrum/tzx_read.o \
	Extern/libspectrum/tzx_write.o \
	Extern/libspectrum/snapshot.o \
	Extern/libspectrum/sna.o \
	Extern/libspectrum/z80.o \
	Extern/libspectrum/utilities.o \
	Extern/libspectrum/memory.o \
	Extern/libspectrum/timings.o \
	Extern/libspectrum/symbol_table.o

# ---- FatFs POSIX shim (built in src/) ----
FATFS_OBJS = fatfs_posix.o

ALL_OBJS = $(OBJS) $(BASE_OBJS) $(SDL_OBJS) $(RESID_OBJS) $(FMT_OBJS) \
           $(LIBSPECTRUM_OBJS) $(FATFS_OBJS)

# Rules.mk uses $(OBJS) for the link step - add all our objects to it
OBJS += $(BASE_OBJS) $(SDL_OBJS) $(RESID_OBJS) $(FMT_OBJS) \
        $(LIBSPECTRUM_OBJS) $(FATFS_OBJS)

# ---- Circle libraries ----
LIBS = \
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

# SimCoupe sources need full hosted C++ headers (<string>, <vector>, <regex> etc.)
# We override -ffreestanding for SimCoupe compilation units via a per-file flag.
# Circle kernel/posix files keep -ffreestanding as set by Rules.mk.
SIMCOUPE_CXXFLAGS = -fno-short-enums -fno-rtti -std=c++17 \
	-mcpu=cortex-a53 -marm -mfpu=neon-fp-armv8 -mfloat-abi=hard \
	-O2 -g -fno-exceptions \
	-DAARCH=32 -DRASPPI=3 -DSTDLIB_SUPPORT=1 \
	-D__circle__=500100 -D__VCCOREVER__=0x04000000

# ---- Include paths ----
INCLUDE += \
	-I../src/include \
	-I. \
	-IBase \
	-ISDL \
	-IExtern \
	-IExtern/ghc \
	-IExtern/z80 \
	-IExtern/fmt/include \
	-IExtern/libspectrum \
	-I../SDL3/include \
	-I../SDL3/include/SDL3 \
	-I../src/include \
	-I../src \
	-I../circle/addon/fatfs \
	-I../circle/addon/SDCard

# ---- Defines ----
CFLAGS += -fno-short-enums -fno-rtti
CFLAGS += -DHAVE_LIBSDL3=1
CFLAGS += -DSDL_DYNAMIC_API=0
CFLAGS += -DSDL_PLATFORM_CIRCLE=1
CFLAGS += -DSDL_VIDEO_DRIVER_CIRCLE=1 -DSDL_AUDIO_DRIVER_CIRCLE=1
CFLAGS += -DSDL_TIMER_CIRCLE=1 -DSDL_THREADS_DISABLED=1
# Disable features not available bare-metal
CFLAGS += -UGL3 -UHAVE_OPENGL -UHAVE_MIDI
CFLAGS += -DNO_OPENGL
# ghc::filesystem: use Circle POSIX shim (see Extern/ghc/filesystem.hpp)
CFLAGS += -DGFH_FILESYSTEM_IMPLEMENTATION
# libspectrum: disable bzip2/gzip (no zlib on bare-metal initially)
CFLAGS += -DLIBSPECTRUM_SUPPORTS_ZLIB_COMPRESSION=0
CFLAGS += -DLIBSPECTRUM_SUPPORTS_BZIP2_COMPRESSION=0
# Inject POSIX compat layer for all SimCoupe sources
CFLAGS += -include ../src/circle_posix_compat.h
# resid
CFLAGS += -DHAVE_RESID -DHAVE_CONFIG_H

# Disable dependency generation: our headers include ARM-only paths
# that the host compiler can't resolve with -M.
CHECK_DEPS = 0

include $(CIRCLEHOME)/Rules.mk

# libc needed for SimCoupe (getenv stubs, string ops)
LIBC = $(shell $(PREFIX)gcc $(ARCHCPU) -print-file-name=libc.a)
ifneq ($(strip $(LIBC)),libc.a)
EXTRALIBS += $(LIBC)
endif

LDFLAGS += --no-enum-size-warning

# ---- Newlib syscall stubs (same as kernel/) ----
OBJS += ../kernel/syscalls.o

# ---- SimCoupe sources: compile WITHOUT -ffreestanding (need hosted C++ STL) ----
# These override the default pattern rules from Rules.mk

SIMCOUPE_ALL_DEFINES = \
	-DHAVE_LIBSDL3=1 \
	-DSDL_DYNAMIC_API=0 \
	-DSDL_PLATFORM_CIRCLE=1 \
	-DSDL_VIDEO_DRIVER_CIRCLE=1 -DSDL_AUDIO_DRIVER_CIRCLE=1 \
	-DSDL_TIMER_CIRCLE=1 -DSDL_THREADS_DISABLED=1 \
	-UGL3 -UHAVE_OPENGL -UHAVE_MIDI -DNO_OPENGL \
	-DLIBSPECTRUM_SUPPORTS_ZLIB_COMPRESSION=0 \
	-DLIBSPECTRUM_SUPPORTS_BZIP2_COMPRESSION=0 \
	-DHAVE_RESID -DHAVE_CONFIG_H \
	-include ../src/circle_posix_compat.h

ARM_CXX = arm-none-eabi-g++
ARM_CC  = arm-none-eabi-gcc

SIMCOUPE_COMPILE = $(ARM_CXX) $(SIMCOUPE_CXXFLAGS) $(SIMCOUPE_ALL_DEFINES) $(INCLUDE) -c $< -o $@

Base/%.o: Base/%.cpp
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	$(SIMCOUPE_COMPILE)

SDL/%.o: SDL/%.cpp
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	$(SIMCOUPE_COMPILE)

resid-src/%.o: resid-src/%.cc
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	$(SIMCOUPE_COMPILE)

Extern/fmt/src/%.o: Extern/fmt/src/%.cc
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	$(SIMCOUPE_COMPILE)

Extern/libspectrum/%.o: Extern/libspectrum/%.c
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	$(ARM_CC) $(SIMCOUPE_CXXFLAGS) $(SIMCOUPE_ALL_DEFINES) $(INCLUDE) -std=gnu11 -c $< -o $@

# Build fatfs_posix.cpp from src/ (uses Circle headers, keep arm compiler)
fatfs_posix.o: ../src/fatfs_posix.cpp
	@echo "  CPP   $@"
	$(ARM_CXX) $(SIMCOUPE_CXXFLAGS) $(INCLUDE) -I../circle/addon/fatfs -I../circle/addon/SDCard -c $< -o $@

# ---- SD card image assembly ----
# Usage: make sdcard
# Requires: RPi firmware files (run 'make -C ../circle/boot firmware' first)
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
