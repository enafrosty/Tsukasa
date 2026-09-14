/*
 * Project Tsukasa — GDT setup with user code/data segments for Ring 3 and a hardware TSS
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

#include "../include/gdt.h"
#include <stdint.h>

struct gdt_entry {
    uint16_t limit_lo;
    uint16_t base_lo;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_hi;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/* 32-bit TSS used only for privilege stack switching on interrupts. */
struct tss_entry {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax, ecx, edx, ebx;
    uint32_t esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

/* GDT layout: 0: null 1: kernel CS 2: kernel DS 3: user   CS 4: user   DS 5: TSS */
static struct gdt_entry gdt[6];
static struct gdt_ptr gdtp;
static struct tss_entry tss;

/* Kernel stack top from boot.s (stack_top label). */
extern char stack_top[];

static void gdt_set_entry(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    gdt[i].base_lo   = (uint16_t)(base & 0xFFFF);
    gdt[i].base_mid  = (uint8_t)((base >> 16) & 0xFF);
    gdt[i].base_hi   = (uint8_t)((base >> 24) & 0xFF);
    gdt[i].limit_lo  = (uint16_t)(limit & 0xFFFF);
    gdt[i].granularity = (uint8_t)((limit >> 16) & 0x0F);
    gdt[i].granularity |= gran & 0xF0;
    gdt[i].access    = access;
}

static void gdt_write_tss(int i)
{
    uint32_t base  = (uint32_t)&tss;
    uint32_t limit = sizeof(tss) - 1;

    gdt_set_entry(i, base, limit, 0x89u, 0x00u);
}

void gdt_init(void)
{
    gdtp.limit = sizeof(gdt) - 1;
    gdtp.base  = (uint32_t)&gdt;

    gdt_set_entry(0, 0, 0, 0, 0);
    gdt_set_entry(1, 0, 0xFFFFFFFFu, 0x9Au, 0xCFu);
    gdt_set_entry(2, 0, 0xFFFFFFFFu, 0x92u, 0xCFu);
    gdt_set_entry(3, 0, 0xFFFFFFFFu, 0xFAu, 0xCFu);
    gdt_set_entry(4, 0, 0xFFFFFFFFu, 0xF2u, 0xCFu);

    {
        uint8_t *p = (uint8_t *)&tss;
        for (uint32_t i = 0; i < sizeof(tss); i++)
            p[i] = 0;
    }
    tss.ss0 = GDT_KERNEL_DS;
    tss.esp0 = (uint32_t)stack_top;
    tss.cs = GDT_KERNEL_CS | 0;
    tss.ss = GDT_KERNEL_DS | 0;
    tss.ds = GDT_KERNEL_DS | 0;
    tss.es = GDT_KERNEL_DS | 0;
    tss.fs = GDT_KERNEL_DS | 0;
    tss.gs = GDT_KERNEL_DS | 0;
    tss.iomap_base = sizeof(tss);

    gdt_write_tss(5);

    __asm__ volatile (
        "lgdt %0\n"
        "ljmp $0x08, $1f\n"
        "1:\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        :
        : "m"(gdtp)
        : "eax"
    );

    uint16_t tss_sel = GDT_TSS;
    __asm__ volatile ("ltr %0" : : "r"(tss_sel));
}
