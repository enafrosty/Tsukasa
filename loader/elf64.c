/*
 * Project Tsukasa — Minimal ELF64 user-process loader
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

#include "elf64.h"

#ifdef __x86_64__

#include <stdint.h>
#include <stddef.h>

#include "../fs/vfs.h"
#include "../include/kprintf.h"
#include "../include/kutils.h"
#include "../include/spinlock.h"
#include "../include/smp.h"
#include "../mm/heap.h"
#include "../mm/pmm.h"
#include "../mm/vmm_x64.h"
#include "../proc/process.h"
#include "../arch/x86_64/cpu/gdt.h"

#define ELF64_PATH_MAX 128
#define USER_STACK_TOP             0x00007FFFFFFF0000ULL
#define USER_STACK_MAX_SIZE        (8ULL * 1024ULL * 1024ULL)
#define USER_STACK_PREFAULT_PAGES  2ULL

typedef struct {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf64_ehdr_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} elf64_phdr_t;

#define PT_LOAD_64 1
#define ET_EXEC_64 2
#define EM_X86_64  62
#define PF_X_64    0x1U
#define PF_W_64    0x2U
#define PF_R_64    0x4U

/* Multi-slot pending handoff table keyed by child PID (or parent PID before child_pid publish). */
#define ELF64_CMDLINE_MAX   256
#define ELF64_PENDING_SLOTS 32

typedef struct {
    int used;
    int parent_pid;
    int child_pid;
    uint64_t seq;
    char path[ELF64_PATH_MAX];
    char args[ELF64_CMDLINE_MAX];
} elf64_pending_slot_t;

static elf64_pending_slot_t g_pending_slots[ELF64_PENDING_SLOTS];
static uint64_t g_pending_next_seq = 1;
static spinlock_t g_pending_lock = SPINLOCK_INIT;

static int stream_load_segment(process_t *p, int fd, const elf64_phdr_t *ph)
{
    if (!ph || ph->p_memsz == 0)
        return 0;

    uint64_t map_flags = PAGING_MAP_READ | PAGING_MAP_USER;
    if (ph->p_flags & PF_W_64)
        map_flags |= PAGING_MAP_WRITE;
    if (ph->p_flags & PF_X_64)
        map_flags |= PAGING_MAP_EXEC;

    uint32_t vma_prot = VMA_PROT_READ;
    if (ph->p_flags & PF_W_64)
        vma_prot |= VMA_PROT_WRITE;
    if (ph->p_flags & PF_X_64)
        vma_prot |= VMA_PROT_EXEC;

    uint64_t start = ph->p_vaddr & ~(VMM_X64_PAGE_SIZE - 1);
    uint64_t end = (ph->p_vaddr + ph->p_memsz + VMM_X64_PAGE_SIZE - 1) & ~(VMM_X64_PAGE_SIZE - 1);
    uint64_t file_end = ph->p_vaddr + ph->p_filesz;

    vm_space_mmap_write_lock(&p->vm_space);
    for (uint64_t vp = start; vp < end;) {
        vm_area_t *ex = vma_find(&p->vm_space, (uintptr_t)vp);
        if (ex) {
            uint64_t seg_end = (ex->end < end) ? ex->end : end;
            uint32_t merged = ex->prot | vma_prot;
            if (merged != ex->prot)
                (void)vma_protect_range(&p->vm_space, (uintptr_t)vp, (size_t)(seg_end - vp), merged);
            vp = seg_end;
        } else {
            uint64_t run_end = vp + VMM_X64_PAGE_SIZE;
            while (run_end < end && !vma_find(&p->vm_space, (uintptr_t)run_end))
                run_end += VMM_X64_PAGE_SIZE;
            if (!vma_insert(&p->vm_space, (uintptr_t)vp, (uintptr_t)run_end, vma_prot,
                            VMA_FLAG_ELF | VMA_FLAG_ANON | VMA_FLAG_PRIVATE)) {
                vm_space_mmap_write_unlock(&p->vm_space);
                return -1;
            }
            vp = run_end;
        }
    }
    vm_space_mmap_write_unlock(&p->vm_space);

    for (uint64_t page = start; page < end; page += VMM_X64_PAGE_SIZE) {
        uint64_t phys = 0;
        uint64_t flags = 0;
        uint8_t *kva = NULL;

        if (vmm_query_page(p->vm_space.pml4_phys, (uintptr_t)page, &phys, &flags) == 0) {
            uint64_t cur_flags = PAGING_MAP_READ | PAGING_MAP_USER;
            if (flags & VMM_X64_PTE_WRITABLE)
                cur_flags |= PAGING_MAP_WRITE;
            if ((flags & VMM_X64_PTE_NO_EXEC) == 0)
                cur_flags |= PAGING_MAP_EXEC;
            uint64_t merged_flags = cur_flags | map_flags;
            if (merged_flags != cur_flags) {
                if (vm_space_protect_user_pages(&p->vm_space, (uintptr_t)page, 1,
                                                merged_flags) != 0)
                    return -1;
            }
            kva = (uint8_t *)vmm_phys_to_virt(phys);
        } else {
            uintptr_t new_phys = pmm_alloc_pages(1);
            if (!new_phys)
                return -1;
            page_t *pg = pmm_phys_to_page(new_phys);
            if (pg)
                pg->flags |= PAGE_FLAG_ANON;
            kva = (uint8_t *)vmm_phys_to_virt(new_phys);
            k_memset(kva, 0, VMM_X64_PAGE_SIZE);
            if (vm_space_map_user_pages(&p->vm_space, (uintptr_t)page, new_phys, 1,
                                        map_flags) != 0) {
                pmm_free_pages(new_phys, 1);
                return -1;
            }
            __atomic_fetch_add(&p->vm_space.anon_pages, 1u, __ATOMIC_RELAXED);
        }

        uint64_t ov_start = (page > ph->p_vaddr) ? page : ph->p_vaddr;
        uint64_t ov_end = (page + VMM_X64_PAGE_SIZE < file_end) ? (page + VMM_X64_PAGE_SIZE) : file_end;

        if (ov_start < ov_end) {
            uint64_t dst_off = ov_start - page;
            uint64_t n = ov_end - ov_start;
            uint64_t file_off = ph->p_offset + (ov_start - ph->p_vaddr);

            vfs_seek(fd, (size_t)file_off, VFS_SEEK_SET);
            if (vfs_read(fd, kva + dst_off, (size_t)n) != (size_t)n)
                return -1;
        }
    }
    return 0;
}

