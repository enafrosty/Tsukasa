/*
 * Project Tsukasa — x86_64 Global Descriptor Table and TSS definitions
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

#ifndef TSUKASA_X64_GDT_H
#define TSUKASA_X64_GDT_H

#include <stdint.h>

#define X64_GDT_KERNEL_CS 0x08
#define X64_GDT_KERNEL_DS 0x10
#define X64_GDT_USER_DS   0x1B
#define X64_GDT_USER_CS   0x23
#define X64_GDT_TSS       0x28

void gdt_init_x64(void);
void tss_set_rsp0_x64(uint64_t rsp0);

void gdt_init_ap_tss(uint32_t cpu_count);
void gdt_load_ap_tss(uint32_t cpu_id);
void gdt_flush(void);
void gdt_reload_segments(void);
void syscall_init_x64(void);

#endif