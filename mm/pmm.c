/*
 * Project Tsukasa — Zoned Buddy Physical Memory Manager
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

#include "pmm.h"
#include "../include/multiboot.h"
#include "../include/boot_info.h"
#include "../include/spinlock.h"
#include "../include/kprintf.h"
#include "vmm_x64.h"
#include <stdbool.h>

extern char _kernel_start[];
extern char _kernel_end[];

#define PMM_ZONE_DMA_LIMIT    0x01000000ULL /* 16 MiB */
#define PMM_ZONE_DMA32_LIMIT  0x100000000ULL /* 4 GiB */

#define PMM_PCP_MAX_CPUS      64u
#define PMM_PCP_HIGH          64u
#define PMM_PCP_BATCH         16u

typedef struct pmm_zone_state {
    page_t *free_lists[PMM_MAX_ORDER + 1];
    size_t free_counts[PMM_MAX_ORDER + 1];
    size_t free_pages;
    size_t total_pages;
} pmm_zone_state_t;

typedef struct pmm_pcp_cache {
    spinlock_t lock;
    page_t *head;
    size_t count;
    size_t zone_counts[PMM_ZONE_COUNT];
    uint8_t initialized;
} pmm_pcp_cache_t;

static page_t *g_pages = NULL;
static size_t g_total_frames = 0;
static size_t g_usable_frames = 0;
static size_t g_buddy_free_frames = 0;
static size_t g_pcp_free_frames = 0;
static uint64_t g_hhdm_offset = 0;
static uintptr_t g_zero_page_phys = 0;

static pmm_zone_state_t g_zones[PMM_ZONE_COUNT];
static pmm_pcp_cache_t g_pcp[PMM_PCP_MAX_CPUS];
static spinlock_t g_pmm_lock = SPINLOCK_INIT;

static inline uintptr_t phys_to_hhdm(uint64_t phys)
{
    if (g_hhdm_offset)
        return (uintptr_t)(phys + g_hhdm_offset);
    return vmm_phys_to_virt(phys);
}

static inline uint8_t phys_to_zone(uint64_t phys)
{
    if (phys < PMM_ZONE_DMA_LIMIT)
        return (uint8_t)PMM_ZONE_DMA;
    if (phys < PMM_ZONE_DMA32_LIMIT)
        return (uint8_t)PMM_ZONE_DMA32;
    return (uint8_t)PMM_ZONE_NORMAL;
}

static inline uint64_t boot_addr_to_phys(const struct tsukasa_boot_info *bi, uint64_t addr)
{
    if (!bi)
        return addr;
    if (bi->hhdm_offset && addr >= bi->hhdm_offset)
        return addr - bi->hhdm_offset;
    if (addr >= TSUKASA_VA_KERNEL_BASE)
        return addr - TSUKASA_VA_KERNEL_BASE;
    return addr;
}

static inline uint32_t current_cpu_safe(void)
{
#ifdef __x86_64__
    extern uint32_t smp_this_cpu_id(void);
    uint32_t id = smp_this_cpu_id();
    return (id < PMM_PCP_MAX_CPUS) ? id : 0u;
#else
    return 0u;
#endif
}

static void free_list_push(int z, unsigned int order, page_t *pg)
{
    pmm_zone_state_t *zs = &g_zones[z];
    page_t *head = zs->free_lists[order];

    pg->flags = PAGE_FLAG_BUDDY;
    pg->order = (uint8_t)order;
    pg->refcount = 0;
    pg->prev = NULL;
    pg->next = head;
    if (head)
        head->prev = pg;
    zs->free_lists[order] = pg;
    zs->free_counts[order]++;
}

static void free_list_remove(int z, unsigned int order, page_t *pg)
{
    pmm_zone_state_t *zs = &g_zones[z];

    if (pg->prev)
        pg->prev->next = pg->next;
    else
        zs->free_lists[order] = pg->next;
    if (pg->next)
        pg->next->prev = pg->prev;

    pg->prev = NULL;
    pg->next = NULL;
    pg->flags &= ~PAGE_FLAG_BUDDY;
    if (zs->free_counts[order] > 0)
        zs->free_counts[order]--;
}

static page_t *buddy_alloc_zone_nolock(int z, unsigned int req_order)
{
    pmm_zone_state_t *zs = &g_zones[z];
    unsigned int cur_order = req_order;

    while (cur_order <= PMM_MAX_ORDER && !zs->free_lists[cur_order])
        cur_order++;

    if (cur_order > PMM_MAX_ORDER)
        return NULL;

    page_t *pg = zs->free_lists[cur_order];
    size_t pfn = (size_t)(pg - g_pages);
    free_list_remove(z, cur_order, pg);

    while (cur_order > req_order) {
        cur_order--;
        size_t split_pfn = pfn + (1ULL << cur_order);
        free_list_push(z, cur_order, &g_pages[split_pfn]);
    }

    size_t n = 1ULL << req_order;
    for (size_t i = 0; i < n; i++) {
        g_pages[pfn + i].flags = 0;
        g_pages[pfn + i].order = 0;
        g_pages[pfn + i].refcount = (i == 0) ? 1u : 0u;
    }
    pg->order = (uint8_t)req_order;
    pg->refcount = 1u;

    if (zs->free_pages >= n)
        zs->free_pages -= n;
    else
        zs->free_pages = 0;
    if (g_buddy_free_frames >= n)
        g_buddy_free_frames -= n;
    else
        g_buddy_free_frames = 0;

    return pg;
}

