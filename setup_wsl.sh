#!/bin/bash
# =============================================================================
# setup_wsl.sh - Install build tools for building a simple OS in WSL (Ubuntu/Debian)
# Run this once before building the OS:  bash setup_wsl.sh   (or chmod +x && ./setup_wsl.sh)
# =============================================================================

set -e

echo "[*] Updating package lists..."
sudo apt-get update -qq

echo "[*] Installing build tools for OS development..."
sudo apt-get install -y \
    build-essential \
    nasm \
    xorriso \
    grub-common \
    grub-pc-bin

echo "[*] Verifying installations..."
gcc --version | head -1
nasm -v
xorriso --version 2>/dev/null | head -1 || true
grub-mkrescue --version 2>/dev/null || true

echo ""
echo "[OK] Setup complete. You can now run: make iso"
echo "     Then load tsukasa.iso in VirtualBox as a new VM (Other -> Other/Unknown)."
