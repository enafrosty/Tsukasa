#!/usr/bin/env bash
#
# Project Tsukasa — Headless QEMU boot smoke test runner
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

ISO="${1:-tsukasa.iso}"
LOG="${2:-boot.log}"
TIMEOUT="${BOOT_TIMEOUT:-120}"

TIMEOUT_BIN="timeout"
if [ -x "/usr/bin/timeout" ]; then
    TIMEOUT_BIN="/usr/bin/timeout"
fi

qemu-img create -f raw disk.img 64M >/dev/null

set +e
"$TIMEOUT_BIN" --foreground "$TIMEOUT" qemu-system-x86_64 \
    -cdrom "$ISO" \
    -hda disk.img \
    -m 256 \
    -smp 2 \
    -vga std \
    -display none \
    -no-reboot \
    -serial stdio \
    -fw_cfg name=opt/org.tsukasa.selftest,string=1 > "$LOG" 2>&1
QEMU_RC=$?
set -e

fail() {
    echo "BOOT TEST FAILED: $1"
    echo "----- last 80 lines -----"
    tail -80 "$LOG"
    exit 1
}

MILESTONES_FILE="${SCRIPT_DIR}/boot-milestones.txt"
if [ ! -f "$MILESTONES_FILE" ]; then
    fail "milestones file not found: $MILESTONES_FILE"
fi

# 1. Required boot milestones.
while IFS= read -r pattern || [ -n "$pattern" ]; do
    pattern="$(echo "$pattern" | tr -d '\r')"
    [ -z "$pattern" ] && continue
    grep -F -q -- "$pattern" "$LOG" || fail "missing milestone: $pattern"
done < "$MILESTONES_FILE"

# 2. No self-test may report failure.
if grep -qiE '\bFAIL\b' "$LOG"; then
    fail "a self-test reported FAIL"
fi

# 3. No crash signatures.
for bad in "PANIC" "panic:" "Unhandled exception" "Page fault" "General protection" \
           "Double fault" "Triple fault"; do
    if grep -qi -- "$bad" "$LOG"; then
        fail "crash signature found: $bad"
    fi
done

# 4. QEMU must have exited cleanly (0), not been killed by timeout (124).
if [ "$QEMU_RC" -eq 124 ]; then
    fail "timed out after ${TIMEOUT}s - kernel hung or never powered off"
fi

if [ "$QEMU_RC" -ne 0 ]; then
    fail "qemu exited with status $QEMU_RC"
fi

echo "BOOT TEST PASSED"
