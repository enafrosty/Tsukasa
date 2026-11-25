/*
 * Project Tsukasa — Per-process virtual address space and augmented RB-tree VMA
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

#include "../include/errno.h"
#include "../include/kprintf.h"
#include "../include/kutils.h"
#include "../include/paging.h"
#include "../include/smp.h"
#include "../proc/process.h"
#include "heap.h"
#include "pmm.h"
#include "vmm_x64.h"

#define RB_BLACK 0u
#define RB_RED   1u

#define VM_USER_STACK_MAX_BYTES (8ULL * 1024ULL * 1024ULL)

static inline uintptr_t align_up_page(uintptr_t v)
{
    return (v + (uintptr_t)(PAGE_SIZE - 1)) & ~(uintptr_t)(PAGE_SIZE - 1);
}

static inline uintptr_t align_down_page(uintptr_t v)
{
    return v & ~(uintptr_t)(PAGE_SIZE - 1);
}

static inline uint64_t read_rflags(void)
{
    uint64_t rf = 0;
#ifdef __x86_64__
    __asm__ volatile ("pushfq\n\tpopq %0" : "=r"(rf) : : "memory");
#endif
    return rf;
}

static void rwsem_init(vm_rwsem_t *sem)
{
    if (!sem)
        return;
    sem->lock = SPINLOCK_INIT;
    sem->count = 0;
    sem->writers_waiting = 0;
    wait_queue_init(&sem->waitq);
}

void vm_space_mmap_read_lock(vm_space_t *space)
{
    if (!space)
        return;
    vm_rwsem_t *sem = &space->mmap_rwsem;
    for (;;) {
        unsigned long flags = spin_lock_irqsave(&sem->lock);
        if (sem->count >= 0 && sem->writers_waiting == 0) {
            sem->count++;
            spin_unlock_irqrestore(&sem->lock, flags);
            return;
        }
        spin_unlock_irqrestore(&sem->lock, flags);

        process_t *cur = process_current();
        if (!cur || !(read_rflags() & (1ULL << 9))) {
            __asm__ volatile ("pause");
            continue;
        }

        wait_queue_entry_t we = { .proc = cur, .next = NULL };
        wait_queue_prepare_to_wait(&sem->waitq, &we);

        flags = spin_lock_irqsave(&sem->lock);
        int ok = (sem->count >= 0 && sem->writers_waiting == 0);
        if (ok)
            sem->count++;
        spin_unlock_irqrestore(&sem->lock, flags);

        if (ok) {
            wait_queue_finish_wait(&sem->waitq, &we);
            return;
        }

        process_yield();
        wait_queue_finish_wait(&sem->waitq, &we);
    }
}

void vm_space_mmap_read_unlock(vm_space_t *space)
{
    if (!space)
        return;
    vm_rwsem_t *sem = &space->mmap_rwsem;
    unsigned long flags = spin_lock_irqsave(&sem->lock);
    if (sem->count > 0)
        sem->count--;
    int wake = (sem->count == 0);
    spin_unlock_irqrestore(&sem->lock, flags);
    if (wake)
        wait_queue_wake_all(&sem->waitq);
}

void vm_space_mmap_write_lock(vm_space_t *space)
{
    if (!space)
        return;
    vm_rwsem_t *sem = &space->mmap_rwsem;
    for (;;) {
        unsigned long flags = spin_lock_irqsave(&sem->lock);
        if (sem->count == 0) {
            sem->count = -1;
            spin_unlock_irqrestore(&sem->lock, flags);
            return;
        }
        spin_unlock_irqrestore(&sem->lock, flags);

        process_t *cur = process_current();
        if (!cur || !(read_rflags() & (1ULL << 9))) {
            __asm__ volatile ("pause");
            continue;
        }

        wait_queue_entry_t we = { .proc = cur, .next = NULL };
        wait_queue_prepare_to_wait(&sem->waitq, &we);

        flags = spin_lock_irqsave(&sem->lock);
        sem->writers_waiting++;
        int ok = (sem->count == 0);
        if (ok) {
            sem->count = -1;
            sem->writers_waiting--;
        }
        spin_unlock_irqrestore(&sem->lock, flags);

        if (ok) {
            wait_queue_finish_wait(&sem->waitq, &we);
            return;
        }

        process_yield();

        flags = spin_lock_irqsave(&sem->lock);
        if (sem->writers_waiting > 0)
            sem->writers_waiting--;
        spin_unlock_irqrestore(&sem->lock, flags);
        wait_queue_finish_wait(&sem->waitq, &we);
    }
}

void vm_space_mmap_write_unlock(vm_space_t *space)
{
    if (!space)
        return;
    vm_rwsem_t *sem = &space->mmap_rwsem;
    unsigned long flags = spin_lock_irqsave(&sem->lock);
    if (sem->count < 0)
        sem->count = 0;
    spin_unlock_irqrestore(&sem->lock, flags);
    wait_queue_wake_all(&sem->waitq);
}

/* --- Augmented Red-Black Tree Helpers --- */

static void rb_recompute(vm_area_t *n)
{
    if (!n)
        return;

    uintptr_t min_s = n->start;
    uintptr_t max_e = n->end;
    uint64_t max_g = 0;

    if (n->rb_left) {
        if (n->rb_left->rb_subtree_min_start < min_s)
            min_s = n->rb_left->rb_subtree_min_start;
        if (n->rb_left->rb_subtree_max_end > max_e)
            max_e = n->rb_left->rb_subtree_max_end;
        if (n->rb_left->rb_subtree_max_gap > max_g)
            max_g = n->rb_left->rb_subtree_max_gap;
        if (n->start > n->rb_left->rb_subtree_max_end) {
            uint64_t g = (uint64_t)(n->start - n->rb_left->rb_subtree_max_end);
            if (g > max_g)
                max_g = g;
        }
    }

    if (n->rb_right) {
        if (n->rb_right->rb_subtree_min_start < min_s)
            min_s = n->rb_right->rb_subtree_min_start;
        if (n->rb_right->rb_subtree_max_end > max_e)
            max_e = n->rb_right->rb_subtree_max_end;
        if (n->rb_right->rb_subtree_max_gap > max_g)
            max_g = n->rb_right->rb_subtree_max_gap;
        if (n->rb_right->rb_subtree_min_start > n->end) {
            uint64_t g = (uint64_t)(n->rb_right->rb_subtree_min_start - n->end);
            if (g > max_g)
                max_g = g;
        }
    }

    n->rb_subtree_min_start = min_s;
    n->rb_subtree_max_end = max_e;
    n->rb_subtree_max_gap = max_g;
}

static void rb_recompute_up(vm_area_t *n)
{
    while (n) {
        rb_recompute(n);
        n = n->rb_parent;
    }
}

static vm_area_t *rb_min(vm_area_t *n)
{
    while (n && n->rb_left)
        n = n->rb_left;
    return n;
}

static vm_area_t *rb_max(vm_area_t *n)
{
    while (n && n->rb_right)
        n = n->rb_right;
    return n;
}

static vm_area_t *rb_next(vm_area_t *n)
{
    if (!n)
        return NULL;
    if (n->rb_right)
        return rb_min(n->rb_right);
    vm_area_t *p = n->rb_parent;
    while (p && n == p->rb_right) {
        n = p;
        p = p->rb_parent;
    }
    return p;
}

