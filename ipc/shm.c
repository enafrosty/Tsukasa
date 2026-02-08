/*
 * shm.c - Shared memory IPC implementation.
 * Manages shared regions for framebuffer and app communication.
 */

#include "shm.h"
#include "../mm/pmm.h"
#include "../mm/paging.h"
#include "../mm/heap.h"
#include "../proc/task.h"
#include "../include/paging.h"
#include <stddef.h>
#include <stdint.h>

#define SHM_MAX_REGIONS 32
#define SHM_VIRT_BASE 0x40000000

struct shm_region {
    int id;
    uintptr_t phys_base;
    size_t size;
    size_t page_count;
    int ref_count;
    int attach_count;
    int in_use;
};

static struct shm_region shm_regions[SHM_MAX_REGIONS];
static int next_shm_id = 1;

static struct shm_region *shm_find(int id)
{
    for (int i = 0; i < SHM_MAX_REGIONS; i++) {
        if (shm_regions[i].in_use && shm_regions[i].id == id)
            return &shm_regions[i];
    }
    return NULL;
}

static struct shm_region *shm_alloc_slot(void)
{
    for (int i = 0; i < SHM_MAX_REGIONS; i++) {
        if (!shm_regions[i].in_use)
            return &shm_regions[i];
    }
    return NULL;
}

int shm_create(size_t size)
{
    if (size == 0)
        return -1;

    size_t page_count = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    uintptr_t phys = pmm_alloc_pages(page_count);
    if (phys == 0)
        return -1;

    struct shm_region *r = shm_alloc_slot();
    if (!r) {
        pmm_free_pages(phys, page_count);
        return -1;
    }

    r->id = next_shm_id++;
    r->phys_base = phys;
    r->size = page_count * PAGE_SIZE;
    r->page_count = page_count;
    r->ref_count = 1;
    r->attach_count = 0;
    r->in_use = 1;
    return r->id;
}

void *shm_attach(int shm_id)
{
    struct shm_region *r = shm_find(shm_id);
    if (!r)
        return NULL;

    /* For now, we use identity mapping - return physical addr as virtual.
     * Proper implementation would map into process address space.
     * Since we don't have per-process page dirs for user yet, this works
     * for kernel tasks. */
    r->attach_count++;
    return (void *)r->phys_base;
}

void shm_detach(void *addr)
{
    if (!addr)
        return;
    for (int i = 0; i < SHM_MAX_REGIONS; i++) {
        if (shm_regions[i].in_use &&
            (uintptr_t)addr >= shm_regions[i].phys_base &&
            (uintptr_t)addr < shm_regions[i].phys_base + shm_regions[i].size) {
            shm_regions[i].attach_count--;
            if (shm_regions[i].attach_count < 0)
                shm_regions[i].attach_count = 0;
            break;
        }
    }
}

int shm_destroy(int shm_id)
{
    struct shm_region *r = shm_find(shm_id);
    if (!r)
        return -1;
    if (r->attach_count > 0)
        return -1;
    r->ref_count--;
    if (r->ref_count <= 0) {
        pmm_free_pages(r->phys_base, r->page_count);
        r->in_use = 0;
    }
    return 0;
}