static int map_zero_pages(process_t *p, uint64_t va, uint64_t size)
{
    uint64_t start = va & ~(VMM_X64_PAGE_SIZE - 1);
    uint64_t end = (va + size + VMM_X64_PAGE_SIZE - 1) & ~(VMM_X64_PAGE_SIZE - 1);

    for (uint64_t page = start; page < end; page += VMM_X64_PAGE_SIZE) {
        uint64_t phys = 0;
        uint64_t flags = 0;
        if (vmm_query_page(p->vm_space.pml4_phys, (uintptr_t)page, &phys, &flags) == 0)
            continue;

        uintptr_t new_phys = pmm_alloc_pages(1);
        if (!new_phys)
            return -1;
        uint8_t *kva = (uint8_t *)vmm_phys_to_virt(new_phys);
        k_memset(kva, 0, VMM_X64_PAGE_SIZE);
        if (vm_space_map_user_pages(&p->vm_space, (uintptr_t)page, new_phys, 1,
                                    PAGING_MAP_READ | PAGING_MAP_WRITE |
                                    PAGING_MAP_USER) != 0) {
            pmm_free_pages(new_phys, 1);
            return -1;
        }
    }
    return 0;
}

static void __attribute__((noreturn)) enter_ring3(uint64_t entry, uint64_t rsp)
{
    uint32_t cpu = smp_this_cpu_id();
    cpu_state_t *cs = smp_get_cpu(cpu);

    __asm__ volatile ("cli");
    if (cs) {
        wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)(uintptr_t)cs);
        wrmsr(MSR_GS_BASE, 0);
    }

    /* gdt.c entry 3 (0x18) = user data, entry 4 (0x20) = 64-bit user code: that order is required by SYSRET. */
    __asm__ volatile (
        "pushq $0x1B\n"
        "pushq %0\n"
        "pushq $0x202\n"
        "pushq $0x23\n"
        "pushq %1\n"
        "iretq\n"
        : : "r"(rsp), "r"(entry) : "memory");
    __builtin_unreachable();
}

static int claim_pending_slot(int self_pid, int self_ppid,
                              char *path_out, char *args_out)
{
    unsigned long irq_flags = spin_lock_irqsave(&g_pending_lock);
    int idx = -1;

    for (int i = 0; i < ELF64_PENDING_SLOTS; i++) {
        if (g_pending_slots[i].used && g_pending_slots[i].child_pid == self_pid) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        for (int i = 0; i < ELF64_PENDING_SLOTS; i++) {
            if (g_pending_slots[i].used &&
                g_pending_slots[i].child_pid < 0 &&
                g_pending_slots[i].parent_pid == self_ppid) {
                idx = i;
                break;
            }
        }
    }

    if (idx < 0) {
        spin_unlock_irqrestore(&g_pending_lock, irq_flags);
        return -1;
    }

    k_memcpy(path_out, g_pending_slots[idx].path, ELF64_PATH_MAX);
    k_memcpy(args_out, g_pending_slots[idx].args, ELF64_CMDLINE_MAX);
    g_pending_slots[idx].used = 0;
    g_pending_slots[idx].parent_pid = -1;
    g_pending_slots[idx].child_pid = -1;
    g_pending_slots[idx].seq = 0;
    spin_unlock_irqrestore(&g_pending_lock, irq_flags);
    return 0;
}

