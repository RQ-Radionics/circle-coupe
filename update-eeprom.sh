#!/usr/bin/env bash
#
# update-eeprom.sh — Backup SD, flash Raspberry Pi OS, update EEPROM, restore
#
# Usage:
#   ./update-eeprom.sh backup      → rsync SD contents to sd-backup/
#   ./update-eeprom.sh flash       → download & flash RPi OS to SD
#   ./update-eeprom.sh restore     → format SD as FAT32 and rsync backup back
#
# After flashing RPi OS:
#   1. Boot Pi 400 with the SD card
#   2. Run: sudo rpi-eeprom-update -a && sudo reboot
#   3. After reboot, verify: vcgencmd bootloader_version
#   4. Shutdown, bring SD back
#   5. Run: ./update-eeprom.sh restore
#

set -euo pipefail

# ---- Config ----
BACKUP_DIR="$(pwd)/sd-backup"
RPIOS_URL="https://downloads.raspberrypi.com/raspios_lite_armhf/images/raspios_lite_armhf-2024-11-19/2024-11-19-raspios-bookworm-armhf-lite.img.xz"
RPIOS_XZ="$BACKUP_DIR/raspios-lite.img.xz"
RPIOS_IMG="$BACKUP_DIR/raspios-lite.img"

# ---- Colors ----
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${CYAN}▸${NC} $*"; }
ok()    { echo -e "${GREEN}✔${NC} $*"; }
warn()  { echo -e "${YELLOW}⚠${NC} $*"; }
fail()  { echo -e "${RED}✖${NC} $*"; exit 1; }

