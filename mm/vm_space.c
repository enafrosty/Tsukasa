/*
 * vm_space.c - Per-process virtual address space ownership.
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

uintptr_t vm_space_reserve_shm_range(vm_space_t *space, size_t page_count)
{
    uint64_t span;
    uintptr_t base;
    uintptr_t next;

    if (!space || page_count == 0)
        return 0;

    span = (uint64_t)page_count * (uint64_t)PAGE_SIZE;
    if (span == 0 || span > (uint64_t)SIZE_MAX)
        return 0;

    base = align_up_page(space->shm_cursor);
    next = (uintptr_t)((uint64_t)base + span);
    if (next < base)
        return 0;

    if (base < (uintptr_t)VM_SPACE_SHM_BASE || next > (uintptr_t)VM_SPACE_SHM_LIMIT)
        return 0;

    space->shm_cursor = next;
    return base;
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
