/*
 * Project Tsukasa — Kernel Serial GDB Stub Implementation
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

#include "include/gdbstub.h"
#include "drv/serial.h"
#include "drv/acpi.h"
#include "include/kprintf.h"
#include "include/kutils.h"
#include "include/ksymbols.h"
#include "mm/vmm_x64.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

static uint16_t g_uart_base = 0;
static bool g_enabled = false;
static volatile bool g_in_stub = false;
static struct gdb_breakpoint g_breakpoints[GDB_MAX_BREAKPOINTS];

#define GDB_BUF_SIZE 2048
static char g_in_buf[GDB_BUF_SIZE];
static char g_out_buf[GDB_BUF_SIZE];

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static char hex_char(uint8_t nibble)
{
    static const char digits[] = "0123456789abcdef";
    return digits[nibble & 0xFu];
}

static uint64_t hex_to_u64(const char **ptr)
{
    uint64_t val = 0;
    int digit;
    while (**ptr && (digit = hex_val(**ptr)) >= 0) {
        val = (val << 4) | (uint64_t)digit;
        (*ptr)++;
    }
    return val;
}

static inline uint64_t read_cr0(void)
{
    uint64_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    return cr0;
}

static inline void write_cr0(uint64_t cr0)
{
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

static inline uint64_t read_cr3_raw(void)
{
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

static inline uint16_t read_ds(void)
{
    uint16_t val;
    __asm__ volatile ("mov %%ds, %0" : "=r"(val));
    return val;
}

static inline uint16_t read_es(void)
{
    uint16_t val;
    __asm__ volatile ("mov %%es, %0" : "=r"(val));
    return val;
}

static inline uint16_t read_fs(void)
{
    uint16_t val;
    __asm__ volatile ("mov %%fs, %0" : "=r"(val));
    return val;
}

static inline uint16_t read_gs(void)
{
    uint16_t val;
    __asm__ volatile ("mov %%gs, %0" : "=r"(val));
    return val;
}

/*
 * Lock-free virtual page table check to prevent faulting when GDB
 * attempts to read or write invalid addresses.
 */
static int gdb_page_present(uintptr_t va, int need_write)
{
    uint64_t top = va >> 47;
    if (top != 0 && top != 0x1FFFFULL)
        return 0;

    uint64_t cr3 = read_cr3_raw() & 0x000FFFFFFFFFF000ULL;
    if (!cr3)
        return 0;

    uint64_t *pml4 = (uint64_t *)(uintptr_t)vmm_phys_to_virt(cr3);
    uint64_t pml4e = pml4[(va >> 39) & 0x1FF];
    if (!(pml4e & 1))
        return 0;

    uint64_t *pdpt = (uint64_t *)(uintptr_t)vmm_phys_to_virt(pml4e & 0x000FFFFFFFFFF000ULL);
    uint64_t pdpte = pdpt[(va >> 30) & 0x1FF];
    if (!(pdpte & 1))
        return 0;
    if (pdpte & (1ULL << 7)) {
        if (need_write && !(pdpte & 2))
            return 0;
        return 1;
    }

    uint64_t *pd = (uint64_t *)(uintptr_t)vmm_phys_to_virt(pdpte & 0x000FFFFFFFFFF000ULL);
    uint64_t pde = pd[(va >> 21) & 0x1FF];
    if (!(pde & 1))
        return 0;
    if (pde & (1ULL << 7)) {
        if (need_write && !(pde & 2))
            return 0;
        return 1;
    }

    uint64_t *pt = (uint64_t *)(uintptr_t)vmm_phys_to_virt(pde & 0x000FFFFFFFFFF000ULL);
    uint64_t pte = pt[(va >> 12) & 0x1FF];
    if (!(pte & 1))
        return 0;
    if (need_write && !(pte & 2))
        return 0;

    return 1;
}

