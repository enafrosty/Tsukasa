#!/usr/bin/env bash
#
# Project Tsukasa - Headless QEMU Guest Test Runner
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

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

ISO="${1:-${REPO_ROOT}/tsukasa.iso}"
LOG="${2:-${REPO_ROOT}/test-guest.log}"
TIMEOUT="${TEST_TIMEOUT:-120}"

TIMEOUT_BIN="timeout"
if [ -x "/usr/bin/timeout" ]; then
    TIMEOUT_BIN="/usr/bin/timeout"
fi

echo "=== Running Guest Tests in QEMU ==="

if [ "${SKIP_BUILD:-0}" != "1" ]; then
    echo "[run-guest-tests] Building apps and ISO..."
    make -C "${REPO_ROOT}" apps
    make -C "${REPO_ROOT}" iso-x86_64
fi

if [ ! -f "$ISO" ]; then
    echo "ERROR: ISO image not found: $ISO" >&2
    exit 1
fi

qemu-img create -f raw "${REPO_ROOT}/disk.img" 64M >/dev/null

set +e
"$TIMEOUT_BIN" --foreground "$TIMEOUT" qemu-system-x86_64 \
    -cdrom "$ISO" \
    -hda "${REPO_ROOT}/disk.img" \
    -m 256 \
    -smp 2 \
    -vga std \
    -display none \
    -no-reboot \
    -serial stdio \
    -fw_cfg name=opt/org.tsukasa.test,string=1 > "$LOG" 2>&1
QEMU_RC=$?
set -e

# Display test lines collected during boot
grep -a -E '^\[TEST\]|^\[testdrv\]' "$LOG" || true

if [ "$QEMU_RC" -eq 124 ]; then
    echo "ERROR: QEMU timed out after ${TIMEOUT}s - guest hung or did not shut down" >&2
    exit 1
fi

if [ "$QEMU_RC" -ne 0 ]; then
    echo "WARNING: QEMU exited with status ${QEMU_RC}" >&2
fi
