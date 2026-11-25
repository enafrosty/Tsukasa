/*
 * Project Tsukasa — ELF64 executable, PIE, PT_INTERP, and execve loader
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
#include "../include/errno.h"
#include "../include/kprintf.h"
#include "../include/kutils.h"
#include "../include/paging.h"
#include "../include/spinlock.h"
#include "../include/smp.h"
#include "../ipc/shm.h"
#include "../mm/heap.h"
#include "../mm/pmm.h"
#include "../mm/vmm_x64.h"
#include "../proc/process.h"
#include "../arch/x86_64/cpu/gdt.h"

#define ELF64_PATH_MAX             256
#define ELF64_CMDLINE_MAX          256
#define ELF64_PENDING_SLOTS        32
#define ELF64_SHEBANG_MAX_DEPTH    4
#define ELF64_EXEC_MAX_ARGS        64
#define ELF64_EXEC_MAX_ENVS        64
#define ELF64_EXEC_MAX_STR_BYTES   16384

#define ELF64_PIE_LOAD_BASE        0x0000555555554000ULL
#define ELF64_INTERP_LOAD_BASE     0x00007F0000000000ULL
#define USER_STACK_TOP             0x00007FFFFFFF0000ULL
#define USER_STACK_MAX_SIZE        (8ULL * 1024ULL * 1024ULL)
#define USER_STACK_PREFAULT_PAGES  4ULL

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

#define PT_LOAD_64    1U
#define PT_INTERP_64  3U
#define PT_PHDR_64    6U
#define ET_EXEC_64    2U
#define ET_DYN_64     3U
#define EM_X86_64     62U
#define PF_X_64       0x1U
#define PF_W_64       0x2U
#define PF_R_64       0x4U

/* Standard Linux x86_64 auxiliary vector keys. */
#define AT_NULL   0ULL
#define AT_PHDR   3ULL
#define AT_PHENT  4ULL
#define AT_PHNUM  5ULL
#define AT_PAGESZ 6ULL
#define AT_BASE   7ULL
#define AT_FLAGS  8ULL
#define AT_ENTRY  9ULL
#define AT_UID    11ULL
#define AT_EUID   12ULL
#define AT_GID    13ULL
#define AT_EGID   14ULL
#define AT_SECURE 23ULL
#define AT_RANDOM 25ULL
#define AT_EXECFN 31ULL

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

static size_t cstr_len(const char *s, size_t max_len)
{
    size_t n = 0;
    if (!s)
        return 0;
    while (n < max_len && s[n])
        n++;
    return n;
}

static uint64_t read_tsc_seed(uint32_t pid)
{
    uint32_t lo = 0, hi = 0;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    uint64_t v = ((uint64_t)hi << 32) | (uint64_t)lo;
    v ^= ((uint64_t)pid << 17) ^ 0x9E3779B97F4A7C15ULL;
    v ^= v >> 30;
    v *= 0xBF58476D1CE4E5B9ULL;
    v ^= v >> 27;
    return v;
}

static int valid_elf64_ehdr(const elf64_ehdr_t *eh, int allow_dyn)
{
    if (!eh)
        return 0;
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F')
        return 0;
    if (eh->e_ident[4] != 2 || eh->e_ident[5] != 1)
        return 0;
    if (eh->e_machine != EM_X86_64)
        return 0;
    if (eh->e_type != ET_EXEC_64 && !(allow_dyn && eh->e_type == ET_DYN_64))
        return 0;
    if (eh->e_phnum == 0 || eh->e_phnum > 64 || eh->e_phentsize != sizeof(elf64_phdr_t))
        return 0;
    return 1;
}