static int gdb_mem_valid(uintptr_t addr, size_t len, int need_write)
{
    if (len == 0)
        return 1;
    if (addr + len < addr)
        return 0;

    uintptr_t first = addr & ~0xFFFULL;
    uintptr_t last = (addr + len - 1) & ~0xFFFULL;
    for (uintptr_t p = first; ; p += 4096) {
        if (!gdb_page_present(p, need_write))
            return 0;
        if (p == last)
            break;
    }
    return 1;
}

static int gdb_recv_packet(char *buf, size_t max_len)
{
    while (1) {
        char c = serial_getc_port(g_uart_base);
        if (c == 0x03) {
            buf[0] = 0x03;
            buf[1] = '\0';
            return 1;
        }
        if (c == '$') {
            size_t idx = 0;
            uint8_t csum = 0;
            while (1) {
                c = serial_getc_port(g_uart_base);
                if (c == '#')
                    break;
                csum += (uint8_t)c;
                if (idx + 1 < max_len)
                    buf[idx++] = c;
            }
            buf[idx] = '\0';

            char h1 = serial_getc_port(g_uart_base);
            char h2 = serial_getc_port(g_uart_base);
            int v1 = hex_val(h1);
            int v2 = hex_val(h2);
            if (v1 < 0 || v2 < 0) {
                serial_write_byte_raw_port(g_uart_base, '-');
                continue;
            }
            uint8_t pkt_csum = (uint8_t)((v1 << 4) | v2);
            if (csum != pkt_csum) {
                serial_write_byte_raw_port(g_uart_base, '-');
                continue;
            }
            serial_write_byte_raw_port(g_uart_base, '+');
#if defined(GDBSTUB_DEBUG)
            kprintf("[gdbstub:rx] %s\n", buf);
#endif
            return (int)idx;
        }
    }
}

static void gdb_send_packet(const char *payload)
{
#if defined(GDBSTUB_DEBUG)
    kprintf("[gdbstub:tx] %s\n", payload);
#endif
    while (1) {
        serial_write_byte_raw_port(g_uart_base, '$');
        uint8_t csum = 0;
        const char *p = payload;
        while (*p) {
            csum += (uint8_t)*p;
            serial_write_byte_raw_port(g_uart_base, (uint8_t)*p);
            p++;
        }
        serial_write_byte_raw_port(g_uart_base, '#');
        serial_write_byte_raw_port(g_uart_base, hex_char(csum >> 4));
        serial_write_byte_raw_port(g_uart_base, hex_char(csum & 0xFu));

        char ack = serial_getc_port(g_uart_base);
        if (ack == '+')
            break;
    }
}

static void append_reg64(char *buf, size_t *pos, uint64_t val)
{
    for (int i = 0; i < 8; i++) {
        uint8_t b = (uint8_t)(val >> (i * 8));
        buf[(*pos)++] = hex_char(b >> 4);
        buf[(*pos)++] = hex_char(b & 0xFu);
    }
}

static void append_reg32(char *buf, size_t *pos, uint32_t val)
{
    for (int i = 0; i < 4; i++) {
        uint8_t b = (uint8_t)(val >> (i * 8));
        buf[(*pos)++] = hex_char(b >> 4);
        buf[(*pos)++] = hex_char(b & 0xFu);
    }
}

static uint64_t parse_reg64(const char *buf)
{
    uint64_t val = 0;
    for (int i = 0; i < 8; i++) {
        int h = hex_val(buf[i * 2]);
        int l = hex_val(buf[i * 2 + 1]);
        if (h < 0 || l < 0) return val;
        uint8_t b = (uint8_t)((h << 4) | l);
        val |= ((uint64_t)b) << (i * 8);
    }
    return val;
}

static uint32_t parse_reg32(const char *buf)
{
    uint32_t val = 0;
    for (int i = 0; i < 4; i++) {
        int h = hex_val(buf[i * 2]);
        int l = hex_val(buf[i * 2 + 1]);
        if (h < 0 || l < 0) return val;
        uint8_t b = (uint8_t)((h << 4) | l);
        val |= ((uint32_t)b) << (i * 8);
    }
    return val;
}

