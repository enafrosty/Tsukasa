/*
 * initrd.c - Initrd from Multiboot modules.
 */

#include "initrd.h"
#include "../include/multiboot.h"
#include <stddef.h>
#include <stdint.h>

static const void *initrd_data;
static size_t initrd_size;

void initrd_init_from_multiboot(const void *mb_info)
{
    initrd_data = NULL;
    initrd_size = 0;
    const struct multiboot_info *mb = (const struct multiboot_info *)mb_info;
    if (!mb || !(mb->flags & 8))
        return;
    if (mb->mods_count == 0)
        return;
    const struct multiboot_mod_list *mod = (const struct multiboot_mod_list *)mb->mods_addr;
    initrd_data = (const void *)(uintptr_t)mod[0].mod_start;
    initrd_size = mod[0].mod_end - mod[0].mod_start;
}

void initrd_init(void)
{
}

int initrd_lookup(const char *path, const void **data, size_t *size)
{
    if (!initrd_data || initrd_size == 0)
        return -1;
    if (!path || path[0] != '/')
        return -1;
    *data = initrd_data;
    *size = initrd_size;
    return 0;
}
