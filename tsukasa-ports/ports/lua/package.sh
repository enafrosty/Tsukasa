#!/usr/bin/env bash
#
# Project Tsukasa - Lua 5.4 Port Package Script
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
    if [ ! -d "${SRC_DIR}/lua-5.4.7" ]; then
        echo "[lua] Fetching lua-5.4.7..."
        (cd "${SRC_DIR}" && curl.exe -L -O https://www.lua.org/ftp/lua-5.4.7.tar.gz && tar -xzf lua-5.4.7.tar.gz)
    fi
}

do_patch() {
    true
}

do_build() {
    ${MAKE_CMD} -C "${PORT_DIR}" all
}

do_install() {
    mkdir -p "${STAGING_DIR}/bin"
    cp "${PORT_DIR}/lua.elf" "${STAGING_DIR}/bin/lua.elf"
    cp "${PORT_DIR}/luac.elf" "${STAGING_DIR}/bin/luac.elf"
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