static void gdb_handle_g(interrupt_frame_t *regs)
{
    size_t pos = 0;
    append_reg64(g_out_buf, &pos, regs->rax);
    append_reg64(g_out_buf, &pos, regs->rbx);
    append_reg64(g_out_buf, &pos, regs->rcx);
    append_reg64(g_out_buf, &pos, regs->rdx);
    append_reg64(g_out_buf, &pos, regs->rsi);
    append_reg64(g_out_buf, &pos, regs->rdi);
    append_reg64(g_out_buf, &pos, regs->rbp);
    append_reg64(g_out_buf, &pos, regs->rsp);
    append_reg64(g_out_buf, &pos, regs->r8);
    append_reg64(g_out_buf, &pos, regs->r9);
    append_reg64(g_out_buf, &pos, regs->r10);
    append_reg64(g_out_buf, &pos, regs->r11);
    append_reg64(g_out_buf, &pos, regs->r12);
    append_reg64(g_out_buf, &pos, regs->r13);
    append_reg64(g_out_buf, &pos, regs->r14);
    append_reg64(g_out_buf, &pos, regs->r15);
    append_reg64(g_out_buf, &pos, regs->rip);
    append_reg32(g_out_buf, &pos, (uint32_t)regs->rflags);
    append_reg32(g_out_buf, &pos, (uint32_t)regs->cs);
    append_reg32(g_out_buf, &pos, (uint32_t)regs->ss);
    append_reg32(g_out_buf, &pos, (uint32_t)read_ds());
    append_reg32(g_out_buf, &pos, (uint32_t)read_es());
    append_reg32(g_out_buf, &pos, (uint32_t)read_fs());
    append_reg32(g_out_buf, &pos, (uint32_t)read_gs());

    /* Fill remaining registers up to 536 bytes (1072 hex characters) with zero */
    while (pos < 536 * 2) {
        g_out_buf[pos++] = '0';
    }
    g_out_buf[pos] = '\0';
    gdb_send_packet(g_out_buf);
}

static void gdb_handle_G(interrupt_frame_t *regs, const char *data)
{
    regs->rax    = parse_reg64(data + 0);
    regs->rbx    = parse_reg64(data + 16);
    regs->rcx    = parse_reg64(data + 32);
    regs->rdx    = parse_reg64(data + 48);
    regs->rsi    = parse_reg64(data + 64);
    regs->rdi    = parse_reg64(data + 80);
    regs->rbp    = parse_reg64(data + 96);
    regs->rsp    = parse_reg64(data + 112);
    regs->r8     = parse_reg64(data + 128);
    regs->r9     = parse_reg64(data + 144);
    regs->r10    = parse_reg64(data + 160);
    regs->r11    = parse_reg64(data + 176);
    regs->r12    = parse_reg64(data + 192);
    regs->r13    = parse_reg64(data + 208);
    regs->r14    = parse_reg64(data + 224);
    regs->r15    = parse_reg64(data + 240);
    regs->rip    = parse_reg64(data + 256);
    regs->rflags = parse_reg32(data + 272);
    regs->cs     = parse_reg32(data + 280);
    regs->ss     = parse_reg32(data + 288);
    gdb_send_packet("OK");
}

static void gdb_handle_m(const char *cmd)
{
    const char *p = cmd + 1;
    uintptr_t addr = (uintptr_t)hex_to_u64(&p);
    if (*p != ',') {
        gdb_send_packet("E01");
        return;
    }
    p++;
    size_t len = (size_t)hex_to_u64(&p);
    if (len > (sizeof(g_out_buf) - 1) / 2)
        len = (sizeof(g_out_buf) - 1) / 2;

    if (!gdb_mem_valid(addr, len, 0)) {
        gdb_send_packet("E14");
        return;
    }

    size_t pos = 0;
    const uint8_t *src = (const uint8_t *)addr;
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = src[i];
        for (int b = 0; b < GDB_MAX_BREAKPOINTS; b++) {
            if (g_breakpoints[b].active && g_breakpoints[b].addr == (unsigned long)(addr + i)) {
                byte = g_breakpoints[b].orig_byte;
                break;
            }
        }
        g_out_buf[pos++] = hex_char(byte >> 4);
        g_out_buf[pos++] = hex_char(byte & 0xFu);
    }
    g_out_buf[pos] = '\0';
    gdb_send_packet(g_out_buf);
}