static int stream_load_segment(vm_space_t *space, int fd, const elf64_phdr_t *ph, uint64_t load_bias)
{
    if (!space || !ph || ph->p_memsz == 0)
        return 0;
    if (ph->p_filesz > ph->p_memsz)
        return -ENOEXEC;
    if (ph->p_vaddr + load_bias < ph->p_vaddr)
        return -ENOEXEC;

    uint64_t vaddr = ph->p_vaddr + load_bias;
    if (vaddr + ph->p_memsz < vaddr)
        return -ENOEXEC;

    uint64_t start = vaddr & ~(VMM_X64_PAGE_SIZE - 1);
    uint64_t end = (vaddr + ph->p_memsz + VMM_X64_PAGE_SIZE - 1) & ~(VMM_X64_PAGE_SIZE - 1);
    if (end <= start || !paging_range_is_user((uintptr_t)start, (size_t)(end - start)))
        return -ENOEXEC;

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

    uint64_t file_end = vaddr + ph->p_filesz;

    vm_space_mmap_write_lock(space);
    for (uint64_t vp = start; vp < end;) {
        vm_area_t *ex = vma_find(space, (uintptr_t)vp);
        if (ex) {
            uint64_t seg_end = (ex->end < end) ? ex->end : end;
            uint32_t merged = ex->prot | vma_prot;
            if (merged != ex->prot)
                (void)vma_protect_range(space, (uintptr_t)vp, (size_t)(seg_end - vp), merged);
            vp = seg_end;
        } else {
            uint64_t run_end = vp + VMM_X64_PAGE_SIZE;
            while (run_end < end && !vma_find(space, (uintptr_t)run_end))
                run_end += VMM_X64_PAGE_SIZE;
            if (!vma_insert(space, (uintptr_t)vp, (uintptr_t)run_end, vma_prot,
                            VMA_FLAG_ELF | VMA_FLAG_ANON | VMA_FLAG_PRIVATE)) {
                vm_space_mmap_write_unlock(space);
                return -ENOMEM;
            }
            vp = run_end;
        }
    }
    vm_space_mmap_write_unlock(space);

    for (uint64_t page = start; page < end; page += VMM_X64_PAGE_SIZE) {
        uint64_t phys = 0;
        uint64_t flags = 0;
        uint8_t *kva = NULL;

        if (vmm_query_page(space->pml4_phys, (uintptr_t)page, &phys, &flags) == 0 &&
            (flags & VMM_X64_PTE_PRESENT)) {
            uint64_t cur_flags = PAGING_MAP_READ | PAGING_MAP_USER;
            if (flags & VMM_X64_PTE_WRITABLE)
                cur_flags |= PAGING_MAP_WRITE;
            if ((flags & VMM_X64_PTE_NO_EXEC) == 0)
                cur_flags |= PAGING_MAP_EXEC;
            uint64_t merged_flags = cur_flags | map_flags;
            if (merged_flags != cur_flags) {
                if (vm_space_protect_user_pages(space, (uintptr_t)page, 1, merged_flags) != 0)
                    return -ENOMEM;
            }
            kva = (uint8_t *)vmm_phys_to_virt(phys);
        } else {
            uintptr_t new_phys = pmm_alloc_pages(1);
            if (!new_phys)
                return -ENOMEM;
            page_t *pg = pmm_phys_to_page(new_phys);
            if (pg)
                pg->flags |= PAGE_FLAG_ANON;
            kva = (uint8_t *)vmm_phys_to_virt(new_phys);
            k_memset(kva, 0, VMM_X64_PAGE_SIZE);
            if (vm_space_map_user_pages(space, (uintptr_t)page, new_phys, 1, map_flags) != 0) {
                (void)pmm_page_put(new_phys);
                return -ENOMEM;
            }
            __atomic_fetch_add(&space->anon_pages, 1u, __ATOMIC_RELAXED);
        }

        uint64_t ov_start = (page > vaddr) ? page : vaddr;
        uint64_t ov_end = (page + VMM_X64_PAGE_SIZE < file_end)
                              ? (page + VMM_X64_PAGE_SIZE)
                              : file_end;

        if (ov_start < ov_end) {
            uint64_t dst_off = ov_start - page;
            uint64_t n = ov_end - ov_start;
            uint64_t file_off = ph->p_offset + (ov_start - vaddr);

            vfs_seek(fd, (size_t)file_off, VFS_SEEK_SET);
            if (vfs_read(fd, kva + dst_off, (size_t)n) != (size_t)n)
                return -ENOEXEC;
        }
    }
    return 0;
}

static int map_stack_pages(vm_space_t *space, uint64_t low_va, uint64_t high_va)
{
    uint64_t start = low_va & ~(VMM_X64_PAGE_SIZE - 1);
    uint64_t end = (high_va + VMM_X64_PAGE_SIZE - 1) & ~(VMM_X64_PAGE_SIZE - 1);

    for (uint64_t page = start; page < end; page += VMM_X64_PAGE_SIZE) {
        uint64_t phys = 0;
        uint64_t flags = 0;
        if (vmm_query_page(space->pml4_phys, (uintptr_t)page, &phys, &flags) == 0 &&
            (flags & VMM_X64_PTE_PRESENT)) {
            continue;
        }

        uintptr_t new_phys = pmm_alloc_pages(1);
        if (!new_phys)
            return -ENOMEM;
        page_t *pg = pmm_phys_to_page(new_phys);
        if (pg)
            pg->flags |= PAGE_FLAG_ANON;
        k_memset((void *)vmm_phys_to_virt(new_phys), 0, VMM_X64_PAGE_SIZE);
        if (vm_space_map_user_pages(space, (uintptr_t)page, new_phys, 1,
                                    PAGING_MAP_READ | PAGING_MAP_WRITE | PAGING_MAP_USER) != 0) {
            (void)pmm_page_put(new_phys);
            return -ENOMEM;
        }
        __atomic_fetch_add(&space->stack_pages, 1u, __ATOMIC_RELAXED);
    }
    return 0;
}

