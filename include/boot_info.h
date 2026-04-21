#ifndef TSUKASA_BOOT_INFO_H
#define TSUKASA_BOOT_INFO_H

#include <stddef.h>
#include <stdint.h>

#define TSUKASA_BOOT_INFO_MAGIC 0x54534B5341424F4FULL

/* Canonical x86_64 VA regions for migration planning and diagnostics. */
#define TSUKASA_VA_KERNEL_BASE 0xFFFFFFFF80000000ULL
#define TSUKASA_VA_DIRECT_BASE 0xFFFF800000000000ULL
#define TSUKASA_VA_MMIO_BASE   0xFFFF900000000000ULL

enum tsukasa_boot_mem_type {
    TSUKASA_BOOT_MEM_USABLE = 1,
    TSUKASA_BOOT_MEM_RESERVED = 2,
    TSUKASA_BOOT_MEM_ACPI_RECLAIMABLE = 3,
    TSUKASA_BOOT_MEM_ACPI_NVS = 4,
    TSUKASA_BOOT_MEM_BAD = 5,
    TSUKASA_BOOT_MEM_BOOTLOADER_RECLAIMABLE = 6,
    TSUKASA_BOOT_MEM_KERNEL_AND_MODULES = 7,
    TSUKASA_BOOT_MEM_FRAMEBUFFER = 8,
};

struct tsukasa_boot_memmap_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
};

struct tsukasa_boot_module {
    uint64_t address;
    uint64_t size;
    const char *path;
    const char *cmdline;
};

struct tsukasa_boot_info {
    uint64_t magic;

    uint64_t hhdm_offset;

    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t reserved0[7];

    uint64_t memmap_entry_count;
    const struct tsukasa_boot_memmap_entry *memmap_entries;

    uint64_t module_count;
    const struct tsukasa_boot_module *modules;
};

static inline int tsukasa_boot_info_is_valid(const void *opaque)
{
    const struct tsukasa_boot_info *bi = (const struct tsukasa_boot_info *)opaque;
    return bi && bi->magic == TSUKASA_BOOT_INFO_MAGIC;
}

#endif