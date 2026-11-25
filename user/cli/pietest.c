/*
 * Project Tsukasa — Freestanding ET_DYN PIE execution and brk test binary
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

static volatile uint64_t g_pie_sentinel = 0x1122334455667788ULL;

static int64_t pie_syscall1(int64_t nr, int64_t a1)
{
    int64_t ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(nr), "D"(a1)
                      : "rcx", "r11", "memory");
    return ret;
}

int pie_main(uint64_t *sp) __attribute__((used));

int pie_main(uint64_t *sp)
{
    uint64_t argc = sp[0];
    uint64_t *p = &sp[1 + argc + 1];
    while (*p != 0)
        p++;
    p++;

    uint64_t pagesz = 0, phdr = 0, base = 99, entry = 0;
    for (uint64_t i = 0; p[i] != AT_NULL; i += 2) {
        if (p[i] == AT_PHDR)
            phdr = p[i + 1];
        else if (p[i] == AT_PAGESZ)
            pagesz = p[i + 1];
        else if (p[i] == AT_BASE)
            base = p[i + 1];
        else if (p[i] == AT_ENTRY)
            entry = p[i + 1];
    }

    if (pagesz != 4096ULL || base != 0ULL)
        return 1;
    if (phdr < 0x0000555555554000ULL || entry < 0x0000555555554000ULL)
        return 2;
    if (g_pie_sentinel != 0x1122334455667788ULL)
        return 3;

    int64_t cur_brk = pie_syscall1(12, 0);
    if (cur_brk < 0x0000555555554000LL)
        return 4;
    int64_t next_brk = pie_syscall1(12, cur_brk + 4096);
    if (next_brk != cur_brk + 4096)
        return 5;

    volatile uint64_t *heap_word = (volatile uint64_t *)(uintptr_t)cur_brk;
    *heap_word = 0xCAFEBABE12345678ULL;
    if (*heap_word != 0xCAFEBABE12345678ULL)
        return 6;

    return 42;
}

__asm__(
    ".global _start\n"
    "_start:\n"
    "  mov %rsp, %rdi\n"
    "  xor %rbp, %rbp\n"
    "  call pie_main\n"
    "  mov %eax, %edi\n"
    "  mov $60, %eax\n"
    "  syscall\n"
    "  hlt\n"
);
