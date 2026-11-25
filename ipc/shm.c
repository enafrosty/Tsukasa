/*
 * Project Tsukasa — Shared memory IPC implementation
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

#include "shm.h"

#include "../include/spinlock.h"
#include "../mm/pmm.h"

#include <stddef.h>
#include <stdint.h>

#include "../include/kprintf.h"
#include "../include/paging.h"
#include "../proc/process.h"
#include "../mm/vmm_x64.h"

#define SHM_MAX_REGIONS 64
#define SHM_MAX_ATTACHMENTS 256
#define SHM_CLEANUP_BATCH 256

struct shm_region {
    int id;
    uint32_t owner_pid;
    uintptr_t phys_base;
    size_t size;
    size_t page_count;
    uint32_t ref_count;
    int destroy_pending;
    int in_use;
};

struct shm_attachment {
    int in_use;
    int shm_id;
    uint32_t pid;
    uintptr_t virt_base;
    size_t page_count;
};

static struct shm_region g_regions[SHM_MAX_REGIONS];
static struct shm_attachment g_attachments[SHM_MAX_ATTACHMENTS];
static int g_next_shm_id = 1;
static spinlock_t g_shm_lock = SPINLOCK_INIT;

static struct shm_region *shm_find_region_locked(int id)
{
    for (int i = 0; i < SHM_MAX_REGIONS; i++) {
        if (g_regions[i].in_use && g_regions[i].id == id)
            return &g_regions[i];
    }
    return NULL;
}

static struct shm_region *shm_alloc_region_locked(void)
{
    for (int i = 0; i < SHM_MAX_REGIONS; i++) {
        if (!g_regions[i].in_use)
            return &g_regions[i];
    }
    return NULL;
}

static struct shm_attachment *shm_find_attachment_locked(uint32_t pid, int shm_id)
{
    for (int i = 0; i < SHM_MAX_ATTACHMENTS; i++) {
        if (!g_attachments[i].in_use)
            continue;
        if (g_attachments[i].pid == pid && g_attachments[i].shm_id == shm_id)
            return &g_attachments[i];
    }
    return NULL;
}

static struct shm_attachment *shm_find_attachment_by_addr_locked(uint32_t pid, uintptr_t virt_addr)
{
    for (int i = 0; i < SHM_MAX_ATTACHMENTS; i++) {
        if (!g_attachments[i].in_use)
            continue;
        if (g_attachments[i].pid == pid && g_attachments[i].virt_base == virt_addr)
            return &g_attachments[i];
    }
    return NULL;
}

static struct shm_attachment *shm_alloc_attachment_locked(void)
{
    for (int i = 0; i < SHM_MAX_ATTACHMENTS; i++) {
        if (!g_attachments[i].in_use)
            return &g_attachments[i];
    }
    return NULL;
}

static void shm_reap_region_locked(struct shm_region *region)
{
    uintptr_t phys;
    size_t pages;

    if (!region || !region->in_use)
        return;
    if (!region->destroy_pending || region->ref_count != 0)
        return;

    phys = region->phys_base;
    pages = region->page_count;

    region->in_use = 0;
    region->id = 0;
    region->owner_pid = 0;
    region->phys_base = 0;
    region->size = 0;
    region->page_count = 0;
    region->ref_count = 0;
    region->destroy_pending = 0;

    if (phys && pages)
        pmm_free_pages(phys, pages);
}

int shm_create(size_t size)
{
    process_t *cur = process_current();
    uintptr_t phys;
    size_t page_count;
    struct shm_region *r;
    int new_id;
    unsigned long flags;

    if (size == 0)
        return -1;

    page_count = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    phys = pmm_alloc_pages(page_count);
    if (!phys)
        return -1;

    uintptr_t hhdm = vmm_x64_hhdm_offset();
    if (hhdm) {
        uint8_t *kptr = (uint8_t *)(phys + hhdm);
        for (size_t b = 0; b < page_count * PAGE_SIZE; b++)
            kptr[b] = 0;
    }

    flags = spin_lock_irqsave(&g_shm_lock);
    r = shm_alloc_region_locked();
    if (!r) {
        spin_unlock_irqrestore(&g_shm_lock, flags);
        pmm_free_pages(phys, page_count);
        return -1;
    }

    new_id = g_next_shm_id++;
    if (g_next_shm_id < 1)
        g_next_shm_id = 1;

    r->in_use = 1;
    r->id = new_id;
    r->owner_pid = cur ? cur->pid : 0;
    r->phys_base = phys;
    r->size = page_count * PAGE_SIZE;
    r->page_count = page_count;
    r->ref_count = 0;
    r->destroy_pending = 0;
    spin_unlock_irqrestore(&g_shm_lock, flags);

    return new_id;
}

void *shm_attach(int shm_id)
{
    process_t *cur = process_current();
    struct shm_region *r;
    struct shm_attachment *att;
    uintptr_t virt_addr;
    uintptr_t phys_base;
    size_t page_count;
    unsigned long flags;

    if (!cur || shm_id <= 0)
        return NULL;

    flags = spin_lock_irqsave(&g_shm_lock);
    r = shm_find_region_locked(shm_id);
    if (!r || r->destroy_pending) {
        spin_unlock_irqrestore(&g_shm_lock, flags);
        return NULL;
    }

    att = shm_find_attachment_locked(cur->pid, shm_id);
    if (att) {
        virt_addr = att->virt_base;
        spin_unlock_irqrestore(&g_shm_lock, flags);
        return (void *)virt_addr;
    }

    virt_addr = vm_space_reserve_shm_range(&cur->vm_space, r->page_count);
    if (!virt_addr) {
        spin_unlock_irqrestore(&g_shm_lock, flags);
        return NULL;
    }

    att = shm_alloc_attachment_locked();
    if (!att) {
        spin_unlock_irqrestore(&g_shm_lock, flags);
        return NULL;
    }

    phys_base = r->phys_base;
    page_count = r->page_count;
    att->in_use = 1;
    att->pid = cur->pid;
    att->shm_id = shm_id;
    att->virt_base = virt_addr;
    att->page_count = page_count;
    r->ref_count++;
    spin_unlock_irqrestore(&g_shm_lock, flags);

    if (vm_space_map_user_pages(&cur->vm_space,
                                virt_addr,
                                phys_base,
                                page_count,
                                PAGING_MAP_READ | PAGING_MAP_WRITE |
                                PAGING_MAP_USER) != 0) {
        flags = spin_lock_irqsave(&g_shm_lock);
        att = shm_find_attachment_by_addr_locked(cur->pid, virt_addr);
        if (att)
            att->in_use = 0;
        r = shm_find_region_locked(shm_id);
        if (r && r->ref_count > 0)
            r->ref_count--;
        spin_unlock_irqrestore(&g_shm_lock, flags);
        return NULL;
    }

    vm_space_note_shm_attach(&cur->vm_space, page_count);
    cur->shm_attachment_count++;
    return (void *)virt_addr;
}

int shm_detach(void *addr)
{
    process_t *cur = process_current();
    struct shm_attachment *att;
    struct shm_region *r;
    uintptr_t virt_addr;
    size_t page_count;
    int shm_id;
    int needs_reap = 0;
    int unmap_rc = 0;
    int remaining = 0;
    unsigned long flags;

    if (!cur || !addr)
        return -1;

    virt_addr = (uintptr_t)addr;
    flags = spin_lock_irqsave(&g_shm_lock);
    att = shm_find_attachment_by_addr_locked(cur->pid, virt_addr);
    if (!att) {
        spin_unlock_irqrestore(&g_shm_lock, flags);
        return -1;
    }

    shm_id = att->shm_id;
    page_count = att->page_count;
    att->in_use = 0;

    for (int i = 0; i < SHM_MAX_ATTACHMENTS; i++) {
        if (g_attachments[i].in_use && g_attachments[i].pid == cur->pid) {
            remaining++;
        }
    }

    r = shm_find_region_locked(shm_id);
    if (r && r->ref_count > 0)
        r->ref_count--;
    if (r && r->destroy_pending && r->ref_count == 0)
        needs_reap = 1;
    spin_unlock_irqrestore(&g_shm_lock, flags);

    unmap_rc = vm_space_unmap_user_pages(&cur->vm_space, virt_addr, page_count);
    vm_space_note_shm_detach(&cur->vm_space, page_count);
    if (remaining == 0) {
        cur->shm_attachment_count = 0;
        cur->vm_space.shm_cursor = (uintptr_t)VM_SPACE_SHM_BASE + (((uintptr_t)cur->pid % 64) * 0x01000000ULL);
    } else {
        cur->shm_attachment_count = (uint32_t)remaining;
    }

    if (needs_reap) {
        flags = spin_lock_irqsave(&g_shm_lock);
        r = shm_find_region_locked(shm_id);
        if (r)
            shm_reap_region_locked(r);
        spin_unlock_irqrestore(&g_shm_lock, flags);
    }

    return unmap_rc;
}

int shm_process_fork(struct process *parent_proc, struct process *child_proc)
{
    process_t *parent = (process_t *)parent_proc;
    process_t *child = (process_t *)child_proc;
    struct shm_fork_item {
        int shm_id;
        uintptr_t virt_base;
        uintptr_t phys_base;
        size_t page_count;
    } items[SHM_CLEANUP_BATCH];
    size_t count = 0;
    unsigned long flags;

    if (!parent || !child)
        return -1;

    flags = spin_lock_irqsave(&g_shm_lock);
    for (int i = 0; i < SHM_MAX_ATTACHMENTS && count < SHM_CLEANUP_BATCH; i++) {
        struct shm_attachment *att = &g_attachments[i];
        struct shm_region *r;
        if (!att->in_use || att->pid != parent->pid)
            continue;
        r = shm_find_region_locked(att->shm_id);
        if (!r || r->destroy_pending)
            continue;

        struct shm_attachment *catt = shm_alloc_attachment_locked();
        if (!catt) {
            spin_unlock_irqrestore(&g_shm_lock, flags);
            shm_process_cleanup(child);
            return -1;
        }
        catt->in_use = 1;
        catt->pid = child->pid;
        catt->shm_id = att->shm_id;
        catt->virt_base = att->virt_base;
        catt->page_count = att->page_count;
        r->ref_count++;

        items[count].shm_id = att->shm_id;
        items[count].virt_base = att->virt_base;
        items[count].phys_base = r->phys_base;
        items[count].page_count = att->page_count;
        count++;
    }
    spin_unlock_irqrestore(&g_shm_lock, flags);

    for (size_t i = 0; i < count; i++) {
        if (vm_space_map_user_pages(&child->vm_space,
                                    items[i].virt_base,
                                    items[i].phys_base,
                                    items[i].page_count,
                                    PAGING_MAP_READ | PAGING_MAP_WRITE |
                                    PAGING_MAP_USER) != 0) {
            shm_process_cleanup(child);
            return -1;
        }
        vm_space_note_shm_attach(&child->vm_space, items[i].page_count);
        child->shm_attachment_count++;
    }
    return 0;
}

void *shm_map(int shm_id)
{
    return shm_attach(shm_id);
}

int shm_unmap(void *addr)
{
    return shm_detach(addr);
}

int shm_destroy(int shm_id)
{
    struct shm_region *r;
    unsigned long flags;

    if (shm_id <= 0)
        return -1;

    flags = spin_lock_irqsave(&g_shm_lock);
    r = shm_find_region_locked(shm_id);
    if (!r) {
        spin_unlock_irqrestore(&g_shm_lock, flags);
        return -1;
    }
    r->destroy_pending = 1;
    if (r->ref_count == 0) {
        shm_reap_region_locked(r);
    }
    spin_unlock_irqrestore(&g_shm_lock, flags);
    return 0;
}

void shm_process_cleanup(struct process *proc)
{
    process_t *p = (process_t *)proc;
    uintptr_t addrs[SHM_CLEANUP_BATCH];
    size_t pages[SHM_CLEANUP_BATCH];
    int ids[SHM_CLEANUP_BATCH];
    size_t count = 0;
    unsigned long flags;

    if (!p)
        return;

    flags = spin_lock_irqsave(&g_shm_lock);
    for (int i = 0; i < SHM_MAX_ATTACHMENTS; i++) {
        struct shm_attachment *att = &g_attachments[i];
        struct shm_region *r;
        if (!att->in_use || att->pid != p->pid)
            continue;
        if (count < SHM_CLEANUP_BATCH) {
            addrs[count] = att->virt_base;
            pages[count] = att->page_count;
            ids[count] = att->shm_id;
            count++;
        }
        r = shm_find_region_locked(att->shm_id);
        if (r && r->ref_count > 0)
            r->ref_count--;
        att->in_use = 0;
    }

    for (int i = 0; i < SHM_MAX_REGIONS; i++) {
        struct shm_region *r = &g_regions[i];
        if (!r->in_use)
            continue;
        if (r->owner_pid == p->pid) {
            r->owner_pid = 0;
            r->destroy_pending = 1;
        }
    }
    spin_unlock_irqrestore(&g_shm_lock, flags);

    for (size_t i = 0; i < count; i++) {
        vm_space_unmap_user_pages(&p->vm_space, addrs[i], pages[i]);
        vm_space_note_shm_detach(&p->vm_space, pages[i]);
    }
    p->shm_attachment_count = 0;
    p->vm_space.shm_cursor = (uintptr_t)VM_SPACE_SHM_BASE + (((uintptr_t)p->pid % 64) * 0x01000000ULL);

    flags = spin_lock_irqsave(&g_shm_lock);
    for (size_t i = 0; i < count; i++) {
        struct shm_region *r = shm_find_region_locked(ids[i]);
        if (r)
            shm_reap_region_locked(r);
    }
    for (int i = 0; i < SHM_MAX_REGIONS; i++) {
        if (g_regions[i].in_use)
            shm_reap_region_locked(&g_regions[i]);
    }
    spin_unlock_irqrestore(&g_shm_lock, flags);
}

void shm_get_stats(struct shm_stats *out)
{
    struct shm_stats stats = {0};
    unsigned long flags;
    if (!out)
        return;

    flags = spin_lock_irqsave(&g_shm_lock);
    for (int i = 0; i < SHM_MAX_REGIONS; i++) {
        if (!g_regions[i].in_use)
            continue;
        stats.region_count++;
        stats.reserved_pages += g_regions[i].page_count;
    }
    for (int i = 0; i < SHM_MAX_ATTACHMENTS; i++) {
        if (g_attachments[i].in_use)
            stats.attachment_count++;
    }
    spin_unlock_irqrestore(&g_shm_lock, flags);

    *out = stats;
}

void shm_dump_state(void)
{
    struct shm_stats stats = {0};
    shm_get_stats(&stats);
    kprintf("[mem][shm] regions=%u attachments=%u reserved_pages=%u\n",
            (uint32_t)stats.region_count,
            (uint32_t)stats.attachment_count,
            (uint32_t)stats.reserved_pages);
}
