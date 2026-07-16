#include "include/smp.h"
#include "arch/x86_64/boot/limine.h"
#include "arch/x86_64/cpu/gdt.h"
#include "arch/x86_64/cpu/idt.h"
#include "include/lapic.h"
#include "mm/pmm.h"
#include "mm/vmm_x64.h"
#include "include/kprintf.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MSR_GS_BASE         0xC0000101
#define MSR_KERNEL_GS_BASE  0xC0000102

static cpu_state_t *cpu_states = NULL;
static uint32_t total_cpus = 0;
static uint32_t bsp_lapic_id = 0;
static cpu_state_t bsp_cpu_state = {0};

static inline void wrmsr(uint32_t msr, uint64_t value)
{
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

static uint32_t read_lapic_id(void)
{
    return lapic_read_id();
}

uint32_t smp_this_cpu_id(void)
{
    if (!cpu_states || total_cpus == 0)
        return 0;

    cpu_state_t *state = NULL;
    __asm__ volatile ("movq %%gs:0, %0" : "=r"(state) : : "memory");
    if (state && state >= cpu_states && state < cpu_states + total_cpus)
        return state->cpu_id;

    uint32_t lapic = read_lapic_id();
    for (uint32_t i = 0; i < total_cpus; i++) {
        if (cpu_states[i].online && cpu_states[i].lapic_id == lapic)
            return i;
    }

    return 0;
}

uint32_t smp_cpu_count(void)
{
    return total_cpus;
}

cpu_state_t *smp_get_cpu(uint32_t cpu_id)
{
    if (cpu_id >= total_cpus)
        return NULL;
    return &cpu_states[cpu_id];
}

static void ap_entry(struct limine_smp_info *info)
{
    uint32_t my_id = (uint32_t)info->extra_argument;

    uint64_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0) : : "memory");
    cr0 &= ~(1ULL << 2);
    cr0 |= (1ULL << 1);
    cr0 |= (1ULL << 5);
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0));

    uint64_t cr4;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4) : : "memory");
    cr4 |= (1ULL << 9);
    cr4 |= (1ULL << 10);
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4));
    __asm__ volatile ("fninit");

    gdt_flush();
    gdt_load_ap_tss(my_id);
    idt_load();

    uint64_t kernel_cr3 = vmm_get_current_pml4();
    __asm__ volatile ("mov %0, %%cr3" : : "r"(kernel_cr3));

    lapic_enable();

    cpu_states[my_id].self = &cpu_states[my_id];
    cpu_states[my_id].online = true;

    wrmsr(MSR_GS_BASE, (uint64_t)(uintptr_t)&cpu_states[my_id]);
    wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)(uintptr_t)&cpu_states[my_id]);

    kprintf("[boot:x64] AP %u online\n", my_id);

    __asm__ volatile ("sti");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void smp_init_bsp(void)
{
    bsp_cpu_state.cpu_id = 0;
    bsp_cpu_state.lapic_id = read_lapic_id();
    bsp_cpu_state.self = &bsp_cpu_state;
    bsp_cpu_state.online = true;
    bsp_lapic_id = bsp_cpu_state.lapic_id;

    wrmsr(MSR_GS_BASE, (uint64_t)(uintptr_t)&bsp_cpu_state);
    wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)(uintptr_t)&bsp_cpu_state);
}

uint32_t smp_init(struct limine_smp_response *smp_resp)
{
    if (!smp_resp || smp_resp->cpu_count <= 1) {
        total_cpus = 1;
        uintptr_t cpu_states_phys = pmm_alloc_pages(1);
        if (!cpu_states_phys)
            return 0;

        cpu_states = (cpu_state_t *)(uintptr_t)vmm_phys_to_virt(cpu_states_phys);
        memset(cpu_states, 0, PAGE_SIZE);
        cpu_states[0].cpu_id = 0;
        cpu_states[0].lapic_id = bsp_lapic_id;
        cpu_states[0].online = true;
        return 1;
    }

    total_cpus = (uint32_t)smp_resp->cpu_count;
    bsp_lapic_id = smp_resp->bsp_lapic_id;

    uintptr_t cpu_states_phys = pmm_alloc_pages((sizeof(cpu_state_t) * total_cpus + PAGE_SIZE - 1) / PAGE_SIZE);
    if (!cpu_states_phys)
        return 0;

    cpu_states = (cpu_state_t *)(uintptr_t)vmm_phys_to_virt(cpu_states_phys);
    memset(cpu_states, 0, sizeof(cpu_state_t) * total_cpus);

    gdt_init_ap_tss(total_cpus);

    for (uint32_t i = 0; i < total_cpus; i++) {
        struct limine_smp_info *cpu = smp_resp->cpus[i];
        cpu_states[i].cpu_id = i;
        cpu_states[i].lapic_id = cpu->lapic_id;
        cpu_states[i].online = false;

        if (cpu->lapic_id == bsp_lapic_id) {
            cpu_states[i] = bsp_cpu_state;
            cpu_states[i].cpu_id = i;
            cpu_states[i].lapic_id = cpu->lapic_id;
            cpu_states[i].online = true;

            wrmsr(MSR_GS_BASE, (uint64_t)(uintptr_t)&cpu_states[i]);
            wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)(uintptr_t)&cpu_states[i]);
            continue;
        }

        uintptr_t stack_phys = pmm_alloc_pages(16);
        if (!stack_phys) {
            continue;
        }
        cpu_states[i].kernel_stack_alloc = (void *)(uintptr_t)vmm_phys_to_virt(stack_phys);
        cpu_states[i].kernel_stack = (uint64_t)(uintptr_t)vmm_phys_to_virt(stack_phys + 16 * PAGE_SIZE);

        cpu->extra_argument = i;
        cpu->goto_address = ap_entry;
    }

    uint32_t online_count = 0;
    uint32_t timeout = 10000000;
    while (timeout-- > 0) {
        online_count = 0;
        for (uint32_t i = 0; i < total_cpus; i++) {
            if (cpu_states[i].online)
                online_count++;
        }
        if (online_count == total_cpus)
            break;
        __asm__ volatile ("pause");
    }

    return online_count;
}

uint32_t smp_get_lapic_id(uint32_t cpu_id)
{
    if (cpu_id >= total_cpus || !cpu_states)
        return 0xFFFFFFFFu;
    return cpu_states[cpu_id].lapic_id;
}
