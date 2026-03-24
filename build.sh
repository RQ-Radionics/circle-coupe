#!/usr/bin/env bash
#
# build.sh — Compila SimCoupe Circle para Raspberry Pi 2B y/o Pi 3B
#
# Uso:
#   ./build.sh          → compila para Pi3 (kernel8-32.img)
#   ./build.sh pi2      → compila para Pi2B (kernel7.img)
#   ./build.sh pi3      → compila para Pi3 (kernel8-32.img)
#   ./build.sh all      → compila para ambas plataformas
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
        info "Creando circle/Config.mk con ARM_ALLOW_MULTI_CORE..."
        echo "DEFINE += -DARM_ALLOW_MULTI_CORE" > circle/Config.mk
    fi
}

# ---- Número de CPUs para compilación paralela ----
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# ---- Limpieza total ----
do_clean() {
    info "Limpiando todo..."
    make -C simcoupe clean RASPPI=3 2>/dev/null || true
    make -C simcoupe clean RASPPI=2 2>/dev/null || true
    # Limpiar libs Circle
    for dir in circle/lib circle/lib/usb circle/lib/input circle/lib/sound \
                circle/lib/sched circle/lib/fs \
                circle/addon/fatfs circle/addon/SDCard; do
        make -C "$dir" clean 2>/dev/null || true
    done
    # Limpiar directorios cmake
    rm -rf build build-pi2
    # Limpiar objetos locales
    rm -f simcoupe/*.o kernel/syscalls.o
    ok "Limpieza completa"
}

# ---- Build para una plataforma ----
build_platform() {
    local RASPPI="$1"  # 2 o 3

    if [ "$RASPPI" = "2" ]; then
        local NAME="Pi2B"
        local KERNEL="kernel7.img"
        local TOOLCHAIN="cmake/circle-toolchain-pi2.cmake"
        local CMAKE_BUILD="build-pi2"
    else
        local NAME="Pi3B"
        local KERNEL="kernel8-32.img"
        local TOOLCHAIN="cmake/circle-toolchain.cmake"
        local CMAKE_BUILD="build"
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
    # Use -j1 for first build to avoid race between cmake and link
    make -C simcoupe RASPPI="$RASPPI" -j1 2>&1 | \
        grep -E "CLEAN|CPP|CC|AR|LD|COPY|WC|error:|warning:|CMAKE" || true

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
    # Busca volúmenes FAT montados que tengan bootcode.bin (típico de RPi)
    for vol in /Volumes/*/; do
        if [ -f "${vol}bootcode.bin" ] || [ -f "${vol}BOOTCODE.BIN" ]; then
            info "SD card detectada en $vol — copiando $KERNEL y config.txt..."
            cp -f "simcoupe/$KERNEL" "$vol"
            cp -f "simcoupe/sdcard/config.txt" "$vol"
            ok "Archivos copiados a $vol"
            return 0
        fi
    done
    warn "No se detectó SD card montada. Copia manualmente:"
    echo "   cp simcoupe/$KERNEL /Volumes/<tu-sd>/"
    echo "   cp simcoupe/sdcard/config.txt /Volumes/<tu-sd>/"
}

# ---- Main ----
TARGET="${1:-pi3}"

check_deps
ensure_circle_config

case "$TARGET" in
    pi2)
        build_platform 2
        copy_to_sdcard "kernel7.img"
        ;;
    pi3)
        build_platform 3
        copy_to_sdcard "kernel8-32.img"
        ;;
    all)
        build_platform 3
        build_platform 2
        echo ""
        ok "Ambos kernels generados:"
        ls -lh simcoupe/kernel7.img simcoupe/kernel8-32.img 2>/dev/null || true
        echo ""
        warn "Para la SD card necesitas ambos ficheros + config.txt:"
        echo "   simcoupe/kernel7.img"
        echo "   simcoupe/kernel8-32.img"
        echo "   simcoupe/sdcard/config.txt"
        echo "   circle/boot/bootcode.bin"
        echo "   circle/boot/start.elf"
        echo "   circle/boot/fixup.dat"
        ;;
    clean)
        do_clean
        ;;
    *)
        echo "Uso: $0 [pi2|pi3|all|clean]"
        echo ""
        echo "  pi2    → kernel7.img    (Raspberry Pi 2B, Cortex-A7)"
        echo "  pi3    → kernel8-32.img (Raspberry Pi 3B, Cortex-A53)  [default]"
        echo "  all    → ambos kernels"
        echo "  clean  → limpia todo para empezar desde cero"
        exit 1
        ;;
esac

echo ""
ok "Listo."
