/*
 * Project Tsukasa — x86_64 Interrupt Descriptor Table setup
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

#include "idt.h"
#include "include/gdbstub.h"
#include "include/smp.h"
#include "include/kprintf.h"
#include "sys/panic.h"
#include "proc/process.h"
#include "mm/vmm_x64.h"
#include "drv/fb.h"
#include "drv/serial.h"
#include "gfx/blit.h"
#include "gfx/font.h"
#include "vga.h"

#define IDT_X64_ENTRIES 256

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[IDT_X64_ENTRIES];

extern void isr_x64_0(void);
extern void isr_x64_1(void);
extern void isr_x64_2(void);
extern void isr_x64_3(void);
extern void isr_x64_4(void);
extern void isr_x64_5(void);
extern void isr_x64_6(void);
extern void isr_x64_7(void);
extern void isr_x64_8(void);
extern void isr_x64_9(void);
extern void isr_x64_10(void);
extern void isr_x64_11(void);
extern void isr_x64_12(void);
extern void isr_x64_13(void);
extern void isr_x64_14(void);
extern void isr_x64_15(void);
extern void isr_x64_16(void);
extern void isr_x64_17(void);
extern void isr_x64_18(void);
extern void isr_x64_19(void);
extern void isr_x64_20(void);
extern void isr_x64_21(void);
extern void isr_x64_22(void);
extern void isr_x64_23(void);
extern void isr_x64_24(void);
extern void isr_x64_25(void);
extern void isr_x64_26(void);
extern void isr_x64_27(void);
extern void isr_x64_28(void);
extern void isr_x64_29(void);
extern void isr_x64_30(void);
extern void isr_x64_31(void);
extern void isr_x64_32(void);
extern void isr_x64_33(void);
extern void isr_x64_34(void);
extern void isr_x64_35(void);
extern void isr_x64_36(void);
extern void isr_x64_37(void);
extern void isr_x64_38(void);
extern void isr_x64_39(void);
extern void isr_x64_40(void);
extern void isr_x64_41(void);
extern void isr_x64_42(void);
extern void isr_x64_43(void);
extern void isr_x64_44(void);
extern void isr_x64_45(void);
extern void isr_x64_46(void);
extern void isr_x64_47(void);
extern void isr_x64_65(void);
extern void isr_x64_128(void);
extern void isr_x64_ignore(void);

static void (*const exception_stubs[32])(void) = {
    isr_x64_0, isr_x64_1, isr_x64_2, isr_x64_3,
    isr_x64_4, isr_x64_5, isr_x64_6, isr_x64_7,
    isr_x64_8, isr_x64_9, isr_x64_10, isr_x64_11,
    isr_x64_12, isr_x64_13, isr_x64_14, isr_x64_15,
    isr_x64_16, isr_x64_17, isr_x64_18, isr_x64_19,
    isr_x64_20, isr_x64_21, isr_x64_22, isr_x64_23,
    isr_x64_24, isr_x64_25, isr_x64_26, isr_x64_27,
    isr_x64_28, isr_x64_29, isr_x64_30, isr_x64_31,
};

static struct idt_ptr idtp;

static void set_gate(uint8_t vec, void (*handler)(void), uint8_t flags)
{
    uint64_t addr = (uint64_t)(uintptr_t)handler;
    idt[vec].offset_low = (uint16_t)(addr & 0xFFFFu);
    idt[vec].selector = 0x08u;
    idt[vec].ist = 0u;
    idt[vec].type_attr = flags;
    idt[vec].offset_mid = (uint16_t)((addr >> 16) & 0xFFFFu);
    idt[vec].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFFu);
    idt[vec].reserved = 0u;
}

static void draw_exception_banner(uint64_t vector, uint64_t error_code, uint64_t rip, uint64_t cr2)
{
    char line1[96];
    char line2[96];

    ksprintf(line1, sizeof(line1),
             "KEX vec=%u err=%08x%08x",
             (uint32_t)vector,
             (uint32_t)(error_code >> 32),
             (uint32_t)(error_code & 0xFFFFFFFFu));

    ksprintf(line2, sizeof(line2),
             "rip=%08x%08x cr2=%08x%08x",
             (uint32_t)(rip >> 32),
             (uint32_t)(rip & 0xFFFFFFFFu),
             (uint32_t)(cr2 >> 32),
             (uint32_t)(cr2 & 0xFFFFFFFFu));

    if (fb_info.addr && fb_info.bpp == 32) {
        fb_fill_rect(0, 0, (int)fb_info.width, 40, rgb(120, 0, 0));
        fb_draw_string(8, 8, line1, rgb(255, 255, 255), rgb(120, 0, 0));
        fb_draw_string(8, 20, line2, rgb(255, 255, 255), rgb(120, 0, 0));
    }

    /*
     * Do not write to legacy VGA text memory on x64 path unless it is
     * explicitly mapped. Unconditional writes to 0xB8000 can recurse into
     * page faults while handling an exception.
     */
}

