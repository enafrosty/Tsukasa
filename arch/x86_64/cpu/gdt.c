#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "gdt.h"
#include "mm/heap.h"
#include "mm/pmm.h"
#include "mm/vmm_x64.h"

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

#define X64_GDT_MAX_APS 64

static struct {
    struct gdt_entry entries[5];
    struct gdt_tss_desc tss_desc;
    struct gdt_tss_desc ap_tss_desc[X64_GDT_MAX_APS];
} __attribute__((packed)) gdt;

static struct gdt_ptr gdtp;
static struct tss64 tss;
static struct tss64 *ap_tss = NULL;
static uint32_t ap_tss_count = 0;

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

static void set_tss_desc(uint32_t index, uint64_t base, uint32_t limit)
{
    struct gdt_tss_desc *desc;
    if (index == 5) {
        desc = &gdt.tss_desc;
    } else {
        desc = &gdt.ap_tss_desc[(index - 7) / 2];
    }

    desc->limit_low = (uint16_t)(limit & 0xFFFFu);
    desc->base_low = (uint16_t)(base & 0xFFFFu);
    desc->base_mid = (uint8_t)((base >> 16) & 0xFFu);
    desc->access = 0x89u;
    desc->gran = (uint8_t)((limit >> 16) & 0x0Fu);
    desc->base_high = (uint8_t)((base >> 24) & 0xFFu);
    desc->base_upper = (uint32_t)((base >> 32) & 0xFFFFFFFFu);
    desc->reserved = 0;
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
    set_tss_desc(5, (uint64_t)(uintptr_t)&tss, (uint32_t)(sizeof(tss) - 1));

    gdtp.limit = (uint16_t)((5 * sizeof(struct gdt_entry) + sizeof(struct gdt_tss_desc)) - 1);
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

void gdt_init_ap_tss(uint32_t cpu_count)
{
    if (cpu_count <= 1)
        return;

    uint32_t ap_count = cpu_count - 1;
    if (ap_count > X64_GDT_MAX_APS)
        ap_count = X64_GDT_MAX_APS;

    ap_tss_count = ap_count;
    ap_tss = kmalloc(ap_count * sizeof(struct tss64));
    if (!ap_tss)
        return;

    memset(ap_tss, 0, ap_count * sizeof(struct tss64));

    for (uint32_t i = 0; i < ap_count; i++) {
        ap_tss[i].iomap_base = (uint16_t)sizeof(struct tss64);

        uintptr_t stack_phys = pmm_alloc_pages(16);
        if (stack_phys) {
            ap_tss[i].rsp0 = (uint64_t)(uintptr_t)vmm_phys_to_virt(stack_phys + 16 * PAGE_SIZE);
        }

        set_tss_desc(7 + (i * 2), (uint64_t)(uintptr_t)&ap_tss[i], (uint32_t)(sizeof(struct tss64) - 1));
    }

    gdtp.limit = (uint16_t)(((5 * sizeof(struct gdt_entry)) + (uint32_t)((ap_count + 1) * sizeof(struct gdt_tss_desc))) - 1);
    gdt_flush();
}

void gdt_load_ap_tss(uint32_t cpu_id)
{
    uint16_t selector;
    if (cpu_id == 0) {
        selector = X64_GDT_TSS;
    } else {
        selector = (uint16_t)((7 + ((cpu_id - 1) * 2)) * sizeof(struct gdt_entry));
    }
    __asm__ volatile ("ltr %0" : : "r"(selector) : "memory");
}

void gdt_flush(void)
{
    __asm__ volatile ("lgdt %0" : : "m"(gdtp) : "memory");
}
