circle-coupe: SimCoupe SAM Coupe Emulator for Raspberry Pi 3B
=============================================================

SD Card Layout (FAT32 partition):

  /bootcode.bin          - RPi firmware stage 1 (from circle/boot/)
  /start.elf             - RPi firmware stage 2 (from circle/boot/)
  /fixup.dat             - RPi firmware fixup   (from circle/boot/)
  /config.txt            - Boot configuration    (this directory)
  /kernel8-32.img        - SimCoupe bare-metal kernel (built by make)
  /simcoupe/             - SimCoupe data directory
  /simcoupe/samcoupe.rom - SAM Coupe ROM image (required)
  /simcoupe/*.dsk        - SAM Coupe disk images (optional)
  /simcoupe/*.mgt        - SAM Coupe disk images (optional)

Build Steps:

  1. Build Circle libraries:
     make -C circle/lib
     make -C circle/lib/usb
     make -C circle/lib/input
     make -C circle/lib/sound
     make -C circle/lib/sched
     make -C circle/lib/fs
     make -C circle/addon/fatfs
     make -C circle/addon/SDCard

  2. Build SDL3-circle:
     cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/circle-toolchain.cmake
     cmake --build build --target SDL3-circle

  3. Build SimCoupe kernel:
     make -C simcoupe

  4. Prepare SD card:
     Copy firmware:  cp circle/boot/bootcode.bin circle/boot/start.elf circle/boot/fixup.dat /path/to/sd/
     Copy config:    cp simcoupe/sdcard/config.txt /path/to/sd/
     Copy kernel:    cp simcoupe/kernel8-32.img /path/to/sd/
     Copy ROM:       mkdir /path/to/sd/simcoupe && cp samcoupe.rom /path/to/sd/simcoupe/
     Copy disks:     cp *.dsk *.mgt /path/to/sd/simcoupe/  (optional)

Serial Debug:

  Connect a USB-to-serial adapter to GPIO 14 (TX) and GPIO 15 (RX).
  Open terminal at 115200 baud. Boot messages appear on serial.

Controls:

  F1  - Insert disk image
  F10 - Options
  F12 - Reset
  Ctrl-F12 - Exit (halt)
  Numpad-9 - Boot drive 1
