/*
 * shm.c - Shared memory IPC implementation.
 */

#include "shm.h"

#include "../include/spinlock.h"
#include "../mm/pmm.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __x86_64__

#include "../include/kprintf.h"
#include "../include/paging.h"
#include "../proc/process.h"

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

    if (size == 0)
        return -1;

    page_count = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    phys = pmm_alloc_pages(page_count);
    if (!phys)
        return -1;

    spin_lock(&g_shm_lock);
    r = shm_alloc_region_locked();
    if (!r) {
        spin_unlock(&g_shm_lock);
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
    spin_unlock(&g_shm_lock);

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

    if (!cur || shm_id <= 0)
        return NULL;

    spin_lock(&g_shm_lock);
    r = shm_find_region_locked(shm_id);
    if (!r) {
        spin_unlock(&g_shm_lock);
        return NULL;
    }

    att = shm_find_attachment_locked(cur->pid, shm_id);
    if (att) {
        virt_addr = att->virt_base;
        spin_unlock(&g_shm_lock);
        return (void *)virt_addr;
    }

    virt_addr = vm_space_reserve_shm_range(&cur->vm_space, r->page_count);
    if (!virt_addr) {
        spin_unlock(&g_shm_lock);
        return NULL;
    }

    att = shm_alloc_attachment_locked();
    if (!att) {
        spin_unlock(&g_shm_lock);
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
    spin_unlock(&g_shm_lock);

    if (vm_space_map_user_pages(&cur->vm_space,
                                virt_addr,
                                phys_base,
                                page_count,
                                PAGING_MAP_READ | PAGING_MAP_WRITE |
                                PAGING_MAP_EXEC | PAGING_MAP_USER) != 0) {
        spin_lock(&g_shm_lock);
        att = shm_find_attachment_by_addr_locked(cur->pid, virt_addr);
        if (att)
            att->in_use = 0;
        r = shm_find_region_locked(shm_id);
        if (r && r->ref_count > 0)
            r->ref_count--;
        spin_unlock(&g_shm_lock);
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

    if (!cur || !addr)
        return -1;

    virt_addr = (uintptr_t)addr;
    spin_lock(&g_shm_lock);
    att = shm_find_attachment_by_addr_locked(cur->pid, virt_addr);
    if (!att) {
        spin_unlock(&g_shm_lock);
        return -1;
    }

    shm_id = att->shm_id;
    page_count = att->page_count;
    att->in_use = 0;

    r = shm_find_region_locked(shm_id);
    if (r && r->ref_count > 0)
        r->ref_count--;
    if (r && r->destroy_pending && r->ref_count == 0)
        needs_reap = 1;
    spin_unlock(&g_shm_lock);

    unmap_rc = vm_space_unmap_user_pages(&cur->vm_space, virt_addr, page_count);
    vm_space_note_shm_detach(&cur->vm_space, page_count);
    if (cur->shm_attachment_count > 0)
        cur->shm_attachment_count--;
    if (cur->shm_attachment_count == 0)
        cur->vm_space.shm_cursor = (uintptr_t)VM_SPACE_SHM_BASE + (((uintptr_t)cur->pid % 64) * 0x01000000ULL);

    if (needs_reap) {
        spin_lock(&g_shm_lock);
        r = shm_find_region_locked(shm_id);
        if (r)
            shm_reap_region_locked(r);
        spin_unlock(&g_shm_lock);
    }

    return unmap_rc;
}

int shm_destroy(int shm_id)
{
    struct shm_region *r;

    if (shm_id <= 0)
        return -1;

    spin_lock(&g_shm_lock);
    r = shm_find_region_locked(shm_id);
    if (!r) {
        spin_unlock(&g_shm_lock);
        return -1;
    }
    if (r->ref_count > 0) {
        spin_unlock(&g_shm_lock);
        return -1;
    }

    r->destroy_pending = 1;
    shm_reap_region_locked(r);
    spin_unlock(&g_shm_lock);
    return 0;
}

void shm_process_cleanup(struct process *proc)
{
    process_t *p = (process_t *)proc;
    uintptr_t addrs[SHM_CLEANUP_BATCH];
    size_t pages[SHM_CLEANUP_BATCH];
    int ids[SHM_CLEANUP_BATCH];
    size_t count = 0;

    if (!p)
        return;

    spin_lock(&g_shm_lock);
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
    spin_unlock(&g_shm_lock);

    for (size_t i = 0; i < count; i++) {
        vm_space_unmap_user_pages(&p->vm_space, addrs[i], pages[i]);
        vm_space_note_shm_detach(&p->vm_space, pages[i]);
    }
    p->shm_attachment_count = 0;
    p->vm_space.shm_cursor = (uintptr_t)VM_SPACE_SHM_BASE + (((uintptr_t)p->pid % 64) * 0x01000000ULL);

    spin_lock(&g_shm_lock);
    for (size_t i = 0; i < count; i++) {
        struct shm_region *r = shm_find_region_locked(ids[i]);
        if (r)
            shm_reap_region_locked(r);
    }
    for (int i = 0; i < SHM_MAX_REGIONS; i++) {
        if (g_regions[i].in_use)
            shm_reap_region_locked(&g_regions[i]);
    }
    spin_unlock(&g_shm_lock);
}

void shm_get_stats(struct shm_stats *out)
{
    struct shm_stats stats = {0};
    if (!out)
        return;

    spin_lock(&g_shm_lock);
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
    spin_unlock(&g_shm_lock);

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

#else

#define SHM_MAX_REGIONS 32

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
static spinlock_t shm_lock = SPINLOCK_INIT;

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

    spin_lock(&shm_lock);
    struct shm_region *r = shm_alloc_slot();
    if (!r) {
        spin_unlock(&shm_lock);
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
    spin_unlock(&shm_lock);
    return r->id;
}

void *shm_attach(int shm_id)
{
    (void)shm_id;
    /* Legacy i386 path is intentionally disabled for physical-return attach. */
    return NULL;
}

int shm_detach(void *addr)
{
    (void)addr;
    return -1;
}

int shm_destroy(int shm_id)
{
    spin_lock(&shm_lock);
    struct shm_region *r = shm_find(shm_id);
    if (!r) {
        spin_unlock(&shm_lock);
        return -1;
    }
    if (r->attach_count > 0) {
        spin_unlock(&shm_lock);
        return -1;
    }
    r->ref_count--;
    int should_free = (r->ref_count <= 0);
    uintptr_t phys = r->phys_base;
    size_t pages = r->page_count;
    if (should_free)
        r->in_use = 0;
    spin_unlock(&shm_lock);
    if (should_free)
        pmm_free_pages(phys, pages);
    return 0;
}

void shm_process_cleanup(struct process *proc)
{
    (void)proc;
}

void shm_get_stats(struct shm_stats *out)
{
    if (!out)
        return;
    out->region_count = 0;
    out->attachment_count = 0;
    out->reserved_pages = 0;
}

void shm_dump_state(void)
{
}

#endif
