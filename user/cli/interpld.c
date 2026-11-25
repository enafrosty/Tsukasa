/*
 * Project Tsukasa — Minimal PT_INTERP test interpreter validating AT_BASE/AT_ENTRY
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
#define AT_PHDR   3ULL
#define AT_PAGESZ 6ULL
#define AT_BASE   7ULL
#define AT_ENTRY  9ULL

uintptr_t interp_main(uint64_t *sp) __attribute__((used));

uintptr_t interp_main(uint64_t *sp)
{
    uint64_t argc = sp[0];
    uint64_t *p = &sp[1 + argc + 1];
    while (*p != 0)
        p++;
    p++;

    uint64_t pagesz = 0, phdr = 0, base = 0, entry = 0;
    for (uint64_t i = 0; p[i] != AT_NULL; i += 2) {
        if (p[i] == AT_PAGESZ)
            pagesz = p[i + 1];
        else if (p[i] == AT_PHDR)
            phdr = p[i + 1];
        else if (p[i] == AT_BASE)
            base = p[i + 1];
        else if (p[i] == AT_ENTRY)
            entry = p[i + 1];
    }

    if (pagesz != 4096ULL || phdr == 0 || base != 0x00007F0000000000ULL)
        return 0;
    if (entry < 0x0000555555554000ULL)
        return 0;

    return (uintptr_t)entry;
}

__asm__(
    ".global _start\n"
    "_start:\n"
    "  mov %rsp, %rdi\n"
    "  push %rsp\n"
    "  xor %rbp, %rbp\n"
    "  sub $8, %rsp\n"
    "  call interp_main\n"
    "  add $8, %rsp\n"
    "  pop %rsp\n"
    "  test %rax, %rax\n"
    "  jz 1f\n"
    "  jmp *%rax\n"
    "1:\n"
    "  mov $98, %edi\n"
    "  mov $60, %eax\n"
    "  syscall\n"
    "  hlt\n"
);