void idt_exception_handler_x64(interrupt_frame_t *frame)
{
    uint64_t vector = frame->int_no;
    uint64_t error_code = frame->err_code;
    uint64_t rip = frame->rip;
    uint64_t cs = frame->cs;
    uint64_t fault_rsp = frame->rsp;
    uint64_t ss = frame->ss;

#if defined(CONFIG_GDBSTUB)
    if (gdbstub_is_enabled()) {
        if (vector == 3) {
            if (gdbstub_has_breakpoint_at(frame->rip - 1)) {
                frame->rip -= 1;
            }
            gdbstub_trap(frame, 5);
            return;
        } else if (vector == 1) {
            frame->rflags &= ~(1ULL << 8);
            gdbstub_trap(frame, 5);
            return;
        }
    }
#endif

    uint64_t cr2 = 0;
    uint64_t cr3 = 0;
    uint64_t k_rsp = 0;
    uint32_t cpu = smp_this_cpu_id();
    process_t *cur = process_current();

    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %%rsp, %0" : "=r"(k_rsp));
    __asm__ volatile ("cli");

    kprintf("[x64][exc][cpu%u] pid=%d (%s) vec=%u err=0x%08x%08x rip=0x%08x%08x cs=0x%04x cr2=0x%08x%08x cr3=0x%08x%08x rsp=0x%08x%08x ss=0x%04x krsp=0x%08x%08x\n",
            cpu,
            cur ? (int)cur->pid : -1,
            (cur && cur->name[0]) ? cur->name : "none",
            (uint32_t)vector,
            (uint32_t)(error_code >> 32),
            (uint32_t)(error_code & 0xFFFFFFFFu),
            (uint32_t)(rip >> 32),
            (uint32_t)(rip & 0xFFFFFFFFu),
            (uint32_t)cs,
            (uint32_t)(cr2 >> 32),
            (uint32_t)(cr2 & 0xFFFFFFFFu),
            (uint32_t)(cr3 >> 32),
            (uint32_t)(cr3 & 0xFFFFFFFFu),
            (uint32_t)(fault_rsp >> 32),
            (uint32_t)(fault_rsp & 0xFFFFFFFFu),
            (uint32_t)ss,
            (uint32_t)(k_rsp >> 32),
            (uint32_t)(k_rsp & 0xFFFFFFFFu));

    if (vector == 14) {
        uint64_t pa = 0, fl = 0;
        int qr = vmm_query_page(cr3, cr2, &pa, &fl);
        kprintf("[x64][exc][cpu%u] vmm_query cr3=0x%08x%08x va=0x%08x%08x -> res=%d pa=0x%08x%08x fl=0x%08x%08x\n",
                cpu, (uint32_t)(cr3 >> 32), (uint32_t)(cr3 & 0xFFFFFFFFu),
                (uint32_t)(cr2 >> 32), (uint32_t)(cr2 & 0xFFFFFFFFu),
                qr, (uint32_t)(pa >> 32), (uint32_t)(pa & 0xFFFFFFFFu),
                (uint32_t)(fl >> 32), (uint32_t)(fl & 0xFFFFFFFFu));
    }

    uint64_t *sp = (uint64_t *)(uintptr_t)k_rsp;
    for (int i = 0; i < 16; i++) {
        kprintf("[x64][exc][cpu%u] st[%d]=0x%08x%08x\n",
                cpu, i, (uint32_t)(sp[i] >> 32), (uint32_t)(sp[i] & 0xFFFFFFFFu));
    }

    draw_exception_banner(vector, error_code, rip, cr2);

    kernel_panic(frame, exception_name((uint8_t)vector));
}

void idt_init_x64(void)
{
    for (uint32_t i = 0; i < 32; i++)
        set_gate((uint8_t)i, exception_stubs[i], 0x8Eu);

    for (uint32_t i = 32; i < IDT_X64_ENTRIES; i++)
        set_gate((uint8_t)i, isr_x64_ignore, 0x8Eu);

    set_gate(32, isr_x64_32, 0x8Eu);
    set_gate(33, isr_x64_33, 0x8Eu);
    set_gate(34, isr_x64_34, 0x8Eu);
    set_gate(35, isr_x64_35, 0x8Eu);
    set_gate(36, isr_x64_36, 0x8Eu);
    set_gate(37, isr_x64_37, 0x8Eu);
    set_gate(38, isr_x64_38, 0x8Eu);
    set_gate(39, isr_x64_39, 0x8Eu);
    set_gate(40, isr_x64_40, 0x8Eu);
    set_gate(41, isr_x64_41, 0x8Eu);
    set_gate(42, isr_x64_42, 0x8Eu);
    set_gate(43, isr_x64_43, 0x8Eu);
    set_gate(44, isr_x64_44, 0x8Eu);
    set_gate(45, isr_x64_45, 0x8Eu);
    set_gate(46, isr_x64_46, 0x8Eu);
    set_gate(47, isr_x64_47, 0x8Eu);
    set_gate(65, isr_x64_65, 0x8Eu);
    set_gate(128, isr_x64_128, 0xEEu);

    idtp.limit = (uint16_t)(sizeof(idt) - 1);
    idtp.base = (uint64_t)(uintptr_t)&idt;

    __asm__ volatile ("lidt %0" : : "m"(idtp));
}

void idt_load(void)
{
    __asm__ volatile ("lidt %0" : : "m"(idtp) : "memory");
}
