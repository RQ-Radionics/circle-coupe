#!/usr/bin/env bash
#
# build.sh — Compila SimCoupe Circle para Raspberry Pi 2B/3B/4B/400
#
# Uso:
#   ./build.sh          → compila para Pi3 (kernel8-32.img)
#   ./build.sh pi2      → compila para Pi2B (kernel7.img)
#   ./build.sh pi3      → compila para Pi3 (kernel8-32.img)
#   ./build.sh pi4      → compila para Pi4B (kernel8-rpi4.img)
#   ./build.sh p400     → compila para Pi400 (kernel8-rpi4.img)
#   ./build.sh all      → compila para todas las plataformas
#   ./build.sh clean    → limpia todo (libs Circle + objetos locales + cmake)
#
# Requisitos:
#   - arm-none-eabi-gcc  (brew install arm-none-eabi-gcc  o apt install gcc-arm-none-eabi)
#   - cmake              (brew install cmake              o apt install cmake)
#   - make
#

set -e  # para si algo falla

# ---- Colores ----
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'
ok()   { echo -e "${GREEN}✓ $*${NC}"; }
info() { echo -e "${CYAN}▶ $*${NC}"; }
warn() { echo -e "${YELLOW}⚠ $*${NC}"; }
fail() { echo -e "${RED}✗ $*${NC}"; exit 1; }

# ---- Directorio raíz del proyecto ----
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# ---- Comprobaciones previas ----
check_deps() {
    command -v arm-none-eabi-gcc >/dev/null 2>&1 || \
        fail "arm-none-eabi-gcc no encontrado.\n  Mac:   brew install --cask gcc-arm-embedded\n  Linux: sudo apt install gcc-arm-none-eabi"
    command -v cmake >/dev/null 2>&1 || \
        fail "cmake no encontrado.\n  Mac:   brew install cmake\n  Linux: sudo apt install cmake"
    command -v make >/dev/null 2>&1 || \
        fail "make no encontrado."
    ok "Herramientas encontradas: $(arm-none-eabi-gcc --version | head -1 | cut -d'(' -f1)"
}

# ---- Ensure Circle Config.mk exists ----
ensure_circle_config() {
    if [ ! -f "circle/Config.mk" ]; then
        info "Creando circle/Config.mk..."
        cat > circle/Config.mk << 'EOF'
# SimCoupe Circle Config.mk
# Kernel size: 8MB (SimCoupe kernel is ~2MB text+data+bss)
# Default 2MB is too small, sysinit.cpp will halt if KERNEL_MAX_SIZE < _end
KERNEL_MAX_SIZE = 0x800000
DEFINE += -DKERNEL_MAX_SIZE=0x800000
DEFINE += -DDEPTH=32
DEFINE += -DARM_ALLOW_MULTI_CORE
EOF
        ok "circle/Config.mk creado"
    fi
}

# ---- Check Raspberry Pi firmware ----
check_firmware() {
    if [ ! -f "circle/boot/bootcode.bin" ] || [ ! -f "circle/boot/start.elf" ] || [ ! -f "circle/boot/start4.elf" ]; then
        warn "Firmware de Raspberry Pi no encontrado."
        info "Descargando firmware actualizado..."
        (cd circle/boot && make) || fail "Error descargando firmware"
        ok "Firmware descargado"
    fi
    if [ ! -f "circle/boot/armstub7-rpi4.bin" ]; then
        warn "armstub7-rpi4.bin no encontrado (necesario para Pi 4)."
        info "Compilando armstub para Pi 4..."
        (cd circle/boot && make armstub) || fail "Error compilando armstub"
        ok "armstub compilado"
    fi
}