static vm_area_t *rb_prev(vm_area_t *n)
{
    if (!n)
        return NULL;
    if (n->rb_left)
        return rb_max(n->rb_left);
    vm_area_t *p = n->rb_parent;
    while (p && n == p->rb_left) {
        n = p;
        p = p->rb_parent;
    }
    return p;
}

static void rb_rotate_left(vm_space_t *space, vm_area_t *x)
{
    vm_area_t *y = x->rb_right;
    x->rb_right = y->rb_left;
    if (y->rb_left)
        y->rb_left->rb_parent = x;
    y->rb_parent = x->rb_parent;
    if (!x->rb_parent)
        space->vma_root = y;
    else if (x == x->rb_parent->rb_left)
        x->rb_parent->rb_left = y;
    else
        x->rb_parent->rb_right = y;
    y->rb_left = x;
    x->rb_parent = y;
    rb_recompute(x);
    rb_recompute(y);
}

static void rb_rotate_right(vm_space_t *space, vm_area_t *y)
{
    vm_area_t *x = y->rb_left;
    y->rb_left = x->rb_right;
    if (x->rb_right)
        x->rb_right->rb_parent = y;
    x->rb_parent = y->rb_parent;
    if (!y->rb_parent)
        space->vma_root = x;
    else if (y == y->rb_parent->rb_right)
        y->rb_parent->rb_right = x;
    else
        y->rb_parent->rb_left = x;
    x->rb_right = y;
    y->rb_parent = x;
    rb_recompute(y);
    rb_recompute(x);
}

static void rb_insert_fixup(vm_space_t *space, vm_area_t *z)
{
    while (z->rb_parent && z->rb_parent->rb_color == RB_RED) {
        vm_area_t *gp = z->rb_parent->rb_parent;
        if (!gp)
            break;
        if (z->rb_parent == gp->rb_left) {
            vm_area_t *y = gp->rb_right;
            if (y && y->rb_color == RB_RED) {
                z->rb_parent->rb_color = RB_BLACK;
                y->rb_color = RB_BLACK;
                gp->rb_color = RB_RED;
                z = gp;
            } else {
                if (z == z->rb_parent->rb_right) {
                    z = z->rb_parent;
                    rb_rotate_left(space, z);
                    gp = z->rb_parent->rb_parent;
                }
                z->rb_parent->rb_color = RB_BLACK;
                if (gp) {
                    gp->rb_color = RB_RED;
                    rb_rotate_right(space, gp);
                }
            }
        } else {
            vm_area_t *y = gp->rb_left;
            if (y && y->rb_color == RB_RED) {
                z->rb_parent->rb_color = RB_BLACK;
                y->rb_color = RB_BLACK;
                gp->rb_color = RB_RED;
                z = gp;
            } else {
                if (z == z->rb_parent->rb_left) {
                    z = z->rb_parent;
                    rb_rotate_right(space, z);
                    gp = z->rb_parent->rb_parent;
                }
                z->rb_parent->rb_color = RB_BLACK;
                if (gp) {
                    gp->rb_color = RB_RED;
                    rb_rotate_left(space, gp);
                }
            }
        }
    }
    if (space->vma_root)
        space->vma_root->rb_color = RB_BLACK;
}

static void rb_transplant(vm_space_t *space, vm_area_t *u, vm_area_t *v)
{
    if (!u->rb_parent)
        space->vma_root = v;
    else if (u == u->rb_parent->rb_left)
        u->rb_parent->rb_left = v;
    else
        u->rb_parent->rb_right = v;
    if (v)
        v->rb_parent = u->rb_parent;
}

static void rb_delete_fixup(vm_space_t *space, vm_area_t *x, vm_area_t *x_parent)
{
    while (x != space->vma_root && (!x || x->rb_color == RB_BLACK)) {
        if (!x_parent)
            break;
        if (x == x_parent->rb_left) {
            vm_area_t *w = x_parent->rb_right;
            if (w && w->rb_color == RB_RED) {
                w->rb_color = RB_BLACK;
                x_parent->rb_color = RB_RED;
                rb_rotate_left(space, x_parent);
                w = x_parent->rb_right;
            }
            if (!w || ((!w->rb_left || w->rb_left->rb_color == RB_BLACK) &&
                       (!w->rb_right || w->rb_right->rb_color == RB_BLACK))) {
                if (w)
                    w->rb_color = RB_RED;
                x = x_parent;
                x_parent = x->rb_parent;
            } else {
                if (!w->rb_right || w->rb_right->rb_color == RB_BLACK) {
                    if (w->rb_left)
                        w->rb_left->rb_color = RB_BLACK;
                    w->rb_color = RB_RED;
                    rb_rotate_right(space, w);
                    w = x_parent->rb_right;
                }
                if (w) {
                    w->rb_color = x_parent->rb_color;
                    if (w->rb_right)
                        w->rb_right->rb_color = RB_BLACK;
                }
                x_parent->rb_color = RB_BLACK;
                rb_rotate_left(space, x_parent);
                x = space->vma_root;
                break;
            }
        } else {
            vm_area_t *w = x_parent->rb_left;
            if (w && w->rb_color == RB_RED) {
                w->rb_color = RB_BLACK;
                x_parent->rb_color = RB_RED;
                rb_rotate_right(space, x_parent);
                w = x_parent->rb_left;
            }
            if (!w || ((!w->rb_right || w->rb_right->rb_color == RB_BLACK) &&
                       (!w->rb_left || w->rb_left->rb_color == RB_BLACK))) {
                if (w)
                    w->rb_color = RB_RED;
                x = x_parent;
                x_parent = x->rb_parent;
            } else {
                if (!w->rb_left || w->rb_left->rb_color == RB_BLACK) {
                    if (w->rb_right)
                        w->rb_right->rb_color = RB_BLACK;
                    w->rb_color = RB_RED;
                    rb_rotate_left(space, w);
                    w = x_parent->rb_left;
                }
                if (w) {
                    w->rb_color = x_parent->rb_color;
                    if (w->rb_left)
                        w->rb_left->rb_color = RB_BLACK;
                }
                x_parent->rb_color = RB_BLACK;
                rb_rotate_right(space, x_parent);
                x = space->vma_root;
                break;
            }
        }
    }
    if (x)
        x->rb_color = RB_BLACK;
    rb_recompute_up(x_parent);
}

static void rb_insert_node(vm_space_t *space, vm_area_t *node)
{
    vm_area_t *y = NULL;
    vm_area_t *x = space->vma_root;

    while (x) {
        y = x;
        if (node->start < x->start)
            x = x->rb_left;
        else
            x = x->rb_right;
    }

    node->rb_parent = y;
    node->rb_left = NULL;
    node->rb_right = NULL;
    node->rb_color = RB_RED;
    node->rb_subtree_min_start = node->start;
    node->rb_subtree_max_end = node->end;
    node->rb_subtree_max_gap = 0;

    if (!y)
        space->vma_root = node;
    else if (node->start < y->start)
        y->rb_left = node;
    else
        y->rb_right = node;

    rb_recompute_up(node);
    rb_insert_fixup(space, node);
    rb_recompute_up(node);
    space->vma_count++;
}

