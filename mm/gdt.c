/*
 * gdt.c - GDT setup with user code/data segments for Ring 3.
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

static struct gdt_entry gdt[6];
static struct gdt_ptr gdtp;

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

void gdt_init(void)
{
    gdtp.limit = sizeof(gdt) - 1;
    gdtp.base  = (uint32_t)&gdt;

    gdt_set_entry(0, 0, 0, 0, 0);
    gdt_set_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); /* kernel CS */
    gdt_set_entry(2, 0, 0xFFFFFFFF, 0x92, 0xCF); /* kernel DS */
    gdt_set_entry(3, 0, 0xFFFFFFFF, 0xFA, 0xCF); /* user CS DPL=3 */
    gdt_set_entry(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); /* user DS DPL=3 */
    gdt_set_entry(5, 0, 0, 0, 0);

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
}