static void elf64_bootstrap(void)
{
    char path[ELF64_PATH_MAX];
    char args[ELF64_CMDLINE_MAX];
    process_t *self = process_current();

    if (!self || claim_pending_slot((int)self->pid, (int)self->ppid, path, args) != 0) {
        kprintf("[elf64] bootstrap without pending path\n");
        process_exit(-1);
    }

    int fd = vfs_open(path);
    if (fd < 0) {
        kprintf("[elf64] open failed: %s\n", path);
        process_exit(-1);
    }

    elf64_ehdr_t eh;
    vfs_seek(fd, 0, VFS_SEEK_SET);
    if (vfs_read(fd, &eh, sizeof(eh)) != sizeof(eh)) {
        kprintf("[elf64] read ehdr failed: %s\n", path);
        vfs_close(fd);
        process_exit(-1);
    }

    if (eh.e_ident[0] != 0x7F || eh.e_ident[1] != 'E' ||
        eh.e_ident[2] != 'L' || eh.e_ident[3] != 'F' ||
        eh.e_ident[4] != 2 /* ELFCLASS64 */ ||
        eh.e_type != ET_EXEC_64 || eh.e_machine != EM_X86_64 ||
        eh.e_phnum == 0 || eh.e_phnum > 64 ||
        eh.e_phentsize != sizeof(elf64_phdr_t)) {
        kprintf("[elf64] not an ELF64 executable: %s\n", path);
        vfs_close(fd);
        process_exit(-1);
    }

    size_t ph_bytes = (size_t)eh.e_phnum * sizeof(elf64_phdr_t);
    elf64_phdr_t *ph = (elf64_phdr_t *)kmalloc(ph_bytes);
    if (!ph) {
        kprintf("[elf64] alloc ph failed: %s\n", path);
        vfs_close(fd);
        process_exit(-1);
    }

    vfs_seek(fd, (size_t)eh.e_phoff, VFS_SEEK_SET);
    if (vfs_read(fd, ph, ph_bytes) != ph_bytes) {
        kprintf("[elf64] read phdr failed: %s\n", path);
        kfree(ph);
        vfs_close(fd);
        process_exit(-1);
    }

    /* Move off the shared kernel PML4 into a private user address space before mapping. */
    if (process_adopt_private_address_space() != 0) {
        kprintf("[elf64] private address space alloc failed\n");
        kfree(ph);
        vfs_close(fd);
        process_exit(-1);
    }

    uint64_t max_elf_end = 0;
    for (uint16_t i = 0; i < eh.e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD_64)
            continue;
        if (stream_load_segment(self, fd, &ph[i]) != 0) {
            kprintf("[elf64] segment map failed\n");
            kfree(ph);
            vfs_close(fd);
            process_exit(-1);
        }
        uint64_t seg_end = (ph[i].p_vaddr + ph[i].p_memsz + VMM_X64_PAGE_SIZE - 1) &
                           ~(VMM_X64_PAGE_SIZE - 1);
        if (seg_end > max_elf_end)
            max_elf_end = seg_end;
    }

    if (max_elf_end < 0x400000ULL)
        max_elf_end = 0x10000000ULL;
    self->vm_space.brk_start = (uintptr_t)max_elf_end;
    self->vm_space.brk_current = (uintptr_t)max_elf_end;
    self->vm_space.brk_limit = (uintptr_t)VM_SPACE_SHM_BASE;

    uint64_t stack_bottom = USER_STACK_TOP - USER_STACK_MAX_SIZE;
    vm_space_mmap_write_lock(&self->vm_space);
    vm_area_t *stack_vma = vma_insert(&self->vm_space,
                                      (uintptr_t)stack_bottom,
                                      (uintptr_t)USER_STACK_TOP,
                                      VMA_PROT_READ | VMA_PROT_WRITE,
                                      VMA_FLAG_GROWSDOWN | VMA_FLAG_ANON | VMA_FLAG_PRIVATE);
    vm_space_mmap_write_unlock(&self->vm_space);
    if (!stack_vma) {
        kprintf("[elf64] stack VMA insert failed\n");
        kfree(ph);
        vfs_close(fd);
        process_exit(-1);
    }

    uint64_t prefault_bytes = USER_STACK_PREFAULT_PAGES * VMM_X64_PAGE_SIZE;
    if (map_zero_pages(self, USER_STACK_TOP - prefault_bytes, prefault_bytes) != 0) {
        kprintf("[elf64] stack map failed\n");
        kfree(ph);
        vfs_close(fd);
        process_exit(-1);
    }
    __atomic_fetch_add(&self->vm_space.stack_pages, (uint32_t)USER_STACK_PREFAULT_PAGES, __ATOMIC_RELAXED);

    uint64_t entry = eh.e_entry;
    kfree(ph);
    vfs_close(fd);
    self->user_entry = entry;

    {
        enum { ELF64_ARGV_MAX = 16 };
        const char *argp[ELF64_ARGV_MAX];
        int argp_len[ELF64_ARGV_MAX];
        int argc = 0;
        int path_len = 0;

        while (path[path_len] && path_len < ELF64_PATH_MAX - 1)
            path_len++;

        const char *s = args;
        while (*s && argc < ELF64_ARGV_MAX) {
            while (*s == ' ' || *s == '\t')
                s++;
            if (!*s)
                break;
            char quote = 0;
            if (*s == '"') {
                quote = '"';
                s++;
            }
            argp[argc] = s;
            while (*s && (quote ? (*s != '"') : (*s != ' ' && *s != '\t')))
                s++;
            argp_len[argc] = (int)(s - argp[argc]);
            argc++;
            if (quote && *s == '"')
                s++;
        }
        if (argc == 0) {
            argp[0] = path;
            argp_len[0] = path_len;
            argc = 1;
        }

        uint64_t sp = USER_STACK_TOP;
        uint64_t argv_va[ELF64_ARGV_MAX];
        for (int i = argc - 1; i >= 0; i--) {
            sp -= (uint64_t)argp_len[i] + 1;
            k_memcpy((void *)(uintptr_t)sp, argp[i], (size_t)argp_len[i]);
            ((char *)(uintptr_t)sp)[argp_len[i]] = '\0';
            argv_va[i] = sp;
        }

        uint64_t vec_bytes = ((uint64_t)argc + 5) * 8;
        uint64_t rsp = ((sp & ~0xFULL) - vec_bytes) & ~0xFULL;
        uint64_t *vec = (uint64_t *)(uintptr_t)rsp;
        vec[0] = (uint64_t)argc;
        for (int i = 0; i < argc; i++)
            vec[1 + i] = argv_va[i];
        vec[1 + argc] = 0;
        vec[2 + argc] = 0;
        vec[3 + argc] = 0;
        vec[4 + argc] = 0;
        if (self && self->kernel_stack)
            tss_set_rsp0_x64(process_stack_top_aligned(self));
        enter_ring3(entry, rsp);
    }
}

