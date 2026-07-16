/*
 * irq.c - IRQ handler dispatch.
 */

#include <stdint.h>
#include <stddef.h>

#include "irq.h"
#include "ps2kbd.h"
#include "ps2mouse.h"
#include "pic.h"
#include "pit.h"

#ifdef __x86_64__
#include "../include/kprintf.h"
#include "../proc/process.h"
#endif

typedef struct irq_hook {
    irq_callback_t callback;
    void *ctx;
} irq_hook_t;

static irq_hook_t g_irq_hooks[16];

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
        pit_irq_tick();
        (void)irq_invoke_hook(0);
        pic_eoi(0);
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

    if (vector == 33) {
        if (irq_invoke_hook(1))
            pic_eoi(1);
        else
            ps2kbd_handler();
        return context_rsp;
    }

    if (vector == 44) {
        if (irq_invoke_hook(12))
            pic_eoi(12);
        else
            ps2mouse_handler();
        return context_rsp;
    }

    if (vector >= 34 && vector <= 47) {
        uint8_t irq = (uint8_t)(vector - 32);
        (void)irq_invoke_hook(irq);
        pic_eoi(irq);
        return context_rsp;
    }

    if (vector >= 32 && vector < 48)
        pic_eoi(vector - 32);

    return context_rsp;
}

#else

void irq_handler_x64_stub(void) {}

#endif