static void rb_remove_node(vm_space_t *space, vm_area_t *z)
{
    vm_area_t *y = z;
    vm_area_t *x = NULL;
    vm_area_t *x_parent = NULL;
    uint8_t y_orig_color = y->rb_color;

    if (!z->rb_left) {
        x = z->rb_right;
        x_parent = z->rb_parent;
        rb_transplant(space, z, z->rb_right);
    } else if (!z->rb_right) {
        x = z->rb_left;
        x_parent = z->rb_parent;
        rb_transplant(space, z, z->rb_left);
    } else {
        y = rb_min(z->rb_right);
        y_orig_color = y->rb_color;
        x = y->rb_right;
        if (y->rb_parent == z) {
            x_parent = y;
        } else {
            x_parent = y->rb_parent;
            rb_transplant(space, y, y->rb_right);
            y->rb_right = z->rb_right;
            if (y->rb_right)
                y->rb_right->rb_parent = y;
        }
        rb_transplant(space, z, y);
        y->rb_left = z->rb_left;
        if (y->rb_left)
            y->rb_left->rb_parent = y;
        y->rb_color = z->rb_color;
    }

    rb_recompute_up(x_parent);
    if (y_orig_color == RB_BLACK)
        rb_delete_fixup(space, x, x_parent);

    z->rb_parent = NULL;
    z->rb_left = NULL;
    z->rb_right = NULL;
    if (space->vma_count > 0)
        space->vma_count--;
}

static vm_area_t *vma_lower_bound_end(vm_area_t *root, uintptr_t addr)
{
    vm_area_t *res = NULL;
    vm_area_t *cur = root;

    while (cur) {
        if (cur->end > addr) {
            res = cur;
            cur = cur->rb_left;
        } else {
            cur = cur->rb_right;
        }
    }
    return res;
}

static vm_area_t *vma_upper_bound_start(vm_area_t *root, uintptr_t limit)
{
    vm_area_t *res = NULL;
    vm_area_t *cur = root;

    while (cur) {
        if (cur->start < limit) {
            res = cur;
            cur = cur->rb_right;
        } else {
            cur = cur->rb_left;
        }
    }
    return res;
}

static uintptr_t vma_search_gap_node(vm_area_t *node,
                                     uintptr_t min_addr,
                                     uintptr_t max_addr,
                                     size_t len)
{
    if (!node)
        return 0;
    if (node->rb_subtree_max_gap < (uint64_t)len)
        return 0;
    if (node->rb_subtree_max_end <= min_addr || node->rb_subtree_min_start >= max_addr)
        return 0;

    if (node->rb_left && node->rb_left->rb_subtree_max_gap >= (uint64_t)len) {
        uintptr_t r = vma_search_gap_node(node->rb_left, min_addr, max_addr, len);
        if (r)
            return r;
    }

    if (node->rb_left) {
        uintptr_t gs = node->rb_left->rb_subtree_max_end;
        uintptr_t ge = node->start;
        uintptr_t lo = (gs > min_addr) ? gs : min_addr;
        uintptr_t hi = (ge < max_addr) ? ge : max_addr;
        if (hi > lo && (size_t)(hi - lo) >= len)
            return lo;
    }

    if (node->rb_right) {
        uintptr_t gs = node->end;
        uintptr_t ge = node->rb_right->rb_subtree_min_start;
        uintptr_t lo = (gs > min_addr) ? gs : min_addr;
        uintptr_t hi = (ge < max_addr) ? ge : max_addr;
        if (hi > lo && (size_t)(hi - lo) >= len)
            return lo;
    }

    if (node->rb_right && node->rb_right->rb_subtree_max_gap >= (uint64_t)len)
        return vma_search_gap_node(node->rb_right, min_addr, max_addr, len);

    return 0;
}

/* --- Public VMA API --- */

vm_area_t *vma_find(vm_space_t *space, uintptr_t addr)
{
    if (!space)
        return NULL;
    vm_area_t *cur = space->vma_root;
    while (cur) {
        if (addr < cur->start)
            cur = cur->rb_left;
        else if (addr >= cur->end)
            cur = cur->rb_right;
        else
            return cur;
    }
    return NULL;
}

uintptr_t vma_find_unmapped_area(vm_space_t *space,
                                 uintptr_t min_addr,
                                 uintptr_t max_addr,
                                 size_t len)
{
    if (!space || len == 0)
        return 0;

    min_addr = align_up_page(min_addr);
    max_addr = align_down_page(max_addr);
    size_t aligned_len = (size_t)align_up_page((uintptr_t)len);
    if (aligned_len == 0 || max_addr <= min_addr || (size_t)(max_addr - min_addr) < aligned_len)
        return 0;

    if (!space->vma_root)
        return min_addr;

    vm_area_t *first = vma_lower_bound_end(space->vma_root, min_addr);
    if (!first || first->start >= max_addr)
        return min_addr;
    if (first->start > min_addr && (size_t)(first->start - min_addr) >= aligned_len)
        return min_addr;

    uintptr_t gap = vma_search_gap_node(space->vma_root, min_addr, max_addr, aligned_len);
    if (gap)
        return gap;

    vm_area_t *last = vma_upper_bound_start(space->vma_root, max_addr);
    if (last) {
        uintptr_t tail = (last->end > min_addr) ? last->end : min_addr;
        if (max_addr > tail && (size_t)(max_addr - tail) >= aligned_len)
            return tail;
    }

    return 0;
}

vm_area_t *vma_insert(vm_space_t *space,
                      uintptr_t start,
                      uintptr_t end,
                      uint32_t prot,
                      uint32_t flags)
{
    if (!space || start >= end)
        return NULL;
    if (!paging_is_page_aligned_uintptr(start) || !paging_is_page_aligned_uintptr(end))
        return NULL;

    vm_area_t *ov = vma_lower_bound_end(space->vma_root, start);
    if (ov && ov->start < end)
        return NULL;

    vm_area_t *vma = (vm_area_t *)kmalloc(sizeof(vm_area_t));
    if (!vma)
        return NULL;
    k_memset(vma, 0, sizeof(*vma));
    vma->start = start;
    vma->end = end;
    vma->prot = prot;
    vma->flags = flags;
    vma->vm_space = space;

    rb_insert_node(space, vma);
    return vma;
}

int vma_remove(vm_space_t *space, vm_area_t *vma)
{
    if (!space || !vma)
        return -1;
    rb_remove_node(space, vma);
    if (vma->close_cb)
        vma->close_cb(vma);
    kfree(vma);
    return 0;
}

static uint64_t map_flags_from_vma(uint32_t prot, uint32_t flags)
{
    uint64_t mf = PAGING_MAP_USER;
    if (prot & VMA_PROT_READ)
        mf |= PAGING_MAP_READ;
    if (prot & VMA_PROT_WRITE)
        mf |= PAGING_MAP_WRITE;
    if (prot & VMA_PROT_EXEC)
        mf |= PAGING_MAP_EXEC;
    if (flags & VMA_FLAG_DEVICE)
        mf |= PAGING_MAP_DEVICE;
    return mf;
}

