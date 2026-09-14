#!/usr/bin/env bash
#
# Project Tsukasa - TinyCC (tcc) Port Package Script
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
MAKE_CMD="${MAKE:-mingw32-make}"

ACTION="${1:-build}"

do_fetch() {
    mkdir -p "${SRC_DIR}"
    if [ ! -d "${SRC_DIR}/tinycc" ]; then
        echo "[tcc] Fetching tinycc..."
        git clone --depth 1 https://github.com/TinyCC/tinycc.git "${SRC_DIR}/tinycc"
    fi
}

do_patch() {
    cat << 'EOF' > "${SRC_DIR}/tinycc/config.h"
#ifndef CONFIG_H
#define CONFIG_H

#define TCC_VERSION "0.9.27"
#define TCC_TARGET_X86_64 1
#define CONFIG_TCCDIR "/usr/lib/tcc"
#define CONFIG_SYSROOT ""
#define CONFIG_TCC_CRTPATH "/usr/lib"
#define CONFIG_TCC_LIBPATHS "/usr/lib"
#define CONFIG_TCC_SYSINCLUDEPATHS "/usr/include"
#define CONFIG_TCC_STATIC 1
#define CONFIG_TCC_SEMLOCK 0
#define CONFIG_TCC_BACKTRACE 0
#define ONE_SOURCE 1
#define ldexpl ldexp

#endif
EOF
}

do_build() {
    ${MAKE_CMD} -C "${PORT_DIR}" all
}

do_install() {
    mkdir -p "${STAGING_DIR}/bin"
    mkdir -p "${STAGING_DIR}/usr/include"
    mkdir -p "${STAGING_DIR}/usr/lib"
    cp "${PORT_DIR}/tcc.elf" "${STAGING_DIR}/bin/tcc.elf"

    # Stage Tsukasa C runtime SDK into staging/usr/ for self-hosting compilation
    local LIBC_DIR="${PORT_DIR}/../../../tsukasa-libc"
    cp -r "${LIBC_DIR}/include/"* "${STAGING_DIR}/usr/include/"
    cp "${LIBC_DIR}/crt/crt0.o" "${STAGING_DIR}/usr/lib/"
    cp "${LIBC_DIR}/lib/libc.a" "${STAGING_DIR}/usr/lib/"
    cp "${LIBC_DIR}/lib/libm.a" "${STAGING_DIR}/usr/lib/"
    cp "${LIBC_DIR}/tsukasa_app.ld" "${STAGING_DIR}/usr/lib/"
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
