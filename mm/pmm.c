/*
 * pmm.c - Physical Memory Manager implementation.
 * Parses Multiboot memory map and manages physical pages via bitmap.
 */

#include "pmm.h"
#include "../include/multiboot.h"
#include <stdbool.h>

/* External symbols from linker. */
extern char _kernel_start[];
extern char _kernel_end[];

/** Bitmap: 1 = free, 0 = allocated. */
static unsigned char pmm_bitmap[PMM_FRAME_COUNT / 8];

/** Total number of frames we are tracking. */
static size_t pmm_total_frames;

/** First frame index (after reserved regions). */
static size_t pmm_first_usable;

static inline size_t addr_to_frame(uintptr_t addr)
{
    return (size_t)(addr / PAGE_SIZE);
}

static inline uintptr_t frame_to_addr(size_t frame)
{
    return (uintptr_t)(frame * PAGE_SIZE);
}

static inline void bitmap_set(size_t frame, bool free)
{
    size_t byte = frame / 8;
    size_t bit = frame % 8;
    if (free)
        pmm_bitmap[byte] |= (1u << bit);
    else
        pmm_bitmap[byte] &= ~(1u << bit);
}

static inline bool bitmap_get(size_t frame)
{
    size_t byte = frame / 8;
    size_t bit = frame % 8;
    return (pmm_bitmap[byte] & (1u << bit)) != 0;
}

static void mark_region(uintptr_t start, uintptr_t end, bool free)
{
    size_t first = addr_to_frame(start);
    size_t last = addr_to_frame(end - 1);
    if (last >= PMM_FRAME_COUNT)
        last = PMM_FRAME_COUNT - 1;
    for (size_t i = first; i <= last; i++)
        bitmap_set(i, free);
}

int pmm_init(const void *mb_info)
{
    const struct multiboot_info *mb = (const struct multiboot_info *)mb_info;

    /* Initially mark all frames as allocated. */
    for (size_t i = 0; i < PMM_FRAME_COUNT; i++)
        bitmap_set(i, false);

    pmm_total_frames = PMM_FRAME_COUNT;
    pmm_first_usable = PMM_FRAME_COUNT;

    if (!mb || !(mb->flags & MULTIBOOT_INFO_MEM_MAP) || !mb->mmap_addr)
        return -1;

    uintptr_t mmap_end = mb->mmap_addr + mb->mmap_length;
    uintptr_t ptr = mb->mmap_addr;

    while (ptr + sizeof(struct multiboot_mmap_entry) <= mmap_end) {
        const struct multiboot_mmap_entry *ent =
            (const struct multiboot_mmap_entry *)ptr;

        uintptr_t base = (uintptr_t)(ent->addr & 0xFFFFFFFFu);
        uintptr_t len = (uintptr_t)(ent->len & 0xFFFFFFFFu);
        uintptr_t end = base + len;

        if (ent->type == MULTIBOOT_MEMORY_AVAILABLE && len > 0) {
            /* Mark available region as free. */
            if (base >= PMM_FRAME_COUNT * PAGE_SIZE)
                break;
            if (end > PMM_FRAME_COUNT * PAGE_SIZE)
                end = PMM_FRAME_COUNT * PAGE_SIZE;
            mark_region(base, end, true);

            if (base < pmm_first_usable * PAGE_SIZE && end > 0x100000) {
                size_t first = addr_to_frame(0x100000);
                if (first < pmm_first_usable)
                    pmm_first_usable = first;
            }
        }

        ptr += (ent->size >= 24) ? ent->size : 24;
    }

    /* Reserve low memory (0 - 1 MiB). */
    mark_region(0, 0x100000, false);

    /* Reserve kernel (.text, .rodata, .data, .bss). */
    uintptr_t kstart = (uintptr_t)_kernel_start;
    uintptr_t kend = (uintptr_t)_kernel_end;
    kend = (kend + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    mark_region(kstart, kend, false);

    /* Reserve Multiboot modules. */
    if (mb->flags & MULTIBOOT_INFO_MODS && mb->mods_count > 0) {
        const struct multiboot_mod_list *mod =
            (const struct multiboot_mod_list *)mb->mods_addr;
        for (unsigned int i = 0; i < mb->mods_count; i++) {
            uintptr_t mstart = mod[i].mod_start & ~(PAGE_SIZE - 1);
            uintptr_t mend = (mod[i].mod_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            mark_region(mstart, mend, false);
        }
    }

    /* Reserve bitmap (in .bss, already covered by kernel). */
    return 0;
}

uintptr_t pmm_alloc(void)
{
    return pmm_alloc_pages(1);
}

uintptr_t pmm_alloc_pages(size_t count)
{
    if (count == 0)
        return 0;

    for (size_t start = pmm_first_usable; start + count <= pmm_total_frames; start++) {
        bool found = true;
        for (size_t i = 0; i < count; i++) {
            if (!bitmap_get(start + i)) {
                found = false;
                start += i;
                break;
            }
        }
        if (found) {
            for (size_t i = 0; i < count; i++)
                bitmap_set(start + i, false);
            return frame_to_addr(start);
        }
    }
    return 0;
}

void pmm_free(uintptr_t phys)
{
    pmm_free_pages(phys, 1);
}

void pmm_free_pages(uintptr_t phys, size_t count)
{
    if (phys == 0 || count == 0)
        return;
    if ((phys & (PAGE_SIZE - 1)) != 0)
        return;

    size_t start = addr_to_frame(phys);
    for (size_t i = 0; i < count && (start + i) < pmm_total_frames; i++)
        bitmap_set(start + i, true);
}