static void account_vma_page_delta(vm_space_t *space, const vm_area_t *vma, int delta)
{
    if (!space)
        return;
    if (delta > 0) {
        __atomic_fetch_add(&space->mapped_pages, (uint32_t)delta, __ATOMIC_RELAXED);
        if (vma) {
            if (vma->flags & VMA_FLAG_FILE)
                __atomic_fetch_add(&space->file_pages, (uint32_t)delta, __ATOMIC_RELAXED);
            else if (vma->flags & VMA_FLAG_GROWSDOWN)
                __atomic_fetch_add(&space->stack_pages, (uint32_t)delta, __ATOMIC_RELAXED);
            else if (vma->flags & VMA_FLAG_HEAP)
                __atomic_fetch_add(&space->heap_pages, (uint32_t)delta, __ATOMIC_RELAXED);
            else if (vma->flags & VMA_FLAG_ANON)
                __atomic_fetch_add(&space->anon_pages, (uint32_t)delta, __ATOMIC_RELAXED);
        }
    } else if (delta < 0) {
        uint32_t sub = (uint32_t)(-delta);
        uint32_t cur = __atomic_load_n(&space->mapped_pages, __ATOMIC_RELAXED);
        __atomic_store_n(&space->mapped_pages, (cur > sub) ? (cur - sub) : 0u, __ATOMIC_RELAXED);
        if (vma) {
            if (vma->flags & VMA_FLAG_FILE) {
                uint32_t c = __atomic_load_n(&space->file_pages, __ATOMIC_RELAXED);
                __atomic_store_n(&space->file_pages, (c > sub) ? (c - sub) : 0u, __ATOMIC_RELAXED);
            } else if (vma->flags & VMA_FLAG_GROWSDOWN) {
                uint32_t c = __atomic_load_n(&space->stack_pages, __ATOMIC_RELAXED);
                __atomic_store_n(&space->stack_pages, (c > sub) ? (c - sub) : 0u, __ATOMIC_RELAXED);
            } else if (vma->flags & VMA_FLAG_HEAP) {
                uint32_t c = __atomic_load_n(&space->heap_pages, __ATOMIC_RELAXED);
                __atomic_store_n(&space->heap_pages, (c > sub) ? (c - sub) : 0u, __ATOMIC_RELAXED);
            } else if (vma->flags & VMA_FLAG_ANON) {
                uint32_t c = __atomic_load_n(&space->anon_pages, __ATOMIC_RELAXED);
                __atomic_store_n(&space->anon_pages, (c > sub) ? (c - sub) : 0u, __ATOMIC_RELAXED);
            }
        }
    }
}

void vm_space_account_vma_page(vm_space_t *space, const vm_area_t *vma, int delta)
{
    account_vma_page_delta(space, vma, delta);
}

static void unmap_vma_span_pages(vm_space_t *space,
                                 vm_area_t *vma,
                                 uintptr_t u_start,
                                 uintptr_t u_end,
                                 int do_shootdown)
{
    if (!space || !space->pml4_phys || u_start >= u_end)
        return;

    if (vma && vma->unmap_cb)
        vma->unmap_cb(vma, u_start, u_end);

    int free_frames = (!vma || !(vma->flags & (VMA_FLAG_SHM | VMA_FLAG_DEVICE)));
    uint64_t batch_phys[64];

    for (uintptr_t cur = u_start; cur < u_end;) {
        size_t batch_pages = 0;
        size_t batch_free = 0;
        uintptr_t batch_start = cur;

        while (cur < u_end && batch_pages < 64u) {
            uint64_t q_phys = 0;
            uint64_t q_flags = 0;
            if (vmm_unmap_page_collect(space->pml4_phys, cur, &q_phys, &q_flags) == 0) {
                account_vma_page_delta(space, vma, -1);
                if (free_frames && q_phys && (q_flags & VMM_X64_PTE_USER))
                    batch_phys[batch_free++] = q_phys;
            }
            cur += PAGE_SIZE;
            batch_pages++;
        }

        if (do_shootdown && batch_pages > 0)
            vmm_tlb_shootdown(space, batch_start, batch_pages);

        for (size_t i = 0; i < batch_free; i++)
            (void)pmm_page_put((uintptr_t)batch_phys[i]);
    }
}

static int vma_can_merge(const vm_area_t *a, const vm_area_t *b)
{
    if (!a || !b || a->end != b->start)
        return 0;
    if (a->prot != b->prot || a->flags != b->flags)
        return 0;
    if (a->flags & (VMA_FLAG_SHM | VMA_FLAG_DEVICE | VMA_FLAG_GROWSDOWN |
                    VMA_FLAG_HEAP | VMA_FLAG_ELF))
        return 0;
    if (a->file != b->file || a->close_cb != b->close_cb ||
        a->open_cb != b->open_cb || a->unmap_cb != b->unmap_cb ||
        a->fault_cb != b->fault_cb)
        return 0;
    if (a->file) {
        if (a->file_offset + (uint64_t)(a->end - a->start) != b->file_offset)
            return 0;
        if (a->file_size != b->file_size)
            return 0;
    }
    return 1;
}

static void vma_try_merge_around(vm_space_t *space, uintptr_t start, uintptr_t end)
{
    vm_area_t *cur = vma_lower_bound_end(space->vma_root, start);
    if (cur) {
        vm_area_t *prev = rb_prev(cur);
        if (prev && vma_can_merge(prev, cur)) {
            prev->end = cur->end;
            rb_remove_node(space, cur);
            if (cur->close_cb)
                cur->close_cb(cur);
            kfree(cur);
            rb_recompute_up(prev);
            cur = prev;
        }
    }

    while (cur && cur->start < end) {
        vm_area_t *next = rb_next(cur);
        if (next && vma_can_merge(cur, next)) {
            cur->end = next->end;
            rb_remove_node(space, next);
            if (next->close_cb)
                next->close_cb(next);
            kfree(next);
            rb_recompute_up(cur);
        } else {
            cur = next;
        }
    }
}

int vma_unmap_range(vm_space_t *space, uintptr_t start, size_t len)
{
    if (!space || len == 0)
        return -EINVAL;
    if (!paging_is_page_aligned_uintptr(start))
        return -EINVAL;

    uintptr_t aligned_len = align_up_page((uintptr_t)len);
    if (aligned_len == 0 || start + aligned_len < start)
        return -EINVAL;
    uintptr_t end = start + aligned_len;

    vm_area_t *vma = vma_lower_bound_end(space->vma_root, start);
    while (vma && vma->start < end) {
        vm_area_t *next = rb_next(vma);
        uintptr_t u_start = (vma->start > start) ? vma->start : start;
        uintptr_t u_end = (vma->end < end) ? vma->end : end;

        unmap_vma_span_pages(space, vma, u_start, u_end, 1);

        if (start <= vma->start && end >= vma->end) {
            (void)vma_remove(space, vma);
        } else if (start <= vma->start && end < vma->end) {
            if (vma->flags & VMA_FLAG_FILE)
                vma->file_offset += (uint64_t)(end - vma->start);
            vma->start = end;
            rb_recompute_up(vma);
        } else if (start > vma->start && end >= vma->end) {
            vma->end = start;
            rb_recompute_up(vma);
        } else {
            vm_area_t *right = (vm_area_t *)kmalloc(sizeof(vm_area_t));
            if (!right)
                return -ENOMEM;
            k_memset(right, 0, sizeof(*right));
            right->start = end;
            right->end = vma->end;
            right->prot = vma->prot;
            right->flags = vma->flags;
            right->vm_space = space;
            right->file = vma->file;
            right->file_size = vma->file_size;
            right->file_offset = (vma->flags & VMA_FLAG_FILE)
                                     ? (vma->file_offset + (uint64_t)(end - vma->start))
                                     : 0ULL;
            right->close_cb = vma->close_cb;
            right->open_cb = vma->open_cb;
            right->unmap_cb = vma->unmap_cb;
            right->fault_cb = vma->fault_cb;

            vma->end = start;
            rb_recompute_up(vma);
            if (right->open_cb)
                right->open_cb(right);
            rb_insert_node(space, right);
        }

        vma = next;
    }

    return 0;
}

