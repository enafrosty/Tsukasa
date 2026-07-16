#include "lapic.h"
#include "mm/vmm_x64.h"
#include "include/spinlock.h"

#include <stdint.h>

static volatile uint32_t *lapic_base = NULL;
static spinlock_t lapic_lock = SPINLOCK_INIT;

#define LAPIC_ID       (0x020 / 4)
#define LAPIC_EOI      (0x0B0 / 4)
#define LAPIC_SVR      (0x0F0 / 4)
#define LAPIC_ICR_LOW  (0x300 / 4)
#define LAPIC_ICR_HIGH (0x310 / 4)

static inline volatile uint32_t *lapic_ptr(void)
{
    if (!lapic_base) {
        uintptr_t mapped = 0;
        if (vmm_map_io_region(0xFEE00000ULL, 0x1000u, &mapped) != 0)
            return NULL;
        lapic_base = (volatile uint32_t *)(uintptr_t)mapped;
    }
    return lapic_base;
}

static inline void lapic_wait(void)
{
    volatile uint32_t *lapic = lapic_ptr();
    if (!lapic)
        return;

    while (lapic[LAPIC_ICR_LOW] & (1u << 12))
        __asm__ volatile ("pause");
}

void lapic_enable(void)
{
    volatile uint32_t *lapic = lapic_ptr();
    if (!lapic)
        return;

    lapic[LAPIC_SVR] = 0x1FF;
}

void lapic_init(void)
{
    if (!lapic_ptr())
        return;

    lapic_enable();
}

uint32_t lapic_read_id(void)
{
    volatile uint32_t *lapic = lapic_ptr();
    if (!lapic)
        return 0;

    return (lapic[LAPIC_ID] >> 24) & 0xFFu;
}

void lapic_eoi(void)
{
    volatile uint32_t *lapic = lapic_ptr();
    if (!lapic)
        return;

    lapic[LAPIC_EOI] = 0;
}

void lapic_send_ipi_all(void)
{
    volatile uint32_t *lapic = lapic_ptr();
    if (!lapic)
        return;

    spin_lock(&lapic_lock);
    lapic_wait();
    lapic[LAPIC_ICR_HIGH] = 0;
    lapic[LAPIC_ICR_LOW] = (0x41u) | (0b11u << 18) | (1u << 14);
    while (lapic[LAPIC_ICR_LOW] & (1u << 12))
        __asm__ volatile ("pause");
    spin_unlock(&lapic_lock);
}

void lapic_send_ipi(uint32_t lapic_id, uint8_t vector)
{
    volatile uint32_t *lapic = lapic_ptr();
    if (!lapic)
        return;

    spin_lock(&lapic_lock);
    lapic_wait();
    lapic[LAPIC_ICR_HIGH] = (lapic_id << 24);
    lapic[LAPIC_ICR_LOW] = (uint32_t)vector | (1u << 14);
    while (lapic[LAPIC_ICR_LOW] & (1u << 12))
        __asm__ volatile ("pause");
    spin_unlock(&lapic_lock);
}
