# SimCoupe Circle — SAM Coupé Emulator for Raspberry Pi

A bare-metal port of SimCoupe (SAM Coupé emulator) running on Raspberry Pi using the Circle C++ framework.

## Introduction

SimCoupe emulates the SAM Coupé — a British Z80-based home computer released in 1989 by Miles Gordon Technology. This version brings SimCoupe to Raspberry Pi as a bare-metal application, eliminating the need for a full operating system.

This port is based on [SimCoupe by Simon Owen](https://github.com/simonowen/simcoupe), adapted to run on [Circle](https://github.com/rsta2/circle), a C++ bare-metal programming environment for Raspberry Pi.

## Features

- **Bare-metal execution** — No Linux required; runs directly on Raspberry Pi hardware
- **Multicore architecture**:
  - Core 0: USB host, scheduler, input handling
  - Core 1: Z80 CPU emulation and video
  - Core 2: Audio synthesis
- **USB input support** — Keyboards and gamepads (up to 2)
- **PWM audio output** — 44.1kHz via headphone jack
- **FAT filesystem** — Load ROMs and disk images from SD card
- **Framebuffer video** — Direct hardware rendering at 800×600

## Supported Hardware

| Raspberry Pi | Kernel File        | CPU Optimization |
|--------------|--------------------|------------------|
| Pi 2B        | `kernel7.img`      | Cortex-A7        |
| Pi 3B        | `kernel8-32.img`   | Cortex-A53       |

## Requirements

- **Toolchain**: `arm-none-eabi-gcc` (ARM cross-compiler)
- **Build tools**: CMake 3.16+, GNU Make
- **SD Card**: FAT32 formatted, 512MB+ recommended
- **SAM Coupé ROM**: `samcoupe.rom` (not included)

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
# Build for Raspberry Pi 3B (default)
./build.sh

# Build for Raspberry Pi 2B
./build.sh pi2

# Build both versions
./build.sh all

# Clean all build artifacts
./build.sh clean
```

## SD Card Setup

Copy the following files to your FAT32 SD card:

```
sdcard/
├── bootcode.bin      # RPi bootloader (from circle/boot/)
├── start.elf         # RPi firmware (from circle/boot/)
├── fixup.dat         # RPi firmware (from circle/boot/)
├── config.txt        # Boot configuration
├── kernel.img        # Use kernel7.img for Pi2, kernel8-32.img for Pi3
└── simcoupe/
    ├── samcoupe.rom  # SAM Coupé ROM (required)
    └── *.dsk         # Disk images (optional)
```

### Generating Firmware Files

```bash
make -C circle/boot firmware
```

## Controls

| Key            | SAM Coupé     |
|----------------|---------------|
| USB Keyboard   | SAM Keyboard  |
| USB Gamepad    | Joystick 1/2  |

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Raspberry Pi                         │
├─────────────────────────────────────────────────────────┤
│  Core 0          │  Core 1          │  Core 2           │
│  ───────         │  ───────         │  ───────          │
│  USB HCI         │  Z80 Emulation   │  Audio Synthesis  │
│  Scheduler       │  Video Update    │  Sound::Frame()  │
│  Keyboard/Gamepad│                  │                   │
├─────────────────────────────────────────────────────────┤
│                    Circle Framework                      │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐    │
│  │ CPWMSound│ │CEMMCDev. │ │CUSBHCIDev│ │FatFs     │    │
│  │(Audio)   │ │(SD Card) │ │(USB)      │ │(FAT32)   │    │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘    │
├─────────────────────────────────────────────────────────┤
│                    Hardware                              │
│  PWM Audio     │  SD Card Slot  │  USB Ports            │
│  (Headphone)   │  (FAT32)       │  (Keyboard/Gamepad)  │
└─────────────────────────────────────────────────────────┘
```

## Project Structure

```
circle-coupe/
├── circle/              # Circle framework (submodule)
├── simcoupe/             # SimCoupe source (submodule)
│   ├── Base/             # Core emulator (CPU, video, sound)
│   ├── Circle/           # Circle-specific backends
│   ├── SDL/              # SDL platform layer (adapted)
│   └── kernel.cpp        # Circle kernel entry point
├── src/                  # Support code
│   ├── video/circle/     # Circle framebuffer driver
│   ├── fatfs_posix.cpp   # POSIX file I/O via FatFs
│   └── ...
├── SDL3/                 # SDL3 library (subset used)
├── build.sh              # Build script
├── circle-config.mk      # Circle configuration
└── cmake/                # CMake toolchain files
```

## Configuration Options

The `circle-config.mk` file controls build settings:

| Option              | Value       | Purpose                           |
|---------------------|-------------|-----------------------------------|
| `AARCH`             | 32          | 32-bit ARM (AArch32)              |
| `RASPPI`            | 2 or 3      | Target Pi version                 |
| `KERNEL_MAX_SIZE`   | 0x800000    | 8MB kernel size limit             |
| `ARM_ALLOW_MULTI_CORE` | (defined) | Enable multicore support       |

## Technical Details

### Memory Model

- Kernel runs at 8MB limit (configurable in `kernel_max_size`)
- Framebuffer depth: 32-bit XRGB8888
- Sound buffer: 1024 samples @ 44.1kHz (~23ms latency)

### Multicore Synchronization

```cpp
// Core 1 signals Core 2 when audio frame is ready
volatile bool g_sound_frame_pending;
volatile bool g_sound_frame_done;
```

Memory barriers (`dmb`) ensure proper synchronization between cores.

### Circle Integration

The `CKernel` class inherits from `CMultiCoreSupport`:

```cpp
class CKernel : public CMultiCoreSupport {
    CUSBHCIDevice  m_USBHCI;      // USB host controller
    CEMMCDevice    m_EMMC;        // SD card interface
    CPWMSoundBaseDevice m_PWMSound; // PWM audio output
    // ...
};
```

## Credits

### SimCoupe Original
- **Simon Owen** — SimCoupe emulator (https://github.com/simonowen/simcoupe)
- Allan Skillman — Original SimCoupe 0.72
- Dave Laundon — CPU contention and sound enhancements
- Dave Hooper — Phillips SAA 1099 chip emulator
- Ivan Kosarev — Z80 CPU core
- Dag Lem — MOS 6581/8580 SID emulator

### Circle Framework
- **Rene Stange** — Circle bare-metal C++ environment (https://github.com/rsta2/circle)

## License

This project is licensed under the GNU General Public License v2.0. See `simcoupe/License.txt` for details.

## Contact

For the original SimCoupe: Simon Owen — https://simonowen.com

For Circle: Rene Stange — See Circle README

For this bare-metal port: See project repository issues.