/*
 * Project Tsukasa — Symmetric Multiprocessing (SMP) core interface
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

#ifndef SMP_H
#define SMP_H

#include <stdbool.h>
#include <stdint.h>

typedef struct cpu_state {
    struct cpu_state *self;
    uint32_t cpu_id;
    uint32_t lapic_id;
    uint64_t kernel_stack;
    void *kernel_stack_alloc;
    volatile bool online;
    /* SYSCALL entry scratch. */
    uint64_t syscall_rsp;
    uint64_t user_rsp;
} cpu_state_t;

/* Fixed %gs offsets used by arch/x86_64/cpu/syscall_entry.asm. */
#define X64_CPU_SYSCALL_RSP_OFF     40
#define X64_CPU_USER_RSP_OFF        48
#define X64_CPU_SYSCALL_RSP_OFF_STR "40"
#define X64_CPU_USER_RSP_OFF_STR    "48"

#define MSR_GS_BASE         0xC0000101
#define MSR_KERNEL_GS_BASE  0xC0000102

static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t low, high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static inline void wrmsr(uint32_t msr, uint64_t value)
{
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

void smp_init_bsp(void);

struct limine_smp_response;
uint32_t smp_init(struct limine_smp_response *smp_resp);
uint32_t smp_this_cpu_id(void);
uint32_t smp_cpu_count(void);
cpu_state_t *smp_get_cpu(uint32_t cpu_id);
uint32_t smp_get_lapic_id(uint32_t cpu_id);

#endif /* SMP_H */