static void gdb_handle_M(const char *cmd)
{
    const char *p = cmd + 1;
    uintptr_t addr = (uintptr_t)hex_to_u64(&p);
    if (*p != ',') {
        gdb_send_packet("E01");
        return;
    }
    p++;
    size_t len = (size_t)hex_to_u64(&p);
    if (*p != ':') {
        gdb_send_packet("E01");
        return;
    }
    p++;

    if (!gdb_mem_valid(addr, len, 0)) {
        gdb_send_packet("E14");
        return;
    }

    uint64_t cr0 = read_cr0();
    write_cr0(cr0 & ~(1ULL << 16));

    uint8_t *dst = (uint8_t *)addr;
    for (size_t i = 0; i < len; i++) {
        int h = hex_val(p[i * 2]);
        int l = hex_val(p[i * 2 + 1]);
        if (h < 0 || l < 0) {
            write_cr0(cr0);
            gdb_send_packet("E01");
            return;
        }
        dst[i] = (uint8_t)((h << 4) | l);
    }

    write_cr0(cr0);
    gdb_send_packet("OK");
}

static void gdb_handle_breakpoint(const char *cmd, bool insert)
{
    const char *p = cmd + 3;
    uintptr_t addr = (uintptr_t)hex_to_u64(&p);

    if (insert) {
        for (int i = 0; i < GDB_MAX_BREAKPOINTS; i++) {
            if (g_breakpoints[i].active && g_breakpoints[i].addr == (unsigned long)addr) {
                gdb_send_packet("OK");
                return;
            }
        }
        int slot = -1;
        for (int i = 0; i < GDB_MAX_BREAKPOINTS; i++) {
            if (!g_breakpoints[i].active) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            gdb_send_packet("E22");
            return;
        }
        if (!gdb_mem_valid(addr, 1, 0)) {
            gdb_send_packet("E14");
            return;
        }

        uint8_t orig = *(uint8_t *)addr;
        uint64_t cr0 = read_cr0();
        write_cr0(cr0 & ~(1ULL << 16));
        *(uint8_t *)addr = 0xCC;
        write_cr0(cr0);

        g_breakpoints[slot].addr = (unsigned long)addr;
        g_breakpoints[slot].orig_byte = orig;
        g_breakpoints[slot].active = 1;
        gdb_send_packet("OK");
    } else {
        for (int i = 0; i < GDB_MAX_BREAKPOINTS; i++) {
            if (g_breakpoints[i].active && g_breakpoints[i].addr == (unsigned long)addr) {
                uint64_t cr0 = read_cr0();
                write_cr0(cr0 & ~(1ULL << 16));
                *(uint8_t *)addr = g_breakpoints[i].orig_byte;
                write_cr0(cr0);
                g_breakpoints[i].active = 0;
                gdb_send_packet("OK");
                return;
            }
        }
        gdb_send_packet("OK");
    }
}

void gdbstub_init(uint16_t uart_base)
{
    g_uart_base = uart_base;
    serial_init_port(g_uart_base);
    for (int i = 0; i < GDB_MAX_BREAKPOINTS; i++) {
        g_breakpoints[i].addr = 0;
        g_breakpoints[i].orig_byte = 0;
        g_breakpoints[i].active = 0;
    }
    g_enabled = true;
    kprintf("[gdbstub] initialized on port 0x%x\n", (uint32_t)g_uart_base);
}

bool gdbstub_is_enabled(void)
{
    return g_enabled;
}

