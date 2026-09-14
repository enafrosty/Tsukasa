/*
 * Project Tsukasa — Multiboot specification structures (version 0.6.96)
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

#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

#define MULTIBOOT_INFO_MEMORY      0x00000001
#define MULTIBOOT_INFO_MEM_MAP     0x00000040
#define MULTIBOOT_INFO_MODS        0x00000008
#define MULTIBOOT_INFO_FRAMEBUFFER 0x00001000

#define MULTIBOOT_MEMORY_AVAILABLE 1

struct multiboot_info {
    unsigned int flags;
    unsigned int mem_lower;
    unsigned int mem_upper;
    unsigned int boot_device;
    unsigned int cmdline;
    unsigned int mods_count;
    unsigned int mods_addr;
    unsigned int syms[4];
    unsigned int mmap_length;
    unsigned int mmap_addr;
    unsigned int drives_length;
    unsigned int drives_addr;
    unsigned int config_table;
    unsigned int boot_loader_name;
    unsigned int apm_table;
    unsigned int vbe_control_info;
    unsigned int vbe_mode_info;
    unsigned short vbe_mode;
    unsigned short vbe_interface_seg;
    unsigned short vbe_interface_off;
    unsigned short vbe_interface_len;
    unsigned long long framebuffer_addr;
    unsigned int framebuffer_pitch;
    unsigned int framebuffer_width;
    unsigned int framebuffer_height;
    unsigned char framebuffer_bpp;
    unsigned char framebuffer_type;
    unsigned char framebuffer_pad[14];
} __attribute__((packed));

struct multiboot_mmap_entry {
    unsigned int size;
    unsigned long long addr;
    unsigned long long len;
    unsigned int type;
} __attribute__((packed));

struct multiboot_mod_list {
    unsigned int mod_start;
    unsigned int mod_end;
    unsigned int cmdline;
    unsigned int pad;
} __attribute__((packed));

#endif /* MULTIBOOT_H */
