/*
 * Project Tsukasa — Per-process virtual address space ownership
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

#include "vm_space.h"

#include "../include/paging.h"
#include "vmm_x64.h"

static uintptr_t align_up_page(uintptr_t v)
{
    return (v + (uintptr_t)(PAGE_SIZE - 1)) & ~(uintptr_t)(PAGE_SIZE - 1);
}

int vm_space_init_kernel(vm_space_t *space)
{
    if (!space)
        return -1;

    space->pml4_phys = vmm_get_current_pml4();
    if (!space->pml4_phys)
        return -1;

    space->user_min = (uintptr_t)PAGING_USER_VA_MIN;
    space->user_max = (uintptr_t)PAGING_USER_VA_MAX;
    space->shm_cursor = (uintptr_t)VM_SPACE_SHM_BASE;
    space->anon_cursor = (uintptr_t)VM_SPACE_ANON_BASE;
    space->mapped_pages = 0;
    space->shm_pages = 0;
    space->owns_pml4 = 0;
    return 0;
}

int vm_space_create(vm_space_t *space)
{
    uint64_t pml4_phys = 0;
    if (!space)
        return -1;

    if (vmm_create_address_space(&pml4_phys) != 0)
        return -1;

    space->pml4_phys = pml4_phys;
    space->user_min = (uintptr_t)PAGING_USER_VA_MIN;
    space->user_max = (uintptr_t)PAGING_USER_VA_MAX;
    space->shm_cursor = (uintptr_t)VM_SPACE_SHM_BASE;
    space->anon_cursor = (uintptr_t)VM_SPACE_ANON_BASE;
    space->mapped_pages = 0;
    space->shm_pages = 0;
    space->owns_pml4 = 1;
    return 0;
}

int vm_space_clone(const vm_space_t *src, vm_space_t *dst)
{
    uint64_t pml4_phys = 0;
    if (!dst)
        return -1;

    if (!src || !src->pml4_phys)
        return vm_space_create(dst);

    if (vmm_clone_address_space(src->pml4_phys, &pml4_phys) != 0)
        return -1;

    dst->pml4_phys = pml4_phys;
    dst->user_min = src->user_min;
    dst->user_max = src->user_max;
    dst->shm_cursor = (uintptr_t)VM_SPACE_SHM_BASE;
    dst->anon_cursor = (uintptr_t)VM_SPACE_ANON_BASE;
    dst->mapped_pages = 0;
    dst->shm_pages = 0;
    dst->owns_pml4 = 1;
    return 0;
}

void vm_space_destroy(vm_space_t *space)
{
    if (!space)
        return;

    if (space->owns_pml4 && space->pml4_phys)
        vmm_destroy_address_space(space->pml4_phys);

    space->pml4_phys = 0;
    space->mapped_pages = 0;
    space->shm_pages = 0;
    space->shm_cursor = (uintptr_t)VM_SPACE_SHM_BASE;
    space->anon_cursor = (uintptr_t)VM_SPACE_ANON_BASE;
    space->owns_pml4 = 0;
}

int vm_space_contains_user_range(const vm_space_t *space, uintptr_t base, size_t size)
{
    if (!space || size == 0)
        return 0;
    if (!paging_is_page_aligned_uintptr(base))
        return 0;
    if ((size & (size_t)(PAGE_SIZE - 1)) != 0)
        return 0;
    if (!paging_range_is_user(base, size))
        return 0;

    return (base >= space->user_min &&
            ((uint64_t)base + (uint64_t)size - 1ULL) <= (uint64_t)space->user_max) ? 1 : 0;
}

#define VM_SPACE_ANON_RECLAIM_THRESHOLD 0x10000000ULL

static int va_page_is_mapped(uint64_t pml4_phys, uintptr_t va)
{
    uint64_t phys = 0;
    uint64_t flags = 0;

    if (!pml4_phys)
        return 0;
    if (vmm_query_page(pml4_phys, va, &phys, &flags) == 0 &&
        (flags & VMM_X64_PTE_PRESENT) != 0) {
        return 1;
    }
    return 0;
}

static uintptr_t scan_free_va_run(uint64_t pml4_phys,
                                  uintptr_t scan_start,
                                  uintptr_t scan_limit,
                                  size_t page_count)
{
    uint64_t span = (uint64_t)page_count * (uint64_t)PAGE_SIZE;
    uintptr_t candidate = scan_start;

    while (candidate <= scan_limit &&
           (uint64_t)(scan_limit - candidate) >= span) {
        size_t run = 0;
        while (run < page_count) {
            uintptr_t probe = candidate + (uintptr_t)(run * (size_t)PAGE_SIZE);
            if (va_page_is_mapped(pml4_phys, probe)) {
                candidate = probe + (uintptr_t)PAGE_SIZE;
                break;
            }
            run++;
        }
        if (run == page_count)
            return candidate;
    }

    return 0;
}

static uintptr_t vm_space_reserve_region(vm_space_t *space,
                                         uintptr_t *cursor_io,
                                         uintptr_t region_base,
                                         uintptr_t region_limit,
                                         size_t page_count,
                                         int prefer_reclaim)
{
    uint64_t span;
    uint64_t region_span;
    uintptr_t start;
    uintptr_t found;
    uintptr_t next;

    if (!space || !cursor_io || page_count == 0)
        return 0;
    if (region_limit <= region_base)
        return 0;
    if (page_count > ((size_t)-1 / (size_t)PAGE_SIZE))
        return 0;

    span = (uint64_t)page_count * (uint64_t)PAGE_SIZE;
    region_span = (uint64_t)region_limit - (uint64_t)region_base;
    if (span == 0 || span > region_span)
        return 0;

    start = (*cursor_io < region_base || *cursor_io >= region_limit)
                ? region_base
                : align_up_page(*cursor_io);
    if (start < region_base || start >= region_limit)
        start = region_base;

    /* Reclaim from base when sparse churn drifts far ahead of mapped pages. */
    if (prefer_reclaim && start > region_base) {
        found = scan_free_va_run(space->pml4_phys, region_base, region_limit, page_count);
        if (!found)
            return 0;
        next = (uintptr_t)((uint64_t)found + span);
        *cursor_io = (next >= region_limit || next < found) ? region_base : next;
        return found;
    }

    found = scan_free_va_run(space->pml4_phys, start, region_limit, page_count);
    if (!found && start > region_base) {
        uint64_t tail = (uint64_t)start + span - (uint64_t)PAGE_SIZE;
        uintptr_t wrap_limit = (tail < (uint64_t)region_limit && tail >= (uint64_t)start)
                                   ? (uintptr_t)tail
                                   : region_limit;
        found = scan_free_va_run(space->pml4_phys, region_base, wrap_limit, page_count);
    }

    if (!found)
        return 0;

    next = (uintptr_t)((uint64_t)found + span);
    *cursor_io = (next >= region_limit || next < found) ? region_base : next;
    return found;
}