static int write_user_space_bytes(vm_space_t *space,
                                  uint64_t dst_va,
                                  const void *src,
                                  size_t len)
{
    const uint8_t *sp = (const uint8_t *)src;
    size_t off = 0;

    while (off < len) {
        uint64_t cur_va = dst_va + (uint64_t)off;
        uint64_t page_va = cur_va & ~(VMM_X64_PAGE_SIZE - 1);
        size_t page_off = (size_t)(cur_va - page_va);
        size_t chunk = VMM_X64_PAGE_SIZE - page_off;
        if (chunk > len - off)
            chunk = len - off;

        uint64_t phys = 0, flags = 0;
        if (vmm_query_page(space->pml4_phys, (uintptr_t)page_va, &phys, &flags) != 0 ||
            !(flags & VMM_X64_PTE_PRESENT)) {
            if (map_stack_pages(space, page_va, page_va + VMM_X64_PAGE_SIZE) != 0)
                return -ENOMEM;
            if (vmm_query_page(space->pml4_phys, (uintptr_t)page_va, &phys, &flags) != 0)
                return -EFAULT;
        }

        uint8_t *kva = (uint8_t *)vmm_phys_to_virt(phys) + page_off;
        k_memcpy(kva, sp + off, chunk);
        off += chunk;
    }
    return 0;
}

static uint64_t min_elf_load_vaddr(const elf64_phdr_t *ph, uint16_t phnum)
{
    uint64_t min_va = UINT64_MAX;
    for (uint16_t i = 0; i < phnum; i++) {
        if (ph[i].p_type != PT_LOAD_64 || ph[i].p_memsz == 0)
            continue;
        uint64_t aligned = ph[i].p_vaddr & ~(VMM_X64_PAGE_SIZE - 1);
        if (aligned < min_va)
            min_va = aligned;
    }
    return (min_va == UINT64_MAX) ? 0ULL : min_va;
}

static int load_interp_image(vm_space_t *space,
                             const char *interp_path,
                             uint64_t *at_base_out,
                             uint64_t *entry_out)
{
    vfs_stat_t st;
    if (vfs_stat(interp_path, &st) != 0)
        return -ENOENT;
    if (st.type == VFS_TYPE_DIR)
        return -EISDIR;

    int fd = vfs_open(interp_path);
    if (fd < 0)
        return fd;

    elf64_ehdr_t eh;
    vfs_seek(fd, 0, VFS_SEEK_SET);
    if (vfs_read(fd, &eh, sizeof(eh)) != sizeof(eh) || !valid_elf64_ehdr(&eh, 1)) {
        vfs_close(fd);
        return -ENOEXEC;
    }

    size_t ph_bytes = (size_t)eh.e_phnum * sizeof(elf64_phdr_t);
    elf64_phdr_t *ph = (elf64_phdr_t *)kmalloc(ph_bytes);
    if (!ph) {
        vfs_close(fd);
        return -ENOMEM;
    }

    vfs_seek(fd, (size_t)eh.e_phoff, VFS_SEEK_SET);
    if (vfs_read(fd, ph, ph_bytes) != ph_bytes) {
        kfree(ph);
        vfs_close(fd);
        return -ENOEXEC;
    }

    uint64_t min_va = min_elf_load_vaddr(ph, eh.e_phnum);
    uint64_t bias = (eh.e_type == ET_DYN_64) ? (ELF64_INTERP_LOAD_BASE - min_va) : 0ULL;

    int rc = 0;
    for (uint16_t i = 0; i < eh.e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD_64)
            continue;
        rc = stream_load_segment(space, fd, &ph[i], bias);
        if (rc != 0)
            break;
    }

    if (rc == 0) {
        uint64_t entry = bias + eh.e_entry;
        if (!paging_range_is_user((uintptr_t)entry, 1))
            rc = -ENOEXEC;
        else {
            *at_base_out = (eh.e_type == ET_DYN_64) ? bias : min_va;
            *entry_out = entry;
        }
    }

    kfree(ph);
    vfs_close(fd);
    return rc;
}

