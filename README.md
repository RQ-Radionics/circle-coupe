# SimCoupe Circle — SAM Coupé Emulator for Raspberry Pi

A bare-metal port of SimCoupe (SAM Coupé emulator) running on Raspberry Pi 2B/3B/4B/400 using the Circle C++ framework.

## Introduction

SimCoupe emulates the SAM Coupé — a British Z80-based home computer released in 1989 by Miles Gordon Technology. This version brings SimCoupe to Raspberry Pi as a bare-metal application, eliminating the need for a full operating system.

This port is based on [SimCoupe by Simon Owen](https://github.com/simonowen/simcoupe), adapted to run on [Circle](https://github.com/rsta2/circle), a C++ bare-metal programming environment for Raspberry Pi.

## Features

- **Bare-metal execution** — No Linux required; runs directly on Raspberry Pi hardware
- **Multicore architecture**:
  - Core 0: USB host, scheduler, input handling, VCHIQ audio transport
  - Core 1: Z80 CPU emulation and video rendering
  - Core 2: Audio synthesis (SAA1099, SID, DAC)
- **USB input** — Keyboard, mouse and gamepads (up to 2)
- **VCHIQ audio** — HDMI or headphone jack output via GPU audio service
- **FAT filesystem** — Load ROMs and disk images from SD card
- **Framebuffer video** — 800x600, 8-bit palette (Pi 2/3) or 32-bit ARGB (Pi 4/400)

## Supported Hardware

| Raspberry Pi | Kernel File      | CPU          | Audio Default | Video         |
|--------------|------------------|--------------|---------------|---------------|
| Pi 2B        | `kernel7.img`    | Cortex-A7    | Headphones    | 8-bit palette |
| Pi 3B        | `kernel8-32.img` | Cortex-A53   | Headphones    | 8-bit palette |
| Pi 4B        | `kernel7l.img`   | Cortex-A72   | HDMI          | 32-bit ARGB   |
| Pi 400       | `kernel7l.img`   | Cortex-A72   | HDMI          | 32-bit ARGB   |

**Pi 400 notes**: Must use HDMI0 port (closest to USB-C power). No headphone jack.

## Audio System

All audio goes through VCHIQ (VideoCore Host Interface Queue), routed to HDMI or headphone jack:

- **Sample rate**: 44100 Hz stereo (matching upstream SimCoupe)
- **Auto-detection**: Pi 4/400 defaults to HDMI, Pi 2/3 to headphones
- **Override**: Set `sounddev=hdmi` or `sounddev=headphones` in `cmdline.txt`
- **Ring buffer**: Core 2 generates audio, Core 0 transports to GPU via VCHIQ
- **Flow control**: GPU callback reports buffered bytes, poll rate-limited

### Audio Pipeline

```
Core 2 (emulator)         Ring Buffer          Core 0 (poll)           GPU
  Sound::FrameUpdate() --> s_ring[] ---------> circle_audio_poll() --> VCHIQ --> speaker
  SAA1099 + DAC + SID      ~1s stereo          1 frame per poll        HDMI/jack
  44100 Hz stereo           176K s16            via WriteSamples()
```

## Requirements

- **Toolchain**: `arm-none-eabi-gcc` (ARM cross-compiler)
- **Build tools**: CMake 3.16+, GNU Make
- **SD Card**: FAT32 formatted, 512MB+ recommended
- **SAM Coupé ROM**: `samcoupe.rom` (included in `simcoupe/Resource/`)

### Installing the toolchain

**macOS:**
```bash
brew install --cask gcc-arm-embedded
brew install cmake
```

**Linux (Debian/Ubuntu):**
```bash
sudo apt install gcc-arm-none-eabi cmake make
```

## Building

```bash
# Build for a specific platform
./build.sh pi2      # Raspberry Pi 2B
./build.sh pi3      # Raspberry Pi 3B (default)
./build.sh pi4      # Raspberry Pi 4B / 400

# Build all platforms
./build.sh all

# Create release ZIP
./build.sh release

# Clean everything
./build.sh clean
```

## SD Card Setup

Format your SD card as FAT32 and copy **all** of the following files to the root. Without the firmware files the Pi will not boot.

| File | Source | Required |
|------|--------|----------|
| `bootcode.bin` | `circle/boot/` | Pi 2B/3B |
| `start.elf` | `circle/boot/` | Pi 2B/3B |
| `start4.elf` | `circle/boot/` | Pi 4B/400 |
| `fixup.dat` | `circle/boot/` | Pi 2B/3B |
| `fixup4.dat` | `circle/boot/` | Pi 4B/400 |
| `armstub7-rpi4.bin` | `circle/boot/` | Pi 4B/400 |
| `bcm2711-rpi-4-b.dtb` | `circle/boot/` | Pi 4B |
| `bcm2711-rpi-400.dtb` | `circle/boot/` | Pi 400 |
| `config.txt` | `simcoupe/sdcard/` | All |
| `cmdline.txt` | `simcoupe/sdcard/` | Optional |
| `kernel7.img` | `simcoupe/` | Pi 2B |
| `kernel8-32.img` | `simcoupe/` | Pi 3B |
| `kernel7l.img` | `simcoupe/` | Pi 4B/400 |
| `simcoupe/samcoupe.rom` | `simcoupe/Resource/` | All |
| `simcoupe/*.dsk` | your collection | Optional |

The Pi firmware auto-selects the correct kernel based on the board. You can place all three kernel files on the same SD card.

The easiest way to set up the SD card is to use `./build.sh release` which creates a ready-to-use ZIP file.

### config.txt

The included `config.txt` supports all Pi models with conditional sections:

```ini
arm_64bit=0
initial_turbo=0
dtparam=audio=on

[pi4]
kernel=kernel7l.img
armstub=armstub7-rpi4.bin
max_framebuffers=2
```

### cmdline.txt

Optional kernel parameters:

```
sounddev=hdmi          # Force HDMI audio (auto-detected on Pi 4/400)
sounddev=headphones    # Force headphone jack (auto-detected on Pi 2/3)
```

If `cmdline.txt` is empty or absent, audio destination is auto-detected.

## Controls

| Input          | Function                     |
|----------------|------------------------------|
| USB Keyboard   | SAM Coupé keyboard           |
| USB Mouse      | SAM Coupé mouse              |
| USB Gamepad    | Joystick 1/2                 |

### Function Keys

| Key            | Function                     |
|----------------|------------------------------|
| F1             | Insert disk (drive 1)        |
| F2             | Insert disk (drive 2)        |
| F9             | Debugger                     |
| F10            | Options menu                 |
| F12            | Reset                        |
| Shift+F12      | Exit (screen goes black)     |
| Numpad 9       | Boot from drive 1            |

## Architecture

```text
+-----------------------------------------------------------+
|                      Raspberry Pi                         |
+------------------+------------------+---------------------+
|  Core 0          |  Core 1          |  Core 2             |
|                  |                  |                     |
|  USB HCI         |  Z80 Emulation   |  Audio Synthesis    |
|  Scheduler       |  Video Rendering |  SAA1099 + DAC +SID |
|  KB/Mouse/Gamepad|  Frame Update    |  Sound::FrameUpdate |
|  VCHIQ Audio Poll|                  |  Ring Buffer Write  |
+------------------+------------------+---------------------+
|  Circle Framework + Multicore-safe Linux Compat Layer     |
|                                                           |
|  VCHIQSound   CScreenDevice   CEMMCDevice   CUSBHCIDevice |
|  (Audio)      (Pi4 Video)     (SD Card)     (USB)         |
+-----------------------------------------------------------+
|  Hardware                                                 |
|  HDMI/Jack Audio  |  HDMI Video  |  SD Card  |  USB      |
+-----------------------------------------------------------+
```

### Key Implementation Details

- **Video**: Pi 2/3 use `CBcmFrameBuffer` (8-bit palette, GPU converts). Pi 4/400 use `CScreenDevice` (32-bit ARGB, software palette LUT) because VCHIQ init disrupts raw framebuffers on BCM2711
- **Audio**: Push-based VCHIQ (adapted from BMC64). Ring buffer decouples emulator (Core 2) from VCHIQ transport (Core 0). Re-START mechanism recovers from GPU pipeline drain
- **Multicore locks**: `linux_multicore_fix.cpp` provides atomic rwlock, mutex, semaphore and completion overrides for Circle's Linux compat layer (originals assert Core 0 only)

## Credits

### SimCoupe Original
- **Simon Owen** — SimCoupe emulator (https://github.com/simonowen/simcoupe)
- Allan Skillman — Original SimCoupe 0.72
- Dave Laundon — CPU contention and sound enhancements
- Dave Hooper — Phillips SAA 1099 chip emulator

### Circle Framework
- **Rene Stange** — Circle bare-metal C++ environment (https://github.com/rsta2/circle)

### Circle Bare-Metal Port
- **RQ Radionics & Rodolfo Guerra**

## License

This project is licensed under the GNU General Public License v2.0. See `simcoupe/License.txt` for details.
