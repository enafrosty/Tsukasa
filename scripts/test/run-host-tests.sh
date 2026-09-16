#!/usr/bin/env bash
#
# Project Tsukasa - Host-Native Test Suite Runner
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

LOG="${1:-${REPO_ROOT}/test-host.log}"
rm -f "$LOG"

CC="${CC:-gcc}"
BUILD_DIR="/tmp/tsukasa-host-tests"
mkdir -p "$BUILD_DIR"

echo "=== Running Host-Native Tests ==="

# 1. Vanilla typography and image pipeline tests
$CC -O2 -I"${REPO_ROOT}/vanilla/include" -I"${REPO_ROOT}/scripts/test" \
    -DVANILLA_HOST \
    -o "${BUILD_DIR}/test_typography" \
    "${REPO_ROOT}/vanilla/tests/test_typography.c" \
    "${REPO_ROOT}/vanilla/server/blitter.c" \
    "${REPO_ROOT}/vanilla/server/font.c" \
    "${REPO_ROOT}/vanilla/server/default_font.c" \
    "${REPO_ROOT}/vanilla/server/image.c" \
    -lm

"${BUILD_DIR}/test_typography" 2>&1 | tee "$LOG"
