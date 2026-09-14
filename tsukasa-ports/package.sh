#!/usr/bin/env bash
#
# Project Tsukasa - Master Ports Package Manager
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

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PORTS_DIR="${SCRIPT_DIR}/ports"
STAGING_DIR="${SCRIPT_DIR}/staging"

REGISTERED_PORTS=("doom" "tcc" "lua")

mkdir -p "${STAGING_DIR}/bin"
mkdir -p "${STAGING_DIR}/usr/include"
mkdir -p "${STAGING_DIR}/usr/lib"
mkdir -p "${STAGING_DIR}/usr/share"

PORT="${1:-all}"
ACTION="${2:-build}"

run_port_action() {
    local p="$1"
    local act="$2"
    local port_script="${PORTS_DIR}/${p}/package.sh"

    if [ ! -f "${port_script}" ]; then
        echo "[ports] Error: port script ${port_script} not found" >&2
        return 1
    fi

    echo "[ports] Executing ${act} for port: ${p}"
    (cd "${PORTS_DIR}/${p}" && bash package.sh "${act}")
}

if [ "${PORT}" = "all" ]; then
    if [ "${ACTION}" = "all" ]; then
        for p in "${REGISTERED_PORTS[@]}"; do
            for a in fetch patch build install; do
                run_port_action "${p}" "${a}"
            done
        done
    else
        for p in "${REGISTERED_PORTS[@]}"; do
            run_port_action "${p}" "${ACTION}"
        done
    fi
else
    run_port_action "${PORT}" "${ACTION}"
fi
