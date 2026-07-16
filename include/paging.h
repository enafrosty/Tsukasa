/*
 * paging.h - Architecture-aware paging contracts.
 */

#ifndef PAGING_H
#define PAGING_H

#include <stddef.h>
#include <stdint.h>

/* Page size in bytes (must match mm/pmm.h). */
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

/*
 * Architecture-neutral mapping flags consumed by x64 VMM helpers.
 * i386 code can ignore these without behavior change.
 */
#define PAGING_MAP_READ    (1ULL << 0)
#define PAGING_MAP_WRITE   (1ULL << 1)
#define PAGING_MAP_EXEC    (1ULL << 2)
#define PAGING_MAP_USER    (1ULL << 3)
#define PAGING_MAP_GLOBAL  (1ULL << 4)
#define PAGING_MAP_DEVICE  (1ULL << 5)

/* Canonical x64 mapping boundaries used for validation. */
#define PAGING_USER_VA_MIN    0x0000000000100000ULL
#define PAGING_USER_VA_MAX    0x00007FFFFFFFF000ULL
#define PAGING_KERNEL_VA_MIN  0xFFFF800000000000ULL
#define PAGING_KERNEL_VA_MAX  0xFFFFFFFFFFFFF000ULL

static inline int paging_is_page_aligned_uintptr(uintptr_t addr)
{
    return ((addr & (uintptr_t)(PAGE_SIZE - 1)) == 0) ? 1 : 0;
}

static inline int paging_is_page_aligned_u64(uint64_t addr)
{
    return ((addr & (uint64_t)(PAGE_SIZE - 1)) == 0) ? 1 : 0;
}

static inline int paging_range_is_user(uintptr_t addr, size_t size)
{
    uint64_t start;
    uint64_t end;

    if (size == 0)
        return 0;

    start = (uint64_t)addr;
    end = start + (uint64_t)size - 1ULL;
    if (end < start)
        return 0;

    return (start >= PAGING_USER_VA_MIN && end <= PAGING_USER_VA_MAX) ? 1 : 0;
}

static inline int paging_range_is_kernel(uintptr_t addr, size_t size)
{
    uint64_t start;
    uint64_t end;

    if (size == 0)
        return 0;

    start = (uint64_t)addr;
    end = start + (uint64_t)size - 1ULL;
    if (end < start)
        return 0;

    return (start >= PAGING_KERNEL_VA_MIN && end <= PAGING_KERNEL_VA_MAX) ? 1 : 0;
}

#ifndef __x86_64__

/* i386 page table/directory entry flags. */
#define PTE_PRESENT   (1u << 0)
#define PTE_WRITABLE  (1u << 1)
#define PTE_USER      (1u << 2)
#define PTE_ACCESSED  (1u << 5)
#define PTE_DIRTY     (1u << 6)

/* Number of entries per page table/directory. */
#define PAGING_ENTRIES 1024

/* Virtual address to page directory index. */
#define PDE_INDEX(virt) (((uint32_t)(virt) >> 22) & 0x3FFu)

/* Virtual address to page table index. */
#define PTE_INDEX(virt) (((uint32_t)(virt) >> 12) & 0x3FFu)

typedef uint32_t pde_t;
typedef uint32_t pte_t;
typedef pde_t page_directory_t[PAGING_ENTRIES];
typedef pte_t page_table_t[PAGING_ENTRIES];

void paging_init(void);
uintptr_t paging_get_current_pd(void);
int paging_map(uintptr_t virt, uintptr_t phys, uint32_t flags);
void paging_unmap(uintptr_t virt);
int paging_map_framebuffer(uintptr_t phys_base, size_t size);

#endif /* !__x86_64__ */

#endif /* PAGING_H */