# ---- Número de CPUs para compilación paralela ----
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# ---- Limpieza total ----
do_clean() {
    info "Limpiando todo..."
    make -C simcoupe clean RASPPI=3 2>/dev/null || true
    make -C simcoupe clean RASPPI=2 2>/dev/null || true
    make -C simcoupe clean RASPPI=4 2>/dev/null || true
    # Limpiar libs Circle
    for dir in circle/lib circle/lib/usb circle/lib/input circle/lib/sound \
                 circle/lib/sched circle/lib/fs \
                 circle/addon/fatfs circle/addon/SDCard \
                 circle/addon/linux circle/addon/vc4/vchiq circle/addon/vc4/sound; do
        make -C "$dir" clean 2>/dev/null || true
    done
    # Limpiar directorios cmake
    rm -rf build build-pi2 build-pi4
    # Limpiar objetos locales
    rm -f simcoupe/*.o kernel/syscalls.o
    ok "Limpieza completa"
}

# ---- Build para una plataforma ----
build_platform() {
    local RASPPI="$1"  # 2, 3 o 4

    if [ "$RASPPI" = "2" ]; then
        local NAME="Pi2B"
        local KERNEL="kernel7.img"
        local TOOLCHAIN="cmake/circle-toolchain-pi2.cmake"
        local CMAKE_BUILD="build-pi2"
    elif [ "$RASPPI" = "3" ]; then
        local NAME="Pi3B"
        local KERNEL="kernel8-32.img"
        local TOOLCHAIN="cmake/circle-toolchain.cmake"
        local CMAKE_BUILD="build"
    else  # RASPPI=4
        local NAME="Pi4B"
        local KERNEL="kernel7l.img"
        local TOOLCHAIN="cmake/circle-toolchain-pi4.cmake"
        local CMAKE_BUILD="build-pi4"
    fi

    echo ""
    echo -e "${CYAN}════════════════════════════════════════${NC}"
    info "Compilando para $NAME → $KERNEL"
    echo -e "${CYAN}════════════════════════════════════════${NC}"

    # Paso 1: Configurar CMake si no existe el directorio
    if [ ! -f "$CMAKE_BUILD/CMakeCache.txt" ]; then
        info "Paso 1/3: Configurando CMake para $NAME..."
        cmake -B "$CMAKE_BUILD" \
              -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
              . 2>&1 | grep -v "^--" | grep -v "^$" || true
        ok "CMake configurado en $CMAKE_BUILD/"
    else
        info "Paso 1/3: CMake ya configurado en $CMAKE_BUILD/ (saltando)"
    fi

    # Paso 2: Compilar libsimcoupe.a y dependencias via CMake
    info "Paso 2/3: Compilando libsimcoupe.a + dependencias..."
    cmake --build "$CMAKE_BUILD" --parallel "$JOBS" 2>&1 | \
        grep -E "^\[|error:|warning:|Built target" || true
    ok "libsimcoupe.a compilada"

    # Paso 3: Compilar libs Circle + enlazar kernel
    info "Paso 3/3: Compilando libs Circle y enlazando $KERNEL..."
    # Clean local .o files to avoid cross-platform contamination
    rm -f simcoupe/*.o 2>/dev/null
    # First make may fail if .o files were just cleaned — retry
    make -C simcoupe RASPPI="$RASPPI" -j1 2>&1 | \
        grep -E "CLEAN|CPP|CC|AR|LD|COPY|WC|error:|warning:|CMAKE" || true
    # Retry if link failed due to missing .o
    if [ ! -f "simcoupe/$KERNEL" ]; then
        make -C simcoupe RASPPI="$RASPPI" -j1 2>&1 | \
            grep -E "CPP|CC|AR|LD|COPY|WC|error:|warning:" || true
    fi

    # Verificar resultado
    if [ -f "simcoupe/$KERNEL" ]; then
        local SIZE=$(ls -lh "simcoupe/$KERNEL" | awk '{print $5}')
        ok "$KERNEL generado ($SIZE)"
    else
        fail "$KERNEL no se generó — revisa los errores arriba"
    fi
}

# ---- Copiar a SD card (si se monta) ----
copy_to_sdcard() {
    local KERNEL="$1"
    local RASPPI="$2"
    # Busca volúmenes FAT montados que tengan bootcode.bin (típico de RPi)
    for vol in /Volumes/*/; do
        if [ -f "${vol}bootcode.bin" ] || [ -f "${vol}BOOTCODE.BIN" ]; then
            info "SD card detectada en $vol — copiando kernel, config.txt, cmdline.txt y firmware..."
            cp -f "simcoupe/$KERNEL" "$vol"
            cp -f "simcoupe/sdcard/config.txt" "$vol"
            cp -f "simcoupe/sdcard/cmdline.txt" "$vol"

            # Copiar firmware apropiado según la plataforma
            if [ "$RASPPI" = "4" ]; then
                # Pi 4/400: necesita start4.elf, fixup4.dat y armstub
                cp -f "circle/boot/start4.elf" "$vol"
                cp -f "circle/boot/fixup4.dat" "$vol"
                cp -f "circle/boot/armstub7-rpi4.bin" "$vol"
                cp -f "circle/boot/bcm2711-rpi-4-b.dtb" "$vol"
                cp -f "circle/boot/bcm2711-rpi-400.dtb" "$vol"
            else
                # Pi 2/3: firmware estándar
                cp -f "circle/boot/bootcode.bin" "$vol"
                cp -f "circle/boot/start.elf" "$vol"
                cp -f "circle/boot/fixup.dat" "$vol"
            fi

            ok "Archivos copiados a $vol"
            return 0
        fi
    done
    warn "No se detectó SD card montada. Copia manualmente:"
    echo "   cp simcoupe/$KERNEL /Volumes/<tu-sd>/"
    echo "   cp simcoupe/sdcard/config.txt /Volumes/<tu-sd>/"
    echo "   cp simcoupe/sdcard/cmdline.txt /Volumes/<tu-sd>/"
    if [ "$RASPPI" = "4" ]; then
        echo "   cp circle/boot/start4.elf /Volumes/<tu-sd>/"
        echo "   cp circle/boot/fixup4.dat /Volumes/<tu-sd>/"
        echo "   cp circle/boot/armstub7-rpi4.bin /Volumes/<tu-sd>/"
        echo "   cp circle/boot/bcm2711-rpi-4-b.dtb /Volumes/<tu-sd>/"
        echo "   cp circle/boot/bcm2711-rpi-400.dtb /Volumes/<tu-sd>/"
    else
        echo "   cp circle/boot/bootcode.bin /Volumes/<tu-sd>/"
        echo "   cp circle/boot/start.elf /Volumes/<tu-sd>/"
        echo "   cp circle/boot/fixup.dat /Volumes/<tu-sd>/"
    fi
}

