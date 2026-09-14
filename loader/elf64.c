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
#define USER_STACK_TOP   0x00007FFFFFFF0000ULL
#define USER_STACK_PAGES 512

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

/* ponytail: single-slot handoff: one elf64_spawn in flight at a time; make it a queue if concurrent user... */
#define ELF64_CMDLINE_MAX 256
static char g_pending_path[ELF64_PATH_MAX];
static char g_pending_args[ELF64_CMDLINE_MAX];
static volatile int g_pending_valid = 0;
static spinlock_t g_pending_lock = SPINLOCK_INIT;

static int stream_load_segment(process_t *p, int fd, const elf64_phdr_t *ph)
{
    if (!ph || ph->p_memsz == 0)
        return 0;

    uint64_t start = ph->p_vaddr & ~(VMM_X64_PAGE_SIZE - 1);
    uint64_t end = (ph->p_vaddr + ph->p_memsz + VMM_X64_PAGE_SIZE - 1) & ~(VMM_X64_PAGE_SIZE - 1);
    uint64_t file_end = ph->p_vaddr + ph->p_filesz;

    for (uint64_t page = start; page < end; page += VMM_X64_PAGE_SIZE) {
        uint64_t phys = 0;
        uint64_t flags = 0;
        uint8_t *kva = NULL;

        if (vmm_query_page(p->vm_space.pml4_phys, (uintptr_t)page, &phys, &flags) == 0) {
            kva = (uint8_t *)vmm_phys_to_virt(phys);
        } else {
            uintptr_t new_phys = pmm_alloc_pages(1);
            if (!new_phys)
                return -1;
            kva = (uint8_t *)vmm_phys_to_virt(new_phys);
            k_memset(kva, 0, VMM_X64_PAGE_SIZE);
            if (vm_space_map_user_pages(&p->vm_space, (uintptr_t)page, new_phys, 1,
                                        PAGING_MAP_READ | PAGING_MAP_WRITE |
                                        PAGING_MAP_EXEC | PAGING_MAP_USER) != 0) {
                pmm_free_pages(new_phys, 1);
                return -1;
            }
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
                                    PAGING_MAP_EXEC | PAGING_MAP_USER) != 0) {
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

static void elf64_bootstrap(void)
{
    char path[ELF64_PATH_MAX];
    char args[ELF64_CMDLINE_MAX];

    spin_lock(&g_pending_lock);
    if (!g_pending_valid) {
        spin_unlock(&g_pending_lock);
        kprintf("[elf64] bootstrap without pending path\n");
        process_exit(-1);
    }
    for (int i = 0; i < ELF64_PATH_MAX; i++)
        path[i] = g_pending_path[i];
    for (int i = 0; i < ELF64_CMDLINE_MAX; i++)
        args[i] = g_pending_args[i];
    g_pending_valid = 0;
    spin_unlock(&g_pending_lock);

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

    process_t *self = process_current();
    for (uint16_t i = 0; i < eh.e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD_64)
            continue;
        if (stream_load_segment(self, fd, &ph[i]) != 0) {
            kprintf("[elf64] segment map failed\n");
            kfree(ph);
            vfs_close(fd);
            process_exit(-1);
        }
    }

    uint64_t stack_base = USER_STACK_TOP - (uint64_t)USER_STACK_PAGES * VMM_X64_PAGE_SIZE;
    if (map_zero_pages(self, stack_base,
                       (uint64_t)USER_STACK_PAGES * VMM_X64_PAGE_SIZE) != 0) {
        kprintf("[elf64] stack map failed\n");
        kfree(ph);
        vfs_close(fd);
        process_exit(-1);
    }

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

    for (int retry = 0; retry < 100; retry++) {
        spin_lock(&g_pending_lock);
        if (!g_pending_valid)
            break;
        spin_unlock(&g_pending_lock);
        process_yield();
    }
    if (g_pending_valid)
        return -1;
    int i = 0;
    while (path[i] && i < ELF64_PATH_MAX - 1) {
        g_pending_path[i] = path[i];
        i++;
    }
    g_pending_path[i] = '\0';
    i = 0;
    if (args) {
        while (args[i] && i < ELF64_CMDLINE_MAX - 1) {
            g_pending_args[i] = args[i];
            i++;
        }
    }
    g_pending_args[i] = '\0';
    g_pending_valid = 1;
    spin_unlock(&g_pending_lock);

    process_t *p = process_spawn_kernel(name ? name : "elf64", elf64_bootstrap);
    if (!p) {
        g_pending_valid = 0;
        return -1;
    }
    return (int)p->pid;
}

int elf64_spawn(const char *path, const char *name)
{
    return elf64_spawn_cmdline(path, NULL, name);
}

#endif /* __x86_64__ */
