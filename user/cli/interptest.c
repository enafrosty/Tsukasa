/*
 * Project Tsukasa — Dynamically interpreted PIE test binary (PT_INTERP)
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

#include <stdint.h>

#define AT_NULL   0ULL
#define AT_BASE   7ULL

int interptest_main(uint64_t *sp) __attribute__((used));

int interptest_main(uint64_t *sp)
{
    uint64_t argc = sp[0];
    if (argc < 1)
        return 1;
    uint64_t *p = &sp[1 + argc + 1];
    while (*p != 0)
        p++;
    p++;

    uint64_t base = 0;
    for (uint64_t i = 0; p[i] != AT_NULL; i += 2) {
        if (p[i] == AT_BASE)
            base = p[i + 1];
    }
    if (base != 0x00007F0000000000ULL)
        return 2;
    return 77;
}

__asm__(
    ".global _start\n"
    "_start:\n"
    "  mov %rsp, %rdi\n"
    "  xor %rbp, %rbp\n"
    "  call interptest_main\n"
    "  mov %eax, %edi\n"
    "  mov $60, %eax\n"
    "  syscall\n"
    "  hlt\n"
);
