/*
 * Project Tsukasa — Per-process virtual address space and VMA tree
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

#include "../include/spinlock.h"
#include "../sys/wait_queue.h"

#define VM_SPACE_SHM_BASE   0x0000000040000000ULL
#define VM_SPACE_SHM_LIMIT  0x0000000080000000ULL

#define VM_SPACE_ANON_BASE  0x0000000080000000ULL
#define VM_SPACE_ANON_LIMIT 0x0000700000000000ULL

#define VMA_PROT_READ       0x1U
#define VMA_PROT_WRITE      0x2U
#define VMA_PROT_EXEC       0x4U

#define VMA_FLAG_ANON       (1U << 0)
#define VMA_FLAG_FILE       (1U << 1)
#define VMA_FLAG_SHARED     (1U << 2)
#define VMA_FLAG_PRIVATE    (1U << 3)
#define VMA_FLAG_GROWSDOWN  (1U << 4)
#define VMA_FLAG_SHM        (1U << 5)
#define VMA_FLAG_DEVICE     (1U << 6)
#define VMA_FLAG_HEAP       (1U << 7)
#define VMA_FLAG_ELF        (1U << 8)

typedef struct vm_rwsem {
    spinlock_t lock;
    int32_t count;
    uint32_t writers_waiting;
    wait_queue_head_t waitq;
} vm_rwsem_t;

typedef struct vm_area {
    uintptr_t start;
    uintptr_t end;
    uint32_t prot;
    uint32_t flags;
    void *file;
    uint64_t file_offset;
    uint64_t file_size;
    void (*close_cb)(struct vm_area *vma);
    void (*open_cb)(struct vm_area *vma);
    void (*unmap_cb)(struct vm_area *vma, uintptr_t start, uintptr_t end);
    int (*fault_cb)(struct vm_area *vma, uintptr_t va, uint64_t error_code);

    struct vm_area *rb_parent;
    struct vm_area *rb_left;
    struct vm_area *rb_right;
    uintptr_t rb_subtree_min_start;
    uintptr_t rb_subtree_max_end;
    uint64_t rb_subtree_max_gap;
    uint8_t rb_color;
} vm_area_t;

typedef struct vm_space {
    uint64_t pml4_phys;
    uintptr_t user_min;
    uintptr_t user_max;
    uintptr_t shm_cursor;
    uintptr_t anon_cursor;
    uint32_t mapped_pages;
    uint32_t shm_pages;
    uint32_t anon_pages;
    uint32_t file_pages;
    uint32_t stack_pages;
    uint32_t heap_pages;
    uint8_t owns_pml4;

    vm_area_t *vma_root;
    uint32_t vma_count;
    vm_rwsem_t mmap_rwsem;
    volatile uint64_t active_cpus;

    uintptr_t brk_start;
    uintptr_t brk_current;
    uintptr_t brk_limit;
} vm_space_t;

int vm_space_init_kernel(vm_space_t *space);
int vm_space_create(vm_space_t *space);
int vm_space_clone(const vm_space_t *src, vm_space_t *dst);
void vm_space_destroy(vm_space_t *space);

void vm_space_mmap_read_lock(vm_space_t *space);
void vm_space_mmap_read_unlock(vm_space_t *space);
void vm_space_mmap_write_lock(vm_space_t *space);
void vm_space_mmap_write_unlock(vm_space_t *space);

vm_area_t *vma_find(vm_space_t *space, uintptr_t addr);
uintptr_t vma_find_unmapped_area(vm_space_t *space,
                                 uintptr_t min_addr,
                                 uintptr_t max_addr,
                                 size_t len);
vm_area_t *vma_insert(vm_space_t *space,
                      uintptr_t start,
                      uintptr_t end,
                      uint32_t prot,
                      uint32_t flags);
int vma_remove(vm_space_t *space, vm_area_t *vma);
int vma_unmap_range(vm_space_t *space, uintptr_t start, size_t len);
int vma_protect_range(vm_space_t *space, uintptr_t start, size_t len, uint32_t prot);
int vma_expand_stack(vm_space_t *space, uintptr_t addr);
int vm_space_handle_page_fault(vm_space_t *space, uintptr_t fault_addr, uint64_t error_code);
int vm_space_brk(vm_space_t *space, uintptr_t new_brk, uintptr_t *cur_out);

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

void vm_space_run_selftests(void);

#endif /* TSUKASA_VM_SPACE_H */
