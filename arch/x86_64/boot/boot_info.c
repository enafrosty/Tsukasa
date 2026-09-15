/*
 * Project Tsukasa — Boot information abstraction and Limine interface
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

#include "limine.h"
#include "include/boot_info.h"

extern void kernel_main_x64(const struct tsukasa_boot_info *boot_info);

#define TSUKASA_BOOT_MEMMAP_MAX 512
#define TSUKASA_BOOT_MODULE_MAX 32

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[3] = {
    0xf9562b2d5c95a6c8,
    0x6a7b384944536bdc,
    3
};

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker = 0;

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0,
    .response = NULL,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0,
    .response = NULL,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0,
    .response = NULL,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST,
    .revision = 0,
    .response = NULL,
    .internal_module_count = 0,
    .internal_modules = NULL,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_kernel_address_request kernel_address_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0,
    .response = NULL,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_kernel_file_request kernel_file_request = {
    .id = LIMINE_KERNEL_FILE_REQUEST,
    .revision = 0,
    .response = NULL,
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_smp_request smp_request = {
    .id = LIMINE_SMP_REQUEST,
    .revision = 0,
    .response = NULL,
    .flags = 0,
};

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker = 0;

static struct tsukasa_boot_info g_boot_info;
static struct tsukasa_boot_memmap_entry g_memmap_entries[TSUKASA_BOOT_MEMMAP_MAX];
static struct tsukasa_boot_module g_modules[TSUKASA_BOOT_MODULE_MAX];

static uint32_t map_mem_type(uint64_t limine_type)
{
    switch (limine_type) {
    case LIMINE_MEMMAP_USABLE:
        return TSUKASA_BOOT_MEM_USABLE;
    case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
        return TSUKASA_BOOT_MEM_ACPI_RECLAIMABLE;
    case LIMINE_MEMMAP_ACPI_NVS:
        return TSUKASA_BOOT_MEM_ACPI_NVS;
    case LIMINE_MEMMAP_BAD_MEMORY:
        return TSUKASA_BOOT_MEM_BAD;
    case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
        return TSUKASA_BOOT_MEM_BOOTLOADER_RECLAIMABLE;
    case LIMINE_MEMMAP_KERNEL_AND_MODULES:
        return TSUKASA_BOOT_MEM_KERNEL_AND_MODULES;
    case LIMINE_MEMMAP_FRAMEBUFFER:
        return TSUKASA_BOOT_MEM_FRAMEBUFFER;
    default:
        return TSUKASA_BOOT_MEM_RESERVED;
    }
}

static void populate_boot_info(void)
{
    g_boot_info.magic = TSUKASA_BOOT_INFO_MAGIC;
    g_boot_info.hhdm_offset = 0;

    g_boot_info.framebuffer_addr = 0;
    g_boot_info.framebuffer_pitch = 0;
    g_boot_info.framebuffer_width = 0;
    g_boot_info.framebuffer_height = 0;
    g_boot_info.framebuffer_bpp = 0;
    g_boot_info.framebuffer_red_mask_size = 0;
    g_boot_info.framebuffer_red_mask_shift = 0;
    g_boot_info.framebuffer_green_mask_size = 0;
    g_boot_info.framebuffer_green_mask_shift = 0;
    g_boot_info.framebuffer_blue_mask_size = 0;
    g_boot_info.framebuffer_blue_mask_shift = 0;

    g_boot_info.memmap_entry_count = 0;
    g_boot_info.memmap_entries = g_memmap_entries;

    g_boot_info.module_count = 0;
    g_boot_info.modules = g_modules;

    if (hhdm_request.response)
        g_boot_info.hhdm_offset = hhdm_request.response->offset;

    if (framebuffer_request.response && framebuffer_request.response->framebuffer_count > 0) {
        struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
        if (fb) {
            uint64_t fb_addr = (uint64_t)(uintptr_t)fb->address;
            if (g_boot_info.hhdm_offset && fb_addr >= g_boot_info.hhdm_offset)
                fb_addr -= g_boot_info.hhdm_offset;

            g_boot_info.framebuffer_addr = fb_addr;
            g_boot_info.framebuffer_pitch = (uint32_t)fb->pitch;
            g_boot_info.framebuffer_width = (uint32_t)fb->width;
            g_boot_info.framebuffer_height = (uint32_t)fb->height;
            g_boot_info.framebuffer_bpp = (uint8_t)fb->bpp;
            g_boot_info.framebuffer_red_mask_size = fb->red_mask_size;
            g_boot_info.framebuffer_red_mask_shift = fb->red_mask_shift;
            g_boot_info.framebuffer_green_mask_size = fb->green_mask_size;
            g_boot_info.framebuffer_green_mask_shift = fb->green_mask_shift;
            g_boot_info.framebuffer_blue_mask_size = fb->blue_mask_size;
            g_boot_info.framebuffer_blue_mask_shift = fb->blue_mask_shift;
        }
    }

    if (memmap_request.response) {
        uint64_t count = memmap_request.response->entry_count;
        if (count > TSUKASA_BOOT_MEMMAP_MAX)
            count = TSUKASA_BOOT_MEMMAP_MAX;

        for (uint64_t i = 0; i < count; i++) {
            struct limine_memmap_entry *ent = memmap_request.response->entries[i];
            if (!ent)
                continue;

            g_memmap_entries[i].base = ent->base;
            g_memmap_entries[i].length = ent->length;
            g_memmap_entries[i].type = map_mem_type(ent->type);
            g_memmap_entries[i].reserved = 0;
        }

        g_boot_info.memmap_entry_count = count;
    }

    if (module_request.response) {
        uint64_t count = module_request.response->module_count;
        if (count > TSUKASA_BOOT_MODULE_MAX)
            count = TSUKASA_BOOT_MODULE_MAX;

        for (uint64_t i = 0; i < count; i++) {
            struct limine_file *mod = module_request.response->modules[i];
            if (!mod)
                continue;

            g_modules[i].address = (uint64_t)(uintptr_t)mod->address;
            g_modules[i].size = mod->size;
            g_modules[i].path = mod->path;
            g_modules[i].cmdline = mod->cmdline;
        }

        g_boot_info.module_count = count;
    }

    g_boot_info.cmdline = NULL;
    if (kernel_file_request.response && kernel_file_request.response->kernel_file)
        g_boot_info.cmdline = kernel_file_request.response->kernel_file->cmdline;

    (void)kernel_address_request;
}

const struct tsukasa_boot_info *tsukasa_boot_info_get(void)
{
    return &g_boot_info;
}

void tsukasa_x64_entry(void)
{
    populate_boot_info();
    kernel_main_x64(&g_boot_info);

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
