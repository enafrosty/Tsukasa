#!/usr/bin/env bash
# Run Tsukasa OS in QEMU (x86_64 Limine ISO)
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

ISO_IMAGE="tsukasa.iso"
DISK_IMAGE="disk.img"

if [ ! -f "$ISO_IMAGE" ]; then
    echo "[!] $ISO_IMAGE not found. Building ISO first..."
    make initrd
    make ARCH=x86_64 iso
fi

if [ ! -f "$DISK_IMAGE" ]; then
    echo "[*] Creating 64MB sparse virtual hard disk: $DISK_IMAGE..."
    qemu-img create -f raw "$DISK_IMAGE" 64M
fi

echo "[*] Launching Tsukasa in QEMU..."
exec qemu-system-x86_64 \
    -cdrom "$ISO_IMAGE" \
    -hda "$DISK_IMAGE" \
    -boot d \
    -m 256 \
    -smp 2 \
    -vga std \
    -serial stdio \
    -netdev user,id=u1 \
    -device e1000,netdev=u1