# ---- Find mounted SD volume ----
# Returns SD_VOL (mount point) and SD_DISK (whole disk identifier)
find_sd_volume() {
    local volumes=()
    local disks=()

    # Look for FAT volumes on external physical disks
    while IFS= read -r line; do
        if [[ "$line" =~ ^/dev/(disk[0-9]+) ]]; then
            local d="${BASH_REMATCH[1]}"
            # Check size — skip disks > 64GB
            local bytes
            bytes=$(diskutil info "/dev/$d" 2>/dev/null | grep "Disk Size" | grep -oE '[0-9]+ Bytes' | awk '{print $1}' || echo "0")
            [ "$bytes" -gt 0 ] 2>/dev/null && [ "$bytes" -le 68719476736 ] 2>/dev/null || continue

            # Find its mounted volume
            local vol
            vol=$(diskutil info "/dev/${d}" 2>/dev/null | grep "Mount Point:" | sed 's/.*Mount Point: *//' || true)
            # If whole disk not mounted, try partition s1
            if [ -z "$vol" ] || [ "$vol" = "" ]; then
                vol=$(diskutil info "/dev/${d}s1" 2>/dev/null | grep "Mount Point:" | sed 's/.*Mount Point: *//' || true)
            fi
            if [ -n "$vol" ] && [ -d "$vol" ]; then
                volumes+=("$vol")
                disks+=("$d")
            fi
        fi
    done < <(diskutil list external physical 2>/dev/null)

    if [ ${#volumes[@]} -eq 0 ]; then
        fail "No mounted SD card found (external disk ≤64GB with a mounted volume).\n  Is the SD card inserted and mounted?"
    elif [ ${#volumes[@]} -eq 1 ]; then
        SD_VOL="${volumes[0]}"
        SD_DISK="${disks[0]}"
    else
        echo ""
        warn "Multiple SD candidates:"
        local i=1
        for v in "${volumes[@]}"; do
            local sz
            sz=$(diskutil info "/dev/${disks[$((i-1))]}" 2>/dev/null | grep "Disk Size:" | sed 's/.*Disk Size: *//' | cut -d'(' -f1 || echo "?")
            echo "  $i) $v  (/dev/${disks[$((i-1))]}, ${sz})"
            ((i++))
        done
        echo ""
        read -rp "Which one? (1-${#volumes[@]}): " choice
        SD_VOL="${volumes[$((choice-1))]}"
        SD_DISK="${disks[$((choice-1))]}"
    fi

    SD_DEV="/dev/$SD_DISK"
    SD_RDEV="/dev/r$SD_DISK"

    local file_count
    file_count=$(find "$SD_VOL" -maxdepth 1 -type f 2>/dev/null | wc -l | tr -d ' ')
    info "SD card: $SD_VOL (/dev/$SD_DISK) — $file_count files in root"
}

confirm() {
    echo -e "${YELLOW}$1${NC}"
    read -rp "Continue? (yes/no): " answer
    [ "$answer" = "yes" ] || fail "Cancelled."
}

# ---- Backup (rsync) ----
do_backup() {
    find_sd_volume
    mkdir -p "$BACKUP_DIR/files"

    if [ -d "$BACKUP_DIR/files" ] && [ "$(ls -A "$BACKUP_DIR/files" 2>/dev/null)" ]; then
        warn "Previous backup exists in $BACKUP_DIR/files/"
        ls -lh "$BACKUP_DIR/files/" | head -20
        echo ""
        read -rp "Overwrite? (yes/no): " answer
        [ "$answer" = "yes" ] || fail "Cancelled."
    fi

    info "Backing up $SD_VOL/ → $BACKUP_DIR/files/"
    rsync -av --delete "$SD_VOL/" "$BACKUP_DIR/files/"

    local file_count
    file_count=$(find "$BACKUP_DIR/files" -type f | wc -l | tr -d ' ')
    ok "Backup complete: $file_count files in $BACKUP_DIR/files/"
    echo ""
    info "You can now run: ./update-eeprom.sh flash"
}

# ---- Flash RPi OS ----
do_flash() {
    find_sd_volume
    mkdir -p "$BACKUP_DIR"

    # Verify backup exists
    if [ ! -d "$BACKUP_DIR/files" ] || [ -z "$(ls -A "$BACKUP_DIR/files" 2>/dev/null)" ]; then
        fail "No backup found. Run './update-eeprom.sh backup' first!"
    fi
    ok "Backup verified in $BACKUP_DIR/files/"

    # Download RPi OS if needed
    if [ -f "$RPIOS_IMG" ]; then
        ok "RPi OS image already available: $RPIOS_IMG"
    else
        if [ ! -f "$RPIOS_XZ" ]; then
            info "Downloading Raspberry Pi OS Lite..."
            curl -L --progress-bar -o "$RPIOS_XZ" "$RPIOS_URL"
            ok "Download complete"
        fi
        info "Decompressing..."
        xz -dk "$RPIOS_XZ"
        ok "Decompressed to $RPIOS_IMG"
    fi

    confirm "Will ERASE $SD_DEV and write Raspberry Pi OS to it"

    info "Unmounting $SD_DEV..."
    diskutil unmountDisk "$SD_DEV" || true

    info "Writing RPi OS to $SD_RDEV..."
    sudo dd if="$RPIOS_IMG" of="$SD_RDEV" bs=4m status=progress
    sync

    # Re-mount to enable SSH
    sleep 2
    diskutil mountDisk "$SD_DEV" 2>/dev/null || true
    sleep 2

    # Find boot partition
    local boot_vol=""
    for vol in /Volumes/bootfs /Volumes/boot; do
        [ -d "$vol" ] && boot_vol="$vol" && break
    done

    if [ -n "$boot_vol" ]; then
        touch "$boot_vol/ssh"
        ok "SSH enabled on $boot_vol"
    else
        warn "Could not find boot partition to enable SSH"
    fi

    diskutil unmountDisk "$SD_DEV" || true

    ok "Raspberry Pi OS written to SD card"
    echo ""
    echo -e "${CYAN}═══════════════════════════════════════════════════${NC}"
    echo -e "${CYAN} Next steps:${NC}"
    echo -e "${CYAN}═══════════════════════════════════════════════════${NC}"
    echo "  1. Insert SD in Pi 400, connect keyboard + monitor"
    echo "  2. Boot and wait (~90s for first boot)"
    echo "  3. Login:  pi / raspberry"
    echo "  4. Run:"
    echo -e "     ${GREEN}sudo rpi-eeprom-update -a${NC}"
    echo -e "     ${GREEN}sudo reboot${NC}"
    echo "  5. After reboot, verify:"
    echo -e "     ${GREEN}vcgencmd bootloader_version${NC}"
    echo "  6. Shutdown:  sudo poweroff"
    echo "  7. Bring SD back and run:"
    echo -e "     ${GREEN}./update-eeprom.sh restore${NC}"
    echo ""
}

# ---- Restore (format FAT32 + rsync) ----
do_restore() {
    find_sd_volume

    if [ ! -d "$BACKUP_DIR/files" ] || [ -z "$(ls -A "$BACKUP_DIR/files" 2>/dev/null)" ]; then
        fail "No backup found in $BACKUP_DIR/files/"
    fi

    local file_count
    file_count=$(find "$BACKUP_DIR/files" -type f | wc -l | tr -d ' ')

    confirm "Will ERASE $SD_DEV, format as FAT32 (PICOUPE), and restore $file_count files"

    info "Unmounting $SD_DEV..."
    diskutil unmountDisk "$SD_DEV" || true

    info "Formatting $SD_DEV as FAT32 (PICOUPE)..."
    diskutil eraseDisk FAT32 PICOUPE MBRFormat "$SD_DEV"

    # Wait for mount
    sleep 2
    local mount_point="/Volumes/PICOUPE"
    if [ ! -d "$mount_point" ]; then
        sleep 3
    fi
    [ -d "$mount_point" ] || fail "FAT32 volume not mounted at $mount_point"

    info "Restoring files to $mount_point/"
    rsync -av "$BACKUP_DIR/files/" "$mount_point/"
    sync

    ok "SD card restored — $file_count files on $mount_point/"
    echo ""
    info "Your circle-coupe SD card is back. Try booting the Pi 400!"
}

# ---- Main ----
case "${1:-}" in
    backup)
        echo ""
        echo -e "${CYAN}══════════════════════════════════════${NC}"
        echo -e "${CYAN} Step 1/3: Backup SD card${NC}"
        echo -e "${CYAN}══════════════════════════════════════${NC}"
        do_backup
        ;;
    flash)
        echo ""
        echo -e "${CYAN}══════════════════════════════════════${NC}"
        echo -e "${CYAN} Step 2/3: Flash Raspberry Pi OS${NC}"
        echo -e "${CYAN}══════════════════════════════════════${NC}"
        do_flash
        ;;
    restore)
        echo ""
        echo -e "${CYAN}══════════════════════════════════════${NC}"
        echo -e "${CYAN} Step 3/3: Restore backup${NC}"
        echo -e "${CYAN}══════════════════════════════════════${NC}"
        do_restore
        ;;
    *)
        echo "Usage: $0 {backup|flash|restore}"
        echo ""
        echo "  backup   → rsync SD card files to sd-backup/files/"
        echo "  flash    → download RPi OS Lite and dd it to SD"
        echo "  restore  → format SD as FAT32 and rsync backup back"
        echo ""
        echo "Workflow:"
        echo "  1. ./update-eeprom.sh backup"
        echo "  2. ./update-eeprom.sh flash"
        echo "  3. Boot Pi 400, run: sudo rpi-eeprom-update -a && sudo reboot"
        echo "  4. Verify: vcgencmd bootloader_version"
        echo "  5. Shutdown Pi 400, bring SD back"
        echo "  6. ./update-eeprom.sh restore"
        exit 1
        ;;
esac