static int build_user_stack(vm_space_t *space,
                            uint32_t pid,
                            const char *exec_path,
                            char *const argv[],
                            int argc,
                            char *const envp[],
                            int envc,
                            uint64_t at_phdr,
                            uint16_t at_phent,
                            uint16_t at_phnum,
                            uint64_t at_base,
                            uint64_t at_entry,
                            uint32_t uid,
                            uint32_t euid,
                            uint32_t gid,
                            uint32_t egid,
                            uint64_t at_secure,
                            uint64_t *rsp_out)
{
    uint64_t stack_bottom = USER_STACK_TOP - USER_STACK_MAX_SIZE;
    vm_space_mmap_write_lock(space);
    vm_area_t *stack_vma = vma_insert(space,
                                      (uintptr_t)stack_bottom,
                                      (uintptr_t)USER_STACK_TOP,
                                      VMA_PROT_READ | VMA_PROT_WRITE,
                                      VMA_FLAG_GROWSDOWN | VMA_FLAG_ANON | VMA_FLAG_PRIVATE);
    vm_space_mmap_write_unlock(space);
    if (!stack_vma)
        return -ENOMEM;

    uint64_t prefault_bytes = USER_STACK_PREFAULT_PAGES * VMM_X64_PAGE_SIZE;
    if (map_stack_pages(space, USER_STACK_TOP - prefault_bytes, USER_STACK_TOP) != 0)
        return -ENOMEM;

    uint64_t sp = USER_STACK_TOP;

    /* 16 bytes for AT_RANDOM seeded per-exec. */
    uint64_t rand_bytes[2];
    rand_bytes[0] = read_tsc_seed(pid);
    rand_bytes[1] = read_tsc_seed(pid ^ 0x5A5A5A5AU);
    sp -= sizeof(rand_bytes);
    uint64_t rand_va = sp;
    if (write_user_space_bytes(space, rand_va, rand_bytes, sizeof(rand_bytes)) != 0)
        return -ENOMEM;

    /* AT_EXECFN string */
    size_t execfn_len = cstr_len(exec_path, ELF64_PATH_MAX - 1) + 1;
    sp -= execfn_len;
    uint64_t execfn_va = sp;
    if (write_user_space_bytes(space, execfn_va, exec_path, execfn_len) != 0)
        return -ENOMEM;

    uint64_t *env_va = NULL;
    if (envc > 0) {
        env_va = (uint64_t *)kmalloc((size_t)envc * sizeof(uint64_t));
        if (!env_va)
            return -ENOMEM;
        for (int i = envc - 1; i >= 0; i--) {
            size_t l = cstr_len(envp[i], ELF64_EXEC_MAX_STR_BYTES - 1) + 1;
            sp -= l;
            env_va[i] = sp;
            if (write_user_space_bytes(space, sp, envp[i], l) != 0) {
                kfree(env_va);
                return -ENOMEM;
            }
        }
    }

    uint64_t *arg_va = NULL;
    if (argc > 0) {
        arg_va = (uint64_t *)kmalloc((size_t)argc * sizeof(uint64_t));
        if (!arg_va) {
            if (env_va) kfree(env_va);
            return -ENOMEM;
        }
        for (int i = argc - 1; i >= 0; i--) {
            size_t l = cstr_len(argv[i], ELF64_EXEC_MAX_STR_BYTES - 1) + 1;
            sp -= l;
            arg_va[i] = sp;
            if (write_user_space_bytes(space, sp, argv[i], l) != 0) {
                kfree(arg_va);
                if (env_va) kfree(env_va);
                return -ENOMEM;
            }
        }
    }

    const size_t aux_pairs = 15;
    size_t vec_qwords = 1 + (size_t)argc + 1 + (size_t)envc + 1 + aux_pairs * 2;
    size_t vec_bytes = vec_qwords * sizeof(uint64_t);
    uint64_t rsp = ((sp & ~0xFULL) - (uint64_t)vec_bytes) & ~0xFULL;

    if (rsp < stack_bottom) {
        if (arg_va) kfree(arg_va);
        if (env_va) kfree(env_va);
        return -E2BIG;
    }

    uint64_t *vec = (uint64_t *)kmalloc(vec_bytes);
    if (!vec) {
        if (arg_va) kfree(arg_va);
        if (env_va) kfree(env_va);
        return -ENOMEM;
    }

    size_t idx = 0;
    vec[idx++] = (uint64_t)argc;
    for (int i = 0; i < argc; i++)
        vec[idx++] = arg_va[i];
    vec[idx++] = 0ULL;
    for (int i = 0; i < envc; i++)
        vec[idx++] = env_va[i];
    vec[idx++] = 0ULL;

    vec[idx++] = AT_PHDR;   vec[idx++] = at_phdr;
    vec[idx++] = AT_PHENT;  vec[idx++] = (uint64_t)at_phent;
    vec[idx++] = AT_PHNUM;  vec[idx++] = (uint64_t)at_phnum;
    vec[idx++] = AT_PAGESZ; vec[idx++] = VMM_X64_PAGE_SIZE;
    vec[idx++] = AT_BASE;   vec[idx++] = at_base;
    vec[idx++] = AT_FLAGS;  vec[idx++] = 0ULL;
    vec[idx++] = AT_ENTRY;  vec[idx++] = at_entry;
    vec[idx++] = AT_UID;    vec[idx++] = (uint64_t)uid;
    vec[idx++] = AT_EUID;   vec[idx++] = (uint64_t)euid;
    vec[idx++] = AT_GID;    vec[idx++] = (uint64_t)gid;
    vec[idx++] = AT_EGID;   vec[idx++] = (uint64_t)egid;
    vec[idx++] = AT_SECURE; vec[idx++] = at_secure;
    vec[idx++] = AT_RANDOM; vec[idx++] = rand_va;
    vec[idx++] = AT_EXECFN; vec[idx++] = execfn_va;
    vec[idx++] = AT_NULL;   vec[idx++] = 0ULL;

    int wrc = write_user_space_bytes(space, rsp, vec, vec_bytes);
    kfree(vec);
    if (arg_va) kfree(arg_va);
    if (env_va) kfree(env_va);
    if (wrc != 0)
        return wrc;

    *rsp_out = rsp;
    return 0;
}

