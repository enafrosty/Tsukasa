/*
 * Project Tsukasa — x86_64 fast SYSCALL / SYSRET initialization and entry
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
#include "include/smp.h"
#include "arch/x86_64/cpu/gdt.h"

#define MSR_EFER        0xC0000080
#define MSR_STAR        0xC0000081
#define MSR_LSTAR       0xC0000082
#define MSR_CSTAR       0xC0000083
#define MSR_SFMASK      0xC0000084

#define EFER_SCE        0x00000001 /* Syscall Enable */

extern void syscall_entry_x64(void);

void syscall_init_x64(void)
{
    /* Enable SCE (Syscall Enable) bit in IA32_EFER */
    uint64_t efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | EFER_SCE);

    /*
     * STAR MSR layout:
     * [63:48] User 32-bit CS / SYSRET base selector (X64_GDT_USER_DS - 8)
     * [47:32] Kernel CS / SYSCALL base selector (X64_GDT_KERNEL_CS)
     */
    uint64_t star = ((uint64_t)(X64_GDT_USER_DS - 8) << 48) |
                    ((uint64_t)X64_GDT_KERNEL_CS << 32);
    wrmsr(MSR_STAR, star);

    /* Set target RIP for 64-bit SYSCALL */
    wrmsr(MSR_LSTAR, (uint64_t)(uintptr_t)syscall_entry_x64);

    /* Mask RFLAGS: clear CF, PF, AF, ZF, SF, TF, IF, DF, OF, NT, RF, AC */
    wrmsr(MSR_SFMASK, 0x00044700ULL);
}