static void buddy_free_block_nolock(page_t *pg, unsigned int order)
{
    size_t pfn = (size_t)(pg - g_pages);
    int z = (int)pg->zone;
    size_t n = 1ULL << order;

    if (pfn >= g_total_frames || pfn + n > g_total_frames || z >= PMM_ZONE_COUNT)
        return;

    g_zones[z].free_pages += n;
    g_buddy_free_frames += n;

    while (order < PMM_MAX_ORDER) {
        size_t buddy_pfn = pfn ^ (1ULL << order);
        if (buddy_pfn >= g_total_frames)
            break;
        page_t *buddy = &g_pages[buddy_pfn];
        if ((int)buddy->zone != z)
            break;
        if ((buddy->flags & PAGE_FLAG_BUDDY) == 0 ||
            buddy->order != (uint8_t)order ||
            buddy->refcount != 0)
            break;

        free_list_remove(z, order, buddy);
        buddy->order = 0;
        if (buddy_pfn < pfn)
            pfn = buddy_pfn;
        order++;
    }

    free_list_push(z, order, &g_pages[pfn]);
}

static page_t *buddy_alloc_preferred_nolock(int start_zone, unsigned int order, int strict_dma)
{
    if (start_zone == PMM_ZONE_DMA && strict_dma)
        return buddy_alloc_zone_nolock(PMM_ZONE_DMA, order);

    for (int z = start_zone; z >= 0; z--) {
        page_t *pg = buddy_alloc_zone_nolock(z, order);
        if (pg)
            return pg;
    }
    return NULL;
}

static void buddy_free_block(page_t *pg, unsigned int order)
{
    unsigned long irq_flags = spin_lock_irqsave(&g_pmm_lock);
    buddy_free_block_nolock(pg, order);
    spin_unlock_irqrestore(&g_pmm_lock, irq_flags);
}

static int pcp_refill(pmm_pcp_cache_t *pcp)
{
    page_t *batch[PMM_PCP_BATCH];
    size_t got = 0;

    unsigned long irq_flags = spin_lock_irqsave(&g_pmm_lock);
    while (got < PMM_PCP_BATCH) {
        page_t *pg = buddy_alloc_preferred_nolock(PMM_ZONE_NORMAL, 0, 0);
        if (!pg)
            break;
        batch[got++] = pg;
    }
    spin_unlock_irqrestore(&g_pmm_lock, irq_flags);

    if (got == 0)
        return -1;

    irq_flags = spin_lock_irqsave(&pcp->lock);
    for (size_t i = 0; i < got; i++) {
        page_t *pg = batch[i];
        pg->flags = PAGE_FLAG_PCP;
        pg->refcount = 0;
        pg->order = 0;
        pg->prev = NULL;
        pg->next = pcp->head;
        pcp->head = pg;
        pcp->count++;
        pcp->zone_counts[pg->zone]++;
        __atomic_fetch_add(&g_pcp_free_frames, 1, __ATOMIC_RELAXED);
    }
    spin_unlock_irqrestore(&pcp->lock, irq_flags);
    return 0;
}

static void pcp_spill(pmm_pcp_cache_t *pcp, size_t target_count)
{
    page_t *batch[PMM_PCP_BATCH];

    for (;;) {
        size_t n = 0;
        unsigned long irq_flags = spin_lock_irqsave(&pcp->lock);
        while (pcp->count > target_count && n < PMM_PCP_BATCH && pcp->head) {
            page_t *pg = pcp->head;
            pcp->head = pg->next;
            pg->next = NULL;
            pg->flags &= ~PAGE_FLAG_PCP;
            pcp->count--;
            if (pcp->zone_counts[pg->zone] > 0)
                pcp->zone_counts[pg->zone]--;
            __atomic_fetch_sub(&g_pcp_free_frames, 1, __ATOMIC_RELAXED);
            batch[n++] = pg;
        }
        spin_unlock_irqrestore(&pcp->lock, irq_flags);

        if (n == 0)
            break;

        irq_flags = spin_lock_irqsave(&g_pmm_lock);
        for (size_t i = 0; i < n; i++)
            buddy_free_block_nolock(batch[i], 0);
        spin_unlock_irqrestore(&g_pmm_lock, irq_flags);
    }
}

static page_t *pcp_alloc_page(void)
{
    uint32_t cpu = current_cpu_safe();
    pmm_pcp_cache_t *pcp = &g_pcp[cpu];

    if (!pcp->initialized)
        return NULL;

    for (int attempt = 0; attempt < 2; attempt++) {
        unsigned long irq_flags = spin_lock_irqsave(&pcp->lock);
        if (pcp->head) {
            page_t *pg = pcp->head;
            pcp->head = pg->next;
            pg->next = NULL;
            pg->prev = NULL;
            pcp->count--;
            if (pcp->zone_counts[pg->zone] > 0)
                pcp->zone_counts[pg->zone]--;
            __atomic_fetch_sub(&g_pcp_free_frames, 1, __ATOMIC_RELAXED);
            pg->flags = 0;
            pg->order = 0;
            pg->refcount = 1u;
            spin_unlock_irqrestore(&pcp->lock, irq_flags);
            return pg;
        }
        spin_unlock_irqrestore(&pcp->lock, irq_flags);

        if (attempt == 0 && pcp_refill(pcp) != 0)
            break;
    }
    return NULL;
}