typedef struct {
    char **argv;
    int argc;
    char **envp;
    int envc;
    char exec_path[ELF64_PATH_MAX];
    int fd;
    vfs_stat_t st;
} prepared_exec_t;

static void free_str_vec(char **vec, int count)
{
    if (!vec)
        return;
    for (int i = 0; i < count; i++) {
        if (vec[i])
            kfree(vec[i]);
    }
    kfree(vec);
}

static char *dup_kstr(const char *s, size_t max_len)
{
    size_t l = cstr_len(s, max_len);
    char *copy = (char *)kmalloc(l + 1);
    if (!copy)
        return NULL;
    k_memcpy(copy, s, l);
    copy[l] = '\0';
    return copy;
}

static int parse_shebang_line(const char *hdr, size_t n,
                              char *interp_out, size_t interp_cap,
                              char *arg_out, size_t arg_cap,
                              int *has_arg_out)
{
    size_t i = 2;
    size_t line_end = n;
    *has_arg_out = 0;
    interp_out[0] = '\0';
    arg_out[0] = '\0';

    for (size_t k = 2; k < n; k++) {
        if (hdr[k] == '\n' || hdr[k] == '\r') {
            line_end = k;
            break;
        }
    }

    while (i < line_end && (hdr[i] == ' ' || hdr[i] == '\t'))
        i++;
    if (i >= line_end)
        return -ENOEXEC;

    size_t ilen = 0;
    while (i < line_end && hdr[i] != ' ' && hdr[i] != '\t') {
        if (ilen + 1 >= interp_cap)
            return -ENAMETOOLONG;
        interp_out[ilen++] = hdr[i++];
    }
    interp_out[ilen] = '\0';
    if (ilen == 0)
        return -ENOEXEC;

    while (i < line_end && (hdr[i] == ' ' || hdr[i] == '\t'))
        i++;
    if (i < line_end) {
        size_t alen = 0;
        while (i < line_end) {
            if (alen + 1 >= arg_cap)
                return -E2BIG;
            arg_out[alen++] = hdr[i++];
        }
        while (alen > 0 && (arg_out[alen - 1] == ' ' || arg_out[alen - 1] == '\t'))
            alen--;
        arg_out[alen] = '\0';
        if (alen > 0)
            *has_arg_out = 1;
    }
    return 0;
}

static int resolve_and_prepare_exec(prepared_exec_t *prep)
{
    for (int depth = 0; ; depth++) {
        if (vfs_stat(prep->exec_path, &prep->st) != 0)
            return -ENOENT;
        if (prep->st.type == VFS_TYPE_DIR)
            return -EISDIR;

        int fd = vfs_open(prep->exec_path);
        if (fd < 0)
            return fd;

        char hdr[256];
        k_memset(hdr, 0, sizeof(hdr));
        vfs_seek(fd, 0, VFS_SEEK_SET);
        size_t got = vfs_read(fd, hdr, sizeof(hdr));
        if (got >= 2 && hdr[0] == '#' && hdr[1] == '!') {
            vfs_close(fd);
            if (depth >= ELF64_SHEBANG_MAX_DEPTH)
                return -ELOOP;

            char interp[ELF64_PATH_MAX];
            char opt_arg[128];
            int has_arg = 0;
            int prc = parse_shebang_line(hdr, got, interp, sizeof(interp),
                                         opt_arg, sizeof(opt_arg), &has_arg);
            if (prc != 0)
                return prc;

            int extra = has_arg ? 2 : 1;
            int skip_orig0 = (prep->argc > 0) ? 1 : 0;
            int new_argc = extra + 1 + (prep->argc - skip_orig0);
            if (new_argc > ELF64_EXEC_MAX_ARGS)
                return -E2BIG;

            char **new_argv = (char **)kmalloc((size_t)new_argc * sizeof(char *));
            if (!new_argv)
                return -ENOMEM;
            k_memset(new_argv, 0, (size_t)new_argc * sizeof(char *));

            int pos = 0;
            new_argv[pos++] = dup_kstr(interp, ELF64_PATH_MAX - 1);
            if (has_arg)
                new_argv[pos++] = dup_kstr(opt_arg, sizeof(opt_arg) - 1);
            new_argv[pos++] = dup_kstr(prep->exec_path, ELF64_PATH_MAX - 1);
            for (int k = skip_orig0; k < prep->argc; k++) {
                new_argv[pos++] = prep->argv[k];
                prep->argv[k] = NULL;
            }
            for (int k = 0; k < extra + 1; k++) {
                if (!new_argv[k]) {
                    free_str_vec(new_argv, new_argc);
                    return -ENOMEM;
                }
            }

            free_str_vec(prep->argv, prep->argc);
            prep->argv = new_argv;
            prep->argc = new_argc;
            k_memset(prep->exec_path, 0, sizeof(prep->exec_path));
            k_memcpy(prep->exec_path, interp, cstr_len(interp, ELF64_PATH_MAX - 1));
            continue;
        }

        vfs_seek(fd, 0, VFS_SEEK_SET);
        prep->fd = fd;
        return 0;
    }
}

