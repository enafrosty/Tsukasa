#include <stdint.h>
#include <stddef.h>

#include "gdt.h"

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t gran;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_tss_desc {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t gran;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

static struct {
    struct gdt_entry entries[5];
    struct gdt_tss_desc tss_desc;
} __attribute__((packed)) gdt;

static struct gdt_ptr gdtp;
static struct tss64 tss;

extern char x64_boot_stack_top[];

static void set_seg(int index, uint8_t access, uint8_t gran)
{
    gdt.entries[index].limit_low = 0;
    gdt.entries[index].base_low = 0;
    gdt.entries[index].base_mid = 0;
    gdt.entries[index].access = access;
    gdt.entries[index].gran = gran;
    gdt.entries[index].base_high = 0;
}

static void set_tss_desc(uint64_t base, uint32_t limit)
{
    gdt.tss_desc.limit_low = (uint16_t)(limit & 0xFFFFu);
    gdt.tss_desc.base_low = (uint16_t)(base & 0xFFFFu);
    gdt.tss_desc.base_mid = (uint8_t)((base >> 16) & 0xFFu);
    gdt.tss_desc.access = 0x89u;
    gdt.tss_desc.gran = (uint8_t)((limit >> 16) & 0x0Fu);
    gdt.tss_desc.base_high = (uint8_t)((base >> 24) & 0xFFu);
    gdt.tss_desc.base_upper = (uint32_t)((base >> 32) & 0xFFFFFFFFu);
    gdt.tss_desc.reserved = 0;
}

void tss_set_rsp0_x64(uint64_t rsp0)
{
    tss.rsp0 = rsp0;
}

void gdt_init_x64(void)
{
    for (size_t i = 0; i < sizeof(tss); i++)
        ((uint8_t *)&tss)[i] = 0;

    tss.rsp0 = (uint64_t)(uintptr_t)x64_boot_stack_top;
    tss.iomap_base = (uint16_t)sizeof(tss);

    set_seg(0, 0x00u, 0x00u);
    set_seg(1, 0x9Au, 0x20u);
    set_seg(2, 0x92u, 0x00u);
    set_seg(3, 0xFAu, 0x20u);
    set_seg(4, 0xF2u, 0x00u);
    set_tss_desc((uint64_t)(uintptr_t)&tss, (uint32_t)(sizeof(tss) - 1));

    gdtp.limit = (uint16_t)(sizeof(gdt) - 1);
    gdtp.base = (uint64_t)(uintptr_t)&gdt;

    __asm__ volatile (
        "lgdt %0\n"
        "movw %1, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "pushq %2\n"
        "lea 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        :
        : "m"(gdtp), "i"(X64_GDT_KERNEL_DS), "i"(X64_GDT_KERNEL_CS)
        : "rax", "memory"
    );

    {
        uint16_t tss_sel = X64_GDT_TSS;
        __asm__ volatile ("ltr %0" : : "r"(tss_sel) : "memory");
    }
}