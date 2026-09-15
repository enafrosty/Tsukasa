#!/usr/bin/env bash
#
# Project Tsukasa — CI Toolchain Installation Script
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

LLVM_VERSION="${LLVM_VERSION:-18}"
INSTALL_SDL="${INSTALL_SDL:-0}"

for arg in "$@"; do
    case "$arg" in
        --with-sdl)
            INSTALL_SDL=1
            ;;
    esac
done

export DEBIAN_FRONTEND=noninteractive

echo "[*] Updating package indices..."
sudo apt-get update -qq

PKGS=(
    build-essential
    "clang-${LLVM_VERSION}"
    "lld-${LLVM_VERSION}"
    "llvm-${LLVM_VERSION}"
    nasm
    xorriso
    mtools
    dosfstools
    grub-common
    grub-pc-bin
    qemu-system-x86
    git
)

if [ "${INSTALL_SDL}" = "1" ] || [ "${INSTALL_SDL}" = "true" ]; then
    PKGS+=(libsdl2-dev)
fi

echo "[*] Installing build packages..."
sudo apt-get install -y --no-install-recommends "${PKGS[@]}"

echo "[*] Configuring toolchain alternatives for LLVM ${LLVM_VERSION}..."
sudo update-alternatives --install /usr/bin/clang clang "/usr/bin/clang-${LLVM_VERSION}" 100
sudo update-alternatives --install /usr/bin/clang++ clang++ "/usr/bin/clang++-${LLVM_VERSION}" 100
sudo update-alternatives --install /usr/bin/ld.lld ld.lld "/usr/bin/ld.lld-${LLVM_VERSION}" 100
sudo update-alternatives --install /usr/bin/llvm-ar llvm-ar "/usr/bin/llvm-ar-${LLVM_VERSION}" 100
sudo update-alternatives --install /usr/bin/llvm-nm llvm-nm "/usr/bin/llvm-nm-${LLVM_VERSION}" 100
sudo update-alternatives --install /usr/bin/llvm-objdump llvm-objdump "/usr/bin/llvm-objdump-${LLVM_VERSION}" 100

echo "[*] Toolchain versions:"
clang --version | head -n 1
ld.lld --version | head -n 1
nasm -v