static int load_prepared_into_space(process_t *proc,
                                    prepared_exec_t *prep,
                                    vm_space_t *space_out,
                                    uint64_t *initial_rip_out,
                                    uint64_t *initial_rsp_out,
                                    uint64_t *elf_entry_out,
                                    uint32_t *new_euid_out,
                                    uint32_t *new_egid_out)
{
    elf64_ehdr_t eh;
    vfs_seek(prep->fd, 0, VFS_SEEK_SET);
    if (vfs_read(prep->fd, &eh, sizeof(eh)) != sizeof(eh) || !valid_elf64_ehdr(&eh, 1))
        return -ENOEXEC;

    size_t ph_bytes = (size_t)eh.e_phnum * sizeof(elf64_phdr_t);
    elf64_phdr_t *ph = (elf64_phdr_t *)kmalloc(ph_bytes);
    if (!ph)
        return -ENOMEM;

    vfs_seek(prep->fd, (size_t)eh.e_phoff, VFS_SEEK_SET);
    if (vfs_read(prep->fd, ph, ph_bytes) != ph_bytes) {
        kfree(ph);
        return -ENOEXEC;
    }

    if (vm_space_create(space_out) != 0) {
        kfree(ph);
        return -ENOMEM;
    }

    uint64_t min_va = min_elf_load_vaddr(ph, eh.e_phnum);
    uint64_t load_bias = (eh.e_type == ET_DYN_64) ? (ELF64_PIE_LOAD_BASE - min_va) : 0ULL;

    char interp_path[ELF64_PATH_MAX];
    int has_interp = 0;
    uint64_t max_elf_end = 0;
    uint64_t at_phdr = 0;
    int rc = 0;

    for (uint16_t i = 0; i < eh.e_phnum; i++) {
        if (ph[i].p_type == PT_PHDR_64) {
            at_phdr = load_bias + ph[i].p_vaddr;
        } else if (ph[i].p_type == PT_INTERP_64) {
            if (ph[i].p_filesz < 2 || ph[i].p_filesz > sizeof(interp_path)) {
                rc = -ENOEXEC;
                break;
            }
            k_memset(interp_path, 0, sizeof(interp_path));
            vfs_seek(prep->fd, (size_t)ph[i].p_offset, VFS_SEEK_SET);
            if (vfs_read(prep->fd, interp_path, (size_t)ph[i].p_filesz) != (size_t)ph[i].p_filesz) {
                rc = -ENOEXEC;
                break;
            }
            interp_path[sizeof(interp_path) - 1] = '\0';
            if (cstr_len(interp_path, sizeof(interp_path)) == 0) {
                rc = -ENOEXEC;
                break;
            }
            has_interp = 1;
        }
    }

    if (rc == 0) {
        for (uint16_t i = 0; i < eh.e_phnum; i++) {
            if (ph[i].p_type != PT_LOAD_64)
                continue;
            rc = stream_load_segment(space_out, prep->fd, &ph[i], load_bias);
            if (rc != 0)
                break;
            uint64_t vaddr = load_bias + ph[i].p_vaddr;
            uint64_t seg_end = (vaddr + ph[i].p_memsz + VMM_X64_PAGE_SIZE - 1) &
                               ~(VMM_X64_PAGE_SIZE - 1);
            if (seg_end > max_elf_end)
                max_elf_end = seg_end;
            if (at_phdr == 0 &&
                eh.e_phoff >= ph[i].p_offset &&
                eh.e_phoff < ph[i].p_offset + ph[i].p_filesz) {
                at_phdr = vaddr + (eh.e_phoff - ph[i].p_offset);
            }
        }
    }

    if (rc == 0) {
        if (max_elf_end < 0x400000ULL)
            max_elf_end = 0x10000000ULL;
        space_out->brk_start = (uintptr_t)max_elf_end;
        space_out->brk_current = (uintptr_t)max_elf_end;
        space_out->brk_limit = (max_elf_end < (uint64_t)VM_SPACE_SHM_BASE)
                                   ? (uintptr_t)VM_SPACE_SHM_BASE
                                   : (uintptr_t)ELF64_INTERP_LOAD_BASE;
    }

    uint64_t elf_entry = load_bias + eh.e_entry;
    uint64_t initial_rip = elf_entry;
    uint64_t at_base = 0ULL;

    if (rc == 0 && !paging_range_is_user((uintptr_t)elf_entry, 1))
        rc = -ENOEXEC;

    if (rc == 0 && has_interp) {
        rc = load_interp_image(space_out, interp_path, &at_base, &initial_rip);
    }

    uint32_t cred_uid = proc->uid;
    uint32_t cred_gid = proc->gid;
    uint32_t new_euid = proc->euid;
    uint32_t new_egid = proc->egid;
    uint64_t at_secure = 0;

    if (rc == 0 && !vfs_path_nosuid(prep->exec_path)) {
        if (prep->st.st_mode & 04000u) {
            new_euid = prep->st.st_uid;
            at_secure = 1;
        }
        if (prep->st.st_mode & 02000u) {
            new_egid = prep->st.st_gid;
            at_secure = 1;
        }
    }

    uint64_t rsp = 0;
    if (rc == 0) {
        rc = build_user_stack(space_out, proc->pid, prep->exec_path,
                              prep->argv, prep->argc,
                              prep->envp, prep->envc,
                              at_phdr, eh.e_phentsize, eh.e_phnum,
                              at_base, elf_entry,
                              cred_uid, new_euid, cred_gid, new_egid,
                              at_secure, &rsp);
    }

    kfree(ph);

    if (rc != 0) {
        vm_space_destroy(space_out);
        return rc;
    }

    *initial_rip_out = initial_rip;
    *initial_rsp_out = rsp;
    *elf_entry_out = elf_entry;
    *new_euid_out = new_euid;
    *new_egid_out = new_egid;
    return 0;
}

