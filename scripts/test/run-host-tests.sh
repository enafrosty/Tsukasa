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

# 2. Vanilla developer tools and debug protocol tests
$CC -O2 \
    -I"${REPO_ROOT}/vanilla/host_emulator/sys_compat" \
    -I"${REPO_ROOT}/vanilla/include" \
    -I"${REPO_ROOT}/scripts/test" \
    -I"${REPO_ROOT}/vanilla/server" \
    -I"${REPO_ROOT}/vanilla/devtools/vendor/microui" \
    -DVANILLA_HOST \
    -o "${BUILD_DIR}/test_devtools" \
    "${REPO_ROOT}/vanilla/tests/test_devtools.c" \
    "${REPO_ROOT}/vanilla/server/blitter.c" \
    "${REPO_ROOT}/vanilla/server/compositor.c" \
    "${REPO_ROOT}/vanilla/server/font.c" \
    "${REPO_ROOT}/vanilla/server/default_font.c" \
    "${REPO_ROOT}/vanilla/server/image.c" \
    "${REPO_ROOT}/vanilla/server/theme.c" \
    "${REPO_ROOT}/vanilla/devtools/vendor/microui/microui.c" \
    "${REPO_ROOT}/vanilla/host_emulator/host_compat.c" \
    -lm

"${BUILD_DIR}/test_devtools" 2>&1 | tee -a "$LOG"

# 3. Vanilla layout engine tests
$CC -O2 \
    -idirafter "${REPO_ROOT}/vanilla/host_emulator/sys_compat" \
    -I"${REPO_ROOT}/vanilla/include" \
    -I"${REPO_ROOT}/vanilla/input" \
    -I"${REPO_ROOT}/vanilla/server" \
    -I"${REPO_ROOT}/vanilla/apps" \
    -I"${REPO_ROOT}/scripts/test" \
    -DVANILLA_HOST \
    -o "${BUILD_DIR}/test_layout" \
    "${REPO_ROOT}/vanilla/layout/test_layout.c" \
    "${REPO_ROOT}/vanilla/layout/layout.c" \
    "${REPO_ROOT}/vanilla/layout/draw_consumer.c" \
    "${REPO_ROOT}/vanilla/apps/app_common.c" \
    "${REPO_ROOT}/vanilla/server/font.c" \
    "${REPO_ROOT}/vanilla/server/default_font.c" \
    "${REPO_ROOT}/vanilla/server/blitter.c" \
    "${REPO_ROOT}/vanilla/server/theme.c" \
    -lm

"${BUILD_DIR}/test_layout" 2>&1 | tee -a "$LOG"

# 4. Vanilla retained widget toolkit tests
$CC -O2 \
    -idirafter "${REPO_ROOT}/vanilla/host_emulator/sys_compat" \
    -I"${REPO_ROOT}/vanilla/include" \
    -I"${REPO_ROOT}/vanilla/input" \
    -I"${REPO_ROOT}/vanilla/server" \
    -I"${REPO_ROOT}/vanilla/apps" \
    -I"${REPO_ROOT}/vanilla/libvanilla-ui" \
    -DVANILLA_HOST \
    -o "${BUILD_DIR}/test_ui" \
    "${REPO_ROOT}/vanilla/libvanilla-ui/test_ui.c" \
    "${REPO_ROOT}/vanilla/libvanilla-ui/ui_ctx.c" \
    "${REPO_ROOT}/vanilla/libvanilla-ui/ui_tree.c" \
    "${REPO_ROOT}/vanilla/libvanilla-ui/ui_event.c" \
    "${REPO_ROOT}/vanilla/libvanilla-ui/ui_render.c" \
    "${REPO_ROOT}/vanilla/libvanilla-ui/ui_text_field.c" \
    "${REPO_ROOT}/vanilla/libvanilla-ui/ui_widgets.c" \
    "${REPO_ROOT}/vanilla/layout/layout.c" \
    "${REPO_ROOT}/vanilla/layout/draw_consumer.c" \
    "${REPO_ROOT}/vanilla/apps/app_common.c" \
    "${REPO_ROOT}/vanilla/server/font.c" \
    "${REPO_ROOT}/vanilla/server/default_font.c" \
    "${REPO_ROOT}/vanilla/server/blitter.c" \
    "${REPO_ROOT}/vanilla/server/theme.c" \
    "${REPO_ROOT}/vanilla/input/keymap.c" \
    -lm

