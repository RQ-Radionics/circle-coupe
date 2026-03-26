# SimCoupe Circle — SAM Coupé Emulator for Raspberry Pi

A bare-metal port of SimCoupe (SAM Coupé emulator) running on Raspberry Pi 2B/3B/4B/400 using the Circle C++ framework.

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
- **Dual audio output** — PWM via headphone jack + HDMI (when available)
- **FAT filesystem** — Load ROMs and disk images from SD card
- **Framebuffer video** — Direct hardware rendering at 800×600

## Audio System

SimCoupe Circle features automatic dual audio output:

### Primary Output (PWM)
- **Device**: `CPWMSoundBaseDevice` via Circle framework
- **Output**: 3.5mm headphone jack
- **Sample Rate**: 22.05kHz (44.1kHz for non-Circle builds)
- **Purpose**: Primary audio device, maintains core synchronization

### Secondary Output (HDMI)
- **Device**: `CHDMISoundBaseDevice` (automatic detection)
- **Output**: HDMI audio (when monitor supports it)
- **Sample Rate**: 48kHz
- **Purpose**: Additional audio output, no configuration required

### Implementation Details
- **Automatic Detection**: HDMI audio initializes automatically if available
- **Simultaneous Output**: Same audio data sent to both devices
- **Fallback**: PWM-only operation if HDMI fails or unavailable
- **Synchronization**: PWM remains primary device for timing control
- **Buffer Size**: 1000ms queue for smooth playback

## Supported Hardware

| Raspberry Pi | Kernel File        | CPU Optimization |
|--------------|--------------------|------------------|
| Pi 2B        | `kernel7.img`      | Cortex-A7        |
| Pi 3B        | `kernel8-32.img`   | Cortex-A53       |
| Pi 4B        | `kernel7l.img`     | Cortex-A72       |
| Pi 400       | `kernel7l.img`     | Cortex-A72       |

## Requirements

- **Toolchain**: `arm-none-eabi-gcc` (ARM cross-compiler)
- **Build tools**: CMake 3.16+, GNU Make
- **SD Card**: FAT32 formatted, 512MB+ recommended
- **SAM Coupé ROM**: `samcoupe.rom` (not included)
- **Raspberry Pi Firmware**: Automatically downloaded during build

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

# Build for Raspberry Pi 4B
./build.sh pi4

# Build for Raspberry Pi 400
./build.sh p400

# Build all supported versions
./build.sh all

# Clean all build artifacts
./build.sh clean
```

## SD Card Setup

Copy the following files to your FAT32 SD card:

```
sdcard/
├── bootcode.bin      # RPi bootloader (Pi 2B/3B only, from circle/boot/)
├── start.elf         # RPi firmware (Pi 2B/3B, from circle/boot/)
├── start4.elf        # RPi firmware (Pi 4B/400, from circle/boot/)
├── fixup.dat         # RPi firmware (Pi 2B/3B, from circle/boot/)
├── fixup4.dat        # RPi firmware (Pi 4B/400, from circle/boot/)
├── armstub7-rpi4.bin # ARM stub (Pi 4B/400 only, from circle/boot/)
├── bcm2711-rpi-4-b.dtb    # Device tree (Pi 4B, from circle/boot/)
├── bcm2711-rpi-400.dtb    # Device tree (Pi 400, from circle/boot/)
├── config.txt        # Boot configuration
├── kernel.img        # Use appropriate kernel for your Pi model:
│                     #   kernel7.img for Pi 2B
│                     #   kernel8-32.img for Pi 3B
│                     #   kernel7l.img for Pi 4B/400
└── simcoupe/
    ├── samcoupe.rom     # SAM Coupé ROM (required)
    ├── samdos2.sbt      # SAM DOS 2.2 system disk (recommended)
    ├── atom.rom         # Atom ROM (optional)
    ├── atomlite.rom     # Atom Lite ROM (optional)
    ├── sp0256-al2.bin   # SP0256-AL2 speech synthesizer (optional)
    └── *.dsk            # Additional disk images (optional)
```

### Generating Firmware Files

The build script automatically downloads and verifies firmware files. If needed manually:

```bash
# Download Raspberry Pi firmware
make -C circle/boot

