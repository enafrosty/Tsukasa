#!/usr/bin/env bash
#
# Project Tsukasa - DOOM (doomgeneric) Port Package Script
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

set -e

PORT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STAGING_DIR="${PORT_DIR}/../../staging"
SRC_DIR="${PORT_DIR}/src"
MAKE_CMD="${MAKE:-make}"

ACTION="${1:-build}"

do_fetch() {
    mkdir -p "${SRC_DIR}"
    if [ ! -d "${SRC_DIR}/doomgeneric" ]; then
        echo "[doom] Fetching doomgeneric..."
        git clone --depth 1 https://github.com/ozkl/doomgeneric.git "${SRC_DIR}/doomgeneric"
    fi
    if [ ! -f "${SRC_DIR}/doom1.wad" ]; then
        echo "[doom] Fetching doom1.wad..."
        git clone --depth 1 https://github.com/samrude005/doom1.wad.git "${SRC_DIR}/wad_repo"
        cp "${SRC_DIR}/wad_repo/doom1.wad" "${SRC_DIR}/doom1.wad"
        rm -rf "${SRC_DIR}/wad_repo"
    fi
}

do_patch() {
    if [ -f "${SRC_DIR}/doomgeneric/d_iwad.c" ]; then
        if ! grep -q "/usr/share/doom" "${SRC_DIR}/doomgeneric/d_iwad.c"; then
            sed -i 's|AddIWADDir (FILES_DIR);|AddIWADDir("."); AddIWADDir("/usr/share/doom"); AddIWADDir("/bin"); AddIWADDir("/fat12"); AddIWADDir("/"); AddIWADDir(FILES_DIR);|g' "${SRC_DIR}/doomgeneric/d_iwad.c"
        fi
    fi
}

do_build() {
    ${MAKE_CMD} -C "${PORT_DIR}" all
}

do_install() {
    mkdir -p "${STAGING_DIR}/bin"
    mkdir -p "${STAGING_DIR}/usr/share/doom"
    cp "${PORT_DIR}/doomgeneric.elf" "${STAGING_DIR}/bin/doomgeneric.elf"
    if [ -f "${SRC_DIR}/doom1.wad" ]; then
        cp "${SRC_DIR}/doom1.wad" "${STAGING_DIR}/usr/share/doom/doom1.wad"
    fi
}

do_clean() {
    ${MAKE_CMD} -C "${PORT_DIR}" clean
}

case "${ACTION}" in
    fetch)   do_fetch ;;
    patch)   do_patch ;;
    build)   do_build ;;
    install) do_install ;;
    clean)   do_clean ;;
    all)
        do_fetch
        do_patch
        do_build
        do_install
        ;;
    *)
        echo "Unknown action: ${ACTION}" >&2
        exit 1
        ;;
esac