static void pcp_free_page(page_t *pg)
{
    uint32_t cpu = current_cpu_safe();
    pmm_pcp_cache_t *pcp = &g_pcp[cpu];

    if (!pcp->initialized) {
        buddy_free_block(pg, 0);
        return;
    }

    unsigned long irq_flags = spin_lock_irqsave(&pcp->lock);
    pg->flags = PAGE_FLAG_PCP;
    pg->order = 0;
    pg->refcount = 0;
    pg->prev = NULL;
    pg->next = pcp->head;
    pcp->head = pg;
    pcp->count++;
    pcp->zone_counts[pg->zone]++;
    __atomic_fetch_add(&g_pcp_free_frames, 1, __ATOMIC_RELAXED);
    size_t cur_cnt = pcp->count;
    spin_unlock_irqrestore(&pcp->lock, irq_flags);

    if (cur_cnt >= PMM_PCP_HIGH)
        pcp_spill(pcp, PMM_PCP_HIGH - PMM_PCP_BATCH);
}

static uint64_t check_reserved_overlap_boot(uint64_t start,
                                            uint64_t end,
                                            uint64_t kstart,
                                            uint64_t kend,
                                            const struct tsukasa_boot_info *bi)
{
    if (start < 0x100000ULL && end > 0ULL)
        return 0x100000ULL;

    if (kend > kstart && start < kend && end > kstart)
        return kend;

    if (bi && bi->modules && bi->module_count > 0) {
        for (uint64_t i = 0; i < bi->module_count; i++) {
            uint64_t mstart = boot_addr_to_phys(bi, bi->modules[i].address) & ~(uint64_t)(PAGE_SIZE - 1);
            uint64_t mend = (boot_addr_to_phys(bi, bi->modules[i].address) + bi->modules[i].size + PAGE_SIZE - 1) &
                            ~(uint64_t)(PAGE_SIZE - 1);
            if (mend > mstart && start < mend && end > mstart)
                return mend;
        }
    }
    return 0;
}