# Build ARM stub for Pi 4 FIQ support
make -C circle/boot armstub
```

### Optional ROMs and Resources

The `simcoupe/Resource/` directory contains additional files that can be copied to your SD card:

- **`samdos2.sbt`** - SAM DOS 2.2 system disk. Provides disk operating system functionality
- **`atom.rom`** - Atom hard disk interface ROM for IDE hard disk emulation
- **`atomlite.rom`** - Atom Lite ROM for simplified Atom compatibility
- **`sp0256-al2.bin`** - SP0256-AL2 speech synthesizer ROM for voice synthesis

These files are automatically detected and loaded if present in the `simcoupe/` directory on the SD card.

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
│  Keyboard/Gamepad│                  │  Dual Audio Out  │
├─────────────────────────────────────────────────────────┤
│                    Circle Framework                      │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐    │
│  │ CPWMSound│ │CHDMISound│ │CEMMCDev. │ │CUSBHCIDev│    │
│  │(PWM)     │ │(HDMI)    │ │(SD Card) │ │(USB)      │    │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘    │
├─────────────────────────────────────────────────────────┤
│                    Hardware                              │
│  PWM Audio     │  HDMI Audio    │  SD Card Slot  │  USB │
│  (Headphone)   │  (Monitor)     │  (FAT32)       │  (KB)│
└─────────────────────────────────────────────────────────┘
```

## Project Structure

```
circle-coupe/
├── .beads/               # Issue tracking (beads system)
├── circle/               # Circle framework (submodule)
│   ├── addon/            # Circle extensions (LVGL, sound, etc.)
│   ├── include/          # Circle headers
│   ├── lib/              # Circle libraries
│   ├── sample/           # Example applications
│   └── ...
├── simcoupe/             # SimCoupe source (submodule)
│   ├── Base/             # Core emulator (CPU, video, sound)
│   ├── Circle/           # Circle-specific backends
│   ├── SDL/              # SDL platform layer (adapted)
│   ├── Win32/            # Windows-specific code (unused)
│   ├── Extern/           # External libraries (resid, fmt)
│   ├── Resource/         # ROMs and system resources
│   └── kernel.cpp        # Circle kernel entry point
├── src/                  # Support code
│   ├── video/circle/     # Circle framebuffer driver
│   ├── fatfs_posix.cpp   # POSIX file I/O via FatFs
│   └── ...
├── build/                # Build artifacts (Pi 3B)
├── build-pi2/            # Build artifacts (Pi 2B)
├── build-pi4/            # Build artifacts (Pi 4B/400)
├── scripts/              # Build and utility scripts
├── SDL3/                 # SDL3 library (subset used)
├── cmake/                # CMake toolchain files
├── build.sh              # Main build script
├── circle-config.mk      # Circle configuration
└── README.md             # This file
```

## Configuration Options

The `circle-config.mk` file controls build settings:

| Option              | Value       | Purpose                           |
|---------------------|-------------|-----------------------------------|
| `AARCH`             | 32          | 32-bit ARM (AArch32)              |
| `RASPPI`            | 2, 3, or 4  | Target Pi version                 |
| `KERNEL_MAX_SIZE`   | 0x800000    | 8MB kernel size limit             |
| `ARM_ALLOW_MULTI_CORE` | (defined) | Enable multicore support       |

## Technical Details

### Memory Model

- Kernel runs at 8MB limit (configurable in `kernel_max_size`)
- Framebuffer depth: 32-bit XRGB8888
- Sound buffer: 1024 samples @ 44.1kHz (~23ms latency)

### Audio Implementation

```cpp
// Dual audio device initialization (Circle/Audio.cpp)
static CPWMSoundBaseDevice *s_pSound = nullptr;      // Primary PWM
static CHDMISoundBaseDevice *s_pSoundHDMI = nullptr; // Secondary HDMI

// Automatic HDMI detection and initialization
s_pSoundHDMI = new CHDMISoundBaseDevice(s_pInterrupt, SAMPLE_FREQ, 2048);
if (s_pSoundHDMI && s_pSoundHDMI->AllocateQueue(1000)) {
    // HDMI available - dual output enabled
    g_audio_status = "dual-running";
}

// Simultaneous audio output
Audio::AddData(pData, len_bytes); // Sends to both devices
```

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
    CUSBHCIDevice  m_USBHCI;          // USB host controller
    CEMMCDevice    m_EMMC;            // SD card interface
    CPWMSoundBaseDevice m_PWMSound;   // PWM audio output (primary)
    CHDMISoundBaseDevice m_HDMISound; // HDMI audio output (secondary)
    // ...
};
```

### Audio Device Management

- **Primary Device (PWM)**: Always available, controls timing/synchronization
- **Secondary Device (HDMI)**: Auto-detected, optional, mirrors PWM output
- **Fallback Strategy**: PWM-only operation if HDMI initialization fails
- **Buffer Synchronization**: Both devices receive identical audio data simultaneously

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