int elf64_spawn_cmdline(const char *path, const char *args, const char *name)
{
    if (!path)
        return -1;

    process_t *cur = process_current();
    int parent_pid = cur ? (int)cur->pid : 0;
    elf64_pending_slot_t *slot = NULL;
    uint64_t my_seq = 0;

    for (int retry = 0; retry < 100; retry++) {
        unsigned long irq_flags = spin_lock_irqsave(&g_pending_lock);
        for (int s = 0; s < ELF64_PENDING_SLOTS; s++) {
            if (!g_pending_slots[s].used) {
                slot = &g_pending_slots[s];
                my_seq = g_pending_next_seq++;
                slot->used = 1;
                slot->parent_pid = parent_pid;
                slot->child_pid = -1;
                slot->seq = my_seq;

                int i = 0;
                while (path[i] && i < ELF64_PATH_MAX - 1) {
                    slot->path[i] = path[i];
                    i++;
                }
                slot->path[i] = '\0';

                i = 0;
                if (args) {
                    while (args[i] && i < ELF64_CMDLINE_MAX - 1) {
                        slot->args[i] = args[i];
                        i++;
                    }
                }
                slot->args[i] = '\0';
                break;
            }
        }
        spin_unlock_irqrestore(&g_pending_lock, irq_flags);
        if (slot)
            break;
        process_yield();
    }

    if (!slot)
        return -1;

    process_t *p = process_spawn_kernel(name ? name : "elf64", elf64_bootstrap);
    int child_pid = p ? (int)p->pid : -1;

    unsigned long irq_flags = spin_lock_irqsave(&g_pending_lock);
    if (!p) {
        if (slot->used && slot->seq == my_seq) {
            slot->used = 0;
            slot->parent_pid = -1;
            slot->child_pid = -1;
            slot->seq = 0;
        }
        spin_unlock_irqrestore(&g_pending_lock, irq_flags);
        return -1;
    }
    if (slot->used && slot->seq == my_seq)
        slot->child_pid = child_pid;
    spin_unlock_irqrestore(&g_pending_lock, irq_flags);
    return child_pid;
}

int elf64_spawn(const char *path, const char *name)
{
    return elf64_spawn_cmdline(path, NULL, name);
}

#endif /* __x86_64__ */
