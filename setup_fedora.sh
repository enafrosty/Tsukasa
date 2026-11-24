#!/usr/bin/env bash
#
# Project Tsukasa - Fedora Linux Toolchain Installation Script
#
# Copyright (C) 2025-2026 frosty (@enafrosty) and Project Tsukasa contributors.
#
# Project Tsukasa was created and is maintained by frosty (@enafrosty).
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation, either version 3 of the License, or (at your
# option) any later version. See the top-level LICENSE file.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#

set -euo pipefail

PKGS=(
    clang
    lld
    llvm
    nasm
    lz4
    xorriso
    mtools
    dosfstools
    e2fsprogs
    qemu-system-x86
    qemu-img
    SDL2-devel
    gcc
    make
    git
    binutils
    gdb
    patch
    python3
)

echo "[*] Installing Project Tsukasa build and runtime dependencies via dnf..."
sudo dnf install -y "${PKGS[@]}"

echo "[*] Verifying installed tools..."
clang --version | head -n 1
ld.lld --version | head -n 1
nasm -v
lz4 --version 2>&1 | head -n 1 || true
xorriso --version 2>/dev/null | head -n 1 || true
mkfs.fat --help 2>&1 | head -n 1 || true
e2fsck -V 2>&1 | head -n 1 || true
mcopy --version | head -n 1 || true
qemu-system-x86_64 --version | head -n 1
echo "SDL2: $(pkg-config --modversion sdl2)"

echo ""
echo "[OK] Fedora setup complete. Next steps:"
echo "     make -j\$(nproc) iso-x86_64  # Build kernel, userland, initrd, and tsukasa.iso"
echo "     ./run_qemu.sh                # Boot tsukasa.iso in QEMU"
echo "     make -C vanilla host-run     # Run the Vanilla SDL2 host emulator"
echo "     make check                   # Run host and QEMU guest test suites"
