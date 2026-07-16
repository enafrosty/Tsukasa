#ifndef TSUKASA_VMM_X64_H
#define TSUKASA_VMM_X64_H

#include <stddef.h>
#include <stdint.h>

#include "include/boot_info.h"
#include "include/paging.h"

#define VMM_X64_PAGE_SIZE 4096ULL
#define VMM_X64_PTE_PRESENT (1ULL << 0)
#define VMM_X64_PTE_WRITABLE (1ULL << 1)
#define VMM_X64_PTE_USER (1ULL << 2)
#define VMM_X64_PTE_WRITE_THROUGH (1ULL << 3)
#define VMM_X64_PTE_CACHE_DISABLE (1ULL << 4)
#define VMM_X64_PTE_NO_EXEC (1ULL << 63)

void vmm_x64_init(uint64_t hhdm_offset);
uint64_t vmm_x64_hhdm_offset(void);

uint64_t vmm_get_current_pml4(void);
void vmm_switch_pml4(uint64_t pml4_phys);
uintptr_t vmm_phys_to_virt(uint64_t phys_addr);
uint64_t vmm_virt_to_phys(uintptr_t virt_addr);

int vmm_create_address_space(uint64_t *pml4_out);
int vmm_clone_address_space(uint64_t src_pml4_phys, uint64_t *pml4_out);
int vmm_destroy_address_space(uint64_t pml4_phys);

int vmm_map_pages(uint64_t pml4_phys,
                  uintptr_t virt_addr,
                  uint64_t phys_addr,
                  size_t page_count,
                  uint64_t map_flags);
int vmm_unmap_pages(uint64_t pml4_phys,
                    uintptr_t virt_addr,
                    size_t page_count);
int vmm_protect_pages(uint64_t pml4_phys,
                      uintptr_t virt_addr,
                      size_t page_count,
                      uint64_t map_flags);
int vmm_query_page(uint64_t pml4_phys,
                   uintptr_t virt_addr,
                   uint64_t *phys_out,
                   uint64_t *pte_flags_out);

int vmm_map_page(uintptr_t virt_addr, uint64_t phys_addr, uint64_t flags);
int vmm_unmap_page(uintptr_t virt_addr);
int vmm_map_io_region(uint64_t phys_base, size_t size, uintptr_t *virt_base);

#endif