"${BUILD_DIR}/test_ui" 2>&1 | tee -a "$LOG"

# 5. Vanilla service registry daemon tests
$CC -O2 \
    -I"${REPO_ROOT}/vanilla/include" \
    -I"${REPO_ROOT}/vanilla/services" \
    -I"${REPO_ROOT}/vanilla/libvanilla" \
    -I"${REPO_ROOT}/scripts/test" \
    -DVANILLA_HOST -DVREG_TEST_NO_MAIN \
    -o "${BUILD_DIR}/test_service_registry" \
    "${REPO_ROOT}/vanilla/tests/test_service_registry.c" \
    "${REPO_ROOT}/vanilla/libvanilla/service.c" \
    "${REPO_ROOT}/vanilla/services/registryd.c" \
    -lpthread

"${BUILD_DIR}/test_service_registry" 2>&1 | tee -a "$LOG"

# 6. Vanilla clipboard daemon tests
$CC -O2 \
    -I"${REPO_ROOT}/vanilla/include" \
    -I"${REPO_ROOT}/vanilla/services" \
    -I"${REPO_ROOT}/vanilla/libvanilla" \
    -I"${REPO_ROOT}/scripts/test" \
    -DVANILLA_HOST -DVCLIP_TEST_NO_MAIN -DVREG_TEST_NO_MAIN \
    -o "${BUILD_DIR}/test_clipboard" \
    "${REPO_ROOT}/vanilla/tests/test_clipboard.c" \
    "${REPO_ROOT}/vanilla/libvanilla/clipboard.c" \
    "${REPO_ROOT}/vanilla/libvanilla/service.c" \
    "${REPO_ROOT}/vanilla/services/clipboardd.c" \
    -lpthread

"${BUILD_DIR}/test_clipboard" 2>&1 | tee -a "$LOG"

# 7. Vanilla terminal VT state machine tests
$CC -O2 \
    -I"${REPO_ROOT}/vanilla/apps" \
    -DVANILLA_HOST \
    -o "${BUILD_DIR}/test_terminal_vt" \
    "${REPO_ROOT}/vanilla/tests/test_terminal_vt.c" \
    "${REPO_ROOT}/vanilla/apps/terminal_vt.c"

"${BUILD_DIR}/test_terminal_vt" 2>&1 | tee -a "$LOG"

# 8. Vanilla notification daemon tests
$CC -O2 \
    -idirafter "${REPO_ROOT}/vanilla/host_emulator/sys_compat" \
    -I"${REPO_ROOT}/vanilla/include" \
    -I"${REPO_ROOT}/vanilla/input" \
    -I"${REPO_ROOT}/vanilla/services" \
    -I"${REPO_ROOT}/vanilla/libvanilla" \
    -I"${REPO_ROOT}/vanilla/server" \
    -I"${REPO_ROOT}/vanilla/apps" \
    -I"${REPO_ROOT}/scripts/test" \
    -DVANILLA_HOST -DVNOTIF_TEST_NO_MAIN -DVREG_TEST_NO_MAIN \
    -o "${BUILD_DIR}/test_notify" \
    "${REPO_ROOT}/vanilla/tests/test_notify.c" \
    "${REPO_ROOT}/vanilla/libvanilla/notify.c" \
    "${REPO_ROOT}/vanilla/libvanilla/service.c" \
    "${REPO_ROOT}/vanilla/services/notifyd.c" \
    "${REPO_ROOT}/vanilla/apps/app_common.c" \
    "${REPO_ROOT}/vanilla/server/font.c" \
    "${REPO_ROOT}/vanilla/server/default_font.c" \
    "${REPO_ROOT}/vanilla/server/theme.c" \
    -lpthread -lm

"${BUILD_DIR}/test_notify" 2>&1 | tee -a "$LOG"

# 9. LZ4 frame packer and kernel decompressor tests
$CC -O2 -Wall -Wextra -Werror \
    -o "${BUILD_DIR}/lz4pack" \
    "${REPO_ROOT}/tools/lz4pack.c"

$CC -O2 -Wall -Wextra -Werror \
    -I"${REPO_ROOT}" \
    -o "${BUILD_DIR}/test_lz4" \
    "${REPO_ROOT}/scripts/test/test_lz4.c" \
    "${REPO_ROOT}/lib/lz4.c"

"${BUILD_DIR}/test_lz4" "${BUILD_DIR}/lz4pack" 2>&1 | tee -a "$LOG"
