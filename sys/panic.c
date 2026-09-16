/*
 * Project Tsukasa — Kernel panic implementation
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

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "sys/panic.h"
#include "sys/kconsole.h"
#include "drv/fb.h"
#include "gfx/blit.h"
#include "gfx/font.h"
#include "include/kprintf.h"
#include "include/ksymbols.h"
#include "proc/process.h"

const char* exception_name(uint8_t vector) {
    static const char* names[32] = {
        "Divide Error", "Debug", "Non-maskable Interrupt", "Breakpoint",
        "Overflow", "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
        "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
        "Stack Segment Fault", "General Protection Fault", "Page Fault", "Reserved",
        "x87 FPU Error", "Alignment Check", "Machine Check", "SIMD Exception",
        "Virtualization Exception", "Control Protection", "Reserved", "Reserved",
        "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
        "Security Exception", "Reserved"
    };
    return vector < 32 ? names[vector] : "Unknown Internal Exception";
}

//  colours
#define COL_BG          0xFF200000
#define COL_WHITE       0xFFFFFFFF
#define COL_RED         0xFFFF5555
#define COL_HIGHLIGHT   0xFFFFFF55

//  page fault error code decoder
static void decode_pf_error(uint64_t err, char *out) {
    const char *labels[] = { "P=", " W=", " U=", " R=", " I=" };
    int pos = 0;
    out[pos++] = '(';
    for (int p = 0; p < 5; p++) {
        const char *s = labels[p];
        while (*s) out[pos++] = *s++;
        out[pos++] = '0' + ((err >> p) & 1);
    }
    out[pos++] = ')';
    out[pos]   = 0;
}

static int panic_y;

static void pline(const char *s, uint32_t col) {
    if (fb_info.addr && fb_info.bpp == 32) {
        fb_draw_string(16, panic_y, s, col, COL_BG);
        panic_y += 12;
    }
    kprintf("%s\n", s);
}

static void preg2(const char *l1, uint64_t v1, const char *l2, uint64_t v2) {
    char buf[96];
    ksprintf(buf, sizeof(buf), "%s0x%08x%08x  %s0x%08x%08x",
             l1, (uint32_t)(v1 >> 32), (uint32_t)v1,
             l2, (uint32_t)(v2 >> 32), (uint32_t)v2);
    pline(buf, COL_WHITE);
}

static void panic_emit_line(const char *line) {
    pline(line, COL_WHITE);
}

volatile bool g_in_panic = false;

void kernel_panic(interrupt_frame_t *regs, const char *error_name) {
    asm volatile("cli");
    g_in_panic = true;

    uint64_t cr0, cr2, cr3, cr4;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    asm volatile("mov %%cr2, %0" : "=r"(cr2));
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    asm volatile("mov %%cr4, %0" : "=r"(cr4));

    kconsole_set_active(true);
    if (fb_info.addr && fb_info.bpp == 32)
        fb_fill_rect(0, 0, (int)fb_info.width, (int)fb_info.height, COL_BG);
    panic_y = 16;

    char buf[96];
    pline("*** [KERNEL PANIC] ***", COL_RED);
    pline(error_name, COL_HIGHLIGHT);

    if (regs) {
        ksprintf(buf, sizeof(buf), "Vector: %u  ErrCode: 0x%x",
                 (uint32_t)regs->int_no, (uint32_t)regs->err_code);
        pline(buf, COL_HIGHLIGHT);
        if (regs->int_no == 14) {
            char pf[32];
            decode_pf_error(regs->err_code, pf);
            ksprintf(buf, sizeof(buf), "PF flags: %s  CR2: 0x%08x%08x",
                     pf, (uint32_t)(cr2 >> 32), (uint32_t)cr2);
            pline(buf, COL_RED);
        }
        pline("", COL_WHITE);
        preg2("RIP:    ", regs->rip,    "RFLAGS: ", regs->rflags);
        preg2("CS:     ", regs->cs,     "SS:     ", regs->ss);
        preg2("RAX: ", regs->rax, "RSI: ", regs->rsi);
        preg2("RBX: ", regs->rbx, "RDI: ", regs->rdi);
        preg2("RCX: ", regs->rcx, "RBP: ", regs->rbp);
        preg2("RDX: ", regs->rdx, "RSP: ", regs->rsp);
        preg2("R8:  ", regs->r8,  "R12: ", regs->r12);
        preg2("R9:  ", regs->r9,  "R13: ", regs->r13);
        preg2("R10: ", regs->r10, "R14: ", regs->r14);
        preg2("R11: ", regs->r11, "R15: ", regs->r15);
    }
    pline("", COL_WHITE);
    preg2("CR0: ", cr0, "CR2: ", cr2);
    preg2("CR3: ", cr3, "CR4: ", cr4);

    process_t *p = process_current();
    if (p) {
        ksprintf(buf, sizeof(buf), "Process: pid=%u name=%s", p->pid, p->name);
        pline(buf, COL_HIGHLIGHT);
    }

    pline("", COL_WHITE);
    pline("backtrace:", COL_HIGHLIGHT);
    uint64_t rbp_val = regs ? regs->rbp : (uint64_t)__builtin_frame_address(0);
    uint64_t rip_hint = regs ? regs->rip : (uint64_t)__builtin_return_address(0);
    k_backtrace_custom(rbp_val, rip_hint, 16, panic_emit_line);

    pline("", COL_WHITE);
    pline("The CPU has been halted. Power off the machine.", COL_WHITE);

    while (1) {
        asm volatile("cli; hlt");
    }
}