int vma_protect_range(vm_space_t *space, uintptr_t start, size_t len, uint32_t prot)
{
    if (!space || len == 0)
        return -EINVAL;
    if (!paging_is_page_aligned_uintptr(start))
        return -EINVAL;
    if ((prot & ~(VMA_PROT_READ | VMA_PROT_WRITE | VMA_PROT_EXEC)) != 0)
        return -EINVAL;

    uintptr_t aligned_len = align_up_page((uintptr_t)len);
    if (aligned_len == 0 || start + aligned_len < start)
        return -ENOMEM;
    uintptr_t end = start + aligned_len;
    if (!paging_range_is_user(start, (size_t)(end - start)))
        return -ENOMEM;

    /* Verify entire range is covered by VMAs with no unmapped holes. */
    {
        vm_area_t *check = vma_find(space, start);
        uintptr_t pos = start;
        while (pos < end) {
            if (!check || check->start > pos)
                return -ENOMEM;
            pos = check->end;
            if (pos < end)
                check = rb_next(check);
        }
    }

    vm_area_t *vma = vma_lower_bound_end(space->vma_root, start);
    while (vma && vma->start < end) {
        vm_area_t *next = rb_next(vma);
        uintptr_t p_start = (vma->start > start) ? vma->start : start;
        uintptr_t p_end = (vma->end < end) ? vma->end : end;

        if (vma->prot != prot) {
            if (p_start > vma->start && p_end < vma->end) {
                vm_area_t *mid = (vm_area_t *)kmalloc(sizeof(vm_area_t));
                vm_area_t *right = (vm_area_t *)kmalloc(sizeof(vm_area_t));
                if (!mid || !right) {
                    if (mid) kfree(mid);
                    if (right) kfree(right);
                    return -ENOMEM;
                }
                *mid = *vma;
                mid->rb_parent = mid->rb_left = mid->rb_right = NULL;
                mid->start = p_start;
                mid->end = p_end;
                mid->prot = prot;
                if (mid->flags & VMA_FLAG_FILE)
                    mid->file_offset = vma->file_offset + (uint64_t)(p_start - vma->start);

                *right = *vma;
                right->rb_parent = right->rb_left = right->rb_right = NULL;
                right->start = p_end;
                right->end = vma->end;
                if (right->flags & VMA_FLAG_FILE)
                    right->file_offset = vma->file_offset + (uint64_t)(p_end - vma->start);

                vma->end = p_start;
                rb_recompute_up(vma);
                if (mid->open_cb) mid->open_cb(mid);
                if (right->open_cb) right->open_cb(right);
                rb_insert_node(space, mid);
                rb_insert_node(space, right);
            } else if (p_start > vma->start) {
                vm_area_t *right = (vm_area_t *)kmalloc(sizeof(vm_area_t));
                if (!right)
                    return -ENOMEM;
                *right = *vma;
                right->rb_parent = right->rb_left = right->rb_right = NULL;
                right->start = p_start;
                right->end = vma->end;
                right->prot = prot;
                if (right->flags & VMA_FLAG_FILE)
                    right->file_offset = vma->file_offset + (uint64_t)(p_start - vma->start);

                vma->end = p_start;
                rb_recompute_up(vma);
                if (right->open_cb) right->open_cb(right);
                rb_insert_node(space, right);
            } else if (p_end < vma->end) {
                vm_area_t *right = (vm_area_t *)kmalloc(sizeof(vm_area_t));
                if (!right)
                    return -ENOMEM;
                *right = *vma;
                right->rb_parent = right->rb_left = right->rb_right = NULL;
                right->start = p_end;
                right->end = vma->end;
                if (right->flags & VMA_FLAG_FILE)
                    right->file_offset = vma->file_offset + (uint64_t)(p_end - vma->start);

                vma->end = p_end;
                vma->prot = prot;
                rb_recompute_up(vma);
                if (right->open_cb) right->open_cb(right);
                rb_insert_node(space, right);
            } else {
                vma->prot = prot;
            }

            uint32_t hw_prot = prot;
            if ((vma->flags & VMA_FLAG_PRIVATE) ||
                ((vma->flags & (VMA_FLAG_FILE | VMA_FLAG_SHARED)) ==
                 (VMA_FLAG_FILE | VMA_FLAG_SHARED))) {
                hw_prot &= ~VMA_PROT_WRITE;
            }
            uint64_t map_flags = map_flags_from_vma(hw_prot, vma->flags);
            size_t pages = (size_t)((p_end - p_start) / PAGE_SIZE);
            if (pages > 0) {
                (void)vmm_protect_present_pages(space->pml4_phys, p_start, pages, map_flags);
                vmm_tlb_shootdown(space, p_start, pages);
            }
        }

        vma = next;
    }

    vma_try_merge_around(space, start, end);
    return 0;
}

int vma_expand_stack(vm_space_t *space, uintptr_t addr)
{
    if (!space)
        return -1;

    uintptr_t page_va = align_down_page(addr);
    if (!paging_range_is_user(page_va, PAGE_SIZE))
        return -1;

    vm_area_t *vma = vma_lower_bound_end(space->vma_root, page_va);
    if (!vma || !(vma->flags & VMA_FLAG_GROWSDOWN))
        return -1;
    if (page_va >= vma->start)
        return 0;
    if (vma->end <= page_va || (uint64_t)(vma->end - page_va) > VM_USER_STACK_MAX_BYTES)
        return -1;

    vm_area_t *prev = rb_prev(vma);
    if (prev && prev->end > page_va)
        return -1;

    vma->start = page_va;
    rb_recompute_up(vma);
    return 0;
}

