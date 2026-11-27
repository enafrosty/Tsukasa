/*
 * Project Tsukasa — IRQ handler dispatch
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
#include <stddef.h>

#include "irq.h"
#include "ps2kbd.h"
#include "ps2mouse.h"
#include "pic.h"
#include "pit.h"

#ifdef __x86_64__
#include "../arch/x86_64/cpu/idt.h"
#include "../include/kprintf.h"
#include "../include/lapic.h"
#include "../include/smp.h"
#include "../include/spinlock.h"
#include "../proc/process.h"
#include "ioapic.h"
#endif

#define IRQ_DYN_VEC_BASE 0x50u
#define IRQ_DYN_VEC_END  0xEFu
#define IRQ_SYSCALL_VEC  0x80u

typedef struct irq_hook {
    irq_callback_t callback;
    void *ctx;
} irq_hook_t;

typedef struct irq_vec_hook {
    irq_vector_callback_t callback;
    void *ctx;
} irq_vec_hook_t;

static irq_hook_t g_irq_hooks[16];
static irq_vec_hook_t g_vec_hooks[256];
static uint64_t g_vec_bitmap[4];
#ifdef __x86_64__
static spinlock_t g_vec_lock = SPINLOCK_INIT;
#endif

static int irq_vec_is_reserved(uint32_t vec)
{
    return (vec < IRQ_DYN_VEC_BASE || vec > IRQ_DYN_VEC_END || vec == IRQ_SYSCALL_VEC);
}

static int irq_vec_bit_test(uint32_t vec)
{
    return (g_vec_bitmap[vec >> 6] & (1ULL << (vec & 63u))) != 0ULL;
}

static void irq_vec_bit_set(uint32_t vec)
{
    g_vec_bitmap[vec >> 6] |= (1ULL << (vec & 63u));
}

static void irq_vec_bit_clear(uint32_t vec)
{
    g_vec_bitmap[vec >> 6] &= ~(1ULL << (vec & 63u));
}

static int irq_vec_is_valid_handler_slot(uint8_t vec)
{
    return (vec >= 48u && vec != 64u && vec != 65u && vec != 66u &&
            vec != 67u && vec != 128u && vec != 255u);
}

int irq_alloc_vectors(int count)
{
    if (count <= 0 || count > 32 || (count & (count - 1)) != 0)
        return -1;

#ifdef __x86_64__
    unsigned long flags = spin_lock_irqsave(&g_vec_lock);
#endif
    uint32_t align = (uint32_t)count;
    uint32_t base = (IRQ_DYN_VEC_BASE + align - 1u) & ~(align - 1u);
    int found = -1;

    for (; base + align - 1u <= IRQ_DYN_VEC_END; base += align) {
        int ok = 1;
        for (uint32_t i = 0; i < align; i++) {
            uint32_t v = base + i;
            if (irq_vec_is_reserved(v) || irq_vec_bit_test(v)) {
                ok = 0;
                break;
            }
        }
        if (ok) {
            for (uint32_t i = 0; i < align; i++)
                irq_vec_bit_set(base + i);
            found = (int)base;
            break;
        }
    }

#ifdef __x86_64__
    spin_unlock_irqrestore(&g_vec_lock, flags);
#endif
    return found;
}

int irq_alloc_vector(void)
{
    return irq_alloc_vectors(1);
}

void irq_free_vector(uint8_t vec)
{
    if (irq_vec_is_reserved(vec))
        return;

#ifdef __x86_64__
    unsigned long flags = spin_lock_irqsave(&g_vec_lock);
#endif
    irq_vec_bit_clear(vec);
    g_vec_hooks[vec].callback = NULL;
    g_vec_hooks[vec].ctx = NULL;
#ifdef __x86_64__
    spin_unlock_irqrestore(&g_vec_lock, flags);
#endif
}

int irq_register_vector_handler(uint8_t vec, irq_vector_callback_t cb, void *ctx)
{
    if (!cb || !irq_vec_is_valid_handler_slot(vec))
        return -1;

#ifdef __x86_64__
    unsigned long flags = spin_lock_irqsave(&g_vec_lock);
#endif
    g_vec_hooks[vec].callback = cb;
    g_vec_hooks[vec].ctx = ctx;
#ifdef __x86_64__
    spin_unlock_irqrestore(&g_vec_lock, flags);
#endif
    return 0;
}

void irq_unregister_vector_handler(uint8_t vec)
{
    if (!irq_vec_is_valid_handler_slot(vec))
        return;

#ifdef __x86_64__
    unsigned long flags = spin_lock_irqsave(&g_vec_lock);
#endif
    g_vec_hooks[vec].callback = NULL;
    g_vec_hooks[vec].ctx = NULL;
#ifdef __x86_64__
    spin_unlock_irqrestore(&g_vec_lock, flags);
#endif
}

int irq_register_handler(uint8_t irq, irq_callback_t callback, void *ctx)
{
    if (irq >= 16 || !callback)
        return -1;
    g_irq_hooks[irq].callback = callback;
    g_irq_hooks[irq].ctx = ctx;
    return 0;
}

void irq_unregister_handler(uint8_t irq)
{
    if (irq >= 16)
        return;
    g_irq_hooks[irq].callback = NULL;
    g_irq_hooks[irq].ctx = NULL;
}

/* Policy: IOAPIC-delivered interrupts are EOI'd at the LAPIC; the PIC fallback (no ACPI/IOAPIC, or the i386... */
void irq_ack(unsigned int irq)
{
#ifdef __x86_64__
    if (ioapic_active()) {
        lapic_eoi();
        return;
    }
#endif
    pic_eoi(irq);
}