uintptr_t vm_space_reserve_shm_range(vm_space_t *space, size_t page_count)
{
    if (!space)
        return 0;

    return vm_space_reserve_region(space,
                                   &space->shm_cursor,
                                   (uintptr_t)VM_SPACE_SHM_BASE,
                                   (uintptr_t)VM_SPACE_SHM_LIMIT,
                                   page_count,
                                   0);
}

uintptr_t vm_space_reserve_anon_range(vm_space_t *space, size_t page_count)
{
    int reclaim = 0;

    if (!space)
        return 0;

    if (space->anon_cursor > (uintptr_t)VM_SPACE_ANON_BASE) {
        uint64_t drift = (uint64_t)space->anon_cursor - (uint64_t)VM_SPACE_ANON_BASE;
        uint64_t live_bytes = (uint64_t)space->mapped_pages * (uint64_t)PAGE_SIZE;
        if (drift >= VM_SPACE_ANON_RECLAIM_THRESHOLD && drift > live_bytes * 2ULL)
            reclaim = 1;
    }

    return vm_space_reserve_region(space,
                                   &space->anon_cursor,
                                   (uintptr_t)VM_SPACE_ANON_BASE,
                                   (uintptr_t)VM_SPACE_ANON_LIMIT,
                                   page_count,
                                   reclaim);
}

int vm_space_map_user_pages(vm_space_t *space,
                            uintptr_t virt_addr,
                            uintptr_t phys_addr,
                            size_t page_count,
                            uint64_t map_flags)
{
    size_t span = page_count * (size_t)PAGE_SIZE;
    if (!space || page_count == 0)
        return -1;
    if ((map_flags & PAGING_MAP_USER) == 0)
        return -1;
    if (!vm_space_contains_user_range(space, virt_addr, span))
        return -1;
    if (!paging_is_page_aligned_uintptr(phys_addr))
        return -1;

    if (vmm_map_pages(space->pml4_phys, virt_addr, (uint64_t)phys_addr, page_count, map_flags) != 0)
        return -1;

    space->mapped_pages += (uint32_t)page_count;
    return 0;
}

int vm_space_unmap_user_pages(vm_space_t *space, uintptr_t virt_addr, size_t page_count)
{
    size_t span = page_count * (size_t)PAGE_SIZE;
    if (!space || page_count == 0)
        return -1;
    if (!vm_space_contains_user_range(space, virt_addr, span))
        return -1;

    if (vmm_unmap_pages(space->pml4_phys, virt_addr, page_count) != 0)
        return -1;

    if (space->mapped_pages > page_count)
        space->mapped_pages -= (uint32_t)page_count;
    else
        space->mapped_pages = 0;

    return 0;
}

int vm_space_protect_user_pages(vm_space_t *space,
                                uintptr_t virt_addr,
                                size_t page_count,
                                uint64_t map_flags)
{
    size_t span = page_count * (size_t)PAGE_SIZE;
    if (!space || page_count == 0)
        return -1;
    if ((map_flags & PAGING_MAP_USER) == 0)
        return -1;
    if (!vm_space_contains_user_range(space, virt_addr, span))
        return -1;

    return vmm_protect_pages(space->pml4_phys, virt_addr, page_count, map_flags);
}

void vm_space_note_shm_attach(vm_space_t *space, size_t page_count)
{
    if (!space || page_count == 0)
        return;
    space->shm_pages += (uint32_t)page_count;
}

void vm_space_note_shm_detach(vm_space_t *space, size_t page_count)
{
    if (!space || page_count == 0)
        return;
    if (space->shm_pages > page_count)
        space->shm_pages -= (uint32_t)page_count;
    else
        space->shm_pages = 0;
}