int vm_space_handle_page_fault(vm_space_t *space, uintptr_t fault_addr, uint64_t error_code)
{
    if (!space || !space->owns_pml4 || !space->pml4_phys)
        return -1;
    if (error_code & 0x8ULL) /* RSVD violation */
        return -1;

    uintptr_t va = align_down_page(fault_addr);
    if (!paging_range_is_user(va, PAGE_SIZE))
        return -1;

    vm_space_mmap_read_lock(space);
    vm_area_t *vma = vma_find(space, va);
    if (!vma) {
        vm_space_mmap_read_unlock(space);
        vm_space_mmap_write_lock(space);
        int exp_rc = vma_expand_stack(space, va);
        vm_space_mmap_write_unlock(space);
        if (exp_rc != 0)
            return -1;
        vm_space_mmap_read_lock(space);
        vma = vma_find(space, va);
        if (!vma) {
            vm_space_mmap_read_unlock(space);
            return -1;
        }
    }

    if ((error_code & 0x2ULL) && !(vma->prot & VMA_PROT_WRITE)) {
        vm_space_mmap_read_unlock(space);
        return -1;
    }
    if ((error_code & 0x10ULL) && !(vma->prot & VMA_PROT_EXEC)) {
        vm_space_mmap_read_unlock(space);
        return -1;
    }
    if (!(vma->prot & VMA_PROT_READ)) {
        vm_space_mmap_read_unlock(space);
        return -1;
    }

    if (vma->fault_cb) {
        int rc = vma->fault_cb(vma, va, error_code);
        vm_space_mmap_read_unlock(space);
        return rc;
    }

    if (vma->flags & (VMA_FLAG_SHM | VMA_FLAG_DEVICE | VMA_FLAG_FILE)) {
        vm_space_mmap_read_unlock(space);
        return -1;
    }

    /* Anonymous MAP_PRIVATE demand paging: shared zero page on read, private frame on write/COW. */
    if ((error_code & 0x1ULL) != 0ULL) {
        if (!(error_code & 0x2ULL)) {
            vm_space_mmap_read_unlock(space);
            return -1;
        }
        uint64_t q_phys = 0, q_flags = 0;
        if (vmm_query_page(space->pml4_phys, va, &q_phys, &q_flags) != 0 ||
            !(q_flags & VMM_X64_PTE_PRESENT)) {
            vm_space_mmap_read_unlock(space);
            return -1;
        }
        uint64_t rw_flags = map_flags_from_vma(vma->prot, vma->flags);
        uintptr_t zero_phys = pmm_zero_page_phys();
        if (q_phys != (uint64_t)zero_phys && pmm_page_refcount((uintptr_t)q_phys) == 1u) {
            int urc = vmm_upgrade_user_page_write(space->pml4_phys, va, q_phys, rw_flags);
            if (urc == 0)
                vmm_tlb_shootdown(space, va, 1);
            vm_space_mmap_read_unlock(space);
            return (urc >= 0) ? 0 : -1;
        }
        uintptr_t new_phys = pmm_alloc_pages(1);
        if (!new_phys) {
            vm_space_mmap_read_unlock(space);
            return -1;
        }
        uint8_t *dst_kva = (uint8_t *)vmm_phys_to_virt((uint64_t)new_phys);
        const uint8_t *src_kva = (const uint8_t *)vmm_phys_to_virt(q_phys);
        k_memcpy(dst_kva, src_kva, PAGE_SIZE);
        page_t *npg = pmm_phys_to_page(new_phys);
        if (npg)
            npg->flags |= PAGE_FLAG_ANON;

        int crc = vmm_cow_replace_user_page(space->pml4_phys, va, q_phys, (uint64_t)new_phys, rw_flags);
        if (crc == 0) {
            vmm_tlb_shootdown(space, va, 1);
            (void)pmm_page_put((uintptr_t)q_phys);
            vm_space_mmap_read_unlock(space);
            return 0;
        }
        (void)pmm_page_put(new_phys);
        vm_space_mmap_read_unlock(space);
        return (crc == 1) ? 0 : -1;
    }

    if (!(error_code & 0x2ULL) && (vma->flags & VMA_FLAG_PRIVATE) &&
        !(vma->flags & VMA_FLAG_GROWSDOWN)) {
        uintptr_t zero_phys = pmm_zero_page_phys();
        if (zero_phys) {
            uint64_t ro_flags = map_flags_from_vma(vma->prot & ~VMA_PROT_WRITE, vma->flags);
            (void)pmm_page_get(zero_phys);
            int mrc = vmm_map_user_page_if_absent(space->pml4_phys, va, (uint64_t)zero_phys, ro_flags);
            if (mrc == 0) {
                account_vma_page_delta(space, vma, 1);
                vm_space_mmap_read_unlock(space);
                return 0;
            }
            (void)pmm_page_put(zero_phys);
            if (mrc == 1) {
                vm_space_mmap_read_unlock(space);
                return 0;
            }
            vm_space_mmap_read_unlock(space);
            return -1;
        }
    }

    uintptr_t phys = pmm_alloc_pages(1);
    if (!phys) {
        vm_space_mmap_read_unlock(space);
        return -1;
    }

    uint8_t *kva = (uint8_t *)vmm_phys_to_virt((uint64_t)phys);
    k_memset(kva, 0, PAGE_SIZE);
    page_t *pg = pmm_phys_to_page(phys);
    if (pg)
        pg->flags |= PAGE_FLAG_ANON;

    uint64_t map_flags = map_flags_from_vma(vma->prot, vma->flags);
    int mrc = vmm_map_user_page_if_absent(space->pml4_phys, va, (uint64_t)phys, map_flags);
    if (mrc == 0) {
        account_vma_page_delta(space, vma, 1);
        vm_space_mmap_read_unlock(space);
        return 0;
    }
    (void)pmm_page_put(phys);
    if (mrc == 1) {
        vm_space_mmap_read_unlock(space);
        return 0;
    }
    vm_space_mmap_read_unlock(space);
    return -1;
}

int vm_space_brk(vm_space_t *space, uintptr_t new_brk, uintptr_t *cur_out)
{
    if (!space || !space->owns_pml4)
        return -ENOMEM;

    vm_space_mmap_write_lock(space);

    if (space->brk_start == 0) {
        space->brk_start = (uintptr_t)0x10000000ULL;
        space->brk_current = space->brk_start;
    }
    if (space->brk_limit == 0 || space->brk_limit > space->user_max)
        space->brk_limit = space->user_max;

    if (new_brk == 0) {
        if (cur_out)
            *cur_out = space->brk_current;
        vm_space_mmap_write_unlock(space);
        return 0;
    }

    if (new_brk < space->brk_start || new_brk >= space->brk_limit) {
        if (cur_out)
            *cur_out = space->brk_current;
        vm_space_mmap_write_unlock(space);
        return -ENOMEM;
    }

    uintptr_t old_end = align_up_page(space->brk_current);
    uintptr_t new_end = align_up_page(new_brk);

    if (new_end > old_end) {
        vm_area_t *ov = vma_lower_bound_end(space->vma_root, old_end);
        if (ov && ov->start < new_end) {
            if (cur_out)
                *cur_out = space->brk_current;
            vm_space_mmap_write_unlock(space);
            return -ENOMEM;
        }

        vm_area_t *heap = vma_find(space, space->brk_start);
        if (heap && (heap->flags & VMA_FLAG_HEAP)) {
            heap->end = new_end;
            rb_recompute_up(heap);
        } else {
            if (!vma_insert(space, space->brk_start, new_end,
                            VMA_PROT_READ | VMA_PROT_WRITE,
                            VMA_FLAG_HEAP | VMA_FLAG_ANON | VMA_FLAG_PRIVATE)) {
                if (cur_out)
                    *cur_out = space->brk_current;
                vm_space_mmap_write_unlock(space);
                return -ENOMEM;
            }
        }
    } else if (new_end < old_end) {
        (void)vma_unmap_range(space, new_end, (size_t)(old_end - new_end));
    }

    space->brk_current = new_brk;
    if (cur_out)
        *cur_out = space->brk_current;
    vm_space_mmap_write_unlock(space);
    return 0;
}

/* --- Address Space Lifecycle --- */

static void vm_space_reset_common(vm_space_t *space)
{
    space->user_min = (uintptr_t)PAGING_USER_VA_MIN;
    space->user_max = (uintptr_t)PAGING_USER_VA_MAX;
    space->shm_cursor = (uintptr_t)VM_SPACE_SHM_BASE;
    space->anon_cursor = (uintptr_t)VM_SPACE_ANON_BASE;
    space->mapped_pages = 0;
    space->shm_pages = 0;
    space->anon_pages = 0;
    space->file_pages = 0;
    space->stack_pages = 0;
    space->heap_pages = 0;
    space->vma_root = NULL;
    space->vma_count = 0;
    rwsem_init(&space->mmap_rwsem);
    space->active_cpus = 0;
    space->brk_start = 0;
    space->brk_current = 0;
    space->brk_limit = (uintptr_t)VM_SPACE_SHM_BASE;
}

