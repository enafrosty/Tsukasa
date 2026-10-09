#!/usr/bin/env bash
#
# Project Tsukasa - Test Results Summarizer
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

LOG_FILES=()
TMP_INPUT=""

if [ "$#" -gt 0 ]; then
    if [ "$1" = "-" ]; then
        TMP_INPUT=$(mktemp /tmp/tsk-summary-XXXXXX.log)
        cat > "$TMP_INPUT"
        LOG_FILES=("$TMP_INPUT")
    else
        LOG_FILES=("$@")
    fi
else
    for f in "${REPO_ROOT}/test-host.log" "${REPO_ROOT}/test-guest.log"; do
        if [ -f "$f" ]; then
            LOG_FILES+=("$f")
        fi
    done
    if [ ${#LOG_FILES[@]} -eq 0 ] && [ -f "${REPO_ROOT}/test.log" ]; then
        LOG_FILES+=("${REPO_ROOT}/test.log")
    fi
    if [ ${#LOG_FILES[@]} -eq 0 ] && [ ! -t 0 ]; then
        TMP_INPUT=$(mktemp /tmp/tsk-summary-XXXXXX.log)
        cat > "$TMP_INPUT"
        LOG_FILES=("$TMP_INPUT")
    fi
fi

if [ ${#LOG_FILES[@]} -eq 0 ]; then
    echo "ERROR: No test logs found to summarize." >&2
    exit 1
fi

CONSOLIDATED=$(mktemp /tmp/tsk-cons-XXXXXX.log)
cleanup() {
    rm -f "$CONSOLIDATED"
    if [ -n "$TMP_INPUT" ] && [ -f "$TMP_INPUT" ]; then
        rm -f "$TMP_INPUT"
    fi
}
trap cleanup EXIT

for f in "${LOG_FILES[@]}"; do
    if [ -f "$f" ]; then
        tr -d '\r' < "$f" >> "$CONSOLIDATED"
    fi
done

passed=$(grep -a -c '^\[TEST\] .* PASS$' "$CONSOLIDATED" || true)
failed=$(grep -a -c '^\[TEST\] .* FAIL' "$CONSOLIDATED" || true)
skipped=$(grep -a -c '^\[TEST\] .* SKIP' "$CONSOLIDATED" || true)

if [ "$failed" -gt 0 ]; then
    echo "Failed tests:"
    grep -a '^\[TEST\] .* FAIL' "$CONSOLIDATED" || true
fi

grep -a -q '^\[TEST\] ALL DONE$' "$CONSOLIDATED" \
    || { echo "harness did not reach ALL DONE - run truncated"; exit 1; }

echo "check: ${passed} passed, ${failed} failed, ${skipped} skipped"
[ "$failed" -eq 0 ]
