#!/usr/bin/env bash
#
# Project Tsukasa — Kernel Symbol Table Generator
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

ELF="${1:-tsukasa_x64.elf}"
OUT="${2:-lib/ksymbols_table.c}"

{
    cat << 'EOF'
/*
 * Project Tsukasa — Kernel Symbol Table (Auto-generated)
 *
 * Copyright (C) 2025-2026 frosty (@enafrosty) and Project Tsukasa contributors.
 *
 * Project Tsukasa was created and is maintained by frosty (@enafrosty).
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. See the top-level LICENSE file.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include "include/ksymbols.h"

const struct ksym ksym_table[] = {
EOF

    nm -n --defined-only "$ELF" \
        | awk '$2 == "t" || $2 == "T" { if (length($3) > 0 && $3 !~ /^\./) printf "    { 0x%sULL, \"%s\" },\n", $1, $3 }'

    cat << 'EOF'
};

const unsigned long ksym_count = sizeof(ksym_table) / sizeof(ksym_table[0]);
EOF
} > "$OUT"
