/*
 * Project Tsukasa — Kernel panic and crash report handler
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

#ifndef TSUKASA_PANIC_H
#define TSUKASA_PANIC_H

#include <stdint.h>
#include <stdbool.h>

/* Matches the stack layout built by isr_exception_common in arch/x86_64/cpu/isr.asm: PUSH_GPRS (rax last)... */
typedef struct interrupt_frame {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t int_no;
    uint64_t err_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} interrupt_frame_t;

const char *exception_name(uint8_t vector);
void kernel_panic(interrupt_frame_t *regs, const char *error_name) __attribute__((noreturn));

#endif