int vm_space_init_kernel(vm_space_t *space)
{
    if (!space)
        return -1;

    uint64_t cur = vmm_get_current_pml4();
    if (!cur)
        return -1;

    space->pml4_phys = cur;
    vm_space_reset_common(space);
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
    vm_space_reset_common(space);
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
    vm_space_reset_common(dst);
    dst->user_min = src->user_min;
    dst->user_max = src->user_max;
    dst->owns_pml4 = 1;
    return 0;
}

extern void vfs_vma_fork_pin_shared_page(const vm_area_t *vma, uintptr_t va);

int vm_space_fork(vm_space_t *src, vm_space_t *dst)
{
    if (!src || !dst)
        return -EINVAL;
    if (!src->owns_pml4 || !src->pml4_phys)
        return vm_space_create(dst);

    if (vm_space_create(dst) != 0)
        return -ENOMEM;

    vm_space_mmap_write_lock(src);

    dst->user_min = src->user_min;
    dst->user_max = src->user_max;
    dst->shm_cursor = src->shm_cursor;
    dst->anon_cursor = src->anon_cursor;
    dst->brk_start = src->brk_start;
    dst->brk_current = src->brk_current;
    dst->brk_limit = src->brk_limit;

    int rc = 0;
    vm_area_t *vma = rb_min(src->vma_root);
    while (vma) {
        vm_area_t *dst_vma = vma_insert(dst, vma->start, vma->end, vma->prot, vma->flags);
        if (!dst_vma) {
            rc = -ENOMEM;
            break;
        }
        dst_vma->file = vma->file;
        dst_vma->file_offset = vma->file_offset;
        dst_vma->file_size = vma->file_size;
        dst_vma->fault_cb = vma->fault_cb;
        dst_vma->open_cb = vma->open_cb;
        dst_vma->close_cb = vma->close_cb;
        dst_vma->unmap_cb = vma->unmap_cb;
        if (dst_vma->open_cb)
            dst_vma->open_cb(dst_vma);

        int is_private = ((vma->flags & VMA_FLAG_PRIVATE) &&
                          !(vma->flags & (VMA_FLAG_SHARED | VMA_FLAG_SHM | VMA_FLAG_DEVICE)))
                             ? 1
                             : 0;
        int is_shared_file = ((vma->flags & (VMA_FLAG_FILE | VMA_FLAG_SHARED)) ==
                              (VMA_FLAG_FILE | VMA_FLAG_SHARED))
                                 ? 1
                                 : 0;
        int need_sd = 0;

        for (uintptr_t va = vma->start; va < vma->end; va += PAGE_SIZE) {
            int copied = 0;
            uint64_t phys = 0;
            if (vmm_fork_copy_pte(src->pml4_phys, dst->pml4_phys, va,
                                  is_private, &copied, &phys, &need_sd) != 0) {
                rc = -ENOMEM;
                break;
            }
            if (!copied)
                continue;

            account_vma_page_delta(dst, dst_vma, 1);
            if (!(vma->flags & (VMA_FLAG_SHM | VMA_FLAG_DEVICE)) && phys != 0) {
                (void)pmm_page_get((uintptr_t)phys);
                if (is_shared_file && phys != (uint64_t)pmm_zero_page_phys())
                    vfs_vma_fork_pin_shared_page(dst_vma, va);
            }
        }
        if (need_sd)
            vmm_tlb_shootdown(src, vma->start, (size_t)((vma->end - vma->start) / PAGE_SIZE));
        if (rc != 0)
            break;

        vma = rb_next(vma);
    }

    vm_space_mmap_write_unlock(src);

    if (rc != 0) {
        vm_space_destroy(dst);
        return rc;
    }
    return 0;
}

void vm_space_rebind_vmas(vm_space_t *space)
{
    if (!space)
        return;
    rwsem_init(&space->mmap_rwsem);
    vm_area_t *vma = rb_min(space->vma_root);
    while (vma) {
        vma->vm_space = space;
        vma = rb_next(vma);
    }
}

