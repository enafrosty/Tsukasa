/*
 * Project Tsukasa — lwIP platform glue: time base and IRQ-safe protection
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

#include "lwip/opt.h"
#include "lwip/arch.h"

#include "../drv/pit.h"

uint32_t sys_now(void)
{
    uint32_t hz = pit_frequency();
    if (hz == 0)
        hz = 100;
    return (uint32_t)((pit_ticks() * 1000u) / hz);
}

sys_prot_t sys_arch_protect(void)
{
    uint64_t flags;
    __asm__ volatile ("pushfq\n\tpopq %0\n\tcli" : "=r"(flags) : : "memory");
    return (sys_prot_t)flags;
}

void sys_arch_unprotect(sys_prot_t pval)
{
    if (pval & 0x200)
        __asm__ volatile ("sti" : : : "memory");
}