bool gdbstub_has_breakpoint_at(unsigned long addr)
{
    for (int i = 0; i < GDB_MAX_BREAKPOINTS; i++) {
        if (g_breakpoints[i].active && g_breakpoints[i].addr == addr)
            return true;
    }
    return false;
}

void gdbstub_breakpoint(void)
{
    __asm__ volatile ("int $3");
}

void gdbstub_trap(interrupt_frame_t *regs, int signo)
{
    if (!g_enabled)
        return;

    if (g_in_stub) {
        serial_puts_port(COM1_BASE, "[gdbstub] fatal: recursive fault in stub\n");
        for (;;)
            __asm__ volatile ("cli; hlt");
    }
    g_in_stub = true;

    ksprintf(g_out_buf, sizeof(g_out_buf), "T%02xthread:1;", signo);
    gdb_send_packet(g_out_buf);

    bool resume = false;
    while (!resume) {
        int len = gdb_recv_packet(g_in_buf, sizeof(g_in_buf));
        if (len <= 0)
            continue;

        if (g_in_buf[0] == 0x03) {
            ksprintf(g_out_buf, sizeof(g_out_buf), "T%02xthread:1;", 2);
            gdb_send_packet(g_out_buf);
            continue;
        }

        switch (g_in_buf[0]) {
        case '?':
            ksprintf(g_out_buf, sizeof(g_out_buf), "T%02xthread:1;", signo);
            gdb_send_packet(g_out_buf);
            break;
        case 'g':
            gdb_handle_g(regs);
            break;
        case 'G':
            gdb_handle_G(regs, g_in_buf + 1);
            break;
        case 'm':
            gdb_handle_m(g_in_buf);
            break;
        case 'M':
            gdb_handle_M(g_in_buf);
            break;
        case 'Z':
            if (g_in_buf[1] == '0' && g_in_buf[2] == ',')
                gdb_handle_breakpoint(g_in_buf, true);
            else
                gdb_send_packet("");
            break;
        case 'z':
            if (g_in_buf[1] == '0' && g_in_buf[2] == ',')
                gdb_handle_breakpoint(g_in_buf, false);
            else
                gdb_send_packet("");
            break;
        case 'c':
            if (g_in_buf[1] != '\0') {
                const char *p = g_in_buf + 1;
                regs->rip = hex_to_u64(&p);
            }
            regs->rflags &= ~(1ULL << 8);
            resume = true;
            break;
        case 's':
            if (g_in_buf[1] != '\0') {
                const char *p = g_in_buf + 1;
                regs->rip = hex_to_u64(&p);
            }
            regs->rflags |= (1ULL << 8);
            resume = true;
            break;
        case 'D':
            gdb_send_packet("OK");
            regs->rflags &= ~(1ULL << 8);
            resume = true;
            break;
        case 'k':
            acpi_power_off();
            for (;;)
                __asm__ volatile ("cli; hlt");
            break;
        case 'H':
            gdb_send_packet("OK");
            break;
        case 'q':
            if (k_strncmp(g_in_buf, "qSupported", 10) == 0) {
                gdb_send_packet("PacketSize=800;swbreak+");
            } else if (k_strcmp(g_in_buf, "qC") == 0) {
                gdb_send_packet("QC1");
            } else if (k_strncmp(g_in_buf, "qAttached", 9) == 0) {
                gdb_send_packet("1");
            } else if (k_strcmp(g_in_buf, "qfThreadInfo") == 0) {
                gdb_send_packet("m1");
            } else if (k_strcmp(g_in_buf, "qsThreadInfo") == 0) {
                gdb_send_packet("l");
            } else if (k_strncmp(g_in_buf, "qOffsets", 8) == 0) {
                gdb_send_packet("Text=0;Data=0;Bss=0");
            } else {
                gdb_send_packet("");
            }
            break;
        default:
            gdb_send_packet("");
            break;
        }
    }

    g_in_stub = false;
}
