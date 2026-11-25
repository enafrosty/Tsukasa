/*
 * Project Tsukasa — Saved Ring 3 register frame across fast SYSCALL/SYSRET
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

#ifndef TSUKASA_SYSCALL_FRAME_H
#define TSUKASA_SYSCALL_FRAME_H

#include <stdint.h>

/* Matches push order at syscall_entry_x64 in arch/x86_64/cpu/syscall_entry.asm. */
typedef struct syscall_frame {
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t rcx;   /* User RIP restored by SYSRET */
    uint64_t r11;   /* User RFLAGS restored by SYSRET */
    uint64_t rsp;   /* User RSP popped before swapgs + sysret */
} syscall_frame_t;

_Static_assert(sizeof(syscall_frame_t) == 15u * sizeof(uint64_t),
               "syscall_frame_t size must match syscall_entry_x64 push count");

#endif /* TSUKASA_SYSCALL_FRAME_H */