static uint64_t find_metadata_region_boot(const struct tsukasa_boot_info *bi,
                                          size_t meta_bytes,
                                          uint64_t kstart,
                                          uint64_t kend)
{
    uint64_t need = (meta_bytes + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);

    for (int pass = 0; pass < 2; pass++) {
        uint64_t min_base = (pass == 0) ? PMM_ZONE_DMA_LIMIT : 0x100000ULL;
        for (uint64_t i = 0; i < bi->memmap_entry_count; i++) {
            const struct tsukasa_boot_memmap_entry *ent = &bi->memmap_entries[i];
            if (ent->type != TSUKASA_BOOT_MEM_USABLE || ent->length < need)
                continue;

            uint64_t base = (ent->base + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
            uint64_t end = (ent->base + ent->length) & ~(uint64_t)(PAGE_SIZE - 1);
            if (base < min_base)
                base = min_base;

            uint64_t cand = base;
            while (cand + need <= end && cand + need > cand) {
                uint64_t hit_end = check_reserved_overlap_boot(cand, cand + need,
                                                               kstart, kend, bi);
                if (hit_end == 0)
                    return cand;
                cand = (hit_end + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
            }
        }
    }
    return 0;
}

static void mark_frames_reserved(uint64_t start, uint64_t end)
{
    if (end <= start || !g_pages)
        return;

    size_t first = (size_t)(start / PAGE_SIZE);
    size_t last = (size_t)((end - 1) / PAGE_SIZE);
    if (first >= g_total_frames)
        return;
    if (last >= g_total_frames)
        last = g_total_frames - 1;

    for (size_t pfn = first; pfn <= last; pfn++) {
        g_pages[pfn].flags = PAGE_FLAG_RESERVED;
        g_pages[pfn].refcount = 1u;
    }
}

static void build_buddy_from_pages(void)
{
    for (int z = 0; z < PMM_ZONE_COUNT; z++) {
        for (unsigned int o = 0; o <= PMM_MAX_ORDER; o++) {
            g_zones[z].free_lists[o] = NULL;
            g_zones[z].free_counts[o] = 0;
        }
        g_zones[z].free_pages = 0;
    }
    g_buddy_free_frames = 0;

    for (size_t pfn = 0; pfn < g_total_frames;) {
        if (g_pages[pfn].flags != 0) {
            pfn++;
            continue;
        }

        uint8_t z = g_pages[pfn].zone;
        unsigned int order = 0;
        size_t block = 1;

        while (order < PMM_MAX_ORDER) {
            size_t next_block = block << 1;
            if ((pfn & (next_block - 1)) != 0)
                break;
            if (pfn + next_block > g_total_frames)
                break;
            int can_merge = 1;
            for (size_t k = block; k < next_block; k++) {
                if (g_pages[pfn + k].flags != 0 || g_pages[pfn + k].zone != z) {
                    can_merge = 0;
                    break;
                }
            }
            if (!can_merge)
                break;
            order++;
            block = next_block;
        }

        free_list_push((int)z, order, &g_pages[pfn]);
        g_zones[z].free_pages += block;
        g_buddy_free_frames += block;
        pfn += block;
    }
}

int pmm_init(const void *boot_info)
{
    const struct multiboot_info *mb = (const struct multiboot_info *)boot_info;

    g_pages = NULL;
    g_total_frames = 0;
    g_usable_frames = 0;
    g_buddy_free_frames = 0;
    g_pcp_free_frames = 0;
    g_zero_page_phys = 0;

    for (int z = 0; z < PMM_ZONE_COUNT; z++) {
        for (unsigned int o = 0; o <= PMM_MAX_ORDER; o++) {
            g_zones[z].free_lists[o] = NULL;
            g_zones[z].free_counts[o] = 0;
        }
        g_zones[z].free_pages = 0;
        g_zones[z].total_pages = 0;
    }
    for (uint32_t c = 0; c < PMM_PCP_MAX_CPUS; c++) {
        g_pcp[c].lock = SPINLOCK_INIT;
        g_pcp[c].head = NULL;
        g_pcp[c].count = 0;
        g_pcp[c].initialized = 0;
        for (int z = 0; z < PMM_ZONE_COUNT; z++)
            g_pcp[c].zone_counts[z] = 0;
    }

    if (tsukasa_boot_info_is_valid(boot_info)) {
        const struct tsukasa_boot_info *bi = (const struct tsukasa_boot_info *)boot_info;
        uint64_t max_phys = 0;

        if (!bi->memmap_entries || bi->memmap_entry_count == 0)
            return -1;

        g_hhdm_offset = bi->hhdm_offset;

        for (uint64_t i = 0; i < bi->memmap_entry_count; i++) {
            const struct tsukasa_boot_memmap_entry *ent = &bi->memmap_entries[i];
            if (ent->type != TSUKASA_BOOT_MEM_USABLE || ent->length == 0)
                continue;
            uint64_t end = ent->base + ent->length;
            if (end > (uint64_t)PMM_FRAME_COUNT * PAGE_SIZE)
                end = (uint64_t)PMM_FRAME_COUNT * PAGE_SIZE;
            if (end > max_phys)
                max_phys = end;
        }

        g_total_frames = (size_t)(max_phys / PAGE_SIZE);
        if (g_total_frames == 0)
            return -1;

        uint64_t kstart = boot_addr_to_phys(bi, (uint64_t)(uintptr_t)_kernel_start) & ~(uint64_t)(PAGE_SIZE - 1);
        uint64_t kend = (boot_addr_to_phys(bi, (uint64_t)(uintptr_t)_kernel_end) + PAGE_SIZE - 1) &
                        ~(uint64_t)(PAGE_SIZE - 1);

        size_t meta_bytes = g_total_frames * sizeof(page_t);
        size_t meta_pages = (meta_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
        uint64_t meta_phys = find_metadata_region_boot(bi, meta_bytes, kstart, kend);
        if (!meta_phys)
            return -1;

        g_pages = (page_t *)phys_to_hhdm(meta_phys);
        uint8_t *meta_raw = (uint8_t *)g_pages;
        for (size_t b = 0; b < meta_pages * PAGE_SIZE; b++)
            meta_raw[b] = 0;

        for (size_t pfn = 0; pfn < g_total_frames; pfn++) {
            g_pages[pfn].flags = PAGE_FLAG_RESERVED;
            g_pages[pfn].refcount = 1u;
            g_pages[pfn].order = 0;
            g_pages[pfn].zone = phys_to_zone((uint64_t)pfn * PAGE_SIZE);
        }

        for (uint64_t i = 0; i < bi->memmap_entry_count; i++) {
            const struct tsukasa_boot_memmap_entry *ent = &bi->memmap_entries[i];
            if (ent->type != TSUKASA_BOOT_MEM_USABLE || ent->length == 0)
                continue;
            uint64_t base = (ent->base + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
            uint64_t end = (ent->base + ent->length) & ~(uint64_t)(PAGE_SIZE - 1);
            if (end <= base)
                continue;
            size_t first = (size_t)(base / PAGE_SIZE);
            size_t last = (size_t)((end - 1) / PAGE_SIZE);
            if (first >= g_total_frames)
                continue;
            if (last >= g_total_frames)
                last = g_total_frames - 1;
            for (size_t pfn = first; pfn <= last; pfn++) {
                if (g_pages[pfn].flags & PAGE_FLAG_RESERVED) {
                    g_pages[pfn].flags = 0;
                    g_pages[pfn].refcount = 0;
                    g_usable_frames++;
                    g_zones[g_pages[pfn].zone].total_pages++;
                }
            }
        }

        mark_frames_reserved(0, 0x100000ULL);
        mark_frames_reserved(kstart, kend);
        if (bi->modules && bi->module_count > 0) {
            for (uint64_t i = 0; i < bi->module_count; i++) {
                uint64_t mstart = boot_addr_to_phys(bi, bi->modules[i].address) & ~(uint64_t)(PAGE_SIZE - 1);
                uint64_t mend = (boot_addr_to_phys(bi, bi->modules[i].address) + bi->modules[i].size + PAGE_SIZE - 1) &
                                ~(uint64_t)(PAGE_SIZE - 1);
                mark_frames_reserved(mstart, mend);
            }
        }
        mark_frames_reserved(meta_phys, meta_phys + (uint64_t)meta_pages * PAGE_SIZE);

        build_buddy_from_pages();

        g_zero_page_phys = pmm_alloc_order(0);
        if (g_zero_page_phys) {
            uint8_t *zp = (uint8_t *)phys_to_hhdm(g_zero_page_phys);
            for (size_t b = 0; b < PAGE_SIZE; b++)
                zp[b] = 0;
            page_t *zpg = pmm_phys_to_page(g_zero_page_phys);
            if (zpg) {
                zpg->flags = PAGE_FLAG_RESERVED;
                zpg->refcount = 1u;
            }
        }

        kprintf("[pmm] buddy zones ready: frames=%u meta_pages=%u meta_pa=0x%08x%08x\n",
                (uint32_t)g_total_frames, (uint32_t)meta_pages,
                (uint32_t)(meta_phys >> 32), (uint32_t)meta_phys);
        return 0;
    }

    if (!mb || !(mb->flags & MULTIBOOT_INFO_MEM_MAP) || !mb->mmap_addr)
        return -1;

    uint64_t max_phys = 0;
    {
        uintptr_t mmap_end = mb->mmap_addr + mb->mmap_length;
        uintptr_t ptr = mb->mmap_addr;
        while (ptr + sizeof(struct multiboot_mmap_entry) <= mmap_end) {
            const struct multiboot_mmap_entry *ent = (const struct multiboot_mmap_entry *)ptr;
            if (ent->type == MULTIBOOT_MEMORY_AVAILABLE && ent->len > 0) {
                uint64_t end = ent->addr + ent->len;
                if (end > (uint64_t)PMM_FRAME_COUNT * PAGE_SIZE)
                    end = (uint64_t)PMM_FRAME_COUNT * PAGE_SIZE;
                if (end > max_phys)
                    max_phys = end;
            }
            ptr += (ent->size >= 24) ? ent->size : 24;
        }
    }

    g_total_frames = (size_t)(max_phys / PAGE_SIZE);
    if (g_total_frames == 0)
        return -1;

    uint64_t kstart = (uint64_t)(uintptr_t)_kernel_start & ~(uint64_t)(PAGE_SIZE - 1);
    uint64_t kend = ((uint64_t)(uintptr_t)_kernel_end + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
    size_t meta_bytes = g_total_frames * sizeof(page_t);
    size_t meta_pages = (meta_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t need = (uint64_t)meta_pages * PAGE_SIZE;
    uint64_t meta_phys = 0;

    {
        uintptr_t mmap_end = mb->mmap_addr + mb->mmap_length;
        uintptr_t ptr = mb->mmap_addr;
        while (ptr + sizeof(struct multiboot_mmap_entry) <= mmap_end) {
            const struct multiboot_mmap_entry *ent = (const struct multiboot_mmap_entry *)ptr;
            if (ent->type == MULTIBOOT_MEMORY_AVAILABLE && ent->len >= need) {
                uint64_t base = (ent->addr + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
                uint64_t end = (ent->addr + ent->len) & ~(uint64_t)(PAGE_SIZE - 1);
                if (base < 0x100000ULL)
                    base = 0x100000ULL;
                if (kend > base && kstart < end && kend > kstart)
                    base = kend;
                if (base + need <= end) {
                    meta_phys = base;
                    break;
                }
            }
            ptr += (ent->size >= 24) ? ent->size : 24;
        }
    }
    if (!meta_phys)
        return -1;

    g_pages = (page_t *)(uintptr_t)meta_phys;
    for (size_t b = 0; b < meta_pages * PAGE_SIZE; b++)
        ((uint8_t *)g_pages)[b] = 0;

    for (size_t pfn = 0; pfn < g_total_frames; pfn++) {
        g_pages[pfn].flags = PAGE_FLAG_RESERVED;
        g_pages[pfn].refcount = 1u;
        g_pages[pfn].zone = phys_to_zone((uint64_t)pfn * PAGE_SIZE);
    }

    {
        uintptr_t mmap_end = mb->mmap_addr + mb->mmap_length;
        uintptr_t ptr = mb->mmap_addr;
        while (ptr + sizeof(struct multiboot_mmap_entry) <= mmap_end) {
            const struct multiboot_mmap_entry *ent = (const struct multiboot_mmap_entry *)ptr;
            if (ent->type == MULTIBOOT_MEMORY_AVAILABLE && ent->len > 0) {
                uint64_t base = (ent->addr + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
                uint64_t end = (ent->addr + ent->len) & ~(uint64_t)(PAGE_SIZE - 1);
                if (end > base) {
                    size_t first = (size_t)(base / PAGE_SIZE);
                    size_t last = (size_t)((end - 1) / PAGE_SIZE);
                    if (last >= g_total_frames)
                        last = g_total_frames - 1;
                    for (size_t pfn = first; pfn <= last; pfn++) {
                        if (g_pages[pfn].flags & PAGE_FLAG_RESERVED) {
                            g_pages[pfn].flags = 0;
                            g_pages[pfn].refcount = 0;
                            g_usable_frames++;
                            g_zones[g_pages[pfn].zone].total_pages++;
                        }
                    }
                }
            }
            ptr += (ent->size >= 24) ? ent->size : 24;
        }
    }

    mark_frames_reserved(0, 0x100000ULL);
    mark_frames_reserved(kstart, kend);
    if (mb->flags & MULTIBOOT_INFO_MODS && mb->mods_count > 0) {
        const struct multiboot_mod_list *mod = (const struct multiboot_mod_list *)(uintptr_t)mb->mods_addr;
        for (unsigned int i = 0; i < mb->mods_count; i++) {
            uint64_t mstart = mod[i].mod_start & ~(uint64_t)(PAGE_SIZE - 1);
            uint64_t mend = (mod[i].mod_end + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
            mark_frames_reserved(mstart, mend);
        }
    }
    mark_frames_reserved(meta_phys, meta_phys + need);
    build_buddy_from_pages();
    return 0;
}

page_t *pmm_phys_to_page(uintptr_t phys)
{
    if (!g_pages)
        return NULL;
    size_t pfn = (size_t)((uint64_t)phys / PAGE_SIZE);
    if (pfn >= g_total_frames)
        return NULL;
    return &g_pages[pfn];
}

uintptr_t pmm_page_to_phys(const page_t *pg)
{
    if (!g_pages || !pg || pg < g_pages || pg >= g_pages + g_total_frames)
        return 0;
    size_t pfn = (size_t)(pg - g_pages);
    return (uintptr_t)(pfn * PAGE_SIZE);
}

uint32_t pmm_page_get(uintptr_t phys)
{
    page_t *pg = pmm_phys_to_page(phys);
    if (!pg)
        return 0;
    if (pg->flags & PAGE_FLAG_RESERVED)
        return __atomic_load_n(&pg->refcount, __ATOMIC_RELAXED);
    if (pg->flags & (PAGE_FLAG_BUDDY | PAGE_FLAG_PCP))
        return 0;

    uint32_t old = __atomic_load_n(&pg->refcount, __ATOMIC_RELAXED);
    for (;;) {
        if (old == 0)
            return 0;
        if (__atomic_compare_exchange_n(&pg->refcount, &old, old + 1u, 0,
                                        __ATOMIC_SEQ_CST, __ATOMIC_RELAXED)) {
            return old + 1u;
        }
    }
}

uint32_t pmm_page_put(uintptr_t phys)
{
    page_t *pg = pmm_phys_to_page(phys);
    if (!pg)
        return 0;
    if (pg->flags & PAGE_FLAG_RESERVED)
        return __atomic_load_n(&pg->refcount, __ATOMIC_RELAXED);
    if (pg->flags & (PAGE_FLAG_BUDDY | PAGE_FLAG_PCP))
        return 0;

    uint32_t old = __atomic_load_n(&pg->refcount, __ATOMIC_RELAXED);
    for (;;) {
        if (old == 0)
            return 0;
        if (__atomic_compare_exchange_n(&pg->refcount, &old, old - 1u, 0,
                                        __ATOMIC_SEQ_CST, __ATOMIC_RELAXED)) {
            uint32_t nw = old - 1u;
            if (nw == 0) {
                unsigned int ord = (unsigned int)pg->order;
                pg->flags &= ~(PAGE_FLAG_ANON | PAGE_FLAG_PAGECACHE |
                               PAGE_FLAG_SLAB | PAGE_FLAG_KERNEL);
                if (ord == 0)
                    pcp_free_page(pg);
                else
                    buddy_free_block(pg, ord);
            }
            return nw;
        }
    }
}

uint32_t pmm_page_refcount(uintptr_t phys)
{
    const page_t *pg = pmm_phys_to_page(phys);
    if (!pg)
        return 0;
    return __atomic_load_n(&pg->refcount, __ATOMIC_RELAXED);
}

uintptr_t pmm_alloc_order(unsigned int order)
{
    if (order > PMM_MAX_ORDER || !g_pages)
        return 0;

    if (order == 0) {
        page_t *pg = pcp_alloc_page();
        if (pg)
            return pmm_page_to_phys(pg);
    }

    unsigned long irq_flags = spin_lock_irqsave(&g_pmm_lock);
    page_t *pg = buddy_alloc_preferred_nolock(PMM_ZONE_NORMAL, order, 0);
    spin_unlock_irqrestore(&g_pmm_lock, irq_flags);
    return pg ? pmm_page_to_phys(pg) : 0;
}

void pmm_free_order(uintptr_t phys, unsigned int order)
{
    if (!phys || (phys & (PAGE_SIZE - 1)) != 0 || order > PMM_MAX_ORDER)
        return;

    page_t *pg = pmm_phys_to_page(phys);
    if (!pg || (pg->flags & (PAGE_FLAG_RESERVED | PAGE_FLAG_BUDDY | PAGE_FLAG_PCP)))
        return;

    uint32_t old = __atomic_load_n(&pg->refcount, __ATOMIC_RELAXED);
    for (;;) {
        if (old == 0)
            return;
        if (__atomic_compare_exchange_n(&pg->refcount, &old, old - 1u, 0,
                                        __ATOMIC_SEQ_CST, __ATOMIC_RELAXED)) {
            if (old - 1u == 0) {
                pg->flags &= ~(PAGE_FLAG_ANON | PAGE_FLAG_PAGECACHE |
                               PAGE_FLAG_SLAB | PAGE_FLAG_KERNEL);
                if (order == 0)
                    pcp_free_page(pg);
                else
                    buddy_free_block(pg, order);
            }
            return;
        }
    }
}

uintptr_t pmm_alloc_zone(int zone, unsigned int order)
{
    if (zone < 0 || zone >= PMM_ZONE_COUNT || order > PMM_MAX_ORDER || !g_pages)
        return 0;

    unsigned long irq_flags = spin_lock_irqsave(&g_pmm_lock);
    page_t *pg = buddy_alloc_preferred_nolock(zone, order, zone == PMM_ZONE_DMA);
    spin_unlock_irqrestore(&g_pmm_lock, irq_flags);
    return pg ? pmm_page_to_phys(pg) : 0;
}

void pmm_pcp_init_cpu(uint32_t cpu_id)
{
    if (cpu_id >= PMM_PCP_MAX_CPUS)
        return;
    pmm_pcp_cache_t *pcp = &g_pcp[cpu_id];
    unsigned long irq_flags = spin_lock_irqsave(&pcp->lock);
    if (!pcp->initialized) {
        pcp->head = NULL;
        pcp->count = 0;
        for (int z = 0; z < PMM_ZONE_COUNT; z++)
            pcp->zone_counts[z] = 0;
        pcp->initialized = 1;
    }
    spin_unlock_irqrestore(&pcp->lock, irq_flags);
}

void pmm_pcp_drain_all(void)
{
    for (uint32_t c = 0; c < PMM_PCP_MAX_CPUS; c++) {
        if (g_pcp[c].initialized)
            pcp_spill(&g_pcp[c], 0);
    }
}

void pmm_get_zone_stats(pmm_zone_stats_t *out)
{
    if (!out)
        return;

    size_t pcp_total = 0;
    size_t pcp_zone[PMM_ZONE_COUNT] = { 0, 0, 0 };

    for (uint32_t c = 0; c < PMM_PCP_MAX_CPUS; c++) {
        if (!g_pcp[c].initialized)
            continue;
        unsigned long flags = spin_lock_irqsave(&g_pcp[c].lock);
        pcp_total += g_pcp[c].count;
        for (int z = 0; z < PMM_ZONE_COUNT; z++)
            pcp_zone[z] += g_pcp[c].zone_counts[z];
        spin_unlock_irqrestore(&g_pcp[c].lock, flags);
    }

    unsigned long irq_flags = spin_lock_irqsave(&g_pmm_lock);
    for (int z = 0; z < PMM_ZONE_COUNT; z++) {
        out->total_pages[z] = g_zones[z].total_pages;
        out->free_pages[z] = g_zones[z].free_pages + pcp_zone[z];
        for (unsigned int o = 0; o <= PMM_MAX_ORDER; o++)
            out->free_by_order[z][o] = g_zones[z].free_counts[o];
    }
    out->pcp_pages = pcp_total;
    spin_unlock_irqrestore(&g_pmm_lock, irq_flags);
}

uintptr_t pmm_zero_page_phys(void)
{
    return g_zero_page_phys;
}

uintptr_t pmm_alloc(void)
{
    return pmm_alloc_order(0);
}

static void buddy_add_range_nolock(size_t pfn, size_t len)
{
    while (len > 0) {
        unsigned int ord = PMM_MAX_ORDER;
        while (ord > 0) {
            size_t sz = 1ULL << ord;
            if (sz <= len && (pfn & (sz - 1)) == 0 &&
                g_pages[pfn].zone == g_pages[pfn + sz - 1].zone)
                break;
            ord--;
        }
        size_t sz = 1ULL << ord;
        buddy_free_block_nolock(&g_pages[pfn], ord);
        pfn += sz;
        len -= sz;
    }
}

static uintptr_t pmm_alloc_large_contiguous(size_t count)
{
    pmm_pcp_drain_all();

    unsigned long irq_flags = spin_lock_irqsave(&g_pmm_lock);
    size_t dma_pfn = (size_t)(PMM_ZONE_DMA_LIMIT / PAGE_SIZE);

    for (int pass = 0; pass < 2; pass++) {
        size_t start_limit = (pass == 0 && dma_pfn < g_total_frames) ? dma_pfn : 0;
        size_t run_start = 0;
        size_t run_len = 0;

        for (size_t pfn = 0; pfn < g_total_frames;) {
            if (g_pages[pfn].flags & PAGE_FLAG_BUDDY) {
                size_t blk_sz = 1ULL << g_pages[pfn].order;
                if (pfn < start_limit) {
                    run_len = 0;
                    pfn += blk_sz;
                    continue;
                }
                if (run_len == 0)
                    run_start = pfn;
                run_len += blk_sz;
                if (run_len >= count) {
                    size_t req_end = run_start + count;
                    size_t cur = run_start;
                    while (cur < req_end) {
                        page_t *blk = &g_pages[cur];
                        size_t sz = 1ULL << blk->order;
                        int z = (int)blk->zone;
                        free_list_remove(z, blk->order, blk);
                        if (g_zones[z].free_pages >= sz)
                            g_zones[z].free_pages -= sz;
                        else
                            g_zones[z].free_pages = 0;
                        if (g_buddy_free_frames >= sz)
                            g_buddy_free_frames -= sz;
                        else
                            g_buddy_free_frames = 0;

                        for (size_t k = 0; k < sz; k++) {
                            g_pages[cur + k].flags = 0;
                            g_pages[cur + k].order = 0;
                            g_pages[cur + k].refcount = 0;
                        }
                        if (cur + sz > req_end)
                            buddy_add_range_nolock(req_end, (cur + sz) - req_end);
                        cur += sz;
                    }
                    for (size_t i = 0; i < count; i++) {
                        g_pages[run_start + i].flags = 0;
                        g_pages[run_start + i].order = 0;
                        g_pages[run_start + i].refcount = 1u;
                    }
                    spin_unlock_irqrestore(&g_pmm_lock, irq_flags);
                    return pmm_page_to_phys(&g_pages[run_start]);
                }
                pfn += blk_sz;
            } else {
                run_len = 0;
                pfn++;
            }
        }
    }

    spin_unlock_irqrestore(&g_pmm_lock, irq_flags);
    return 0;
}

uintptr_t pmm_alloc_pages(size_t count)
{
    if (count == 0 || !g_pages)
        return 0;
    if (count == 1)
        return pmm_alloc_order(0);

    unsigned int order = 0;
    while (order <= PMM_MAX_ORDER && (1ULL << order) < count)
        order++;
    if (order > PMM_MAX_ORDER)
        return pmm_alloc_large_contiguous(count);

    unsigned long irq_flags = spin_lock_irqsave(&g_pmm_lock);
    page_t *pg = buddy_alloc_preferred_nolock(PMM_ZONE_NORMAL, order, 0);
    if (!pg) {
        spin_unlock_irqrestore(&g_pmm_lock, irq_flags);
        return 0;
    }

    size_t pfn = (size_t)(pg - g_pages);
    size_t block = 1ULL << order;

    /* Mark the requested prefix as individually freeable order-0 pages. */
    for (size_t i = 0; i < count; i++) {
        g_pages[pfn + i].flags = 0;
        g_pages[pfn + i].order = 0;
        g_pages[pfn + i].refcount = 1u;
    }

    /* Return trailing excess frames immediately as power-of-two buddy chunks. */
    buddy_add_range_nolock(pfn + count, block - count);

    spin_unlock_irqrestore(&g_pmm_lock, irq_flags);
    return pmm_page_to_phys(pg);
}

void pmm_free(uintptr_t phys)
{
    if (!phys || (phys & (PAGE_SIZE - 1)) != 0)
        return;
    (void)pmm_page_put(phys);
}

void pmm_free_pages(uintptr_t phys, size_t count)
{
    if (!phys || count == 0 || (phys & (PAGE_SIZE - 1)) != 0)
        return;

    page_t *head = pmm_phys_to_page(phys);
    if (!head)
        return;

    if (head->order > 0 && (1ULL << head->order) == count) {
        pmm_free_order(phys, head->order);
        return;
    }

    for (size_t i = 0; i < count; i++)
        (void)pmm_page_put(phys + i * PAGE_SIZE);
}

uintptr_t pmm_total_page_count(void)
{
    return g_usable_frames ? (uintptr_t)g_usable_frames : (uintptr_t)g_total_frames;
}

uintptr_t pmm_free_page_count(void)
{
    size_t pcp_free = __atomic_load_n(&g_pcp_free_frames, __ATOMIC_RELAXED);
    return (uintptr_t)(g_buddy_free_frames + pcp_free);
}

uintptr_t pmm_used_page_count(void)
{
    size_t total = (size_t)pmm_total_page_count();
    size_t free_now = (size_t)pmm_free_page_count();
    if (total < free_now)
        return 0;
    return (uintptr_t)(total - free_now);
}

void pmm_run_selftests(void)
{
    int pass = 0;
    int fail = 0;
    uintptr_t base_free = pmm_free_page_count();

    /* 1. Buddy split/coalesce across orders 0..5. */
    {
        uintptr_t p5 = pmm_alloc_order(5);
        uintptr_t p0a = pmm_alloc_order(0);
        uintptr_t p0b = pmm_alloc_order(0);
        int ok = (p5 != 0 && p0a != 0 && p0b != 0 && (p5 & ((32u * PAGE_SIZE) - 1)) == 0);
        pmm_free_order(p0a, 0);
        pmm_free_order(p0b, 0);
        pmm_free_order(p5, 5);
        pmm_pcp_drain_all();
        if (ok && pmm_free_page_count() == base_free) {
            pass++;
            kprintf("[selftest] pmm buddy split/coalesce PASS\n");
        } else {
            fail++;
            kprintf("[selftest] pmm buddy split/coalesce FAIL before=%u after=%u\n",
                    (uint32_t)base_free, (uint32_t)pmm_free_page_count());
        }
    }

    /* 2. Non-power-of-two pmm_alloc_pages/pmm_free_pages leak check. */
    {
        uintptr_t p3 = pmm_alloc_pages(3);
        uintptr_t p5 = pmm_alloc_pages(5);
        uintptr_t mid = pmm_free_page_count();
        int ok = (p3 != 0 && p5 != 0 && mid == base_free - 8);
        pmm_free_pages(p3, 3);
        pmm_free_pages(p5, 5);
        pmm_pcp_drain_all();
        if (ok && pmm_free_page_count() == base_free) {
            pass++;
            kprintf("[selftest] pmm non-pow2 alloc/free PASS\n");
        } else {
            fail++;
            kprintf("[selftest] pmm non-pow2 alloc/free FAIL before=%u mid=%u after=%u\n",
                    (uint32_t)base_free, (uint32_t)mid, (uint32_t)pmm_free_page_count());
        }
    }

    /* 3. struct page refcount get/put lifecycle and DMA zone constraint. */
    {
        uintptr_t p = pmm_alloc_order(0);
        uintptr_t dma = pmm_alloc_zone(PMM_ZONE_DMA, 0);
        int ok = (p != 0 && pmm_page_refcount(p) == 1);
        if (ok)
            ok = (pmm_page_get(p) == 2 && pmm_page_refcount(p) == 2);
        if (ok)
            ok = (pmm_page_put(p) == 1 && pmm_free_page_count() < base_free);
        if (ok)
            ok = (pmm_page_put(p) == 0);
        if (dma) {
            if (dma >= PMM_ZONE_DMA_LIMIT)
                ok = 0;
            pmm_free_order(dma, 0);
        }
        pmm_pcp_drain_all();
        if (ok && pmm_free_page_count() == base_free) {
            pass++;
            kprintf("[selftest] pmm refcount/zone PASS\n");
        } else {
            fail++;
            kprintf("[selftest] pmm refcount/zone FAIL dma=0x%08x before=%u after=%u\n",
                    (uint32_t)dma, (uint32_t)base_free, (uint32_t)pmm_free_page_count());
        }
    }

    kprintf("[selftest] pmm done pass=%d errors=%d\n", pass, fail);
}