# ---- Create release ZIP with all platforms ----
do_release() {
    local VERSION=$(date +%Y%m%d)
    local RELEASE_DIR="release/circle-coupe-${VERSION}"
    local ZIP_FILE="release/circle-coupe-${VERSION}.zip"
    
    info "Compilando todos los kernels..."
    
    # Build all platforms
    build_platform 3
    build_platform 2
    build_platform 4
    
    info "Creando paquete de release..."
    
    # Clean release directory
    rm -rf "$RELEASE_DIR"
    mkdir -p "$RELEASE_DIR/simcoupe"
    
    # ---- Kernels (all in root, user renames as needed) ----
    cp -f simcoupe/kernel7.img "$RELEASE_DIR/"
    cp -f simcoupe/kernel8-32.img "$RELEASE_DIR/"
    cp -f simcoupe/kernel7l.img "$RELEASE_DIR/"
    
    # ---- Firmware (all in root - Pi auto-selects correct one) ----
    # Pi 2B/3B firmware
    cp -f circle/boot/bootcode.bin "$RELEASE_DIR/"
    cp -f circle/boot/start.elf "$RELEASE_DIR/"
    cp -f circle/boot/fixup.dat "$RELEASE_DIR/"
    # Pi 4B/400 firmware
    cp -f circle/boot/start4.elf "$RELEASE_DIR/"
    cp -f circle/boot/fixup4.dat "$RELEASE_DIR/"
    cp -f circle/boot/armstub7-rpi4.bin "$RELEASE_DIR/"
    cp -f circle/boot/bcm2711-rpi-4-b.dtb "$RELEASE_DIR/"
    cp -f circle/boot/bcm2711-rpi-400.dtb "$RELEASE_DIR/"
    
    # ---- Config (in root) ----
    cp -f simcoupe/sdcard/config.txt "$RELEASE_DIR/"
    cp -f simcoupe/sdcard/cmdline.txt "$RELEASE_DIR/"
    
    # ---- SimCoupe assets (simcoupe/ folder) ----
    cp -f simcoupe/Resource/* "$RELEASE_DIR/simcoupe/"
    
    # ---- Create README ----
    cat > "$RELEASE_DIR/README.txt" << 'EOFREADME'
circle-coupe: SimCoupe SAM Coupe Emulator for Raspberry Pi
===========================================================

Instalacion (copiar TODO a la raiz de la SD FAT32):

  bootcode.bin        - RPi firmware (todas las Pi)
  start.elf           - Pi 2B/3B firmware
  start4.elf          - Pi 4B/400 firmware
  fixup.dat           - Pi 2B/3B fixup
  fixup4.dat          - Pi 4B/400 fixup
  armstub7-rpi4.bin   - Pi 4B/400 armstub
  *.dtb               - Pi 4B/400 device trees
  
  config.txt          - Configuracion de boot
  
  kernel7.img         - Kernel para Pi 2B
  kernel8-32.img      - Kernel para Pi 3B
  kernel7l.img        - Kernel para Pi 4B/400
  
  simcoupe/           - Assets del emulador
    samcoupe.rom      - ROM SAM Coupe (required)
    atom.rom          - ROM Atom HDD interface
    atomlite.rom      - ROM Atom Lite
    *.map/*.sbt       - Debug symbols
    sp0256-al2.bin    - Speech synthesizer
    SimCoupe.bmp      - Splash image

NOTA: Los kernels tienen nombres distintos. Tu Pi usara el correcto
      automaticamente. Solo necesitas renombrar si quieres
      sobrescribir el kernel por defecto:
      
      Pi 2B:  renombra kernel7.img     a kernel.img
      Pi 3B:  renombra kernel8-32.img a kernel.img  
      Pi 4B:  renombra kernel7l.img    a kernel.img

Controles:
  F1       - Insertar disco
  F10      - Menu de opciones
  F12      - Reset
  Ctrl-F12 - Salir
  Numpad-9 - Boot desde drive 1

Debug serie: 115200 baud, GPIO 14 (TX) / GPIO 15 (RX)
EOFREADME

    # ---- Create ZIP ----
    mkdir -p release
    cd release
    rm -f "circle-coupe-${VERSION}.zip"
    zip -r "circle-coupe-${VERSION}.zip" "circle-coupe-${VERSION}"
    cd ..
    
    local ZIP_SIZE=$(ls -lh "$ZIP_FILE" | awk '{print $5}')
    
    ok "Release creado: $ZIP_FILE ($ZIP_SIZE)"
    echo ""
    echo "Contenido del ZIP:"
    echo "  kernel7.img, kernel8-32.img, kernel7l.img  (kernels)"
    echo "  bootcode.bin, start.elf, start4.elf, etc.   (firmware)"
    echo "  config.txt                                  (config)"
    echo "  simcoupe/                                   (ROMs + assets)"
    echo ""
    echo "Para instalar:"
    echo "  unzip $ZIP_FILE -d /Volumes/<sd>/"
}

# ---- Main ----
TARGET="${1:-pi3}"

check_deps
ensure_circle_config
check_firmware

case "$TARGET" in
    pi2)
        build_platform 2
        copy_to_sdcard "kernel7.img" 2
        ;;
    pi3)
        build_platform 3
        copy_to_sdcard "kernel8-32.img" 3
        ;;
    pi4|p400)
        build_platform 4
        copy_to_sdcard "kernel7l.img" 4
        ;;
    all)
        build_platform 3
        build_platform 2
        build_platform 4
        echo ""
        ok "Todos los kernels generados:"
        ls -lh simcoupe/kernel7.img simcoupe/kernel8-32.img simcoupe/kernel7l.img 2>/dev/null || true
        echo ""
        warn "Para la SD card necesitas los ficheros correspondientes + config.txt + firmware:"
        echo "   simcoupe/kernel7.img        (para Pi 2B)"
        echo "   simcoupe/kernel8-32.img     (para Pi 3B)"
        echo "   simcoupe/kernel7l.img       (para Pi 4B/P400)"
        echo "   simcoupe/sdcard/config.txt"
        echo "   circle/boot/bootcode.bin    (Pi 2B/3B)"
        echo "   circle/boot/start.elf       (Pi 2B/3B)"
        echo "   circle/boot/fixup.dat       (Pi 2B/3B)"
        echo "   circle/boot/start4.elf      (Pi 4B/400)"
        echo "   circle/boot/fixup4.dat      (Pi 4B/400)"
        echo "   circle/boot/armstub7-rpi4.bin (Pi 4B/400)"
        ;;
    release)
        do_release
        ;;
    clean)
        do_clean
        ;;
    *)
        echo "Uso: $0 [pi2|pi3|pi4|p400|all|release|clean]"
        echo ""
        echo "  pi2     → kernel7.img      (Raspberry Pi 2B, Cortex-A7)"
        echo "  pi3     → kernel8-32.img   (Raspberry Pi 3B, Cortex-A53)  [default]"
        echo "  pi4     → kernel7l.img     (Raspberry Pi 4B, Cortex-A72)"
        echo "  p400    → kernel7l.img     (Raspberry Pi 400, Cortex-A72)"
        echo "  all     → compila todos los kernels"
        echo "  release → crea ZIP con todo listo para SD (kernels + firmware + assets)"
        echo "  clean   → limpia todo para empezar desde cero"
        exit 1
        ;;
esac

echo ""
ok "Listo."