static int irq_invoke_hook(uint8_t irq)
{
    if (irq >= 16)
        return 0;
    if (!g_irq_hooks[irq].callback)
        return 0;
    g_irq_hooks[irq].callback(irq, g_irq_hooks[irq].ctx);
    return 1;
}

void irq_handler(unsigned int vector)
{
    if (vector == 32) {
        pit_irq_tick();
        if (!irq_invoke_hook(0))
            pic_eoi(0);
        return;
    }

    if (vector == 33) {
        if (irq_invoke_hook(1))
            pic_eoi(1);
        else
            ps2kbd_handler();
        return;
    }

    if (vector == 44) {
        if (irq_invoke_hook(12))
            pic_eoi(12);
        else
            ps2mouse_handler();
        return;
    }

    if (vector >= 34 && vector <= 47) {
        uint8_t irq = (uint8_t)(vector - 32);
        (void)irq_invoke_hook(irq);
        pic_eoi(irq);
        return;
    }

    if (vector >= 32 && vector < 48)
        pic_eoi(vector - 32);
}

#ifdef __x86_64__
static int g_irq32_trace_once;

uint64_t irq_handler_x64(unsigned int vector, uint64_t context_rsp)
{
    if (vector == 32) {
        uint64_t next_rsp;
        uint32_t cpu = smp_this_cpu_id();

        if (cpu == 0) {
            if (!lapic_timer_active())
                pit_irq_tick();
            (void)irq_invoke_hook(0);
            irq_ack(0);
        }
        if (lapic_timer_active())
            return context_rsp;
        next_rsp = process_schedule_tick(context_rsp);

        if (!g_irq32_trace_once) {
            g_irq32_trace_once = 1;
            if (next_rsp) {
                uint64_t *ctx = (uint64_t *)(uintptr_t)next_rsp;
                kprintf("[irq32] cur_rsp=0x%08x%08x next_rsp=0x%08x%08x pid=%d\n",
                        (uint32_t)(context_rsp >> 32), (uint32_t)(context_rsp & 0xFFFFFFFFu),
                        (uint32_t)(next_rsp >> 32), (uint32_t)(next_rsp & 0xFFFFFFFFu),
                        process_current_pid());
                kprintf("[irq32] frame rax=0x%08x%08x vec=0x%08x%08x rip=0x%08x%08x cs=0x%08x%08x rflags=0x%08x%08x\n",
                        (uint32_t)(ctx[0] >> 32), (uint32_t)(ctx[0] & 0xFFFFFFFFu),
                        (uint32_t)(ctx[15] >> 32), (uint32_t)(ctx[15] & 0xFFFFFFFFu),
                        (uint32_t)(ctx[16] >> 32), (uint32_t)(ctx[16] & 0xFFFFFFFFu),
                        (uint32_t)(ctx[17] >> 32), (uint32_t)(ctx[17] & 0xFFFFFFFFu),
                        (uint32_t)(ctx[18] >> 32), (uint32_t)(ctx[18] & 0xFFFFFFFFu));
                kprintf("[irq32] q15=0x%08x%08x q16=0x%08x%08x q17=0x%08x%08x q18=0x%08x%08x q19=0x%08x%08x\n",
                        (uint32_t)(ctx[15] >> 32), (uint32_t)(ctx[15] & 0xFFFFFFFFu),
                        (uint32_t)(ctx[16] >> 32), (uint32_t)(ctx[16] & 0xFFFFFFFFu),
                        (uint32_t)(ctx[17] >> 32), (uint32_t)(ctx[17] & 0xFFFFFFFFu),
                        (uint32_t)(ctx[18] >> 32), (uint32_t)(ctx[18] & 0xFFFFFFFFu),
                        (uint32_t)(ctx[19] >> 32), (uint32_t)(ctx[19] & 0xFFFFFFFFu));
                kprintf("[irq32] q20=0x%08x%08x q21=0x%08x%08x q22=0x%08x%08x q23=0x%08x%08x\n",
                        (uint32_t)(ctx[20] >> 32), (uint32_t)(ctx[20] & 0xFFFFFFFFu),
                        (uint32_t)(ctx[21] >> 32), (uint32_t)(ctx[21] & 0xFFFFFFFFu),
                        (uint32_t)(ctx[22] >> 32), (uint32_t)(ctx[22] & 0xFFFFFFFFu),
                        (uint32_t)(ctx[23] >> 32), (uint32_t)(ctx[23] & 0xFFFFFFFFu));
            }
        }
        return next_rsp;
    }

    if (vector == 64) {
        return process_schedule_yield(context_rsp);
    }

    if (vector == 65) {
        /* LAPIC-domain interrupt: EOI precedes reschedule check. */
        lapic_eoi();
        return process_schedule_ipi(context_rsp);
    }

    if (vector == IDT_VEC_LAPIC_TIMER) {
        uint32_t cpu = smp_this_cpu_id();
        lapic_eoi();
        lapic_timer_note_tick(cpu);
        if (cpu == 0)
            pit_irq_tick();
        return process_schedule_tick(context_rsp);
    }

    if (vector == 33) {
        if (irq_invoke_hook(1))
            irq_ack(1);
        else
            ps2kbd_handler();
        return context_rsp;
    }

    if (vector == 44) {
        if (irq_invoke_hook(12))
            irq_ack(12);
        else
            ps2mouse_handler();
        return context_rsp;
    }

    if (vector >= 34 && vector <= 47) {
        uint8_t irq = (uint8_t)(vector - 32);
        (void)irq_invoke_hook(irq);
        irq_ack(irq);
        return context_rsp;
    }

    if (vector >= 32 && vector < 48) {
        irq_ack(vector - 32);
        return context_rsp;
    }

    if (vector >= 48 && vector < 255) {
        irq_vector_callback_t cb;
        void *ctx;
        unsigned long flags = spin_lock_irqsave(&g_vec_lock);
        cb = g_vec_hooks[vector].callback;
        ctx = g_vec_hooks[vector].ctx;
        spin_unlock_irqrestore(&g_vec_lock, flags);
        if (cb)
            cb((uint8_t)vector, ctx);
        lapic_eoi();
        return context_rsp;
    }

    return context_rsp;
}

#else

void irq_handler_x64_stub(void) {}

#endif