void vm_space_destroy(vm_space_t *space)
{
    if (!space)
        return;

    while (space->vma_root) {
        vm_area_t *vma = rb_min(space->vma_root);
        if (!vma)
            break;
        unmap_vma_span_pages(space, vma, vma->start, vma->end, 0);
        (void)vma_remove(space, vma);
    }

    if (space->owns_pml4 && space->pml4_phys)
        vmm_destroy_address_space(space->pml4_phys);

    space->pml4_phys = 0;
    vm_space_reset_common(space);
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

uintptr_t vm_space_reserve_shm_range(vm_space_t *space, size_t page_count)
{
    if (!space || page_count == 0)
        return 0;

    if (page_count > ((size_t)-1 / (size_t)PAGE_SIZE))
        return 0;
    uint64_t span = (uint64_t)page_count * (uint64_t)PAGE_SIZE;
    uintptr_t base = (uintptr_t)VM_SPACE_SHM_BASE;
    uintptr_t limit = (uintptr_t)VM_SPACE_SHM_LIMIT;
    if (span == 0 || span > (uint64_t)(limit - base))
        return 0;

    uintptr_t start = (space->shm_cursor < base || space->shm_cursor >= limit)
                          ? base
                          : align_up_page(space->shm_cursor);
    uintptr_t found = scan_free_va_run(space->pml4_phys, start, limit, page_count);
    if (!found && start > base)
        found = scan_free_va_run(space->pml4_phys, base, limit, page_count);
    if (!found)
        return 0;

    uintptr_t next = (uintptr_t)((uint64_t)found + span);
    space->shm_cursor = (next >= limit || next < found) ? base : next;
    return found;
}

uintptr_t vm_space_reserve_anon_range(vm_space_t *space, size_t page_count)
{
    if (!space || page_count == 0)
        return 0;
    if (page_count > ((size_t)-1 / (size_t)PAGE_SIZE))
        return 0;

    size_t len = page_count * (size_t)PAGE_SIZE;
    uintptr_t found = vma_find_unmapped_area(space,
                                             (uintptr_t)VM_SPACE_ANON_BASE,
                                             (uintptr_t)VM_SPACE_ANON_LIMIT,
                                             len);
    if (found)
        space->anon_cursor = found + len;
    return found;
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

    __atomic_fetch_add(&space->mapped_pages, (uint32_t)page_count, __ATOMIC_RELAXED);
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

    vmm_tlb_shootdown(space, virt_addr, page_count);

    uint32_t cur = __atomic_load_n(&space->mapped_pages, __ATOMIC_RELAXED);
    __atomic_store_n(&space->mapped_pages,
                     (cur > page_count) ? (cur - (uint32_t)page_count) : 0u,
                     __ATOMIC_RELAXED);
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

    if (vmm_protect_pages(space->pml4_phys, virt_addr, page_count, map_flags) != 0)
        return -1;

    vmm_tlb_shootdown(space, virt_addr, page_count);
    return 0;
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

/* --- Kernel VMA & Augmented RB-Tree Self-Tests --- */

static int verify_rb_subtree(const vm_area_t *n, int *black_height_out)
{
    if (!n) {
        *black_height_out = 1;
        return 1;
    }

    if (n->rb_color == RB_RED) {
        if ((n->rb_left && n->rb_left->rb_color == RB_RED) ||
            (n->rb_right && n->rb_right->rb_color == RB_RED))
            return 0;
    }

    if (n->rb_left && n->rb_left->rb_subtree_max_end > n->start)
        return 0;
    if (n->rb_right && n->rb_right->rb_subtree_min_start < n->end)
        return 0;

    int lh = 0, rh = 0;
    if (!verify_rb_subtree(n->rb_left, &lh) || !verify_rb_subtree(n->rb_right, &rh))
        return 0;
    if (lh != rh)
        return 0;

    uintptr_t exp_min = n->rb_left ? n->rb_left->rb_subtree_min_start : n->start;
    uintptr_t exp_max = n->rb_right ? n->rb_right->rb_subtree_max_end : n->end;
    uint64_t exp_gap = 0;
    if (n->rb_left) {
        if (n->rb_left->rb_subtree_max_gap > exp_gap)
            exp_gap = n->rb_left->rb_subtree_max_gap;
        uint64_t lg = (uint64_t)(n->start - n->rb_left->rb_subtree_max_end);
        if (lg > exp_gap)
            exp_gap = lg;
    }
    if (n->rb_right) {
        if (n->rb_right->rb_subtree_max_gap > exp_gap)
            exp_gap = n->rb_right->rb_subtree_max_gap;
        uint64_t rg = (uint64_t)(n->rb_right->rb_subtree_min_start - n->end);
        if (rg > exp_gap)
            exp_gap = rg;
    }

    if (n->rb_subtree_min_start != exp_min ||
        n->rb_subtree_max_end != exp_max ||
        n->rb_subtree_max_gap != exp_gap)
        return 0;

    *black_height_out = lh + ((n->rb_color == RB_BLACK) ? 1 : 0);
    return 1;
}

static uintptr_t brute_find_gap(vm_space_t *space, uintptr_t min_a, uintptr_t max_a, size_t len)
{
    uintptr_t cur = min_a;
    vm_area_t *it = rb_min(space->vma_root);
    while (it) {
        if (it->end <= cur) {
            it = rb_next(it);
            continue;
        }
        if (it->start >= max_a)
            break;
        if (it->start > cur && (size_t)(it->start - cur) >= len)
            return cur;
        if (it->end > cur)
            cur = it->end;
        it = rb_next(it);
    }
    if (max_a > cur && (size_t)(max_a - cur) >= len)
        return cur;
    return 0;
}

void vm_space_run_selftests(void)
{
    int pass = 0;
    int fail = 0;
    vm_space_t sp;

    if (vm_space_create(&sp) != 0) {
        kprintf("[selftest] rb_vma FAIL: vm_space_create failed\n");
        return;
    }

    /* 1. Randomized insert/remove + O(log N) gap oracle check. */
    {
        int ok = 1;
        uint32_t lcg = 0xCAFEBABEu;
        const uintptr_t lo = (uintptr_t)VM_SPACE_ANON_BASE;
        const uintptr_t hi = (uintptr_t)VM_SPACE_ANON_BASE + 0x100000ULL; /* 256 pages */

        for (int step = 0; step < 200 && ok; step++) {
            lcg = lcg * 1664525u + 1013904223u;
            size_t npages = ((lcg >> 16) % 6u) + 1u;
            size_t len = npages * PAGE_SIZE;

            if ((lcg & 3u) != 0u) {
                uintptr_t got = vma_find_unmapped_area(&sp, lo, hi, len);
                uintptr_t exp = brute_find_gap(&sp, lo, hi, len);
                if (got != exp) {
                    ok = 0;
                    break;
                }
                if (got != 0) {
                    if (!vma_insert(&sp, got, got + len, VMA_PROT_READ | VMA_PROT_WRITE,
                                    VMA_FLAG_ANON | VMA_FLAG_PRIVATE)) {
                        ok = 0;
                        break;
                    }
                }
            } else if (sp.vma_count > 0) {
                uint32_t idx = (lcg >> 8) % sp.vma_count;
                vm_area_t *it = rb_min(sp.vma_root);
                while (idx-- > 0 && it)
                    it = rb_next(it);
                if (it)
                    (void)vma_remove(&sp, it);
            }

            int bh = 0;
            if (!verify_rb_subtree(sp.vma_root, &bh)) {
                ok = 0;
                break;
            }
            if (sp.vma_root && sp.vma_root->rb_color != RB_BLACK) {
                ok = 0;
                break;
            }
        }

        if (ok) {
            pass++;
            kprintf("[selftest] rb_vma aug/gap PASS (nodes=%u)\n", sp.vma_count);
        } else {
            fail++;
            kprintf("[selftest] rb_vma aug/gap FAIL\n");
        }
    }

    /* 2. VMA split + mprotect merge + partial unmap check. */
    {
        uintptr_t base = (uintptr_t)VM_SPACE_ANON_BASE + 0x2000000ULL;
        int ok = 1;
        vm_area_t *v = vma_insert(&sp, base, base + 8u * PAGE_SIZE,
                                  VMA_PROT_READ | VMA_PROT_WRITE,
                                  VMA_FLAG_ANON | VMA_FLAG_PRIVATE);
        if (!v)
            ok = 0;

        if (ok && vma_protect_range(&sp, base + 2u * PAGE_SIZE, 2u * PAGE_SIZE, VMA_PROT_READ) != 0)
            ok = 0;
        if (ok) {
            vm_area_t *a = vma_find(&sp, base);
            vm_area_t *b = vma_find(&sp, base + 2u * PAGE_SIZE);
            vm_area_t *c = vma_find(&sp, base + 4u * PAGE_SIZE);
            if (!a || !b || !c || a == b || b == c ||
                a->end != base + 2u * PAGE_SIZE ||
                b->end != base + 4u * PAGE_SIZE ||
                b->prot != VMA_PROT_READ ||
                c->end != base + 8u * PAGE_SIZE)
                ok = 0;
        }

        /* Restore prot and verify adjacent VMAs merge back into one. */
        if (ok && vma_protect_range(&sp, base + 2u * PAGE_SIZE, 2u * PAGE_SIZE,
                                    VMA_PROT_READ | VMA_PROT_WRITE) != 0)
            ok = 0;
        if (ok) {
            vm_area_t *m = vma_find(&sp, base);
            if (!m || m->start != base || m->end != base + 8u * PAGE_SIZE)
                ok = 0;
        }

        /* Partial middle unmap splits cleanly. */
        if (ok && vma_unmap_range(&sp, base + 3u * PAGE_SIZE, 2u * PAGE_SIZE) != 0)
            ok = 0;
        if (ok) {
            vm_area_t *l = vma_find(&sp, base + 2u * PAGE_SIZE);
            vm_area_t *g = vma_find(&sp, base + 3u * PAGE_SIZE);
            vm_area_t *r = vma_find(&sp, base + 5u * PAGE_SIZE);
            if (!l || g != NULL || !r || l->end != base + 3u * PAGE_SIZE ||
                r->start != base + 5u * PAGE_SIZE || r->end != base + 8u * PAGE_SIZE)
                ok = 0;
        }

        int bh = 0;
        if (ok && !verify_rb_subtree(sp.vma_root, &bh))
            ok = 0;

        if (ok) {
            pass++;
            kprintf("[selftest] rb_vma split/merge PASS\n");
        } else {
            fail++;
            kprintf("[selftest] rb_vma split/merge FAIL\n");
        }
    }

    vm_space_destroy(&sp);
    kprintf("[selftest] rb_vma done pass=%d errors=%d\n", pass, fail);
}