int elf64_execve_current(const char *path,
                         char *const argv[],
                         char *const envp[],
                         syscall_frame_t *frame)
{
    process_t *cur = process_current();
    if (!cur || !path || !frame)
        return -EINVAL;

    prepared_exec_t prep;
    k_memset(&prep, 0, sizeof(prep));
    prep.fd = -1;

    size_t plen = cstr_len(path, ELF64_PATH_MAX);
    if (plen == 0 || plen >= ELF64_PATH_MAX)
        return -ENAMETOOLONG;
    k_memcpy(prep.exec_path, path, plen + 1);

    size_t total_str = 0;
    if (argv) {
        int count = 0;
        while (argv[count]) {
            if (count >= ELF64_EXEC_MAX_ARGS)
                return -E2BIG;
            total_str += cstr_len(argv[count], ELF64_EXEC_MAX_STR_BYTES) + 1;
            if (total_str > ELF64_EXEC_MAX_STR_BYTES)
                return -E2BIG;
            count++;
        }
        if (count > 0) {
            prep.argv = (char **)kmalloc((size_t)count * sizeof(char *));
            if (!prep.argv)
                return -ENOMEM;
            k_memset(prep.argv, 0, (size_t)count * sizeof(char *));
            for (int i = 0; i < count; i++) {
                prep.argv[i] = dup_kstr(argv[i], ELF64_EXEC_MAX_STR_BYTES - 1);
                if (!prep.argv[i]) {
                    free_str_vec(prep.argv, count);
                    return -ENOMEM;
                }
            }
            prep.argc = count;
        }
    }
    if (prep.argc == 0) {
        prep.argv = (char **)kmalloc(sizeof(char *));
        if (!prep.argv)
            return -ENOMEM;
        prep.argv[0] = dup_kstr(path, ELF64_PATH_MAX - 1);
        if (!prep.argv[0]) {
            kfree(prep.argv);
            return -ENOMEM;
        }
        prep.argc = 1;
    }

    if (envp) {
        int count = 0;
        while (envp[count]) {
            if (count >= ELF64_EXEC_MAX_ENVS) {
                free_str_vec(prep.argv, prep.argc);
                return -E2BIG;
            }
            total_str += cstr_len(envp[count], ELF64_EXEC_MAX_STR_BYTES) + 1;
            if (total_str > ELF64_EXEC_MAX_STR_BYTES) {
                free_str_vec(prep.argv, prep.argc);
                return -E2BIG;
            }
            count++;
        }
        if (count > 0) {
            prep.envp = (char **)kmalloc((size_t)count * sizeof(char *));
            if (!prep.envp) {
                free_str_vec(prep.argv, prep.argc);
                return -ENOMEM;
            }
            k_memset(prep.envp, 0, (size_t)count * sizeof(char *));
            for (int i = 0; i < count; i++) {
                prep.envp[i] = dup_kstr(envp[i], ELF64_EXEC_MAX_STR_BYTES - 1);
                if (!prep.envp[i]) {
                    free_str_vec(prep.envp, count);
                    free_str_vec(prep.argv, prep.argc);
                    return -ENOMEM;
                }
            }
            prep.envc = count;
        }
    }

    int rc = resolve_and_prepare_exec(&prep);
    vm_space_t new_space;
    k_memset(&new_space, 0, sizeof(new_space));
    uint64_t initial_rip = 0;
    uint64_t initial_rsp = 0;
    uint64_t elf_entry = 0;
    uint32_t new_euid = cur->euid;
    uint32_t new_egid = cur->egid;

    if (rc == 0) {
        rc = load_prepared_into_space(cur, &prep, &new_space,
                                      &initial_rip, &initial_rsp, &elf_entry,
                                      &new_euid, &new_egid);
    }

    if (prep.fd >= 0) {
        vfs_close(prep.fd);
        prep.fd = -1;
    }

    if (rc != 0) {
        free_str_vec(prep.envp, prep.envc);
        free_str_vec(prep.argv, prep.argc);
        return rc;
    }

    /* Point of no return: replace process address space, close CLOEXEC FDs, update state. */
    shm_process_cleanup(cur);
    vm_space_t old_space;
    (void)process_swap_vm_space(cur, &new_space, &old_space);
    vm_space_destroy(&old_space);

    vfs_process_close_cloexec(cur);
    process_set_credentials(cur, cur->uid, new_euid, new_euid, cur->gid, new_egid, new_egid);
    process_execve_reset_state(cur);
    cur->user_entry = elf_entry;

    /* Set cmdline from new path. */
    process_set_cmdline((int)cur->pid, prep.exec_path);
    {
        const char *base = prep.exec_path;
        for (const char *p = prep.exec_path; *p; p++) {
            if (*p == '/' && p[1])
                base = p + 1;
        }
        size_t bl = cstr_len(base, PROCESS_NAME_MAX - 1);
        k_memcpy(cur->name, base, bl);
        cur->name[bl] = '\0';
    }

    free_str_vec(prep.envp, prep.envc);
    free_str_vec(prep.argv, prep.argc);

    /* Rewrite saved Ring-3 return frame so sysret enters new image cleanly. */
    frame->rdi = 0ULL;
    frame->rsi = 0ULL;
    frame->rdx = 0ULL;
    frame->r8  = 0ULL;
    frame->r9  = 0ULL;
    frame->r10 = 0ULL;
    frame->r15 = 0ULL;
    frame->r14 = 0ULL;
    frame->r13 = 0ULL;
    frame->r12 = 0ULL;
    frame->rbx = 0ULL;
    frame->rbp = 0ULL;
    frame->rcx = initial_rip;
    frame->r11 = 0x202ULL;
    frame->rsp = initial_rsp;
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

    prepared_exec_t prep;
    k_memset(&prep, 0, sizeof(prep));
    prep.fd = -1;
    k_memcpy(prep.exec_path, path, cstr_len(path, ELF64_PATH_MAX - 1) + 1);

    enum { SPAWN_ARGV_MAX = 16 };
    const char *tok[SPAWN_ARGV_MAX];
    int tok_len[SPAWN_ARGV_MAX];
    int argc = 0;
    const char *s = args;
    while (*s && argc < SPAWN_ARGV_MAX) {
        while (*s == ' ' || *s == '\t')
            s++;
        if (!*s)
            break;
        char quote = 0;
        if (*s == '"') {
            quote = '"';
            s++;
        }
        tok[argc] = s;
        while (*s && (quote ? (*s != '"') : (*s != ' ' && *s != '\t')))
            s++;
        tok_len[argc] = (int)(s - tok[argc]);
        argc++;
        if (quote && *s == '"')
            s++;
    }
    if (argc == 0) {
        tok[0] = path;
        tok_len[0] = (int)cstr_len(path, ELF64_PATH_MAX - 1);
        argc = 1;
    }

    prep.argv = (char **)kmalloc((size_t)argc * sizeof(char *));
    if (!prep.argv)
        process_exit(-1);
    k_memset(prep.argv, 0, (size_t)argc * sizeof(char *));
    for (int i = 0; i < argc; i++) {
        prep.argv[i] = (char *)kmalloc((size_t)tok_len[i] + 1);
        if (!prep.argv[i]) {
            free_str_vec(prep.argv, argc);
            process_exit(-1);
        }
        k_memcpy(prep.argv[i], tok[i], (size_t)tok_len[i]);
        prep.argv[i][tok_len[i]] = '\0';
    }
    prep.argc = argc;

    int rc = resolve_and_prepare_exec(&prep);
    vm_space_t new_space;
    k_memset(&new_space, 0, sizeof(new_space));
    uint64_t initial_rip = 0;
    uint64_t initial_rsp = 0;
    uint64_t elf_entry = 0;
    uint32_t new_euid = self->euid;
    uint32_t new_egid = self->egid;

    if (rc == 0) {
        rc = load_prepared_into_space(self, &prep, &new_space,
                                      &initial_rip, &initial_rsp, &elf_entry,
                                      &new_euid, &new_egid);
    }

    if (prep.fd >= 0) {
        vfs_close(prep.fd);
        prep.fd = -1;
    }
    free_str_vec(prep.argv, prep.argc);

    if (rc != 0) {
        kprintf("[elf64] spawn load failed (%d): %s\n", rc, path);
        process_exit(-1);
    }

    (void)process_swap_vm_space(self, &new_space, NULL);
    process_set_credentials(self, self->uid, new_euid, new_euid, self->gid, new_egid, new_egid);
    self->user_entry = elf_entry;

    if (self->kernel_stack)
        tss_set_rsp0_x64(process_stack_top_aligned(self));
    enter_ring3(initial_rip, initial_rsp);
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
