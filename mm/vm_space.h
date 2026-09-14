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

#ifndef TSUKASA_VM_SPACE_H
#define TSUKASA_VM_SPACE_H

#include <stddef.h>
#include <stdint.h>

#define VM_SPACE_SHM_BASE   0x0000000040000000ULL
#define VM_SPACE_SHM_LIMIT  0x0000000080000000ULL

#define VM_SPACE_ANON_BASE  0x0000000080000000ULL
#define VM_SPACE_ANON_LIMIT 0x0000700000000000ULL

typedef struct vm_space {
    uint64_t pml4_phys;
    uintptr_t user_min;
    uintptr_t user_max;
    uintptr_t shm_cursor;
    uintptr_t anon_cursor;
    uint32_t mapped_pages;
    uint32_t shm_pages;
    uint8_t owns_pml4;
} vm_space_t;

int vm_space_init_kernel(vm_space_t *space);
int vm_space_create(vm_space_t *space);
int vm_space_clone(const vm_space_t *src, vm_space_t *dst);
void vm_space_destroy(vm_space_t *space);

int vm_space_contains_user_range(const vm_space_t *space, uintptr_t base, size_t size);
uintptr_t vm_space_reserve_shm_range(vm_space_t *space, size_t page_count);
uintptr_t vm_space_reserve_anon_range(vm_space_t *space, size_t page_count);

int vm_space_map_user_pages(vm_space_t *space,
                            uintptr_t virt_addr,
                            uintptr_t phys_addr,
                            size_t page_count,
                            uint64_t map_flags);
int vm_space_unmap_user_pages(vm_space_t *space,
                              uintptr_t virt_addr,
                              size_t page_count);
int vm_space_protect_user_pages(vm_space_t *space,
                                uintptr_t virt_addr,
                                size_t page_count,
                                uint64_t map_flags);

void vm_space_note_shm_attach(vm_space_t *space, size_t page_count);
void vm_space_note_shm_detach(vm_space_t *space, size_t page_count);

#endif /* TSUKASA_VM_SPACE_H */
