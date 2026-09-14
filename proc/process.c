/*
 * Project Tsukasa — x86_64 process model, scheduler, lifecycle, and Phase 2 tests
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

#include "process.h"

#include <stddef.h>
#include <stdint.h>

#include "../arch/x86_64/cpu/gdt.h"
#include "../include/paging.h"
#include "../include/errno.h"
#include "../include/kprintf.h"
#include "../include/smp.h"
#include "../include/spinlock.h"
#include "../fs/vfs.h"
#include "../loader/exec.h"
#include "../loader/elf64.h"
#include "../ipc/shm.h"
#include "../mm/heap.h"
#include "../mm/pmm.h"
#include "../mm/vmm_x64.h"
#include "../gfx/gui_srv.h"
#include "../gfx/wm.h"
#include "../net/network.h"
#include "../sys/futex.h"
#include "../sys/work_queue.h"
#include "../syscall/syscall.h"
#include "../tty/tty.h"
#include "../user/include/shell.h"

#define CPU_COUNT_MAX 32
#define WAIT_STATUS_EXIT(code)   (((code) & 0xFF) << 8)
#define WAIT_STATUS_SIGNAL(sig)  ((sig) & 0x7F)
#define WAIT_EXIT_CODE(st)       (((st) >> 8) & 0xFF)
#define WAIT_TERM_SIGNAL(st)     ((st) & 0x7F)

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
#define PROCESS_STACK_PAGES ((PROCESS_STACK_SIZE + PAGE_SIZE - 1) / PAGE_SIZE)
#define PROCESS_CONTEXT_GPR_QWORDS 15u
#define PROCESS_CONTEXT_TOTAL_QWORDS 21u
#define PROCESS_CONTEXT_GUARD_QWORDS 16u
#define PROCESS_CONTEXT_FRAME_QWORDS (PROCESS_CONTEXT_TOTAL_QWORDS + PROCESS_CONTEXT_GUARD_QWORDS)
#define PROCESS_CONTEXT_TOP_BIAS 24u
#define PROCESS_CONTEXT_IRET_INDEX 19u

static process_t g_processes[PROCESS_MAX_COUNT];
static process_t *g_current[CPU_COUNT_MAX + 1];
static process_t *g_idle[CPU_COUNT_MAX + 1];
static process_t *g_runq_head[PROCESS_PRIORITY_LEVELS];
static process_t *g_runq_tail[PROCESS_PRIORITY_LEVELS];

static spinlock_t g_sched_lock = SPINLOCK_INIT;
static uint32_t g_next_pid = 1;
static volatile uint64_t g_sched_ticks;
static int g_ctx_warned;

static uint32_t g_cpu_count = 1;
static volatile uint64_t g_core_ticks[CPU_COUNT_MAX + 1];
static volatile uint64_t g_ipi_ticks[CPU_COUNT_MAX + 1];
static volatile uint64_t g_idle_seen_ticks[CPU_COUNT_MAX + 1];
static volatile uint64_t g_idle_loop_ticks[CPU_COUNT_MAX + 1];

static void process_ap_idle_entry(void);

static int g_inited;

static inline uint64_t irq_save_disable(void)
{
    uint64_t flags = 0;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(uint64_t flags)
{
    if (flags & (1ULL << 9))
        __asm__ volatile ("sti" : : : "memory");
    else
        __asm__ volatile ("cli" : : : "memory");
}

static inline uint32_t sched_this_cpu(void)
{
    uint32_t cpu = smp_this_cpu_id();
    return (cpu < CPU_COUNT_MAX) ? cpu : CPU_COUNT_MAX;
}

static int process_is_current_anywhere_locked(const process_t *p)
{
    for (uint32_t cpu = 0; cpu < g_cpu_count; cpu++) {
        if (g_current[cpu] == p)
            return 1;
    }
    return 0;
}

static int process_is_current_on_other_cpu_locked(const process_t *p, uint32_t this_cpu)
{
    for (uint32_t cpu = 0; cpu < g_cpu_count; cpu++) {
        if (cpu != this_cpu && g_current[cpu] == p)
            return 1;
    }
    return 0;
}

static void name_copy(char *dst, const char *src, int cap)
{
    int i = 0;
    if (!dst || cap <= 0)
        return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void process_init_io_state(process_t *p, const process_t *parent)
{
    if (!p)
        return;
    for (int i = 0; i < PROCESS_MAX_OPEN_FILES; i++) {
        p->open_files[i] = NULL;
        p->fds[i].fd = -1;
        p->fds[i].flags = 0;
        p->fds[i].offset = 0;
        p->fds[i].file_desc = -1;
    }
    p->next_fd = 0;
    if (parent && parent->cwd[0]) {
        name_copy(p->cwd, parent->cwd, PROCESS_CWD_MAX);
    } else {
        name_copy(p->cwd, "/", PROCESS_CWD_MAX);
    }
    p->cmdline[0] = '\0';
}

static void process_init_sched_state(process_t *p, process_t *parent)
{
    if (!p)
        return;

    p->priority = parent ? parent->priority : PROCESS_DEFAULT_PRIORITY;
    p->time_slice = PROCESS_DEFAULT_TIMESLICE;
    p->cpu_affinity = parent ? parent->cpu_affinity : sched_this_cpu();
    if (p->cpu_affinity == PROCESS_CPU_AFFINITY_ANY || p->cpu_affinity >= g_cpu_count)
        p->cpu_affinity = sched_this_cpu();
    p->cpu_id = p->cpu_affinity;
    p->sched_ticks = 0;
    p->created_time = g_sched_ticks;
    p->next_queue = NULL;
    p->prev_queue = NULL;
    p->parent = parent;
    p->child_count = 0;

    p->main_thread.tid = p->pid;
    p->main_thread.cpu_id = p->cpu_id;
    p->main_thread.state = THREAD_READY;
    p->main_thread.priority = p->priority;
    p->main_thread.time_slice = p->time_slice;
    p->main_thread.sched_ticks = 0;
    p->main_thread.kernel_rsp = p->kernel_rsp;
    p->main_thread.kernel_stack = p->kernel_stack;
    p->main_thread.user_rsp = 0;
    p->main_thread.entry = p->entry;
    p->main_thread.process = p;
    p->main_thread.next_queue = NULL;
    p->main_thread.prev_queue = NULL;
}

static process_t *find_by_pid_locked(int pid)
{
    for (int i = 0; i < PROCESS_MAX_COUNT; i++) {
        if (g_processes[i].used && (int)g_processes[i].pid == pid)
            return &g_processes[i];
    }
    return NULL;
}

static process_t *alloc_process_slot_locked(void)
{
    for (int i = 0; i < PROCESS_MAX_COUNT; i++) {
        if (!g_processes[i].used)
            return &g_processes[i];
    }
    return NULL;
}

static void runq_push_locked(process_t *p)
{
    uint32_t pri;
    if (!p)
        return;
    if (p->is_idle)
        return;
    pri = p->priority;
    if (pri >= PROCESS_PRIORITY_LEVELS)
        pri = PROCESS_PRIORITY_LEVELS - 1;
    p->rq_next = NULL;
    p->next_queue = NULL;
    p->prev_queue = g_runq_tail[pri];
    if (!g_runq_head[pri]) {
        g_runq_head[pri] = p;
        g_runq_tail[pri] = p;
        return;
    }
    g_runq_tail[pri]->rq_next = p;
    g_runq_tail[pri]->next_queue = p;
    g_runq_tail[pri] = p;
}

static inline int runq_eligible_on_cpu_locked(const process_t *p, uint32_t cpu)
{
    if (process_is_current_on_other_cpu_locked(p, cpu))
        return 0;
    return p->cpu_affinity == PROCESS_CPU_AFFINITY_ANY || p->cpu_affinity == cpu;
}

static process_t *runq_pop_locked(uint32_t cpu)
{
    for (uint32_t pri = 0; pri < PROCESS_PRIORITY_LEVELS; pri++) {
        process_t *prev = NULL;
        process_t *p = g_runq_head[pri];
        while (p && !runq_eligible_on_cpu_locked(p, cpu)) {
            prev = p;
            p = p->rq_next;
        }
        if (!p)
            continue;
        if (prev)
            prev->rq_next = p->rq_next;
        else
            g_runq_head[pri] = p->rq_next;
        if (g_runq_tail[pri] == p)
            g_runq_tail[pri] = prev;
        if (p->rq_next)
            p->rq_next->prev_queue = prev;
        if (prev)
            prev->next_queue = p->rq_next;
        p->rq_next = NULL;
        p->next_queue = NULL;
        p->prev_queue = NULL;
        return p;
    }
    return NULL;
}

static int runq_best_priority_locked(uint32_t cpu)
{
    for (uint32_t pri = 0; pri < PROCESS_PRIORITY_LEVELS; pri++) {
        for (const process_t *p = g_runq_head[pri]; p; p = p->rq_next) {
            if (runq_eligible_on_cpu_locked(p, cpu))
                return (int)pri;
        }
    }
    return -1;
}

static void runq_remove_locked(process_t *target)
{
    uint32_t pri;
    process_t *prev = NULL;
    process_t *cur;

    if (!target)
        return;
    pri = target->priority;
    if (pri >= PROCESS_PRIORITY_LEVELS)
        pri = PROCESS_PRIORITY_LEVELS - 1;

    cur = g_runq_head[pri];
    while (cur) {
        if (cur == target) {
            if (prev)
                prev->rq_next = cur->rq_next;
            else
                g_runq_head[pri] = cur->rq_next;
            if (g_runq_tail[pri] == cur)
                g_runq_tail[pri] = prev;
            if (cur->rq_next)
                cur->rq_next->prev_queue = prev;
            if (prev)
                prev->next_queue = cur->rq_next;
            cur->rq_next = NULL;
            cur->next_queue = NULL;
            cur->prev_queue = NULL;
            return;
        }
        prev = cur;
        cur = cur->rq_next;
    }
}

static void parent_link_child_locked(process_t *parent, process_t *child)
{
    if (!parent || !child)
        return;
    child->parent_next_child = parent->children_head;
    parent->children_head = child;
    child->parent = parent;
    if (parent->child_count < PROCESS_MAX_CHILDREN)
        parent->children[parent->child_count++] = child;
}

static void parent_unlink_child_locked(process_t *parent, process_t *child)
{
    process_t *prev = NULL;
    process_t *cur = NULL;
    uint32_t idx = 0;
    if (!parent || !child)
        return;

    cur = parent->children_head;
    while (cur) {
        if (cur == child) {
            if (prev)
                prev->parent_next_child = cur->parent_next_child;
            else
                parent->children_head = cur->parent_next_child;
            child->parent_next_child = NULL;
            child->parent = NULL;
            break;
        }
        prev = cur;
        cur = cur->parent_next_child;
    }

    while (idx < parent->child_count) {
        if (parent->children[idx] == child) {
            for (uint32_t j = idx + 1; j < parent->child_count; j++)
                parent->children[j - 1] = parent->children[j];
            parent->children[--parent->child_count] = NULL;
            break;
        }
        idx++;
    }
}

static void free_process_resources_locked(process_t *p)
{
    if (!p)
        return;

    gui_srv_process_cleanup((int)p->pid);
    vfs_process_cleanup(p);
    shm_process_cleanup(p);
    vm_space_destroy(&p->vm_space);

    if (p->kernel_stack) {
        if (p->kernel_stack_from_pmm) {
            if (p->kernel_stack_phys)
                pmm_free_pages(p->kernel_stack_phys, PROCESS_STACK_PAGES);
        } else {
            kfree(p->kernel_stack);
        }
        p->kernel_stack = NULL;
        p->kernel_stack_phys = 0;
        p->kernel_stack_from_pmm = 0;
    }
}

static void reset_process_slot_locked(process_t *p)
{
    if (!p)
        return;
    free_process_resources_locked(p);
    p->used = 0;
    p->state = PROCESS_DEAD;
    p->shm_attachment_count = 0;
    p->rq_next = NULL;
    p->next_queue = NULL;
    p->prev_queue = NULL;
    p->parent = NULL;
    p->child_count = 0;
    p->children_head = NULL;
    p->parent_next_child = NULL;
}

/* Initial context matches the x64 IRQ epilogue: [rax..r15][vector][rip][cs][rflags][rsp][ss] with rsp... */
extern void process_entry_resume(void);

static int setup_initial_context_locked(process_t *p)
{
    uint64_t *sp;
    uintptr_t top;
    if (!p || !p->kernel_stack)
        return -1;

    /* Keep final entry rsp 16-byte ABI compatible (rsp % 16 == 8) and reserve in-frame rsp/ss words so iretq has... */
    top = (((uintptr_t)p->kernel_stack + PROCESS_STACK_SIZE) & ~(uintptr_t)0xFULL) -
          (uintptr_t)PROCESS_CONTEXT_TOP_BIAS;
    sp = (uint64_t *)top;
    sp -= PROCESS_CONTEXT_IRET_INDEX;

    for (int i = 0; i < 15; i++)
        sp[i] = 0;

    sp[15] = 0;
    sp[16] = (uint64_t)(uintptr_t)process_entry_resume;
    sp[17] = X64_GDT_KERNEL_CS;
    sp[18] = 0x202;
    sp[19] = (uint64_t)top;
    sp[20] = X64_GDT_KERNEL_DS;

    for (uint32_t i = 0; i < PROCESS_CONTEXT_GUARD_QWORDS; i++)
        sp[PROCESS_CONTEXT_TOTAL_QWORDS + i] = 0;

    p->kernel_rsp = (uint64_t)(uintptr_t)sp;
    p->main_thread.kernel_rsp = p->kernel_rsp;
    p->main_thread.kernel_stack = p->kernel_stack;
    return 0;
}

/* Issue #21 (smp1 false-positive fix): a saved frame at kernel_rsp is RESUMABLE when its iret tail (RIP@16... */
static int frame_is_resumable(const uint64_t *ctx, uint64_t low, uint64_t high)
{
    uint64_t rip = ctx[16];
    uint64_t cs = ctx[17];
    uint64_t rsp = ctx[19];
    uint64_t ss = ctx[20];

    if (cs == X64_GDT_KERNEL_CS && ss == X64_GDT_KERNEL_DS)
        return rip != 0 &&
               rsp >= low && rsp <= (high - sizeof(uint64_t));

    if (cs == X64_GDT_USER_CS && ss == X64_GDT_USER_DS)
        return rip >= PAGING_USER_VA_MIN && rip < PAGING_USER_VA_MAX &&
               rsp >= PAGING_USER_VA_MIN && rsp < PAGING_USER_VA_MAX;

    return 0;
}

static int validate_or_repair_context_locked(process_t *p)
{
    uint64_t low;
    uint64_t high;
    uint64_t *ctx;

    if (!p || !p->kernel_stack)
        return -1;

    low = (uint64_t)(uintptr_t)p->kernel_stack;
    high = low + PROCESS_STACK_SIZE;

    if (p->kernel_rsp < low ||
        p->kernel_rsp > (high - (PROCESS_CONTEXT_TOTAL_QWORDS * sizeof(uint64_t)))) {
        if (!g_ctx_warned) {
            g_ctx_warned = 1;
            kprintf("[proc] WARN: bad kernel_rsp pid=%u rsp=0x%08x%08x stack=0x%08x%08x-0x%08x%08x (repair)\n",
                    (unsigned)p->pid,
                    (uint32_t)(p->kernel_rsp >> 32), (uint32_t)(p->kernel_rsp & 0xFFFFFFFFu),
                    (uint32_t)(low >> 32), (uint32_t)(low & 0xFFFFFFFFu),
                    (uint32_t)(high >> 32), (uint32_t)(high & 0xFFFFFFFFu));
        }
        return setup_initial_context_locked(p);
    }

    ctx = (uint64_t *)(uintptr_t)p->kernel_rsp;
    if (!frame_is_resumable(ctx, low, high)) {
        if (!g_ctx_warned) {
            g_ctx_warned = 1;
            kprintf("[proc] WARN: bad ctx frame pid=%u rip=0x%08x%08x cs=0x%08x%08x rsp=0x%08x%08x ss=0x%08x%08x (repair)\n",
                    (unsigned)p->pid,
                    (uint32_t)(ctx[16] >> 32), (uint32_t)(ctx[16] & 0xFFFFFFFFu),
                    (uint32_t)(ctx[17] >> 32), (uint32_t)(ctx[17] & 0xFFFFFFFFu),
                    (uint32_t)(ctx[19] >> 32), (uint32_t)(ctx[19] & 0xFFFFFFFFu),
                    (uint32_t)(ctx[20] >> 32), (uint32_t)(ctx[20] & 0xFFFFFFFFu));
        }
        return setup_initial_context_locked(p);
    }

    return 0;
}

static int alloc_process_stack_locked(process_t *p)
{
    uintptr_t stack_phys = 0;
    uintptr_t stack_virt = 0;
    if (!p)
        return -1;

    stack_phys = pmm_alloc_pages(PROCESS_STACK_PAGES);
    if (!stack_phys) {
        kprintf("[proc] WARN: stack alloc failed for '%s' (pid=%u) count=%u free_pages=%u\n",
                p->name, (unsigned)p->pid, (unsigned)PROCESS_STACK_PAGES, (unsigned)pmm_free_page_count());
        return -1;
    }

    stack_virt = (uintptr_t)vmm_phys_to_virt((uint64_t)stack_phys);
    if (!stack_virt)
        return -1;

    p->kernel_stack = (void *)stack_virt;
    p->kernel_stack_phys = stack_phys;
    p->kernel_stack_from_pmm = 1;
    p->main_thread.kernel_stack = p->kernel_stack;

    return p->kernel_stack ? 0 : -1;
}

uint64_t process_stack_top_aligned(const process_t *p)
{
    if (!p || !p->kernel_stack)
        return 0;
    return (((uint64_t)(uintptr_t)p->kernel_stack + PROCESS_STACK_SIZE) &
            ~0xFULL);
}

static void wake_parent_if_waiting_locked(process_t *child)
{
    process_t *parent;
    if (!child || child->ppid == 0)
        return;
    parent = find_by_pid_locked((int)child->ppid);
    if (!parent)
        return;
    if (parent->state == PROCESS_BLOCKED) {
        parent->state = PROCESS_READY;
        parent->main_thread.state = THREAD_READY;
        runq_push_locked(parent);
    }
}

void process_block_current(void)
{
    unsigned long irqf = spin_lock_irqsave(&g_sched_lock);
    process_t *self = g_current[sched_this_cpu()];
    if (self && !self->is_idle) {
        self->state = PROCESS_BLOCKED;
        self->main_thread.state = THREAD_BLOCKED;
    }
    spin_unlock_irqrestore(&g_sched_lock, irqf);
}

int process_wake_blocked(process_t *p)
{
    int woke = 0;
    unsigned long irqf = spin_lock_irqsave(&g_sched_lock);
    if (p && p->used && !p->is_idle && p->state == PROCESS_BLOCKED) {
        p->state = PROCESS_READY;
        p->main_thread.state = THREAD_READY;
        runq_push_locked(p);
        woke = 1;
    }
    spin_unlock_irqrestore(&g_sched_lock, irqf);
    return woke;
}

static void mark_zombie_locked(process_t *p, int wait_status)
{
    if (!p)
        return;
    if (p->state == PROCESS_ZOMBIE || p->state == PROCESS_DEAD)
        return;

    /* Reparent orphaned children to PID 1 (init) */
    process_t *init_proc = find_by_pid_locked(1);
    process_t *child = p->children_head;
    while (child) {
        process_t *next_child = child->parent_next_child;
        child->ppid = (init_proc && init_proc != p) ? 1 : 0;
        if (init_proc && init_proc != p) {
            child->parent_next_child = init_proc->children_head;
            init_proc->children_head = child;
            if (child->state == PROCESS_ZOMBIE)
                wake_parent_if_waiting_locked(child);
        } else {
            child->parent_next_child = NULL;
        }
        child = next_child;
    }
    p->children_head = NULL;

    p->wait_status = wait_status;
    p->exit_code = WAIT_EXIT_CODE(wait_status);
    p->exit_signal = WAIT_TERM_SIGNAL(wait_status);
    p->exit_time = g_sched_ticks;
    p->state = PROCESS_ZOMBIE;
    p->main_thread.state = THREAD_ZOMBIE;
    p->kill_pending = 0;
    runq_remove_locked(p);
    wake_parent_if_waiting_locked(p);
}

static int signal_pick_lowest(uint64_t mask)
{
    for (int sig = 1; sig < PROCESS_MAX_SIGNALS; sig++) {
        if (mask & (1ULL << (uint32_t)sig))
            return sig;
    }
    return -1;
}

static int prepare_signal_action_locked(process_t *p, uintptr_t *handler_out, int *sig_out)
{
    uint64_t pending;
    int sig;
    uintptr_t handler;

    if (!p || p->state == PROCESS_ZOMBIE || p->state == PROCESS_DEAD)
        return 0;

    pending = p->signal_pending & ~p->signal_mask;
    if (!pending)
        return 0;

    sig = signal_pick_lowest(pending);
    if (sig < 0)
        return 0;

    p->signal_pending &= ~(1ULL << (uint32_t)sig);
    handler = p->signal_handlers[sig];

    if (handler == PROCESS_SIG_IGN)
        return 0;

    /* PID 1 (init) cannot be terminated by unhandled signals */
    if (p->pid == 1 && (handler == PROCESS_SIG_DFL || sig == PROCESS_SIGKILL || sig == PROCESS_SIGINT))
        return 0;

    if (handler == PROCESS_SIG_DFL || sig == PROCESS_SIGKILL || sig == PROCESS_SIGINT) {
        mark_zombie_locked(p, WAIT_STATUS_SIGNAL(sig));
        return 0;
    }

    if (handler_out)
        *handler_out = handler;
    if (sig_out)
        *sig_out = sig;
    return 1;
}

static process_t *create_kernel_process_locked(const char *name, process_entry_t entry, process_t *parent)
{
    process_t *p = alloc_process_slot_locked();
    const vm_space_t *template_space = NULL;
    if (!p)
        return NULL;

    for (size_t i = 0; i < sizeof(*p); i++)
        ((uint8_t *)p)[i] = 0;

    p->used = 1;
    p->pid = g_next_pid++;
    p->ppid = parent ? parent->pid : 0;
    p->pgid = p->pid;
    p->sid = parent ? parent->sid : p->pid;
    p->uid = parent ? parent->uid : 0;
    p->gid = parent ? parent->gid : 0;
    p->cpu_id = 0;
    p->state = PROCESS_READY;
    p->entry = entry;
    p->tty_id = parent ? parent->tty_id : 0;
    p->shm_attachment_count = 0;
    process_init_sched_state(p, parent);
    process_init_io_state(p, parent);
    if (parent)
        vfs_process_inherit(p, parent);
    name_copy(p->name, name ? name : "proc", PROCESS_NAME_MAX);

    if (parent)
        template_space = &parent->vm_space;
    else if (g_current[sched_this_cpu()])
        template_space = &g_current[sched_this_cpu()]->vm_space;

    if (template_space && template_space->pml4_phys) {
        p->vm_space.pml4_phys = template_space->pml4_phys;
        p->vm_space.user_min = template_space->user_min;
        p->vm_space.user_max = template_space->user_max;
        p->vm_space.shm_cursor = (uintptr_t)VM_SPACE_SHM_BASE + (((uintptr_t)p->pid % 64) * 0x01000000ULL);
        p->vm_space.anon_cursor = (uintptr_t)VM_SPACE_ANON_BASE;
        p->vm_space.mapped_pages = 0;
        p->vm_space.shm_pages = 0;
        p->vm_space.owns_pml4 = 0;
    } else if (vm_space_create(&p->vm_space) != 0) {
        p->used = 0;
        kprintf("[proc] WARN: vm_space init failed for '%s' (pid=%u)\n",
                p->name, (unsigned)p->pid);
        return NULL;
    }
    p->va_space = &p->vm_space;

    if (alloc_process_stack_locked(p) != 0) {
        vm_space_destroy(&p->vm_space);
        p->used = 0;
        kprintf("[proc] WARN: stack alloc failed for '%s' (pid=%u)\n",
                p->name, (unsigned)p->pid);
        return NULL;
    }

    if (setup_initial_context_locked(p) != 0) {
        free_process_resources_locked(p);
        p->used = 0;
        return NULL;
    }
    p->main_thread.state = THREAD_READY;
    p->main_thread.entry = p->entry;

    if (parent)
        parent_link_child_locked(parent, p);

    runq_push_locked(p);
    return p;
}

void process_init(void)
{
    uint64_t flags;
    process_t *bootstrap = NULL;
    process_t *idle = NULL;

    if (g_inited)
        return;

    g_cpu_count = smp_cpu_count();
    if (g_cpu_count < 1)
        g_cpu_count = 1;
    if (g_cpu_count > CPU_COUNT_MAX)
        g_cpu_count = CPU_COUNT_MAX;

    flags = irq_save_disable();
    spin_lock(&g_sched_lock);

    for (int i = 0; i < PROCESS_MAX_COUNT; i++) {
        for (size_t j = 0; j < sizeof(g_processes[i]); j++)
            ((uint8_t *)&g_processes[i])[j] = 0;
    }

    for (uint32_t pri = 0; pri < PROCESS_PRIORITY_LEVELS; pri++) {
        g_runq_head[pri] = NULL;
        g_runq_tail[pri] = NULL;
    }
    g_sched_ticks = 0;
    g_next_pid = 1;

    bootstrap = alloc_process_slot_locked();
    if (!bootstrap) {
        spin_unlock(&g_sched_lock);
        irq_restore(flags);
        return;
    }

    bootstrap->used = 1;
    bootstrap->pid = 0;
    bootstrap->ppid = 0;
    bootstrap->pgid = 0;
    bootstrap->sid = 0;
    bootstrap->uid = 0;
    bootstrap->gid = 0;
    bootstrap->cpu_id = 0;
    bootstrap->state = PROCESS_BLOCKED;
    bootstrap->tty_id = 0;
    bootstrap->shm_attachment_count = 0;
    bootstrap->entry = NULL;
    process_init_sched_state(bootstrap, NULL);
    bootstrap->main_thread.state = THREAD_BLOCKED;
    process_init_io_state(bootstrap, NULL);
    name_copy(bootstrap->name, "swapper", PROCESS_NAME_MAX);
    if (vm_space_init_kernel(&bootstrap->vm_space) != 0) {
        spin_unlock(&g_sched_lock);
        irq_restore(flags);
        return;
    }
    bootstrap->va_space = &bootstrap->vm_space;
    g_current[0] = bootstrap;

    idle = create_kernel_process_locked("idle0", NULL, NULL);
    if (idle) {
        idle->is_idle = 1;
        idle->pid = 0;
        idle->pgid = 0;
        idle->sid = 0;
        g_idle[0] = idle;
        runq_remove_locked(idle);
    }

    /* First non-idle user/system task will receive PID 1 */
    g_next_pid = 1;

    spin_unlock(&g_sched_lock);
    irq_restore(flags);
    g_inited = 1;

    /* Each adopts the work-queue drain loop its core is already executing (see process_spawn_idle_for_cpu). */
    for (uint32_t cpu = 1; cpu < g_cpu_count; cpu++)
        process_spawn_idle_for_cpu(cpu);
}

process_t *process_current(void)
{
    return g_current[sched_this_cpu()];
}

int process_current_pid(void)
{
    process_t *p = process_current();
    return p ? (int)p->pid : -1;
}

int process_get_pgid(int pid)
{
    process_t *p;
    uint64_t flags = irq_save_disable();
    spin_lock(&g_sched_lock);
    p = find_by_pid_locked(pid);
    spin_unlock(&g_sched_lock);
    irq_restore(flags);
    return p ? (int)p->pgid : -1;
}

/* Give the calling process a private user address space: its own PML4 (with the kernel higher-half shared,... */
int process_adopt_private_address_space(void)
{
    process_t *p;
    vm_space_t priv;
    uint64_t flags;

    if (vm_space_create(&priv) != 0)
        return -1;

    flags = irq_save_disable();
    spin_lock(&g_sched_lock);
    p = g_current[sched_this_cpu()];
    if (!p || p->vm_space.owns_pml4) {
        spin_unlock(&g_sched_lock);
        irq_restore(flags);
        vm_space_destroy(&priv);
        return p ? 0 : -1;
    }
    p->vm_space.pml4_phys = priv.pml4_phys;
    p->vm_space.user_min = priv.user_min;
    p->vm_space.user_max = priv.user_max;
    p->vm_space.shm_cursor = priv.shm_cursor;
    p->vm_space.anon_cursor = priv.anon_cursor;
    p->vm_space.mapped_pages = 0;
    p->vm_space.shm_pages = 0;
    p->vm_space.owns_pml4 = 1;
    vmm_switch_pml4(priv.pml4_phys);
    spin_unlock(&g_sched_lock);
    irq_restore(flags);
    return 0;
}

process_t *process_spawn_kernel(const char *name, process_entry_t entry)
{
    process_t *parent;
    process_t *child;
    uint64_t flags;
    if (!entry)
        return NULL;

    flags = irq_save_disable();
    spin_lock(&g_sched_lock);
    parent = g_current[sched_this_cpu()];
    child = create_kernel_process_locked(name, entry, parent);
    spin_unlock(&g_sched_lock);
    irq_restore(flags);
    return child;
}

process_t *process_spawn_kernel_pinned(const char *name, process_entry_t entry,
                                       uint32_t cpu_id)
{
    process_t *parent;
    process_t *child;
    uint64_t flags;
    if (!entry || cpu_id >= g_cpu_count)
        return NULL;

    flags = irq_save_disable();
    spin_lock(&g_sched_lock);
    parent = g_current[sched_this_cpu()];
    child = create_kernel_process_locked(name, entry, parent);
    if (child) {
        runq_remove_locked(child);
        child->cpu_affinity = cpu_id;
        child->cpu_id = cpu_id;
        child->main_thread.cpu_id = cpu_id;
        runq_push_locked(child);
    }
    spin_unlock(&g_sched_lock);
    irq_restore(flags);
    return child;
}

/* The AP is already executing sys/smp.c::ap_entry()'s work_queue_drain_loop() on its own boot live execution... */
process_t *process_spawn_idle_for_cpu(uint32_t cpu_id)
{
    process_t *p;
    cpu_state_t *cs;
    uint64_t flags;
    char idle_name[8];
    int n = 0;

    if (cpu_id == 0 || cpu_id >= g_cpu_count)
        return NULL;
    cs = smp_get_cpu(cpu_id);
    if (!cs || !cs->online || !cs->kernel_stack)
        return NULL;
    if (g_idle[cpu_id])
        return g_idle[cpu_id];

    idle_name[n++] = 'i';
    idle_name[n++] = 'd';
    idle_name[n++] = 'l';
    idle_name[n++] = 'e';
    if (cpu_id >= 10)
        idle_name[n++] = (char)('0' + (cpu_id / 10) % 10);
    idle_name[n++] = (char)('0' + cpu_id % 10);
    idle_name[n] = '\0';

    flags = irq_save_disable();
    spin_lock(&g_sched_lock);

    p = alloc_process_slot_locked();
    if (!p) {
        spin_unlock(&g_sched_lock);
        irq_restore(flags);
        kprintf("[guide06] WARN: no process slot for %s\n", idle_name);
        return NULL;
    }

    for (size_t i = 0; i < sizeof(*p); i++)
        ((uint8_t *)p)[i] = 0;

    p->used = 1;
    p->pid = 0;
    p->ppid = 0;
    p->pgid = 0;
    p->sid = 0;
    p->uid = 0;
    p->gid = 0;
    p->cpu_id = cpu_id;
    p->state = PROCESS_RUNNING;
    p->is_idle = 1;
    p->entry = process_ap_idle_entry;
    p->tty_id = 0;
    p->shm_attachment_count = 0;
    process_init_sched_state(p, NULL);
    p->cpu_affinity = cpu_id;
    process_init_io_state(p, NULL);
    name_copy(p->name, idle_name, PROCESS_NAME_MAX);

    p->kernel_stack = (void *)(uintptr_t)(cs->kernel_stack - PROCESS_STACK_SIZE);
    p->kernel_stack_phys = 0;
    p->kernel_stack_from_pmm = 0;
    p->kernel_rsp = 0;
    p->main_thread.kernel_stack = p->kernel_stack;
    p->main_thread.kernel_rsp = 0;
    p->main_thread.cpu_id = cpu_id;
    p->main_thread.state = THREAD_RUNNING;

    if (g_current[0] && g_current[0]->vm_space.pml4_phys) {
        /* Share the boot kernel address space, exactly like other kernel processes spawned without a parent... */
        p->vm_space.pml4_phys = g_current[0]->vm_space.pml4_phys;
        p->vm_space.user_min = g_current[0]->vm_space.user_min;
        p->vm_space.user_max = g_current[0]->vm_space.user_max;
        p->vm_space.shm_cursor = (uintptr_t)VM_SPACE_SHM_BASE + (((uintptr_t)p->pid % 64) * 0x01000000ULL);
        p->vm_space.mapped_pages = 0;
        p->vm_space.shm_pages = 0;
        p->vm_space.owns_pml4 = 0;
    } else if (vm_space_init_kernel(&p->vm_space) != 0) {
        p->used = 0;
        spin_unlock(&g_sched_lock);
        irq_restore(flags);
        kprintf("[guide06] WARN: vm_space init failed for %s\n", idle_name);
        return NULL;
    }
    p->va_space = &p->vm_space;

    g_idle[cpu_id] = p;
    g_current[cpu_id] = p;

    spin_unlock(&g_sched_lock);
    irq_restore(flags);

    kprintf("[guide06] %s pid=%u adopted cpu %u drain loop\n",
            p->name, (unsigned)p->pid, cpu_id);
    return p;
}

int process_exec(int pid, process_entry_t entry, const char *name)
{
    process_t *p;
    uint64_t flags;
    if (!entry || pid <= 0)
        return -1;

    flags = irq_save_disable();
    spin_lock(&g_sched_lock);

    p = find_by_pid_locked(pid);
    if (!p || p->state == PROCESS_DEAD || p->state == PROCESS_ZOMBIE ||
        process_is_current_anywhere_locked(p)) {
        spin_unlock(&g_sched_lock);
        irq_restore(flags);
        return -1;
    }

    shm_process_cleanup(p);
    p->vm_space.shm_cursor = (uintptr_t)VM_SPACE_SHM_BASE + (((uintptr_t)p->pid % 64) * 0x01000000ULL);
    p->vm_space.mapped_pages = 0;
    p->vm_space.shm_pages = 0;
    p->shm_attachment_count = 0;

    p->entry = entry;
    p->signal_pending = 0;
    p->signal_mask = 0;
    for (int i = 0; i < PROCESS_MAX_SIGNALS; i++)
        p->signal_handlers[i] = PROCESS_SIG_DFL;
    for (int i = 0; i < PROCESS_MAX_SIGNALS; i++) {
        p->sig_actions[i].handler = NULL;
        p->sig_actions[i].mask = 0;
        p->sig_actions[i].flags = 0;
        p->sig_actions[i].action = SIGNAL_ACTION_DEFAULT;
    }
    if (name && name[0])
        name_copy(p->name, name, PROCESS_NAME_MAX);
    p->cmdline[0] = '\0';
    if (setup_initial_context_locked(p) != 0) {
        spin_unlock(&g_sched_lock);
        irq_restore(flags);
        return -1;
    }

    spin_unlock(&g_sched_lock);
    irq_restore(flags);
    return 0;
}

int process_exit_current(int code)
{
    process_t *cur;
    uint64_t flags = irq_save_disable();
    spin_lock(&g_sched_lock);
    cur = g_current[sched_this_cpu()];
    if (!cur) {
        spin_unlock(&g_sched_lock);
        irq_restore(flags);
        return -1;
    }
    mark_zombie_locked(cur, WAIT_STATUS_EXIT(code));
    spin_unlock(&g_sched_lock);
    irq_restore(flags);
    return 0;
}

void process_exit(int code)
{
    process_exit_current(code);
    process_yield();
    for (;;)
        __asm__ volatile ("hlt");
}

/* Terminate the current process because it took a fatal fault (e.g. */
void process_terminate_current(int sig)
{
    process_t *cur;
    uint64_t flags = irq_save_disable();
    spin_lock(&g_sched_lock);
    cur = g_current[sched_this_cpu()];
    if (cur)
        mark_zombie_locked(cur, WAIT_STATUS_SIGNAL(sig));
    spin_unlock(&g_sched_lock);
    irq_restore(flags);
    process_yield();
    for (;;)
        __asm__ volatile ("hlt");
}

static int wait_match_child(process_t *caller, process_t *child, int target_pid)
{
    if (!caller || !child)
        return 0;
    if (target_pid == -1)
        return 1;
    if (target_pid > 0)
        return ((int)child->pid == target_pid) ? 1 : 0;
    if (target_pid == 0)
        return (child->pgid == caller->pgid) ? 1 : 0;
    return (child->pgid == (uint32_t)(-target_pid)) ? 1 : 0;
}

int process_waitpid(int caller_pid, int target_pid, int options, int *status_out)
{
    for (;;) {
        process_t *caller;
        process_t *child;
        process_t *self;
        int found_match = 0;
        int found_any_child = 0;
        uint64_t flags = irq_save_disable();

        spin_lock(&g_sched_lock);
        caller = find_by_pid_locked(caller_pid);
        if (!caller) {
            spin_unlock(&g_sched_lock);
            irq_restore(flags);
            return -1;
        }

        child = caller->children_head;
        while (child) {
            found_any_child = 1;
            if (wait_match_child(caller, child, target_pid)) {
                found_match = 1;
                if (child->state == PROCESS_ZOMBIE &&
                    !process_is_current_anywhere_locked(child)) {
                    int pid = (int)child->pid;
                    if (status_out)
                        *status_out = child->wait_status;
                    parent_unlink_child_locked(caller, child);
                    reset_process_slot_locked(child);
                    spin_unlock(&g_sched_lock);
                    irq_restore(flags);
                    return pid;
                }
            }
            child = child->parent_next_child;
        }

        if (!found_any_child || !found_match) {
            spin_unlock(&g_sched_lock);
            irq_restore(flags);
            return -1;
        }

        if (options & PROCESS_WAIT_WNOHANG) {
            spin_unlock(&g_sched_lock);
            irq_restore(flags);
            return 0;
        }

        self = g_current[sched_this_cpu()];
        if (self && self->pid == caller->pid) {
            self->state = PROCESS_BLOCKED;
            self->main_thread.state = THREAD_BLOCKED;
            spin_unlock(&g_sched_lock);
            irq_restore(flags);
            process_yield();
            continue;
        }

        spin_unlock(&g_sched_lock);
        irq_restore(flags);
        return -2;
    }
}

int process_kill(int pid, int sig)
{
    process_t *p;
    uint64_t flags;
    if (pid <= 0)
        return -1;

    flags = irq_save_disable();
    spin_lock(&g_sched_lock);
    p = find_by_pid_locked(pid);
    if (!p || p->state == PROCESS_DEAD) {
        spin_unlock(&g_sched_lock);
        irq_restore(flags);
        return -1;
    }

    if (sig == 0) {
        spin_unlock(&g_sched_lock);
        irq_restore(flags);
        return 0;
    }

    if (sig <= 0 || sig >= PROCESS_MAX_SIGNALS) {
        spin_unlock(&g_sched_lock);
        irq_restore(flags);
        return -1;
    }

    /* PID 1 (init) cannot be killed */
    if (pid == 1 && (sig == PROCESS_SIGKILL || sig == PROCESS_SIGSTOP || sig == PROCESS_SIGTERM)) {
        spin_unlock(&g_sched_lock);
        irq_restore(flags);
        return -1;
    }

    if (sig == PROCESS_SIGKILL) {
        mark_zombie_locked(p, WAIT_STATUS_SIGNAL(sig));
    } else {
        p->signal_pending |= (1ULL << (uint32_t)sig);
        if (p->state == PROCESS_BLOCKED) {
            p->state = PROCESS_READY;
            p->main_thread.state = THREAD_READY;
            runq_push_locked(p);
        }
    }

    spin_unlock(&g_sched_lock);
    irq_restore(flags);
    return 0;
}

int process_kill_pgid(int pgid, int sig)
{
    int count = 0;
    uint64_t flags;
    if (pgid <= 0 || sig <= 0 || sig >= PROCESS_MAX_SIGNALS)
        return -1;

    flags = irq_save_disable();
    spin_lock(&g_sched_lock);
    for (int i = 0; i < PROCESS_MAX_COUNT; i++) {
        process_t *p = &g_processes[i];
        if (!p->used || p->state == PROCESS_DEAD || p->state == PROCESS_ZOMBIE)
            continue;
        if ((int)p->pgid != pgid)
            continue;
        if (p->pid == 1 && (sig == PROCESS_SIGKILL || sig == PROCESS_SIGSTOP || sig == PROCESS_SIGTERM))
            continue;
        if (sig == PROCESS_SIGKILL)
            mark_zombie_locked(p, WAIT_STATUS_SIGNAL(sig));
        else {
            p->signal_pending |= (1ULL << (uint32_t)sig);
            if (p->state == PROCESS_BLOCKED) {
                p->state = PROCESS_READY;
                p->main_thread.state = THREAD_READY;
                runq_push_locked(p);
            }
        }
        count++;
    }
    spin_unlock(&g_sched_lock);
    irq_restore(flags);
    return count;
}

int process_set_tty(int pid, int tty_id)
{
    process_t *p;
    uint64_t flags = irq_save_disable();
    spin_lock(&g_sched_lock);
    p = find_by_pid_locked(pid);
    if (!p) {
        spin_unlock(&g_sched_lock);
        irq_restore(flags);
        return -1;
    }
    p->tty_id = tty_id;
    spin_unlock(&g_sched_lock);
    irq_restore(flags);
    return 0;
}

int process_set_priority(int pid, uint32_t priority)
{
    process_t *p;
    uint64_t flags;
    if (pid <= 0 || priority >= PROCESS_PRIORITY_LEVELS)
        return -1;

    flags = irq_save_disable();
    spin_lock(&g_sched_lock);
    p = find_by_pid_locked(pid);
    if (!p || !p->used || p->state == PROCESS_DEAD) {
        spin_unlock(&g_sched_lock);
        irq_restore(flags);
        return -1;
    }

    if (p->state == PROCESS_READY)
        runq_remove_locked(p);
    p->priority = priority;
    p->main_thread.priority = priority;
    if (p->state == PROCESS_READY)
        runq_push_locked(p);

    spin_unlock(&g_sched_lock);
    irq_restore(flags);
    return 0;
}

int process_set_cmdline(int pid, const char *cmdline)
{
    process_t *p;
    uint64_t flags = irq_save_disable();
    spin_lock(&g_sched_lock);
    p = find_by_pid_locked(pid);
    if (!p) {
        spin_unlock(&g_sched_lock);
        irq_restore(flags);
        return -1;
    }
    if (!cmdline)
        p->cmdline[0] = '\0';
    else
        name_copy(p->cmdline, cmdline, PROCESS_CMDLINE_MAX);
    spin_unlock(&g_sched_lock);
    irq_restore(flags);
    return 0;
}

int process_get_cmdline(int pid, char *buf, size_t size)
{
    process_t *p;
    uint64_t flags;
    int i = 0;
    if (!buf || size == 0)
        return -1;
    flags = irq_save_disable();
    spin_lock(&g_sched_lock);
    p = find_by_pid_locked(pid);
    if (!p) {
        spin_unlock(&g_sched_lock);
        irq_restore(flags);
        return -1;
    }
    while (p->cmdline[i] && i + 1 < (int)size) {
        buf[i] = p->cmdline[i];
        i++;
    }
    buf[i] = '\0';
    spin_unlock(&g_sched_lock);
    irq_restore(flags);
    return 0;
}

const char *process_current_cmdline(void)
{
    process_t *cur = process_current();
    return cur ? cur->cmdline : "";
}

int process_clone_fd(int dst_pid, int dst_fd, int src_pid, int src_fd)
{
    process_t *dst;
    process_t *src;
    int rc = -1;
    uint64_t flags = irq_save_disable();
    spin_lock(&g_sched_lock);
    dst = find_by_pid_locked(dst_pid);
    src = find_by_pid_locked(src_pid);
    if (dst && src)
        rc = vfs_process_dup2(dst, dst_fd, src, src_fd);
    spin_unlock(&g_sched_lock);
    irq_restore(flags);
    return rc;
}

int process_signal_register(int pid, int sig, process_signal_handler_t handler)
{
    process_t *p;
    uintptr_t h = (uintptr_t)handler;
    uint64_t flags;

    if (sig <= 0 || sig >= PROCESS_MAX_SIGNALS)
        return -1;
    if (sig == PROCESS_SIGKILL && h != PROCESS_SIG_DFL)
        return -1;

    flags = irq_save_disable();
    spin_lock(&g_sched_lock);
    p = find_by_pid_locked(pid);
    if (!p) {
        spin_unlock(&g_sched_lock);
        irq_restore(flags);
        return -1;
    }
    p->signal_handlers[sig] = h;
    p->sig_actions[sig].handler = (h > 1) ? handler : NULL;
    p->sig_actions[sig].action = (h == PROCESS_SIG_IGN) ? SIGNAL_ACTION_IGNORE :
                                 ((h == PROCESS_SIG_DFL) ? SIGNAL_ACTION_DEFAULT :
                                  SIGNAL_ACTION_HANDLER);
    spin_unlock(&g_sched_lock);
    irq_restore(flags);
    return 0;
}

int process_signal_mask(int pid, int how, uint64_t set, uint64_t *old_set)
{
    process_t *p;
    uint64_t flags = irq_save_disable();
    spin_lock(&g_sched_lock);
    p = find_by_pid_locked(pid);
    if (!p) {
        spin_unlock(&g_sched_lock);
        irq_restore(flags);
        return -1;
    }
    if (old_set)
        *old_set = p->signal_mask;
    if (how == SIG_BLOCK)
        p->signal_mask |= set;
    else if (how == SIG_UNBLOCK)
        p->signal_mask &= ~set;
    else if (how == SIG_SETMASK)
        p->signal_mask = set;
    else {
        spin_unlock(&g_sched_lock);
        irq_restore(flags);
        return -1;
    }
    p->signal_mask &= ~(1ULL << PROCESS_SIGKILL);
    spin_unlock(&g_sched_lock);
    irq_restore(flags);
    return 0;
}

int process_signal_pending(int pid, uint64_t *pending_out)
{
    process_t *p;
    uint64_t flags;
    if (!pending_out)
        return -1;
    flags = irq_save_disable();
    spin_lock(&g_sched_lock);
    p = find_by_pid_locked(pid);
    if (!p) {
        spin_unlock(&g_sched_lock);
        irq_restore(flags);
        return -1;
    }
    *pending_out = p->signal_pending;
    spin_unlock(&g_sched_lock);
    irq_restore(flags);
    return 0;
}

int process_signal_send(int pid, int sig)
{
    return process_kill(pid, sig);
}

uint64_t process_schedule_tick(uint64_t current_rsp)
{
    process_t *cur;
    process_t *next;
    uintptr_t signal_handler = 0;
    int delivered_sig = 0;
    uint64_t next_rsp;
    uint32_t cpu;
    uint64_t flags = irq_save_disable();

    cpu = sched_this_cpu();
    if (cpu >= g_cpu_count) {
        irq_restore(flags);
        return current_rsp;
    }

    spin_lock(&g_sched_lock);
    if (cpu == 0)
        g_sched_ticks++;
    g_core_ticks[cpu]++;

    cur = g_current[cpu];
    if (cur) {
        if (cur->is_idle)
            g_idle_seen_ticks[cpu]++;
        cur->kernel_rsp = current_rsp;
        cur->ticks++;
        cur->sched_ticks++;
        cur->main_thread.kernel_rsp = current_rsp;
        cur->main_thread.sched_ticks++;

        if (cur->state == PROCESS_RUNNING &&
            prepare_signal_action_locked(cur, &signal_handler, &delivered_sig)) {
        }

        if (cur->state == PROCESS_RUNNING) {
            int best_pri = runq_best_priority_locked(cpu);
            if (!cur->is_idle && cur->time_slice > 0) {
                cur->time_slice--;
                cur->main_thread.time_slice = cur->time_slice;
            }
            if (!cur->is_idle &&
                cur->time_slice > 0 &&
                (best_pri < 0 || cur->priority <= (uint32_t)best_pri)) {
                next_rsp = cur->kernel_rsp ? cur->kernel_rsp : current_rsp;
                spin_unlock(&g_sched_lock);
                irq_restore(flags);
                if (signal_handler > 1) {
                    process_signal_handler_t fn = (process_signal_handler_t)(uintptr_t)signal_handler;
                    fn(delivered_sig);
                }
                return next_rsp;
            }
            if (cur->is_idle) {
                cur->state = PROCESS_READY;
                cur->main_thread.state = THREAD_READY;
            } else {
                cur->state = PROCESS_READY;
                cur->main_thread.state = THREAD_READY;
                cur->time_slice = PROCESS_DEFAULT_TIMESLICE;
                cur->main_thread.time_slice = cur->time_slice;
                runq_push_locked(cur);
            }
        }
    }

    next = runq_pop_locked(cpu);
    if (!next)
        next = g_idle[cpu];
    if (!next)
        next = cur;

    if (next) {
        next->state = PROCESS_RUNNING;
        next->main_thread.state = THREAD_RUNNING;
        next->cpu_id = cpu;
        next->main_thread.cpu_id = cpu;
    }
    g_current[cpu] = next;
    if (next && next->kernel_stack) {
        if (validate_or_repair_context_locked(next) != 0) {
            process_t *fallback = cur ? cur : g_idle[cpu];
            if (fallback) {
                next = fallback;
                next->state = PROCESS_RUNNING;
                next->main_thread.state = THREAD_RUNNING;
                next->cpu_id = cpu;
                next->main_thread.cpu_id = cpu;
                g_current[cpu] = next;
            }
        }
        if (next && next->kernel_stack)
            tss_set_rsp0_x64(process_stack_top_aligned(next));
    }
    if (next && next->vm_space.owns_pml4 && next->vm_space.pml4_phys)
        vmm_switch_pml4(next->vm_space.pml4_phys);
    else if (vmm_get_kernel_pml4())
        vmm_switch_pml4(vmm_get_kernel_pml4());

    next_rsp = next ? next->kernel_rsp : current_rsp;
    spin_unlock(&g_sched_lock);
    irq_restore(flags);

    if (signal_handler > 1) {
        process_signal_handler_t fn = (process_signal_handler_t)(uintptr_t)signal_handler;
        fn(delivered_sig);
    }

    return next_rsp ? next_rsp : current_rsp;
}

uint64_t process_schedule_ipi(uint64_t current_rsp)
{
    g_ipi_ticks[sched_this_cpu()]++;
    return process_schedule_tick(current_rsp);
}

void process_idle_loop_note(void)
{
    g_idle_loop_ticks[sched_this_cpu()]++;
}

static void process_ap_idle_entry(void)
{
    work_queue_drain_loop();
}

void process_yield(void)
{
    process_t *cur = process_current();
    if (cur) {
        cur->time_slice = 0;
        cur->main_thread.time_slice = 0;
    }
    __asm__ volatile ("int $32");
}

uint64_t process_ticks(void)
{
    return g_sched_ticks;
}

void process_start_scheduler(void)
{
    for (;;) {
        __asm__ volatile ("sti; hlt");
    }
}

int process_snapshot(process_snapshot_t *out, int max)
{
    int count = 0;
    uint64_t flags;

    if (!out || max <= 0)
        return -1;

    flags = irq_save_disable();
    spin_lock(&g_sched_lock);
    for (int i = 0; i < PROCESS_MAX_COUNT && count < max; i++) {
        process_t *p = &g_processes[i];
        process_snapshot_t *dst = &out[count];
        if (!p->used || p->state == PROCESS_DEAD)
            continue;
        dst->pid = p->pid;
        dst->ppid = p->ppid;
        dst->pgid = p->pgid;
        dst->cpu_id = p->cpu_id;
        dst->state = p->state;
        dst->priority = p->priority;
        dst->time_slice = p->time_slice;
        dst->ticks = p->ticks;
        dst->sched_ticks = p->sched_ticks;
        dst->shm_attachments = p->shm_attachment_count;
        dst->tty_id = p->tty_id;
        dst->is_idle = p->is_idle;
        name_copy(dst->name, p->name, PROCESS_NAME_MAX);
        name_copy(dst->cwd, p->cwd, PROCESS_CWD_MAX);
        name_copy(dst->cmdline, p->cmdline, PROCESS_CMDLINE_MAX);
        count++;
    }
    spin_unlock(&g_sched_lock);
    irq_restore(flags);
    return count;
}

int process_get_info(int pid, process_snapshot_t *out)
{
    process_t *p;
    uint64_t flags;

    if (pid <= 0)
        return -1;

    flags = irq_save_disable();
    spin_lock(&g_sched_lock);
    p = find_by_pid_locked(pid);
    if (!p || !p->used || p->state == PROCESS_DEAD) {
        spin_unlock(&g_sched_lock);
        irq_restore(flags);
        return -1;
    }
    if (out) {
        out->pid = p->pid;
        out->ppid = p->ppid;
        out->pgid = p->pgid;
        out->cpu_id = p->cpu_id;
        out->state = p->state;
        out->priority = p->priority;
        out->time_slice = p->time_slice;
        out->ticks = p->ticks;
        out->sched_ticks = p->sched_ticks;
        out->shm_attachments = p->shm_attachment_count;
        out->tty_id = p->tty_id;
        out->is_idle = p->is_idle;
        name_copy(out->name, p->name, PROCESS_NAME_MAX);
        name_copy(out->cwd, p->cwd, PROCESS_CWD_MAX);
        name_copy(out->cmdline, p->cmdline, PROCESS_CMDLINE_MAX);
    }
    spin_unlock(&g_sched_lock);
    irq_restore(flags);
    return 0;
}

void process_get_memory_totals(size_t *proc_count_out,
                               size_t *mapped_pages_out,
                               size_t *shm_pages_out,
                               size_t *shm_attachments_out)
{
    size_t proc_count = 0;
    size_t mapped_pages = 0;
    size_t shm_pages = 0;
    size_t shm_attachments = 0;
    uint64_t flags = irq_save_disable();

    spin_lock(&g_sched_lock);
    for (int i = 0; i < PROCESS_MAX_COUNT; i++) {
        process_t *p = &g_processes[i];
        if (!p->used || p->state == PROCESS_DEAD)
            continue;
        proc_count++;
        mapped_pages += p->vm_space.mapped_pages;
        shm_pages += p->vm_space.shm_pages;
        shm_attachments += p->shm_attachment_count;
    }
    spin_unlock(&g_sched_lock);
    irq_restore(flags);

    if (proc_count_out)
        *proc_count_out = proc_count;
    if (mapped_pages_out)
        *mapped_pages_out = mapped_pages;
    if (shm_pages_out)
        *shm_pages_out = shm_pages;
    if (shm_attachments_out)
        *shm_attachments_out = shm_attachments;
}

void process_dump_memory_state(void)
{
    uint64_t flags = irq_save_disable();
    spin_lock(&g_sched_lock);
    kprintf("[mem][proc] pid ppid state maps shm_pages shm_att pml4\n");
    for (int i = 0; i < PROCESS_MAX_COUNT; i++) {
        process_t *p = &g_processes[i];
        const char *state = "unknown";
        if (!p->used || p->state == PROCESS_DEAD)
            continue;
        if (p->state == PROCESS_CREATED) state = "created";
        else if (p->state == PROCESS_READY) state = "ready";
        else if (p->state == PROCESS_RUNNING) state = "running";
        else if (p->state == PROCESS_BLOCKED) state = "blocked";
        else if (p->state == PROCESS_SLEEPING) state = "sleeping";
        else if (p->state == PROCESS_STOPPED) state = "stopped";
        else if (p->state == PROCESS_ZOMBIE) state = "zombie";
        kprintf("[mem][proc] %u %u %s %u %u %u 0x%08x%08x\n",
                (unsigned)p->pid,
                (unsigned)p->ppid,
                state,
                (unsigned)p->vm_space.mapped_pages,
                (unsigned)p->vm_space.shm_pages,
                (unsigned)p->shm_attachment_count,
                (uint32_t)(p->vm_space.pml4_phys >> 32),
                (uint32_t)(p->vm_space.pml4_phys & 0xFFFFFFFFu));
    }
    spin_unlock(&g_sched_lock);
    irq_restore(flags);
}

static void process_idle_entry(void)
{
    for (;;)
        __asm__ volatile ("hlt");
}

void process_entry_trampoline(void)
{
    for (;;) {
        process_t *cur = process_current();
        process_entry_t fn;

        if (!cur) {
            __asm__ volatile ("hlt");
            continue;
        }

        if (cur->is_idle) {
            if (cur->entry)
                cur->entry();
            process_idle_entry();
        }

        fn = cur->entry;
        kprintf("[proc] enter pid=%u name=%s entry=0x%08x%08x\n",
                (unsigned)cur->pid,
                cur->name,
                (uint32_t)((uintptr_t)fn >> 32),
                (uint32_t)((uintptr_t)fn & 0xFFFFFFFFu));
        if (!fn) {
            kprintf("[proc] null entry for pid=%u\n", (unsigned)cur->pid);
            process_exit(0);
        }

        fn();
        process_exit(0);
    }
}

static volatile uint64_t g_burn_a;
static volatile uint64_t g_burn_b;
static volatile int g_burn_stop;

static volatile int g_exec_marker;

static volatile int g_sig_ready;
static volatile int g_sig_unmask;
static volatile int g_sig_handler_count;
static volatile uint64_t g_prio_high_done_tick;
static volatile uint64_t g_prio_low_done_tick;

static void selftest_burn_a(void)
{
    while (!g_burn_stop) {
        g_burn_a++;
        __asm__ volatile ("pause");
    }
}

static void selftest_burn_b(void)
{
    while (!g_burn_stop) {
        g_burn_b++;
        __asm__ volatile ("pause");
    }
}

static void selftest_prio_high(void)
{
    for (int i = 0; i < 32; i++)
        process_yield();
    g_prio_high_done_tick = process_ticks();
    process_exit(0);
}

static void selftest_prio_low(void)
{
    for (int i = 0; i < 32; i++)
        process_yield();
    g_prio_low_done_tick = process_ticks();
    process_exit(0);
}

static void selftest_exec_source(void)
{
    for (;;)
        process_yield();
}

static void selftest_exec_target(void)
{
    g_exec_marker = 1;
    process_exit(42);
}

static void selftest_sig_handler(int sig)
{
    (void)sig;
    g_sig_handler_count++;
}

static void selftest_signal_target(void)
{
    uint64_t mask = (1ULL << PROCESS_SIGUSR1);
    int pid = process_current_pid();
    process_signal_register(pid, PROCESS_SIGUSR1, selftest_sig_handler);
    process_signal_mask(pid, SIG_SETMASK, mask, NULL);
    g_sig_ready = 1;

    while (!g_sig_unmask)
        process_yield();

    process_signal_mask(pid, SIG_UNBLOCK, mask, NULL);
    {
        uint64_t start = process_ticks();
        while (g_sig_handler_count == 0 && process_ticks() - start < 200)
            process_yield();
    }

    process_exit((g_sig_handler_count > 0) ? 0 : 1);
}

static void selftest_tty_fg_target(void)
{
    for (;;)
        process_yield();
}

static int selftest_wait_child(int pid, int *status_out)
{
    int caller = process_current_pid();
    return process_waitpid(caller, pid, 0, status_out);
}

static volatile uint32_t g_phase2_done_w;

static volatile uint32_t g_guide20_done_w;

static volatile uint32_t g_guide15_done_w;

static void phase2_selftest_entry(void)
{
    int pass = 0;
    int fail = 0;

    kprintf("[phase2][test] start\n");

    {
        process_t *cur = process_current();
        int ok = cur &&
                 PROCESS_MAX_COUNT == 256 &&
                 PROCESS_MAX_SIGNALS == 64 &&
                 PROCESS_PRIORITY_LEVELS == 256 &&
                 cur->priority < PROCESS_PRIORITY_LEVELS &&
                 cur->time_slice <= PROCESS_DEFAULT_TIMESLICE &&
                 cur->time_slice > 0 &&
                 cur->va_space == &cur->vm_space &&
                 cur->main_thread.process == cur &&
                 cur->main_thread.tid == cur->pid &&
                 cur->main_thread.priority == cur->priority;
        if (ok) {
            pass++;
            kprintf("[phase2][step1] tcb/process layout PASS pid=%u pri=%u slice=%u signals=%u table=%u\n",
                    cur->pid,
                    cur->priority,
                    cur->time_slice,
                    PROCESS_MAX_SIGNALS,
                    PROCESS_MAX_COUNT);
        } else {
            fail++;
            kprintf("[phase2][step1] tcb/process layout FAIL\n");
        }
    }

    {
        process_t *low;
        process_t *high;
        int st = 0;
        g_prio_high_done_tick = 0;
        g_prio_low_done_tick = 0;
        low = process_spawn_kernel("prio-low", selftest_prio_low);
        high = process_spawn_kernel("prio-high", selftest_prio_high);
        if (low && high &&
            process_set_priority((int)low->pid, 20) == 0 &&
            process_set_priority((int)high->pid, 10) == 0 &&
            selftest_wait_child((int)high->pid, &st) == (int)high->pid &&
            selftest_wait_child((int)low->pid, &st) == (int)low->pid &&
            g_prio_high_done_tick > 0 &&
            g_prio_low_done_tick > 0 &&
            g_prio_high_done_tick <= g_prio_low_done_tick) {
            pass++;
            kprintf("[phase2][step2] priority queues PASS high_tick=%u low_tick=%u\n",
                    (uint32_t)g_prio_high_done_tick,
                    (uint32_t)g_prio_low_done_tick);
        } else {
            fail++;
            kprintf("[phase2][step2] priority queues FAIL high_tick=%u low_tick=%u\n",
                    (uint32_t)g_prio_high_done_tick,
                    (uint32_t)g_prio_low_done_tick);
        }
    }

    {
        process_t *a;
        process_t *b;
        int st = 0;
        g_burn_a = 0;
        g_burn_b = 0;
        g_burn_stop = 0;
        a = process_spawn_kernel("burn-a", selftest_burn_a);
        b = process_spawn_kernel("burn-b", selftest_burn_b);
        {
            uint64_t start = process_ticks();
            while (process_ticks() - start < 150)
                process_yield();
        }
        g_burn_stop = 1;
        if (a) selftest_wait_child((int)a->pid, &st);
        if (b) selftest_wait_child((int)b->pid, &st);
        if (g_burn_a > 0 && g_burn_b > 0) {
            pass++;
            kprintf("[phase2][test] preemption PASS a=%u b=%u\n",
                    (uint32_t)g_burn_a, (uint32_t)g_burn_b);
        } else {
            fail++;
            kprintf("[phase2][test] preemption FAIL a=%u b=%u\n",
                    (uint32_t)g_burn_a, (uint32_t)g_burn_b);
        }
    }

    {
        process_t *child = process_spawn_kernel("exec-src", selftest_exec_source);
        int st = 0;
        g_exec_marker = 0;
        if (child &&
            process_exec((int)child->pid, selftest_exec_target, "exec-target") == 0 &&
            selftest_wait_child((int)child->pid, &st) == (int)child->pid &&
            WAIT_EXIT_CODE(st) == 42 &&
            g_exec_marker == 1) {
            pass++;
            kprintf("[phase2][test] lifecycle exec/wait PASS status=%d\n", WAIT_EXIT_CODE(st));
        } else {
            fail++;
            kprintf("[phase2][test] lifecycle exec/wait FAIL\n");
        }
    }

    {
        process_t *kill_target = process_spawn_kernel("kill-target", selftest_exec_source);
        int st = 0;
        if (kill_target &&
            process_kill((int)kill_target->pid, PROCESS_SIGKILL) == 0 &&
            selftest_wait_child((int)kill_target->pid, &st) == (int)kill_target->pid &&
            WAIT_TERM_SIGNAL(st) == PROCESS_SIGKILL) {
            pass++;
            kprintf("[phase2][test] lifecycle kill PASS sig=%d\n", WAIT_TERM_SIGNAL(st));
        } else {
            fail++;
            kprintf("[phase2][test] lifecycle kill FAIL\n");
        }
    }

    {
        process_t *sig_target;
        int st = 0;
        uint64_t pending = 0;
        g_sig_ready = 0;
        g_sig_unmask = 0;
        g_sig_handler_count = 0;
        sig_target = process_spawn_kernel("sig-target", selftest_signal_target);
        while (!g_sig_ready)
            process_yield();
        if (sig_target)
            process_signal_send((int)sig_target->pid, PROCESS_SIGUSR1);
        {
            uint64_t start = process_ticks();
            while (process_ticks() - start < 40)
                process_yield();
        }
        if (sig_target)
            process_signal_pending((int)sig_target->pid, &pending);
        g_sig_unmask = 1;
        if (sig_target &&
            (pending & (1ULL << PROCESS_SIGUSR1)) &&
            selftest_wait_child((int)sig_target->pid, &st) == (int)sig_target->pid &&
            WAIT_EXIT_CODE(st) == 0 &&
            g_sig_handler_count > 0) {
            pass++;
            kprintf("[phase2][test] signal mask/handler PASS pending=0x%08x\n",
                    (uint32_t)(pending & 0xFFFFFFFFu));
        } else {
            fail++;
            kprintf("[phase2][test] signal mask/handler FAIL pending=0x%08x count=%d status=%d\n",
                    (uint32_t)(pending & 0xFFFFFFFFu),
                    g_sig_handler_count,
                    WAIT_EXIT_CODE(st));
        }
    }

    {
        process_t *fg = process_spawn_kernel("tty-fg", selftest_tty_fg_target);
        int st = 0;
        int tty_id = tty_get_active();
        if (fg) {
            tty_set_foreground_pgid(tty_id, process_get_pgid((int)fg->pid));
            tty_handle_scancode(29, 1);
            tty_handle_scancode(46, 1);
            tty_handle_scancode(29, 0);
        }
        if (fg &&
            selftest_wait_child((int)fg->pid, &st) == (int)fg->pid &&
            WAIT_TERM_SIGNAL(st) == PROCESS_SIGINT) {
            pass++;
            kprintf("[phase2][test] tty ctrl-c PASS sig=%d\n", WAIT_TERM_SIGNAL(st));
        } else {
            fail++;
            kprintf("[phase2][test] tty ctrl-c FAIL\n");
        }
    }

    kprintf("[phase2][test] done pass=%d fail=%d\n", pass, fail);
    g_phase2_done_w = 1;
    kernel_futex_wake(&g_phase2_done_w, 64);
    process_exit((fail == 0) ? 0 : 1);
}

void process_run_phase2_selftests(void)
{
    exec_register_builtin("builtin:phase2-exec-target", selftest_exec_target);
    process_spawn_kernel("phase2-selftest", phase2_selftest_entry);
}

#ifdef __x86_64__
static void phase1_fault_isolation_entry(void)
{
    int pid = -1;
    int status = -1;
    int r;

    /* elf64_spawn uses a single-slot handoff; retry while another spawn (e.g. */
    for (int i = 0; i < 200 && pid < 0; i++) {
        pid = elf64_spawn("/fat12/fault.elf", "fault-victim");
        if (pid < 0)
            process_yield();
    }
    if (pid < 0) {
        kprintf("[phase1][test] ring3 fault-isolation FAIL (spawn)\n");
        process_exit(0);
    }

    r = process_waitpid(process_current_pid(), pid, 0, &status);
    if (r == pid && WAIT_TERM_SIGNAL(status) == PROCESS_SIGSEGV)
        kprintf("[phase1][test] ring3 fault-isolation PASS victim pid=%d killed by SIGSEGV=%d, kernel alive\n",
                pid, WAIT_TERM_SIGNAL(status));
    else
        kprintf("[phase1][test] ring3 fault-isolation FAIL pid=%d r=%d status=0x%x sig=%d\n",
                pid, r, (unsigned)status, WAIT_TERM_SIGNAL(status));
    process_exit(0);
}
#endif

#ifdef __x86_64__
/* Prove ELF loading is reachable through the real SYS_SPAWN dispatch (not just the in-kernel elf64_spawn()... */
static void phase1_elf_spawn_syscall_entry(void)
{
    long pid = -1;
    int status = -1;
    int r;

    for (int i = 0; i < 200 && pid < 0; i++) {
        pid = (long)syscall_handler(SYS_SYSTEM, SYSTEM_CMD_SPAWN,
                                    (uintptr_t)"/fat12/hello.elf", 0, 0, 0);
        if (pid < 0)
            process_yield();
    }
    if (pid < 0) {
        kprintf("[phase1][test] elf spawn-by-syscall FAIL (spawn)\n");
        process_exit(0);
    }

    r = process_waitpid(process_current_pid(), (int)pid, 0, &status);
    if (r == (int)pid)
        kprintf("[phase1][test] elf spawn-by-syscall PASS pid=%d ran ring3 (exit code=%d)\n",
                (int)pid, (status >> 8) & 0xFF);
    else
        kprintf("[phase1][test] elf spawn-by-syscall FAIL pid=%d r=%d\n", (int)pid, r);
    process_exit(0);
}

static void phase1_spawn_wait_entry(void)
{
    struct tsukasa_spawn_request req;
    long pid = -1;
    int status = -1;
    int r;

    req.path = "/fat12/exit7.elf";
    req.args = "/fat12/exit7.elf";
    req.stdin_fd = -1;
    req.stdout_fd = -1;
    req.stderr_fd = -1;
    req.tty_id = -1;

    for (int i = 0; i < 200 && pid < 0; i++) {
        pid = (long)syscall_handler(SYS_SYSTEM, SYSTEM_CMD_SPAWN_EX,
                                    (uintptr_t)&req, 0, 0, 0);
        if (pid < 0)
            process_yield();
    }
    if (pid < 0) {
        kprintf("[phase1][test] spawn+wait(disk) FAIL (spawn)\n");
        process_exit(0);
    }

    r = process_waitpid(process_current_pid(), (int)pid, 0, &status);
    if (r == (int)pid && WAIT_EXIT_CODE(status) == 7)
        kprintf("[phase1][test] spawn+wait(disk exit7) PASS pid=%d exit_code=%d\n",
                (int)pid, WAIT_EXIT_CODE(status));
    else
        kprintf("[phase1][test] spawn+wait(disk exit7) FAIL pid=%d r=%d status=0x%x code=%d\n",
                (int)pid, r, (unsigned)status, WAIT_EXIT_CODE(status));
    process_exit(0);
}
#endif

void process_run_phase1_selftests(void)
{
#ifdef __x86_64__
    if (!process_spawn_kernel("phase1-selftest", phase1_fault_isolation_entry))
        kprintf("[phase1][test] WARN: failed to spawn phase1-selftest\n");
    if (!process_spawn_kernel("phase1-elf-spawn", phase1_elf_spawn_syscall_entry))
        kprintf("[phase1][test] WARN: failed to spawn phase1-elf-spawn\n");
    if (!process_spawn_kernel("phase1-spawn-wait", phase1_spawn_wait_entry))
        kprintf("[phase1][test] WARN: failed to spawn phase1-spawn-wait\n");
#endif
}

static volatile int g_p3_shm_stage;
static volatile int g_p3_shm_id;
static volatile int g_p3_shm_fail;
static volatile int g_p3_shm_seen_peer;
static volatile int g_p3_shm_destroy_while_attached;
static volatile int g_phase3_selftests_done;
static volatile int g_phase4_selftests_done;
static volatile int g_phase7_selftests_done;

static void phase3_shm_owner_task(void)
{
    int id = shm_create(PAGE_SIZE);
    volatile uint32_t *buf = NULL;
    if (id < 0) {
        g_p3_shm_fail = 1;
        process_exit(1);
    }

    buf = (volatile uint32_t *)shm_attach(id);
    if (!buf) {
        g_p3_shm_fail = 1;
        process_exit(1);
    }

    g_p3_shm_id = id;
    *buf = 0x11223344u;
    g_p3_shm_destroy_while_attached = shm_destroy(id);
    g_p3_shm_stage = 1;

    while (g_p3_shm_stage < 2)
        process_yield();

    if (*buf != 0x55667788u)
        g_p3_shm_fail = 1;
    else
        g_p3_shm_seen_peer = 1;

    if (shm_detach((void *)buf) != 0)
        g_p3_shm_fail = 1;
    if (shm_destroy(id) != 0)
        g_p3_shm_fail = 1;

    g_p3_shm_stage = 3;
    process_exit(g_p3_shm_fail ? 1 : 0);
}

static void phase3_shm_peer_task(void)
{
    volatile uint32_t *buf;
    while (g_p3_shm_stage < 1)
        process_yield();

    buf = (volatile uint32_t *)shm_attach(g_p3_shm_id);
    if (!buf) {
        g_p3_shm_fail = 1;
        process_exit(1);
    }

    if (*buf != 0x11223344u)
        g_p3_shm_fail = 1;
    *buf = 0x55667788u;

    if (shm_detach((void *)buf) != 0)
        g_p3_shm_fail = 1;
    g_p3_shm_stage = 2;
    process_exit(g_p3_shm_fail ? 1 : 0);
}

static void phase3_exit_leak_task(void)
{
    int id = shm_create(PAGE_SIZE);
    volatile uint32_t *buf;
    if (id < 0)
        process_exit(1);

    buf = (volatile uint32_t *)shm_attach(id);
    if (!buf)
        process_exit(1);

    *buf = 0xA5A55A5Au;
    process_exit(0);
}

static int phase3_mapping_api_test(void)
{
    process_t *cur = process_current();
    uintptr_t phys;
    uintptr_t va;
    volatile uint32_t *probe;
    int rc = 0;

    if (!cur)
        return -1;

    phys = pmm_alloc_pages(1);
    if (!phys)
        return -1;

    va = vm_space_reserve_shm_range(&cur->vm_space, 1);
    if (!va) {
        pmm_free_pages(phys, 1);
        return -1;
    }

    if (vm_space_map_user_pages(&cur->vm_space,
                                va,
                                phys,
                                1,
                                PAGING_MAP_READ | PAGING_MAP_WRITE |
                                PAGING_MAP_EXEC | PAGING_MAP_USER) != 0) {
        pmm_free_pages(phys, 1);
        return -1;
    }

    probe = (volatile uint32_t *)va;
    *probe = 0xDEADBEEFu;
    if (*probe != 0xDEADBEEFu)
        rc = -1;

    if (vm_space_protect_user_pages(&cur->vm_space,
                                    va,
                                    1,
                                    PAGING_MAP_READ | PAGING_MAP_EXEC |
                                    PAGING_MAP_USER) != 0) {
        rc = -1;
    }

    if (vm_space_unmap_user_pages(&cur->vm_space, va, 1) != 0)
        rc = -1;

    if (vm_space_map_user_pages(&cur->vm_space,
                                va + 7,
                                phys,
                                1,
                                PAGING_MAP_READ | PAGING_MAP_WRITE |
                                PAGING_MAP_EXEC | PAGING_MAP_USER) == 0) {
        rc = -1;
    }

    if (vm_space_map_user_pages(&cur->vm_space,
                                (uintptr_t)PAGING_KERNEL_VA_MIN,
                                phys,
                                1,
                                PAGING_MAP_READ | PAGING_MAP_WRITE |
                                PAGING_MAP_EXEC | PAGING_MAP_USER) == 0) {
        rc = -1;
    }

    pmm_free_pages(phys, 1);
    return rc;
}

static int phase3_shm_ipc_test(void)
{
    process_t *owner;
    process_t *peer;
    int st_owner = 0;
    int st_peer = 0;

    g_p3_shm_stage = 0;
    g_p3_shm_id = -1;
    g_p3_shm_fail = 0;
    g_p3_shm_seen_peer = 0;
    g_p3_shm_destroy_while_attached = 0;

    owner = process_spawn_kernel("p3-shm-owner", phase3_shm_owner_task);
    peer = process_spawn_kernel("p3-shm-peer", phase3_shm_peer_task);
    if (!owner || !peer)
        return -1;

    if (selftest_wait_child((int)owner->pid, &st_owner) != (int)owner->pid)
        return -1;
    if (selftest_wait_child((int)peer->pid, &st_peer) != (int)peer->pid)
        return -1;

    if (WAIT_EXIT_CODE(st_owner) != 0 || WAIT_EXIT_CODE(st_peer) != 0)
        return -1;
    if (g_p3_shm_fail)
        return -1;
    if (g_p3_shm_destroy_while_attached == 0)
        return -1;
    if (!g_p3_shm_seen_peer)
        return -1;
    if (g_p3_shm_stage != 3)
        return -1;

    return 0;
}

static int phase3_shm_exit_cleanup_test(void)
{
    process_t *leaker;
    struct shm_stats stats = {0};
    int st = 0;

    leaker = process_spawn_kernel("p3-shm-leaker", phase3_exit_leak_task);
    if (!leaker)
        return -1;
    if (selftest_wait_child((int)leaker->pid, &st) != (int)leaker->pid)
        return -1;
    if (WAIT_EXIT_CODE(st) != 0)
        return -1;

    shm_get_stats(&stats);
    if (stats.attachment_count != 0)
        return -1;

    return 0;
}

static int phase3_shm_churn_test(void)
{
    uintptr_t free_before = pmm_free_page_count();
    uintptr_t free_after;

    for (int i = 0; i < 128; i++) {
        int id = shm_create(PAGE_SIZE * 2);
        void *addr;
        if (id < 0)
            return -1;
        addr = shm_attach(id);
        if (!addr)
            return -1;
        ((volatile uint32_t *)addr)[0] = (uint32_t)i;
        if (shm_detach(addr) != 0)
            return -1;
        if (shm_destroy(id) != 0)
            return -1;
    }

    free_after = pmm_free_page_count();
    if (free_after < free_before)
        return -1;
    return 0;
}

static void phase3_selftest_entry(void)
{
    int pass = 0;
    int fail = 0;

    kprintf("[phase3][test] start\n");

    if (phase3_mapping_api_test() == 0) {
        pass++;
        kprintf("[phase3][test] mapping api PASS\n");
    } else {
        fail++;
        kprintf("[phase3][test] mapping api FAIL\n");
    }

    if (phase3_shm_ipc_test() == 0) {
        pass++;
        kprintf("[phase3][test] shm ipc PASS\n");
    } else {
        fail++;
        kprintf("[phase3][test] shm ipc FAIL\n");
    }

    if (phase3_shm_exit_cleanup_test() == 0) {
        pass++;
        kprintf("[phase3][test] exit cleanup PASS\n");
    } else {
        fail++;
        kprintf("[phase3][test] exit cleanup FAIL\n");
    }

    if (phase3_shm_churn_test() == 0) {
        pass++;
        kprintf("[phase3][test] shm churn PASS\n");
    } else {
        fail++;
        kprintf("[phase3][test] shm churn FAIL\n");
    }

    kprintf("[phase3][test] done pass=%d fail=%d\n", pass, fail);
    g_phase3_selftests_done = 1;
    process_exit((fail == 0) ? 0 : 1);
}

void process_run_phase3_selftests(void)
{
    g_phase3_selftests_done = 0;
    process_spawn_kernel("phase3-selftest", phase3_selftest_entry);
}

static int phase4_fd_semantics_test(void)
{
    static const char data[] = "phase4-fd";
    char buf[32];
    vfs_stat_t st;
    int fd;
    int dupfd;
    int pipefd[2];
    size_t got;

    fd = vfs_create("/tmp/phase4_fd.txt");
    if (fd < 0)
        return -1;
    if (vfs_write(fd, data, sizeof(data) - 1) != sizeof(data) - 1) {
        vfs_close(fd);
        return -1;
    }
    vfs_close(fd);

    if (vfs_stat("/tmp/phase4_fd.txt", &st) != 0 || st.size != sizeof(data) - 1)
        return -1;

    fd = vfs_open("/tmp/phase4_fd.txt");
    if (fd < 0)
        return -1;
    dupfd = vfs_dup(fd);
    if (dupfd < 0) {
        vfs_close(fd);
        return -1;
    }

    got = vfs_read(fd, buf, 5);
    if (got != 5) {
        vfs_close(fd);
        vfs_close(dupfd);
        return -1;
    }
    got = vfs_read(dupfd, buf, sizeof(buf));
    if (got != (sizeof(data) - 1 - 5)) {
        vfs_close(fd);
        vfs_close(dupfd);
        return -1;
    }
    vfs_close(fd);
    vfs_close(dupfd);

    if (vfs_pipe(pipefd) != 0)
        return -1;
    if (vfs_write(pipefd[1], data, 4) != 4) {
        vfs_close(pipefd[0]);
        vfs_close(pipefd[1]);
        return -1;
    }
    got = vfs_read(pipefd[0], buf, 4);
    vfs_close(pipefd[0]);
    vfs_close(pipefd[1]);
    if (got != 4)
        return -1;
    return 0;
}

static int phase4_path_and_pseudofs_test(void)
{
    char cwd[VFS_PATH_MAX];
    char names[8][VFS_NAME_MAX];
    int n;
    int fd;
    char tmp[64];

    if (vfs_getcwd(cwd, sizeof(cwd)) != 0)
        return -1;
    if (vfs_chdir("/tmp") != 0)
        return -1;
    fd = vfs_create("./phase4_rel.txt");
    if (fd < 0)
        return -1;
    if (vfs_write(fd, "ok", 2) != 2) {
        vfs_close(fd);
        return -1;
    }
    vfs_close(fd);
    if (vfs_chdir(cwd) != 0)
        return -1;

    n = vfs_list("/proc", names, 8);
    if (n <= 0)
        return -1;
    fd = vfs_open("/proc/processes");
    if (fd < 0)
        return -1;
    if (vfs_read(fd, tmp, sizeof(tmp)) == 0) {
        vfs_close(fd);
        return -1;
    }
    vfs_close(fd);

    n = vfs_list("/sys", names, 8);
    if (n <= 0)
        return -1;
    fd = vfs_open("/sys/memory");
    if (fd < 0)
        return -1;
    if (vfs_read(fd, tmp, sizeof(tmp)) == 0) {
        vfs_close(fd);
        return -1;
    }
    vfs_close(fd);
    return 0;
}

static void phase4_selftest_entry(void)
{
    int pass = 0;
    int fail = 0;
    uint64_t start = process_ticks();

    while (!g_phase3_selftests_done && (process_ticks() - start) < 400)
        process_yield();

    kprintf("[phase4][test] start\n");

    if (phase4_fd_semantics_test() == 0) {
        pass++;
        kprintf("[phase4][test] fd semantics PASS\n");
    } else {
        fail++;
        kprintf("[phase4][test] fd semantics FAIL\n");
    }

    if (phase4_path_and_pseudofs_test() == 0) {
        pass++;
        kprintf("[phase4][test] path+pseudofs PASS\n");
    } else {
        fail++;
        kprintf("[phase4][test] path+pseudofs FAIL\n");
    }

    kprintf("[phase4][test] done pass=%d fail=%d\n", pass, fail);
    g_phase4_selftests_done = 1;
    process_exit((fail == 0) ? 0 : 1);
}

void process_run_phase4_selftests(void)
{
    g_phase4_selftests_done = 0;
    process_spawn_kernel("phase4-selftest", phase4_selftest_entry);
}

static int phase5_sysfs_visibility_test(void)
{
    int fd;
    char buf[256];

    fd = vfs_open("/sys/devices/pci");
    if (fd < 0)
        return -1;
    if (vfs_read(fd, buf, sizeof(buf)) == 0) {
        vfs_close(fd);
        return -1;
    }
    vfs_close(fd);

    fd = vfs_open("/sys/net/status");
    if (fd < 0)
        return -1;
    if (vfs_read(fd, buf, sizeof(buf)) == 0) {
        vfs_close(fd);
        return -1;
    }
    vfs_close(fd);
    return 0;
}

static int phase5_ipv4_is_zero(const struct tsukasa_net_ipv4 *ip)
{
    if (!ip)
        return 1;
    return ip->bytes[0] == 0 &&
           ip->bytes[1] == 0 &&
           ip->bytes[2] == 0 &&
           ip->bytes[3] == 0;
}

static int phase5_network_syscall_test(void)
{
    struct tsukasa_net_link_info link;
    struct tsukasa_net_stats stats;
    struct tsukasa_net_ipv4 dns_ip;
    struct tsukasa_net_dns_req dns_req;
    struct tsukasa_net_ping_req ping_req;
    struct tsukasa_net_tcp_connect_req conn_req;
    struct tsukasa_net_tcp_recv_req recv_req;
    static const char http_probe[] = "HEAD / HTTP/1.0\r\nHost: example.com\r\n\r\n";
    char recv_buf[128];
    int rc;
    int has_ip;
    uint64_t start;
    int dns_ok = 0;
    int tcp_connected = 0;

    rc = (int)syscall_handler(SYS_SYSTEM, SYSTEM_CMD_NET_INIT, 0, 0, 0, 0);
    if (rc != 0)
        return -1;

    if ((int)syscall_handler(SYS_SYSTEM, SYSTEM_CMD_NET_GET_LINK, (uintptr_t)&link, 0, 0, 0) != 0)
        return -1;
    if ((int)syscall_handler(SYS_SYSTEM, SYSTEM_CMD_NET_GET_STATS, (uintptr_t)&stats, 0, 0, 0) != 0)
        return -1;
    if (!stats.stack_initialized)
        return -1;

    /* DHCP may legitimately fail in some headless CI/QEMU environments. */
    rc = (int)syscall_handler(SYS_SYSTEM, SYSTEM_CMD_NET_DHCP, 0, 0, 0, 0);
    if (rc != 0 && rc != -1)
        return -1;

    start = process_ticks();
    has_ip = (int)syscall_handler(SYS_SYSTEM, SYSTEM_CMD_NET_HAS_IP, 0, 0, 0, 0);
    while (!has_ip && (process_ticks() - start) < 900) {
        syscall_handler(SYS_SYSTEM, SYSTEM_CMD_NET_POLL, 0, 0, 0, 0);
        process_yield();
        has_ip = (int)syscall_handler(SYS_SYSTEM, SYSTEM_CMD_NET_HAS_IP, 0, 0, 0, 0);
    }

    if (!has_ip) {
        /* Offline mode: ensure syscall path is still stable and errors are sane. */
        (void)syscall_handler(SYS_SYSTEM, SYSTEM_CMD_NET_TCP_CLOSE, 0, 0, 0, 0);
        rc = (int)syscall_handler(SYS_SYSTEM, SYSTEM_CMD_NET_TCP_SEND,
                                  (uintptr_t)http_probe,
                                  sizeof(http_probe) - 1,
                                  0,
                                  0);
        if (rc >= 0)
            return -1;

        recv_req.buffer = recv_buf;
        recv_req.max_len = sizeof(recv_buf);
        recv_req.wait = 0;
        rc = (int)syscall_handler(SYS_SYSTEM, SYSTEM_CMD_NET_TCP_RECV, (uintptr_t)&recv_req, 0, 0, 0);
        if (rc >= 0)
            return -1;

        return 0;
    }

    dns_req.name = "example.com";
    dns_req.out_ip = &dns_ip;
    if ((int)syscall_handler(SYS_SYSTEM, SYSTEM_CMD_NET_DNS_LOOKUP, (uintptr_t)&dns_req, 0, 0, 0) == 0) {
        dns_ok = 1;
    } else {
        dns_ip = link.gateway;
        if (phase5_ipv4_is_zero(&dns_ip)) {
            dns_ip.bytes[0] = 10;
            dns_ip.bytes[1] = 0;
            dns_ip.bytes[2] = 2;
            dns_ip.bytes[3] = 2;
        }
    }

    ping_req.ip = link.gateway;
    if (phase5_ipv4_is_zero(&ping_req.ip)) {
        ping_req.ip.bytes[0] = 10;
        ping_req.ip.bytes[1] = 0;
        ping_req.ip.bytes[2] = 2;
        ping_req.ip.bytes[3] = 2;
    }
    ping_req.timeout_ms = 1000;
    (void)syscall_handler(SYS_SYSTEM, SYSTEM_CMD_NET_PING, (uintptr_t)&ping_req, 0, 0, 0);

    conn_req.ip = dns_ip;
    conn_req.port = 80;
    rc = (int)syscall_handler(SYS_SYSTEM, SYSTEM_CMD_NET_TCP_CONNECT, (uintptr_t)&conn_req, 0, 0, 0);
    if (rc == 0) {
        tcp_connected = 1;
    } else if (dns_ok) {
        return -1;
    }

    if (tcp_connected) {
        rc = (int)syscall_handler(SYS_SYSTEM, SYSTEM_CMD_NET_TCP_SEND,
                                  (uintptr_t)http_probe,
                                  sizeof(http_probe) - 1,
                                  0,
                                  0);
        if (rc < 0)
            return -1;

        recv_req.buffer = recv_buf;
        recv_req.max_len = sizeof(recv_buf);
        recv_req.wait = 1;
        (void)syscall_handler(SYS_SYSTEM, SYSTEM_CMD_NET_TCP_RECV, (uintptr_t)&recv_req, 0, 0, 0);
        if ((int)syscall_handler(SYS_SYSTEM, SYSTEM_CMD_NET_TCP_CLOSE, 0, 0, 0, 0) != 0)
            return -1;
    }

    for (int i = 0; i < 8; i++) {
        rc = (int)syscall_handler(SYS_SYSTEM, SYSTEM_CMD_NET_TCP_CONNECT, (uintptr_t)&conn_req, 0, 0, 0);
        if (rc == 0) {
            if ((int)syscall_handler(SYS_SYSTEM, SYSTEM_CMD_NET_TCP_CLOSE, 0, 0, 0, 0) != 0)
                return -1;
        }
    }

    return 0;
}

static void phase5_selftest_entry(void)
{
    int pass = 0;
    int fail = 0;
    uint64_t start = process_ticks();

    while (!g_phase4_selftests_done && (process_ticks() - start) < 600)
        process_yield();

    kprintf("[phase5][test] start\n");

    if (phase5_sysfs_visibility_test() == 0) {
        pass++;
        kprintf("[phase5][test] sysfs visibility PASS\n");
    } else {
        fail++;
        kprintf("[phase5][test] sysfs visibility FAIL\n");
    }

    if (phase5_network_syscall_test() == 0) {
        pass++;
        kprintf("[phase5][test] net syscall path PASS\n");
    } else {
        fail++;
        kprintf("[phase5][test] net syscall path FAIL\n");
    }

    kprintf("[phase5][test] done pass=%d fail=%d\n", pass, fail);
    process_exit((fail == 0) ? 0 : 1);
}

void process_run_phase5_selftests(void)
{
    process_spawn_kernel("phase5-selftest", phase5_selftest_entry);
}

static int phase7_read_file(const char *path, char *buf, size_t cap)
{
    int fd;
    size_t n;
    if (!path || !buf || cap == 0)
        return -1;
    fd = vfs_open(path);
    if (fd < 0)
        return -1;
    n = vfs_read(fd, buf, cap - 1);
    vfs_close(fd);
    if (n == (size_t)-1)
        return -1;
    buf[n] = '\0';
    return (int)n;
}

static int phase7_streq(const char *a, const char *b)
{
    int i = 0;
    if (!a || !b)
        return 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i])
            return 0;
        i++;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static int phase7_shell_redirection_test(void)
{
    char buf[64];
    if (shell_exec_line("echo phase7 > /tmp/p7_redir.txt", 1, 2) != 0)
        return -1;
    if (phase7_read_file("/tmp/p7_redir.txt", buf, sizeof(buf)) < 0)
        return -1;
    return phase7_streq(buf, "phase7\n") ? 0 : -1;
}

static int phase7_shell_pipeline_test(void)
{
    char buf[64];
    if (shell_exec_line("echo cpu | grep cpu > /tmp/p7_pipe.txt", 1, 2) != 0)
        return -1;
    if (phase7_read_file("/tmp/p7_pipe.txt", buf, sizeof(buf)) < 0)
        return -1;
    return phase7_streq(buf, "cpu\n") ? 0 : -1;
}

static int phase7_shell_rc_test(void)
{
    int fd;
    char buf[64];
    static const char rc_data[] =
        "# phase7 rc test\n"
        "echo rc-ok > /tmp/p7_rc_ok.txt\n";

    fd = vfs_create("/tmp/p7_test.rc");
    if (fd < 0)
        return -1;
    if (vfs_write(fd, rc_data, sizeof(rc_data) - 1) != sizeof(rc_data) - 1) {
        vfs_close(fd);
        return -1;
    }
    vfs_close(fd);

    if (shell_run_rc_file("/tmp/p7_test.rc", 1, 2) != 0)
        return -1;
    if (phase7_read_file("/tmp/p7_rc_ok.txt", buf, sizeof(buf)) < 0)
        return -1;
    return phase7_streq(buf, "rc-ok\n") ? 0 : -1;
}

static void phase7_selftest_entry(void)
{
    int pass = 0;
    int fail = 0;

    kprintf("[phase7][test] start\n");

    if (phase7_shell_redirection_test() == 0) {
        pass++;
        kprintf("[phase7][test] shell redirection PASS\n");
    } else {
        fail++;
        kprintf("[phase7][test] shell redirection FAIL\n");
    }

    if (phase7_shell_pipeline_test() == 0) {
        pass++;
        kprintf("[phase7][test] shell pipeline PASS\n");
    } else {
        fail++;
        kprintf("[phase7][test] shell pipeline FAIL\n");
    }

    if (phase7_shell_rc_test() == 0) {
        pass++;
        kprintf("[phase7][test] shell rc PASS\n");
    } else {
        fail++;
        kprintf("[phase7][test] shell rc FAIL\n");
    }

    kprintf("[phase7][test] done pass=%d fail=%d\n", pass, fail);
    g_phase7_selftests_done = 1;
    process_exit((fail == 0) ? 0 : 1);
}

void process_run_phase7_selftests(void)
{
    g_phase7_selftests_done = 0;
    process_spawn_kernel("phase7-selftest", phase7_selftest_entry);
}

/* frosty (2026-07-22): the boot-time window storms — the gui/input stress windows and the core-app-launch... */
#define PHASE8_GUI_TESTS 0

/* frosty (2026-07-23): the proc/mem STRESS STORM (192 p8-kill-target spawns + shm churn + forced reap) can... */
#define PHASE8_STRESS_STORM 0

#define PHASE8_GUI_WINDOWS      3
#define PHASE8_GUI_STORM_ITERS  320
#define PHASE8_INPUT_FLOOD_ITERS 1400
#define PHASE8_PROC_ROUNDS      24
#define PHASE8_PROC_FANOUT      8
#define PHASE8_SHM_CHURN_ITERS  192
#define PHASE8_FS_CHURN_ITERS   192
#define PHASE8_NET_SOAK_ITERS   32

static volatile uint64_t g_p8_fair_a;
static volatile uint64_t g_p8_fair_b;
static volatile uint64_t g_p8_fair_c;
static volatile int g_p8_fair_stop;

#if PHASE8_STRESS_STORM
static volatile int g_p8_shm_id;
static volatile int g_p8_shm_fail;
static volatile int g_p8_shm_start;
#endif

static uint64_t phase8_pack_i32(int a, int b)
{
    return ((uint64_t)(uint32_t)b << 32) | (uint64_t)(uint32_t)a;
}

static int phase8_make_tmp_path(char *out, int cap, int idx)
{
    const char *prefix = "/tmp/p8_fd_";
    const char *suffix = ".txt";
    char rev[16];
    int oi = 0;
    int rn = 0;

    idx = idx % 8;

    if (!out || cap <= 0 || idx < 0)
        return -1;

    while (*prefix) {
        if (oi >= cap - 1)
            return -1;
        out[oi++] = *prefix++;
    }

    if (idx == 0) {
        rev[rn++] = '0';
    } else {
        while (idx > 0 && rn < (int)sizeof(rev)) {
            rev[rn++] = (char)('0' + (idx % 10));
            idx /= 10;
        }
    }

    while (rn > 0) {
        if (oi >= cap - 1)
            return -1;
        out[oi++] = rev[--rn];
    }

    while (*suffix) {
        if (oi >= cap - 1)
            return -1;
        out[oi++] = *suffix++;
    }
    out[oi] = '\0';
    return 0;
}

static int phase8_net_ip_is_zero(const net_ipv4_addr_t *ip)
{
    if (!ip)
        return 1;
    return ip->bytes[0] == 0 &&
           ip->bytes[1] == 0 &&
           ip->bytes[2] == 0 &&
           ip->bytes[3] == 0;
}

static void phase8_fair_worker_a(void)
{
    while (!g_p8_fair_stop) {
        g_p8_fair_a++;
        __asm__ volatile ("pause");
    }
    process_exit(0);
}

static void phase8_fair_worker_b(void)
{
    while (!g_p8_fair_stop) {
        g_p8_fair_b++;
        __asm__ volatile ("pause");
    }
    process_exit(0);
}

static void phase8_fair_worker_c(void)
{
    while (!g_p8_fair_stop) {
        g_p8_fair_c++;
        __asm__ volatile ("pause");
    }
    process_exit(0);
}

static int phase8_scheduler_fairness_test(uint32_t *ratio_pct_out,
                                          uint64_t *min_ticks_out,
                                          uint64_t *max_ticks_out)
{
    process_t *a;
    process_t *b;
    process_t *c;
    int st = 0;
    uint64_t max_v;
    uint64_t min_v;
    uint32_t ratio_pct;

    g_p8_fair_a = 0;
    g_p8_fair_b = 0;
    g_p8_fair_c = 0;
    g_p8_fair_stop = 0;

    a = process_spawn_kernel("p8-fair-a", phase8_fair_worker_a);
    b = process_spawn_kernel("p8-fair-b", phase8_fair_worker_b);
    c = process_spawn_kernel("p8-fair-c", phase8_fair_worker_c);
    if (!a || !b || !c)
        return -1;

    {
        uint64_t start = process_ticks();
        while ((process_ticks() - start) < 220)
            process_yield();
    }
    g_p8_fair_stop = 1;

    if (selftest_wait_child((int)a->pid, &st) != (int)a->pid)
        return -1;
    if (selftest_wait_child((int)b->pid, &st) != (int)b->pid)
        return -1;
    if (selftest_wait_child((int)c->pid, &st) != (int)c->pid)
        return -1;

    max_v = g_p8_fair_a;
    if (g_p8_fair_b > max_v)
        max_v = g_p8_fair_b;
    if (g_p8_fair_c > max_v)
        max_v = g_p8_fair_c;

    min_v = g_p8_fair_a;
    if (g_p8_fair_b < min_v)
        min_v = g_p8_fair_b;
    if (g_p8_fair_c < min_v)
        min_v = g_p8_fair_c;

    if (max_v == 0 || min_v == 0)
        return -1;

    ratio_pct = (uint32_t)((min_v * 100u) / max_v);
    if (ratio_pct_out)
        *ratio_pct_out = ratio_pct;
    if (min_ticks_out)
        *min_ticks_out = min_v;
    if (max_ticks_out)
        *max_ticks_out = max_v;

    return (ratio_pct >= 35u) ? 0 : -1;
}

#if PHASE8_GUI_TESTS
static int phase8_gui_input_stress_test(uint32_t *event_count_out,
                                        uint32_t *draw_ops_out,
                                        uint32_t *ops_per_100ticks_out)
{
    int pid = process_current_pid();
    int handles[PHASE8_GUI_WINDOWS];
    int ok = 0;
    uint32_t draw_ops = 0;
    uint32_t ev_count = 0;
    uint64_t start_ticks = 0;
    uint64_t elapsed_ticks = 1;

    for (int i = 0; i < PHASE8_GUI_WINDOWS; i++)
        handles[i] = 0;

    for (int i = 0; i < PHASE8_GUI_WINDOWS; i++) {
        handles[i] = gui_srv_window_create(pid,
                                           "phase8-gui",
                                           60 + i * 50,
                                           60 + i * 40,
                                           240,
                                           160);
        if (handles[i] < 0)
            goto cleanup;
    }

    start_ticks = process_ticks();
    for (int i = 0; i < PHASE8_GUI_STORM_ITERS; i++) {
        int h = handles[i % PHASE8_GUI_WINDOWS];
        int x = (i * 11) % 140;
        int y = (i * 7) % 96;
        uint32_t color = 0xFF002020u | ((uint32_t)(i * 41) & 0x0000FFFFu);

        if (gui_srv_draw_rect(pid, h, x, y, 90, 28, color) != GUI_OK)
            goto cleanup;
        if (gui_srv_draw_text(pid, h, 8, 8, "p8", 0xFFFFFFFFu) != GUI_OK)
            goto cleanup;
        if (gui_srv_mark_dirty(pid, h, 0, 0, 220, 140) != GUI_OK)
            goto cleanup;
        draw_ops += 3;

        if ((i & 31) == 0)
            gui_srv_window_set_title(pid, h, (i & 63) ? "phase8-gui" : "phase8-gui*");
        if ((i & 7) == 0)
            process_yield();
    }

    for (int i = 0; i < (PHASE8_GUI_STORM_ITERS / 4); i++) {
        wm_window_t *top = wm_get_top();
        int tx;
        int ty;
        int rx;
        int ry;

        if (!top)
            goto cleanup;

        tx = top->x + top->w / 2;
        ty = top->y + 8;
        wm_handle_mouse(tx, ty, MOUSE_BUTTON_LEFT, MOUSE_BUTTON_LEFT);
        wm_handle_mouse(tx + 20, ty + 10, MOUSE_BUTTON_LEFT, 0);
        wm_handle_mouse(tx + 40, ty + 12, MOUSE_BUTTON_LEFT, 0);
        wm_handle_mouse(tx + 40, ty + 12, 0, MOUSE_BUTTON_LEFT);

        rx = top->x + top->w - 2;
        ry = top->y + top->h - 2;
        wm_handle_mouse(rx, ry, MOUSE_BUTTON_LEFT, MOUSE_BUTTON_LEFT);
        wm_handle_mouse(rx + 6, ry + 4, MOUSE_BUTTON_LEFT, 0);
        wm_handle_mouse(rx + 12, ry + 8, MOUSE_BUTTON_LEFT, 0);
        wm_handle_mouse(rx + 12, ry + 8, 0, MOUSE_BUTTON_LEFT);
        process_yield();
    }

    for (int i = 0; i < PHASE8_INPUT_FLOOD_ITERS; i++) {
        wm_window_t *top = wm_get_top();
        int usable_w;
        int usable_h;
        int mx;
        int my;

        if (!top)
            break;
        usable_w = top->w - (2 * WM_BORDER_PX);
        usable_h = top->h - (2 * WM_BORDER_PX) - WM_TITLE_BAR_H;
        if (usable_w < 8)
            usable_w = 8;
        if (usable_h < 8)
            usable_h = 8;

        mx = top->x + WM_BORDER_PX + ((i * 5) % usable_w);
        my = top->y + WM_BORDER_PX + WM_TITLE_BAR_H + ((i * 11) % usable_h);
        wm_handle_mouse(mx, my, 0, 0);
        if ((i % 120) == 0) {
            wm_handle_mouse(mx, my, MOUSE_BUTTON_LEFT, MOUSE_BUTTON_LEFT);
            wm_handle_mouse(mx + 1, my + 1, MOUSE_BUTTON_LEFT, 0);
            wm_handle_mouse(mx + 1, my + 1, 0, MOUSE_BUTTON_LEFT);
        }
    }

    for (int i = 0; i < PHASE8_GUI_WINDOWS; i++) {
        struct tsukasa_gui_event ev;
        int guard = 0;
        for (;;) {
            int rc;
            if (guard++ > 8192)
                goto cleanup;
            rc = gui_srv_get_event(pid, handles[i], &ev);
            if (rc == GUI_OK) {
                ev_count++;
                continue;
            }
            if (rc == GUI_ERR_AGAIN)
                break;
            goto cleanup;
        }
    }

    elapsed_ticks = process_ticks() - start_ticks;
    if (elapsed_ticks == 0)
        elapsed_ticks = 1;
    if (ev_count < 64)
        goto cleanup;

    ok = 1;

cleanup:
    for (int i = 0; i < PHASE8_GUI_WINDOWS; i++) {
        if (handles[i] > 0)
            (void)gui_srv_window_destroy(pid, handles[i]);
    }

    if (event_count_out)
        *event_count_out = ev_count;
    if (draw_ops_out)
        *draw_ops_out = draw_ops;
    if (ops_per_100ticks_out)
        *ops_per_100ticks_out = (uint32_t)((draw_ops * 100u) / (uint32_t)(elapsed_ticks ? elapsed_ticks : 1u));

    return ok ? 0 : -1;
}
#endif

#if PHASE8_STRESS_STORM
static void phase8_kill_target(void)
{
    for (;;)
        process_yield();
}

static void phase8_shm_peer_task(void)
{
    while (!g_p8_shm_start)
        process_yield();

    for (int i = 0; i < PHASE8_SHM_CHURN_ITERS; i++) {
        volatile uint32_t *buf = (volatile uint32_t *)shm_attach(g_p8_shm_id);
        if (!buf) {
            g_p8_shm_fail = 1;
            process_exit(1);
        }
        buf[0] ^= (uint32_t)i;
        if (shm_detach((void *)buf) != 0) {
            g_p8_shm_fail = 1;
            process_exit(1);
        }
    }
    process_exit(0);
}

static int phase8_process_memory_stress_test(uint32_t *spawned_out,
                                             uint32_t *killed_out,
                                             uint32_t *reaped_out,
                                             uint32_t *shm_ops_out,
                                             uintptr_t *free_before_out,
                                             uintptr_t *free_after_out)
{
    uintptr_t free_before = pmm_free_page_count();
    uintptr_t free_after = free_before;
    uint32_t spawned = 0;
    uint32_t killed = 0;
    uint32_t reaped = 0;
    uint32_t shm_ops = 0;

    for (int round = 0; round < PHASE8_PROC_ROUNDS; round++) {
        process_t *children[PHASE8_PROC_FANOUT];
        int st = 0;
        for (int i = 0; i < PHASE8_PROC_FANOUT; i++)
            children[i] = NULL;

        for (int i = 0; i < PHASE8_PROC_FANOUT; i++) {
            children[i] = process_spawn_kernel("p8-kill-target", phase8_kill_target);
            if (children[i])
                spawned++;
        }

        {
            uint64_t start = process_ticks();
            while ((process_ticks() - start) < 8)
                process_yield();
        }

        for (int i = 0; i < PHASE8_PROC_FANOUT; i++) {
            if (!children[i])
                continue;
            if (process_kill((int)children[i]->pid, PROCESS_SIGKILL) == 0)
                killed++;
        }

        for (int i = 0; i < PHASE8_PROC_FANOUT; i++) {
            int waited;
            if (!children[i])
                continue;
            waited = selftest_wait_child((int)children[i]->pid, &st);
            if (waited != (int)children[i]->pid)
                return -1;
            if (WAIT_TERM_SIGNAL(st) != PROCESS_SIGKILL)
                return -1;
            reaped++;
        }
    }

    g_p8_shm_fail = 0;
    g_p8_shm_start = 0;
    g_p8_shm_id = shm_create(PAGE_SIZE * 2);
    if (g_p8_shm_id < 0)
        return -1;
    {
        process_t *peer = process_spawn_kernel("p8-shm-peer", phase8_shm_peer_task);
        int st = 0;
        if (!peer) {
            shm_destroy(g_p8_shm_id);
            return -1;
        }
        g_p8_shm_start = 1;

        for (int i = 0; i < PHASE8_SHM_CHURN_ITERS; i++) {
            volatile uint32_t *buf = (volatile uint32_t *)shm_attach(g_p8_shm_id);
            if (!buf) {
                shm_destroy(g_p8_shm_id);
                return -1;
            }
            buf[0] = (uint32_t)i;
            if (shm_detach((void *)buf) != 0) {
                shm_destroy(g_p8_shm_id);
                return -1;
            }
            shm_ops++;
        }

        if (selftest_wait_child((int)peer->pid, &st) != (int)peer->pid) {
            shm_destroy(g_p8_shm_id);
            return -1;
        }
        if (WAIT_EXIT_CODE(st) != 0 || g_p8_shm_fail) {
            shm_destroy(g_p8_shm_id);
            return -1;
        }
    }

    shm_ops += PHASE8_SHM_CHURN_ITERS;
    if (shm_destroy(g_p8_shm_id) != 0)
        return -1;

    {
        struct shm_stats stats = {0};
        shm_get_stats(&stats);
        if (stats.attachment_count != 0)
            return -1;
    }

    free_after = pmm_free_page_count();
    if (free_after + 16 < free_before)
        return -1;
    if (spawned == 0 || killed == 0 || reaped == 0)
        return -1;

    if (spawned_out)
        *spawned_out = spawned;
    if (killed_out)
        *killed_out = killed;
    if (reaped_out)
        *reaped_out = reaped;
    if (shm_ops_out)
        *shm_ops_out = shm_ops;
    if (free_before_out)
        *free_before_out = free_before;
    if (free_after_out)
        *free_after_out = free_after;
    return 0;
}
#endif

static int phase8_storage_network_stress_test(uint32_t *fd_ops_out,
                                              uint32_t *net_attempts_out,
                                              uint32_t *net_success_out,
                                              int *net_online_out)
{
    uint32_t fd_ops = 0;
    uint32_t net_attempts = 0;
    uint32_t net_success = 0;
    int net_online = 0;

    for (int i = 0; i < PHASE8_FS_CHURN_ITERS; i++) {
        char path[64];
        char buf[16];
        int fd;
        int pfd[2];
        if (phase8_make_tmp_path(path, (int)sizeof(path), i) != 0)
            return -1;

        fd = vfs_create(path);
        if (fd < 0)
            return -1;
        if (vfs_write(fd, "phase8", 6) != 6) {
            vfs_close(fd);
            return -1;
        }
        vfs_close(fd);
        fd_ops += 2;

        fd = vfs_open(path);
        if (fd < 0)
            return -1;
        if (vfs_read(fd, buf, sizeof(buf)) == 0) {
            vfs_close(fd);
            return -1;
        }
        vfs_close(fd);
        fd_ops += 2;

        if (vfs_pipe(pfd) != 0)
            return -1;
        if (vfs_write(pfd[1], "xx", 2) != 2) {
            vfs_close(pfd[0]);
            vfs_close(pfd[1]);
            return -1;
        }
        if (vfs_read(pfd[0], buf, 2) != 2) {
            vfs_close(pfd[0]);
            vfs_close(pfd[1]);
            return -1;
        }
        vfs_close(pfd[0]);
        vfs_close(pfd[1]);
        fd_ops += 4;

        if ((i & 31) == 0) {
            if (vfs_chdir("/tmp") != 0)
                return -1;
            if (vfs_chdir("/") != 0)
                return -1;
            fd_ops += 2;
        }
    }

    if (network_initialize_stack() == 0) {
        net_link_info_t link;
        net_tcp_connect_req_t req;
        static const char probe[] = "HEAD / HTTP/1.0\r\nHost: example.com\r\n\r\n";
        char recv_buf[96];
        int has_ip = 0;

        if (network_get_link_info(&link) != 0)
            return -1;
        (void)network_dhcp_acquire();

        for (int i = 0; i < 120; i++) {
            network_poll();
            if (network_has_ipv4()) {
                has_ip = 1;
                break;
            }
            process_yield();
        }
        net_online = has_ip;
        req.ip = link.gateway;
        if (phase8_net_ip_is_zero(&req.ip)) {
            req.ip.bytes[0] = 10;
            req.ip.bytes[1] = 0;
            req.ip.bytes[2] = 2;
            req.ip.bytes[3] = 2;
        }
        req.port = 80;

        for (int i = 0; i < PHASE8_NET_SOAK_ITERS; i++) {
            int rc;
            network_poll();
            rc = network_tcp_connect(&req);
            net_attempts++;
            if (rc == 0) {
                net_success++;
                (void)network_tcp_send(probe, sizeof(probe) - 1);
                (void)network_tcp_recv(recv_buf, sizeof(recv_buf), 0);
            }
            (void)network_tcp_close();
            process_yield();
        }
    } else {
        char probe = 'x';
        if (network_tcp_send(&probe, 1) >= 0)
            return -1;
    }

    if (fd_ops_out)
        *fd_ops_out = fd_ops;
    if (net_attempts_out)
        *net_attempts_out = net_attempts;
    if (net_success_out)
        *net_success_out = net_success;
    if (net_online_out)
        *net_online_out = net_online;
    return 0;
}

static int phase8_invalid_syscall_fault_test(int *checks_out)
{
    int checks = 0;
    int rc;

    rc = (int)syscall_handler(SYS_FS, 0xFFFFu, 0, 0, 0, 0);
    if (rc != -1)
        return -1;
    checks++;

    rc = (int)syscall_handler(SYS_SYSTEM, SYSTEM_CMD_SPAWN, 0, 0, 0, 0);
    if (rc != -1)
        return -1;
    checks++;

    rc = (int)syscall_handler(SYS_SYSTEM, SYSTEM_CMD_SPAWN_EX, 0, 0, 0, 0);
    if (rc != -1)
        return -1;
    checks++;

    rc = (int)syscall_handler(SYS_SYSTEM, SYSTEM_CMD_NET_DNS_LOOKUP, 0, 0, 0, 0);
    if (rc != -1)
        return -1;
    checks++;

    rc = (int)syscall_handler(SYS_GUI,
                              GUI_CMD_WINDOW_CREATE,
                              (uintptr_t)"phase8-bad",
                              (uintptr_t)phase8_pack_i32(10, 10),
                              (uintptr_t)phase8_pack_i32(0, 0),
                              0);
    if (rc != GUI_ERR_INVALID)
        return -1;
    checks++;

    rc = (int)syscall_handler(SYS_SYSTEM,
                              SYSTEM_CMD_WAITPID,
                              99999u,
                              0,
                              PROCESS_WAIT_WNOHANG,
                              0);
    if (rc != -1)
        return -1;
    checks++;

    if (checks_out)
        *checks_out = checks;
    return 0;
}

#if PHASE8_GUI_TESTS
static int phase8_core_app_launch_test(int *launched_out, int *passed_out, int *total_out)
{
    static const char *apps[] = {
        "/apps/terminal",
        "/apps/filemgr",
        "/apps/notepad",
        "/apps/settings",
        "/apps/calc",
        "/apps/diagnostics",
        "/apps/network",
        "/apps/about"
    };
    int launched = 0;
    int passed = 0;
    int total = (int)(sizeof(apps) / sizeof(apps[0]));

    for (int i = 0; i < total; i++) {
        int pid;
        int st = 0;
        pid = (int)syscall_handler(SYS_SYSTEM,
                                   SYSTEM_CMD_SPAWN,
                                   (uintptr_t)apps[i],
                                   0,
                                   0,
                                   0);
        if (pid <= 0)
            continue;
        launched++;

        {
            uint64_t start = process_ticks();
            while ((process_ticks() - start) < 10)
                process_yield();
        }

        (void)process_kill(pid, PROCESS_SIGKILL);
        if (selftest_wait_child(pid, &st) != pid)
            continue;
        if (WAIT_TERM_SIGNAL(st) == PROCESS_SIGKILL || WAIT_EXIT_CODE(st) == 0)
            passed++;
    }

    if (launched_out)
        *launched_out = launched;
    if (passed_out)
        *passed_out = passed;
    if (total_out)
        *total_out = total;
    return (launched == total && passed == total) ? 0 : -1;
}
#endif

static void phase8_selftest_entry(void)
{
    int pass = 0;
    int fail = 0;
    int blocker = 0;
    int major = 0;
    int minor = 0;

    uint32_t fairness_ratio = 0;
    uint64_t fairness_min = 0;
    uint64_t fairness_max = 0;
#if PHASE8_GUI_TESTS
    uint32_t gui_events = 0;
    uint32_t gui_draw_ops = 0;
    uint32_t gui_ops_per_100ticks = 0;
#endif
#if PHASE8_STRESS_STORM
    uint32_t spawned = 0;
    uint32_t killed = 0;
    uint32_t reaped = 0;
    uint32_t shm_ops = 0;
    uintptr_t free_before = 0;
    uintptr_t free_after = 0;
#endif
    uint32_t fd_ops = 0;
    uint32_t net_attempts = 0;
    uint32_t net_success = 0;
    int net_online = 0;
    int fault_checks = 0;
#if PHASE8_GUI_TESTS
    int apps_launched = 0;
    int apps_passed = 0;
    int apps_total = 0;
#endif

    {
        uint64_t wait_start = process_ticks();
        while (!g_phase7_selftests_done && (process_ticks() - wait_start) < 700)
            process_yield();
    }

    kprintf("[phase8][test] start\n");

    if (phase8_scheduler_fairness_test(&fairness_ratio, &fairness_min, &fairness_max) == 0) {
        pass++;
        kprintf("[phase8][perf] scheduler fairness PASS ratio_pct=%u min=%u max=%u\n",
                fairness_ratio,
                (uint32_t)fairness_min,
                (uint32_t)fairness_max);
    } else {
        fail++;
        blocker++;
        kprintf("[phase8][perf] scheduler fairness FAIL ratio_pct=%u min=%u max=%u\n",
                fairness_ratio,
                (uint32_t)fairness_min,
                (uint32_t)fairness_max);
    }

#if PHASE8_GUI_TESTS
    if (phase8_gui_input_stress_test(&gui_events, &gui_draw_ops, &gui_ops_per_100ticks) == 0) {
        pass++;
        kprintf("[phase8][stress] gui/input PASS events=%u draw_ops=%u ops_per_100ticks=%u\n",
                gui_events,
                gui_draw_ops,
                gui_ops_per_100ticks);
    } else {
        fail++;
        blocker++;
        kprintf("[phase8][stress] gui/input FAIL events=%u draw_ops=%u ops_per_100ticks=%u\n",
                gui_events,
                gui_draw_ops,
                gui_ops_per_100ticks);
    }

    if (gui_ops_per_100ticks >= 50) {
        pass++;
        kprintf("[phase8][perf] redraw throughput PASS ops_per_100ticks=%u\n",
                gui_ops_per_100ticks);
    } else {
        fail++;
        major++;
        kprintf("[phase8][perf] redraw throughput FAIL ops_per_100ticks=%u\n",
                gui_ops_per_100ticks);
    }
#else
    kprintf("[phase8][stress] gui/input SKIPPED (PHASE8_GUI_TESTS=0 — "
            "boot window storm disabled per frosty; issue #21 family)\n");
    kprintf("[phase8][perf] redraw throughput SKIPPED (PHASE8_GUI_TESTS=0)\n");
#endif

#if PHASE8_STRESS_STORM
    if (phase8_process_memory_stress_test(&spawned,
                                          &killed,
                                          &reaped,
                                          &shm_ops,
                                          &free_before,
                                          &free_after) == 0) {
        pass++;
        kprintf("[phase8][stress] proc/mem PASS spawned=%u killed=%u reaped=%u shm_ops=%u free_before=%u free_after=%u\n",
                spawned,
                killed,
                reaped,
                shm_ops,
                (uint32_t)free_before,
                (uint32_t)free_after);
        kprintf("[phase8][fault] forced reap PASS spawned=%u reaped=%u\n",
                spawned,
                reaped);
    } else {
        fail++;
        blocker++;
        kprintf("[phase8][stress] proc/mem FAIL spawned=%u killed=%u reaped=%u shm_ops=%u free_before=%u free_after=%u\n",
                spawned,
                killed,
                reaped,
                shm_ops,
                (uint32_t)free_before,
                (uint32_t)free_after);
        kprintf("[phase8][fault] forced reap FAIL spawned=%u reaped=%u\n",
                spawned,
                reaped);
    }
#else
    kprintf("[phase8][stress] proc/mem SKIPPED (PHASE8_STRESS_STORM=0 — "
            "192-spawn soak trips the issue-#17 reap race and exhausts "
            "the process table on desktop boots)\n");
    kprintf("[phase8][fault] forced reap SKIPPED (PHASE8_STRESS_STORM=0)\n");
#endif

    if (phase8_storage_network_stress_test(&fd_ops, &net_attempts, &net_success, &net_online) == 0) {
        pass++;
        kprintf("[phase8][stress] storage/net PASS fd_ops=%u net_attempts=%u net_success=%u net_online=%d\n",
                fd_ops,
                net_attempts,
                net_success,
                net_online);
    } else {
        fail++;
        major++;
        kprintf("[phase8][stress] storage/net FAIL fd_ops=%u net_attempts=%u net_success=%u net_online=%d\n",
                fd_ops,
                net_attempts,
                net_success,
                net_online);
    }

    if (phase8_invalid_syscall_fault_test(&fault_checks) == 0) {
        pass++;
        kprintf("[phase8][fault] invalid-syscalls PASS checks=%d\n", fault_checks);
    } else {
        fail++;
        blocker++;
        kprintf("[phase8][fault] invalid-syscalls FAIL checks=%d\n", fault_checks);
    }

#if PHASE8_GUI_TESTS
    if (phase8_core_app_launch_test(&apps_launched, &apps_passed, &apps_total) == 0) {
        pass++;
        kprintf("[phase8][regression] core-app-launch PASS launched=%d/%d\n",
                apps_passed,
                apps_total);
    } else {
        fail++;
        blocker++;
        kprintf("[phase8][regression] core-app-launch FAIL launched=%d/%d (spawned=%d)\n",
                apps_passed,
                apps_total,
                apps_launched);
    }
#else
    kprintf("[phase8][regression] core-app-launch SKIPPED (PHASE8_GUI_TESTS=0)\n");
#endif

    kprintf("[phase8][triage] blocker=%d major=%d minor=%d\n", blocker, major, minor);
    kprintf("[phase8][test] done pass=%d fail=%d\n", pass, fail);
    process_exit((fail == 0) ? 0 : 1);
}

void process_run_phase8_selftests(void)
{
    process_spawn_kernel("phase8-selftest", phase8_selftest_entry);
}

static volatile uint32_t g_guide06_pin_min;
static volatile uint32_t g_guide06_pin_max;
static volatile int g_guide06_pin_done;

static void guide06_pinned_entry(void)
{
    uint32_t min_cpu = 0xFFFFFFFFu;
    uint32_t max_cpu = 0;

    for (int i = 0; i < 16; i++) {
        uint32_t cpu = smp_this_cpu_id();
        if (cpu < min_cpu)
            min_cpu = cpu;
        if (cpu > max_cpu)
            max_cpu = cpu;
        process_yield();
    }
    g_guide06_pin_min = min_cpu;
    g_guide06_pin_max = max_cpu;
    kprintf("[guide06] pinned process ran on cpu %u..%u (cpu_affinity=2)\n",
            min_cpu, max_cpu);
    g_guide06_pin_done = 1;
    process_exit(0);
}

static void guide06_selftest_entry(void)
{
    uint64_t tick0[CPU_COUNT_MAX];
    uint64_t ipi0[CPU_COUNT_MAX];
    uint64_t idle0[CPU_COUNT_MAX];
    int pass = 0;
    int fail = 0;
    uint32_t ncpu = g_cpu_count;

    kprintf("[guide06][test] start ncpu=%u smp_cpu_count=%u\n",
            ncpu, smp_cpu_count());

    for (uint32_t c = 0; c < ncpu; c++) {
        tick0[c] = g_core_ticks[c];
        ipi0[c] = g_ipi_ticks[c];
        idle0[c] = g_idle_seen_ticks[c];
    }

    {
        uint64_t start = process_ticks();
        while (process_ticks() - start < 200)
            process_yield();
    }

    {
        int ipi_ok = 1;
        int tick_ok = 1;
        for (uint32_t c = 0; c < ncpu; c++) {
            uint64_t dt = g_core_ticks[c] - tick0[c];
            uint64_t di = g_ipi_ticks[c] - ipi0[c];
            uint64_t dd = g_idle_seen_ticks[c] - idle0[c];
            kprintf("[guide06] cpu%u ticks+%u ipi+%u idle_seen+%u\n",
                    c, (uint32_t)dt, (uint32_t)di, (uint32_t)dd);
            if (dt == 0)
                tick_ok = 0;
            if (c > 0 && di == 0)
                ipi_ok = 0;
        }

        if (tick_ok) {
            pass++;
            kprintf("[guide06] tick distribution PASS (all %u cpus ticked)\n", ncpu);
        } else {
            fail++;
            kprintf("[guide06] tick distribution FAIL\n");
        }

        if (ncpu == 1) {
            kprintf("[guide06] ipi delivery N/A (single cpu, regression mode)\n");
        } else {
            if (ipi_ok) {
                pass++;
                kprintf("[guide06] ipi delivery PASS (every AP took vector 0x41)\n");
            } else {
                fail++;
                kprintf("[guide06] ipi delivery FAIL\n");
            }
        }
    }

    if (ncpu >= 3) {
        process_t *pin;
        g_guide06_pin_done = 0;
        g_guide06_pin_min = 0xFFFFFFFFu;
        g_guide06_pin_max = 0xFFFFFFFFu;
        pin = process_spawn_kernel_pinned("guide06-pin2", guide06_pinned_entry, 2);
        if (!pin) {
            fail++;
            kprintf("[guide06] affinity pin FAIL (spawn failed)\n");
        } else {
            int status = 0;
            int r = process_waitpid(process_current_pid(), (int)pin->pid, 0, &status);
            if (r == (int)pin->pid && g_guide06_pin_done &&
                g_guide06_pin_min == 2 && g_guide06_pin_max == 2) {
                pass++;
                kprintf("[guide06] affinity pin PASS (cpu_affinity=2 held)\n");
            } else {
                fail++;
                kprintf("[guide06] affinity pin FAIL (r=%d done=%d cpu %u..%u)\n",
                        r, g_guide06_pin_done, g_guide06_pin_min, g_guide06_pin_max);
            }
        }
    } else {
        kprintf("[guide06] affinity pin SKIP (ncpu=%u < 3)\n", ncpu);
    }

    /* Idle heartbeat: one count per drain-loop iteration on each AP (work_queue_drain_loop ->... */
    if (ncpu > 1) {
        uint64_t loop0[CPU_COUNT_MAX];
        int idle_ok = 1;
        for (uint32_t c = 0; c < ncpu; c++)
            loop0[c] = g_idle_loop_ticks[c];
        {
            uint64_t start = process_ticks();
            while (process_ticks() - start < 300)
                process_yield();
        }
        for (uint32_t c = 1; c < ncpu; c++) {
            uint64_t dl = g_idle_loop_ticks[c] - loop0[c];
            kprintf("[guide06] cpu%u idle_loop+%u\n", c, (uint32_t)dl);
            if (dl == 0)
                idle_ok = 0;
        }
        if (idle_ok) {
            pass++;
            kprintf("[guide06] idle heartbeat PASS (every AP ran its idle drain loop)\n");
        } else {
            fail++;
            kprintf("[guide06] idle heartbeat FAIL\n");
        }
    } else {
        kprintf("[guide06] idle heartbeat N/A (single cpu, regression mode)\n");
    }

    kprintf("[guide06][test] done pass=%d fail=%d\n", pass, fail);
    process_exit((fail == 0) ? 0 : 1);
}

void process_run_guide06_selftests(void)
{
    process_spawn_kernel("guide06-selftest", guide06_selftest_entry);
}

#define G10_TICKS 500 /* 5 s at the 100 Hz PIT — per bounded wait */

static volatile uint32_t g10_m1_w1;
static volatile uint32_t g10_m1_w2;
static volatile int g10_m1_a_rc;
static volatile int g10_m1_b_wake_rc;
static volatile int g10_m1_b_hs_ok;

static volatile uint32_t g10_m2_w;

static volatile uint32_t g10_m3_w;
static volatile uint32_t g10_m3_woken;
static volatile uint32_t g10_m3_wait_bad;

/* [0] and [64] are 256 bytes apart: key delta 64, same bucket (& 63). */
static volatile uint32_t g10_m4_arr[65];
static volatile uint32_t g10_m4_c_woke;
static volatile uint32_t g10_m4_d_woke;

static int guide10_state_of(int pid)
{
    int st = -1;
    unsigned long irqf = spin_lock_irqsave(&g_sched_lock);
    process_t *p = find_by_pid_locked(pid);
    if (p)
        st = (int)p->state;
    spin_unlock_irqrestore(&g_sched_lock, irqf);
    return st;
}

/* Yield until pid reaches `state` or ~timeout ticks pass. 1 = reached. */
static int guide10_wait_state(int pid, int state, uint64_t timeout)
{
    uint64_t t0 = g_sched_ticks;
    while (g_sched_ticks - t0 < timeout) {
        if (guide10_state_of(pid) == state)
            return 1;
        process_yield();
    }
    return guide10_state_of(pid) == state;
}

static void guide10_force_wake(int pid)
{
    process_t *p;
    unsigned long irqf = spin_lock_irqsave(&g_sched_lock);
    p = find_by_pid_locked(pid);
    spin_unlock_irqrestore(&g_sched_lock, irqf);
    if (p)
        process_wake_blocked(p);
}

/* M1: A blocks on w1; B publishes w1=1 then wakes it; A replies on w2 the same way. */
static void guide10_m1_waiter(void)
{
    long rc = kernel_futex_wait(&g10_m1_w1, 0);
    g10_m1_a_rc = (int)rc;
    g10_m1_w2 = 1;
    kernel_futex_wake(&g10_m1_w2, 1);
    process_exit(0);
}

static void guide10_m1_waker(void)
{
    long rc;
    g10_m1_w1 = 1;
    g10_m1_b_wake_rc = (int)kernel_futex_wake(&g10_m1_w1, 1);
    rc = kernel_futex_wait(&g10_m1_w2, 0);
    g10_m1_b_hs_ok = (rc == 0) || (rc == -EAGAIN && g10_m1_w2 == 1);
    process_exit(0);
}

static void guide10_m3_waiter(void)
{
    long rc = kernel_futex_wait(&g10_m3_w, 0);
    if (rc != 0)
        __atomic_fetch_add(&g10_m3_wait_bad, 1, __ATOMIC_SEQ_CST);
    __atomic_fetch_add(&g10_m3_woken, 1, __ATOMIC_SEQ_CST);
    process_exit(0);
}

static void guide10_m4_c(void)
{
    kernel_futex_wait(&g10_m4_arr[0], 0);
    g10_m4_c_woke = 1;
    process_exit(0);
}

static void guide10_m4_d(void)
{
    kernel_futex_wait(&g10_m4_arr[64], 0);
    g10_m4_d_woke = 1;
    process_exit(0);
}

static int guide10_spawn_cli(const char *args, const char *name)
{
    int pid = -1;
    for (int i = 0; i < 200 && pid < 0; i++) {
        pid = elf64_spawn_cmdline("/fat12/futex10.elf", args, name);
        if (pid < 0)
            process_yield();
    }
    return pid;
}

static void guide10_selftest_entry(void)
{
    int pass = 0;
    int fail = 0;
    int st;
    uint64_t t0;

    kprintf("[guide10][test] start\n");

    {
        process_t *a;
        process_t *b;
        int ok = 0;
        g10_m1_w1 = 0;
        g10_m1_w2 = 0;
        g10_m1_a_rc = -1000;
        g10_m1_b_wake_rc = -1000;
        g10_m1_b_hs_ok = 0;
        a = process_spawn_kernel("g10-m1-waiter", guide10_m1_waiter);
        if (a && guide10_wait_state((int)a->pid, PROCESS_BLOCKED, G10_TICKS)) {
            b = process_spawn_kernel("g10-m1-waker", guide10_m1_waker);
            if (b) {
                t0 = g_sched_ticks;
                while (g10_m1_a_rc == -1000 && g_sched_ticks - t0 < G10_TICKS)
                    process_yield();
                if (g10_m1_a_rc == -1000) {
                    process_wake_blocked(a);
                    process_wake_blocked(b);
                } else {
                    ok = 1;
                }
                selftest_wait_child((int)a->pid, &st);
                selftest_wait_child((int)b->pid, &st);
                ok = ok && (g10_m1_a_rc == 0) && (g10_m1_b_wake_rc == 1) &&
                     g10_m1_b_hs_ok;
            }
        } else if (a) {
            process_wake_blocked(a);
            selftest_wait_child((int)a->pid, &st);
        }
        if (ok) {
            pass++;
            kprintf("[guide10] m1 handshake PASS (wait rc=0, wake count=1, reply leg ok)\n");
        } else {
            fail++;
            kprintf("[guide10] m1 handshake FAIL a_rc=%d wake_rc=%d hs=%d\n",
                    g10_m1_a_rc, g10_m1_b_wake_rc, g10_m1_b_hs_ok);
        }
    }

    {
        long rc1;
        long rc2;
        g10_m2_w = 7;
        rc1 = kernel_futex_wait(&g10_m2_w, 3);
        rc2 = kernel_futex_wake(&g10_m2_w, 8);
        if (rc1 == -EAGAIN && futex_pending_waiters(&g10_m2_w) == 0 &&
            rc2 == 0) {
            pass++;
            kprintf("[guide10] m2 eagain PASS (no block, no waiter, wake=0)\n");
        } else {
            fail++;
            kprintf("[guide10] m2 eagain FAIL rc1=%d rc2=%d pending=%d\n",
                    (int)rc1, (int)rc2, futex_pending_waiters(&g10_m2_w));
        }
    }

    {
        process_t *w[3];
        int ok = 1;
        int i;
        long rc1 = -1000;
        long rc2 = -1000;
        g10_m3_w = 0;
        g10_m3_woken = 0;
        g10_m3_wait_bad = 0;
        w[0] = process_spawn_kernel("g10-m3-a", guide10_m3_waiter);
        w[1] = process_spawn_kernel("g10-m3-b", guide10_m3_waiter);
        w[2] = process_spawn_kernel("g10-m3-c", guide10_m3_waiter);
        for (i = 0; i < 3; i++) {
            if (!w[i] ||
                !guide10_wait_state((int)w[i]->pid, PROCESS_BLOCKED, G10_TICKS))
                ok = 0;
        }
        if (ok && futex_pending_waiters(&g10_m3_w) != 3)
            ok = 0;
        if (ok) {
            rc1 = kernel_futex_wake(&g10_m3_w, 1);
            t0 = g_sched_ticks;
            while (g10_m3_woken < 1 && g_sched_ticks - t0 < G10_TICKS)
                process_yield();
            t0 = g_sched_ticks;
            while (g_sched_ticks - t0 < 20)
                process_yield();
            if (rc1 != 1 || g10_m3_woken != 1 ||
                futex_pending_waiters(&g10_m3_w) != 2)
                ok = 0;
        }
        if (ok) {
            rc2 = kernel_futex_wake(&g10_m3_w, 2);
            t0 = g_sched_ticks;
            while (g10_m3_woken < 3 && g_sched_ticks - t0 < G10_TICKS)
                process_yield();
            if (rc2 != 2 || g10_m3_woken != 3 || g10_m3_wait_bad != 0)
                ok = 0;
        }
        for (i = 0; i < 3; i++) {
            if (w[i]) {
                process_wake_blocked(w[i]);
                selftest_wait_child((int)w[i]->pid, &st);
            }
        }
        if (ok) {
            pass++;
            kprintf("[guide10] m3 wake count PASS (1 of 3, then the other 2)\n");
        } else {
            fail++;
            kprintf("[guide10] m3 wake count FAIL rc1=%d rc2=%d woken=%u bad=%u pending=%d\n",
                    (int)rc1, (int)rc2, g10_m3_woken, g10_m3_wait_bad,
                    futex_pending_waiters(&g10_m3_w));
        }
    }

    {
        process_t *c;
        process_t *d;
        int ok = 0;
        int c_held = 0;
        long rc1 = -1000;
        long rc2 = -1000;
        uintptr_t k0 = ((uintptr_t)&g10_m4_arr[0] >> 2) & 63;
        uintptr_t k64 = ((uintptr_t)&g10_m4_arr[64] >> 2) & 63;
        g10_m4_arr[0] = 0;
        g10_m4_arr[64] = 0;
        g10_m4_c_woke = 0;
        g10_m4_d_woke = 0;
        c = process_spawn_kernel("g10-m4-c", guide10_m4_c);
        d = process_spawn_kernel("g10-m4-d", guide10_m4_d);
        if (k0 == k64 && c && d &&
            guide10_wait_state((int)c->pid, PROCESS_BLOCKED, G10_TICKS) &&
            guide10_wait_state((int)d->pid, PROCESS_BLOCKED, G10_TICKS)) {
            rc1 = kernel_futex_wake(&g10_m4_arr[64], 8);
            t0 = g_sched_ticks;
            while (g10_m4_d_woke == 0 && g_sched_ticks - t0 < G10_TICKS)
                process_yield();
            c_held = (guide10_state_of((int)c->pid) == PROCESS_BLOCKED) &&
                     (futex_pending_waiters(&g10_m4_arr[0]) == 1);
            if (rc1 == 1 && g10_m4_d_woke == 1 && c_held) {
                rc2 = kernel_futex_wake(&g10_m4_arr[0], 1);
                t0 = g_sched_ticks;
                while (g10_m4_c_woke == 0 && g_sched_ticks - t0 < G10_TICKS)
                    process_yield();
                ok = (rc2 == 1 && g10_m4_c_woke == 1);
            }
        }
        if (c) {
            process_wake_blocked(c);
            selftest_wait_child((int)c->pid, &st);
        }
        if (d) {
            process_wake_blocked(d);
            selftest_wait_child((int)d->pid, &st);
        }
        if (ok) {
            pass++;
            kprintf("[guide10] m4 bucket collision PASS (key %u==%u, neighbor undisturbed)\n",
                    (unsigned)k0, (unsigned)k64);
        } else {
            fail++;
            kprintf("[guide10] m4 bucket collision FAIL k0=%u k64=%u rc1=%d rc2=%d c_woke=%u d_woke=%u held=%d\n",
                    (unsigned)k0, (unsigned)k64, (int)rc1, (int)rc2,
                    g10_m4_c_woke, g10_m4_d_woke, c_held);
        }
    }

    {
        int ok = 0;
        int code = -1;
        int ppid;
        int wpid = -1;
        int kpid;
        ppid = guide10_spawn_cli("futex10 probe", "futex10-probe");
        st = -1;
        code = (ppid > 0 && selftest_wait_child(ppid, &st) == ppid)
                   ? ((st >> 8) & 0xFF) : -1;
        if (code == 0) {
            wpid = guide10_spawn_cli("futex10 waiter", "futex10-wait");
            if (wpid > 0 &&
                guide10_wait_state(wpid, PROCESS_BLOCKED, G10_TICKS)) {
                kpid = guide10_spawn_cli("futex10 waker", "futex10-wake");
                st = -1;
                code = (kpid > 0 && selftest_wait_child(kpid, &st) == kpid)
                           ? ((st >> 8) & 0xFF) : -1;
                if (code == 0) {
                    st = -1;
                    code = (selftest_wait_child(wpid, &st) == wpid)
                               ? ((st >> 8) & 0xFF) : -1;
                    ok = (code == 0);
                } else {
                    code = 100 + ((code < 0) ? 99 : code);
                    guide10_force_wake(wpid);
                    selftest_wait_child(wpid, &st);
                }
            } else {
                code = -2;
                if (wpid > 0) {
                    guide10_force_wake(wpid);
                    selftest_wait_child(wpid, &st);
                }
            }
        }
        if (ok) {
            pass++;
            kprintf("[guide10] m5 ring-3 round trip PASS (probe clean, cross-process wake=1, waiter exit 0)\n");
        } else {
            fail++;
            kprintf("[guide10] m5 ring-3 round trip FAIL code=%d\n", code);
        }
    }

    kprintf("[guide10][test] done pass=%d fail=%d\n", pass, fail);
    process_exit((fail == 0) ? 0 : 1);
}

void process_run_guide10_selftests(void)
{
    process_spawn_kernel("guide10-selftest", guide10_selftest_entry);
}

/* AF_UNIX sockets: rendezvous (bind/listen/connect/accept), byte-exact */
/* data both ways, error paths, fd-teardown cleanup, and the ring-3 */
/* flat-table glue via /fat12/usock20.elf (futex10/m5 precedent). Runs */
/* as a spawned process (guide10 pattern) because M2/M3 need the */
/* scheduler. Idempotent: socket paths are RAM-registry entries that */
/* die with their fds; every path bound here is released here. */

#define G20_TICKS 500

static volatile int g20_srv_rc = -1000;
static volatile int g20_cli_rc = -1000;
static volatile int g20_bind_rc = -1000;

static void guide20_srv(void)
{
    int lfd = vfs_socket_create();
    int afd = -EAGAIN;
    char buf[8];
    int rc = 1;
    uint64_t t0 = g_sched_ticks;

    if (lfd >= 0 && vfs_socket_bind(lfd, "/tmp/g20-echo.sock") == 0 &&
        vfs_socket_listen(lfd) == 0) {
        while (afd == -EAGAIN && g_sched_ticks - t0 < G20_TICKS) {
            afd = vfs_socket_accept(lfd);
            if (afd == -EAGAIN)
                process_yield();
        }
        if (afd >= 0) {
            int got = 0;
            t0 = g_sched_ticks;
            while (got < 4 && g_sched_ticks - t0 < G20_TICKS) {
                size_t r = vfs_read(afd, buf + got, (size_t)(4 - got));
                if (r > 0)
                    got += (int)r;
                else
                    process_yield();
            }
            if (got == 4 && buf[0] == 'p' && buf[1] == 'i' &&
                buf[2] == 'n' && buf[3] == 'g' &&
                vfs_write(afd, "pong", 4) == 4)
                rc = 0;
            vfs_close(afd);
        }
    }
    if (lfd >= 0)
        vfs_close(lfd);
    g20_srv_rc = rc;
    process_exit(rc);
}

static void guide20_cli(void)
{
    int fd = vfs_socket_create();
    char buf[8];
    int rc = 1;
    int con = -1;
    uint64_t t0 = g_sched_ticks;

    if (fd >= 0) {
        /* The server may not have bound yet: retry while refused. */
        while (con != 0 && g_sched_ticks - t0 < G20_TICKS) {
            con = vfs_socket_connect(fd, "/tmp/g20-echo.sock");
            if (con != 0)
                process_yield();
        }
        if (con == 0 && vfs_write(fd, "ping", 4) == 4) {
            int got = 0;
            t0 = g_sched_ticks;
            while (got < 4 && g_sched_ticks - t0 < G20_TICKS) {
                size_t r = vfs_read(fd, buf + got, (size_t)(4 - got));
                if (r > 0)
                    got += (int)r;
                else
                    process_yield();
            }
            if (got == 4 && buf[0] == 'p' && buf[1] == 'o' &&
                buf[2] == 'n' && buf[3] == 'g')
                rc = 0;
        }
        vfs_close(fd);
    }
    g20_cli_rc = rc;
    process_exit(rc);
}

/* Binds + listens, then exits WITHOUT closing: process teardown (vfs_process_cleanup -> file_release) must... */
static void guide20_binder(void)
{
    int fd = vfs_socket_create();

    g20_bind_rc = (fd >= 0 &&
                   vfs_socket_bind(fd, "/tmp/g20-dead.sock") == 0 &&
                   vfs_socket_listen(fd) == 0) ? 0 : 1;
    process_exit(0);
}

static int guide20_spawn_cli(const char *args, const char *name)
{
    int pid = -1;
    for (int i = 0; i < 200 && pid < 0; i++) {
        pid = elf64_spawn_cmdline("/fat12/usock20.elf", args, name);
        if (pid < 0)
            process_yield();
    }
    return pid;
}

static void guide20_selftest_entry(void)
{
    int pass = 0;
    int fail = 0;
    int st;

    /* Wait for phase2 (blocking futex, not a yield-spin): kernel_futex_wait returns 0 on wake or -EAGAIN if... */
    while (g_phase2_done_w == 0) {
        if (kernel_futex_wait(&g_phase2_done_w, 0) == -EAGAIN)
            break;
    }

    kprintf("[guide20][test] start\n");

    {
        int step = 0;
        int lfd = -1, cfd = -1, afd = -1, xfd = -1;
        char buf[8];

        do {
            step = 1;
            lfd = vfs_socket_create();
            if (lfd < 0)
                break;
            step = 2;
            if (vfs_socket_bind(lfd, "/tmp/g20-m1.sock") != 0)
                break;
            step = 3;
            xfd = vfs_socket_create();
            if (xfd < 0 ||
                vfs_socket_bind(xfd, "/tmp/g20-m1.sock") != -EADDRINUSE)
                break;
            vfs_close(xfd);
            xfd = -1;
            step = 4;
            if (vfs_socket_listen(lfd) != 0)
                break;
            step = 5;
            if (vfs_socket_accept(lfd) != -EAGAIN)
                break;
            step = 6;
            cfd = vfs_socket_create();
            if (cfd < 0 || vfs_socket_connect(cfd, "/tmp/g20-m1.sock") != 0)
                break;
            step = 7;
            {
                vfs_pollfd_t pf;
                pf.fd = lfd;
                pf.events = VFS_POLLIN;
                pf.revents = 0;
                if (vfs_poll(&pf, 1, 0) != 1 || !(pf.revents & VFS_POLLIN))
                    break;
            }
            step = 8;
            afd = vfs_socket_accept(lfd);
            if (afd < 0)
                break;
            step = 9;
            if (vfs_write(cfd, "ping!", 5) != 5 ||
                vfs_read(afd, buf, sizeof(buf)) != 5 ||
                buf[0] != 'p' || buf[4] != '!')
                break;
            step = 10;
            if (vfs_write(afd, "reply", 5) != 5 ||
                vfs_read(cfd, buf, sizeof(buf)) != 5 ||
                buf[0] != 'r' || buf[4] != 'y')
                break;
            step = 11;
            xfd = vfs_socket_create();
            if (xfd < 0 ||
                vfs_socket_connect(xfd, "/tmp/g20-none.sock") != -ECONNREFUSED)
                break;
            vfs_close(xfd);
            xfd = -1;
            step = 12;
            vfs_close(afd);
            afd = -1;
            vfs_close(cfd);
            cfd = -1;
            vfs_close(lfd);
            lfd = -1;
            xfd = vfs_socket_create();
            if (xfd < 0 || vfs_socket_bind(xfd, "/tmp/g20-m1.sock") != 0)
                break;
            vfs_close(xfd);
            xfd = -1;
            step = 0;
        } while (0);

        if (afd >= 0)
            vfs_close(afd);
        if (cfd >= 0)
            vfs_close(cfd);
        if (lfd >= 0)
            vfs_close(lfd);
        if (xfd >= 0)
            vfs_close(xfd);
        if (step == 0) {
            pass++;
            kprintf("[guide20] m1 loopback+errors PASS "
                    "(bind/dup/listen/EAGAIN/POLLIN/2-way/refused/rebind)\n");
        } else {
            fail++;
            kprintf("[guide20] m1 loopback+errors FAIL (step=%d)\n", step);
        }
    }

    {
        process_t *s;
        process_t *c;
        int ok = 0;

        g20_srv_rc = -1000;
        g20_cli_rc = -1000;
        s = process_spawn_kernel("g20-srv", guide20_srv);
        c = s ? process_spawn_kernel("g20-cli", guide20_cli) : NULL;
        if (s && c) {
            selftest_wait_child((int)s->pid, &st);
            selftest_wait_child((int)c->pid, &st);
            ok = (g20_srv_rc == 0 && g20_cli_rc == 0);
        } else if (s) {
            selftest_wait_child((int)s->pid, &st);
        }
        if (ok) {
            pass++;
            kprintf("[guide20] m2 two-process echo PASS (ping->pong)\n");
        } else {
            fail++;
            kprintf("[guide20] m2 two-process echo FAIL srv=%d cli=%d\n",
                    g20_srv_rc, g20_cli_rc);
        }
    }

    {
        process_t *b;
        int ok = 0;

        g20_bind_rc = -1000;
        b = process_spawn_kernel("g20-binder", guide20_binder);
        if (b) {
            int pfd;
            selftest_wait_child((int)b->pid, &st);
            pfd = vfs_socket_create();
            if (g20_bind_rc == 0 && pfd >= 0 &&
                vfs_socket_connect(pfd, "/tmp/g20-dead.sock") ==
                    -ECONNREFUSED &&
                vfs_socket_bind(pfd, "/tmp/g20-dead.sock") == 0)
                ok = 1;
            if (pfd >= 0)
                vfs_close(pfd);
        }
        if (ok) {
            pass++;
            kprintf("[guide20] m3 death-cleanup PASS "
                    "(listener unregistered on owner exit)\n");
        } else {
            fail++;
            kprintf("[guide20] m3 death-cleanup FAIL (bind_rc=%d)\n",
                    g20_bind_rc);
        }
    }

    /* M4 — ring-3 flat-table glue via /fat12/usock20.elf. */
    {
        int ok = 0;
        int code = -1;
        int sig = -1;
        int pid;

        pid = guide20_spawn_cli("usock20 validate", "usock20-val");
        st = -1;
        if (pid > 0 && selftest_wait_child(pid, &st) == pid) {
            code = WAIT_EXIT_CODE(st);
            sig = WAIT_TERM_SIGNAL(st);
        }
        if (code == 0 && sig == 0) {
            pid = guide20_spawn_cli("usock20 loopback", "usock20-loop");
            st = -1;
            code = -1;
            sig = -1;
            if (pid > 0 && selftest_wait_child(pid, &st) == pid) {
                code = WAIT_EXIT_CODE(st);
                sig = WAIT_TERM_SIGNAL(st);
            }
            ok = (code == 0 && sig == 0);
        }
        if (ok) {
            pass++;
            kprintf("[guide20] m4 ring-3 syscalls PASS "
                    "(validate + loopback: clean exit 0)\n");
        } else {
            fail++;
            kprintf("[guide20] m4 ring-3 syscalls FAIL code=%d sig=%d\n",
                    code, sig);
        }
    }

    kprintf("[guide20][test] done pass=%d fail=%d\n", pass, fail);

    g_guide20_done_w = 1;
    kernel_futex_wake(&g_guide20_done_w, 64);

    process_exit((fail == 0) ? 0 : 1);
}

void process_run_guide20_selftests(void)
{
    process_spawn_kernel("guide20-selftest", guide20_selftest_entry);
}

/* Userland display server: a ring-3 compositor (/fat12/wsrv15.elf) that */
/* owns /dev/fb0 + KD_GRAPHICS, listens on an AF_UNIX socket, and */
/* composites client pixels delivered via ipc/shm.c surfaces. A ring-3 */
/* client (/fat12/wapp15.elf) draws a window through the protocol. */
/* Verdicts come back purely through exit codes (usock20 precedent), so */
/* the checks are signal-aware (a #UD/#GP kill has exit byte 0). Runs */
/* after [guide20] (blocks on g_guide20_done_w) so the two socket suites */
/* never overlap. Idempotent: socket path is a RAM registry entry that */
/* dies with its fds; every shm surface is reclaimed; KD_TEXT restored. */

static int guide15_spawn(const char *path, const char *args, const char *name)
{
    int pid = -1;
    for (int i = 0; i < 200 && pid < 0; i++) {
        pid = elf64_spawn_cmdline(path, args, name);
        if (pid < 0)
            process_yield();
    }
    return pid;
}

/* Spawn a compositor + client pair and collect both exit codes and both termination signals. */
static int guide15_run_pair(const char *srv_args, const char *srv_name,
                            const char *cli_path,
                            const char *cli_args, const char *cli_name,
                            int expect_cc,
                            int *sc, int *ss, int *cc, int *cs)
{
    int sp, cp, st;

    *sc = *ss = *cc = *cs = -1;
    sp = guide15_spawn("/fat12/wsrv15.elf", srv_args, srv_name);
    if (sp < 0)
        return -1;
    cp = guide15_spawn(cli_path, cli_args, cli_name);
    if (cp < 0) {
        st = -1;
        selftest_wait_child(sp, &st);
        return -1;
    }

    /* The CLIENT is the timeout authority: every wapp15 loop is bounded and always exits with a verdict, so this... */
    st = -1;
    if (selftest_wait_child(cp, &st) == cp) {
        *cc = WAIT_EXIT_CODE(st);
        *cs = WAIT_TERM_SIGNAL(st);
    }
    if (*cc != expect_cc || *cs != 0)
        process_kill(sp, PROCESS_SIGKILL);
    st = -1;
    if (selftest_wait_child(sp, &st) == sp) {
        *sc = WAIT_EXIT_CODE(st);
        *ss = WAIT_TERM_SIGNAL(st);
    }
    return 0;
}

static void guide15_selftest_entry(void)
{
    int pass = 0;
    int fail = 0;

    /* Sequence after phase2 (timing tests) AND guide20 (socket suite), blocking on each done-futex rather than... */
    while (g_phase2_done_w == 0) {
        if (kernel_futex_wait(&g_phase2_done_w, 0) == -EAGAIN)
            break;
    }
    while (g_guide20_done_w == 0) {
        if (kernel_futex_wait(&g_guide20_done_w, 0) == -EAGAIN)
            break;
    }

    kprintf("[guide15][test] start\n");

    {
        int sc, ss, cc, cs;
        int r = guide15_run_pair("wsrv15 m1", "wsrv15-m1",
                                 "/fat12/wapp15.elf",
                                 "wapp15 handshake", "wapp15-hs", 0,
                                 &sc, &ss, &cc, &cs);
        if (r == 0 && sc == 0 && ss == 0 && cc == 0 && cs == 0) {
            pass++;
            kprintf("[guide15] m1 handshake PASS "
                    "(socket create-surface/reply/destroy round-trip)\n");
        } else {
            fail++;
            kprintf("[guide15] m1 handshake FAIL srv=%d/sig%d cli=%d/sig%d\n",
                    sc, ss, cc, cs);
        }
    }

    /* M2 — shm pixels: client fills its surface, DAMAGEs it, the server composites it to the real framebuffer... */
    {
        int sc, ss, cc, cs;
        int r = guide15_run_pair("wsrv15 m2", "wsrv15-m2",
                                 "/fat12/wapp15.elf",
                                 "wapp15 draw", "wapp15-draw", 0,
                                 &sc, &ss, &cc, &cs);
        if (r == 0 && sc == 20 && ss == 0) {
            kprintf("[guide15] m2 shm pixels SKIPPED (no 32bpp framebuffer)\n");
        } else if (r == 0 && sc == 0 && ss == 0 && cc == 0 && cs == 0) {
            pass++;
            kprintf("[guide15] m2 shm pixels PASS "
                    "(client shm -> composited -> fb readback byte-exact)\n");
        } else {
            fail++;
            kprintf("[guide15] m2 shm pixels FAIL srv=%d/sig%d cli=%d/sig%d\n",
                    sc, ss, cc, cs);
        }
    }

    /* M3 — death cleanup: client draws then dies WITHOUT destroy/close; the server must reclaim the surface +... */
    {
        int sc, ss, cc, cs;
        int r = guide15_run_pair("wsrv15 m3", "wsrv15-m3",
                                 "/fat12/wapp15.elf",
                                 "wapp15 die", "wapp15-die", 0,
                                 &sc, &ss, &cc, &cs);
        if (r == 0 && sc == 0 && ss == 0 && cc == 0 && cs == 0) {
            pass++;
            kprintf("[guide15] m3 death-cleanup PASS "
                    "(HUP -> surface + shm reclaimed, no leak)\n");
        } else {
            fail++;
            kprintf("[guide15] m3 death-cleanup FAIL srv=%d/sig%d cli=%d/sig%d\n",
                    sc, ss, cc, cs);
        }
    }

    kprintf("[guide15][test] done pass=%d fail=%d\n", pass, fail);

    g_guide15_done_w = 1;
    kernel_futex_wake(&g_guide15_done_w, 64);

    process_exit((fail == 0) ? 0 : 1);
}

void process_run_guide15_selftests(void)
{
    process_spawn_kernel("guide15-selftest", guide15_selftest_entry);
}

/* GUI toolkit: the retained widget kit in user/lib/libwidget.c (base */
/* type, enum signals, box/grid layout) is KERNEL-LINKED, so m1 unit- */
/* checks its layout/signal/dispatch math right here — deterministic, */
/* no ring 3 involved. m2 then runs the full ring-3 demo (wdemo16.elf, */
/* self-driving: synthetic events through the same dispatch path real */
/* input uses) against the guide-15 compositor, byte-exact readback and */
/* all. Sequenced after [guide15] via its done-futex. */

#include "../user/include/libwidget.h"

static int g16_sig_log[4];
static int g16_sig_n;
static int g16_btn_clicks;

static void g16_lis_a(ui_widget_t *w, void *ud)
{
    (void)w; (void)ud;
    if (g16_sig_n < 4) g16_sig_log[g16_sig_n++] = 1;
}

static void g16_lis_b(ui_widget_t *w, void *ud)
{
    (void)w; (void)ud;
    if (g16_sig_n < 4) g16_sig_log[g16_sig_n++] = 2;
}

static void g16_btn_click(void *ud)
{
    (void)ud;
    g16_btn_clicks++;
}

/* Returns 0, or the failing step number. */
static int guide16_unit_checks(void)
{
    static ui_box_t box;
    static ui_box_t hbox;
    static ui_grid_t grid;
    static ui_widget_t c1, c2, c3, ga, gb, gc, root;
    static widget_button_t btn;
    ui_tk_event_t ev;

    /* Box packing: fixed | expand | fixed in a 288-wide row, spacing 4, padding 2 each. */
    ui_widget_base_init(&c1, UI_WIDGET_PLAIN, 0, 0, 70, 26);
    ui_widget_base_init(&c2, UI_WIDGET_PLAIN, 0, 0, 70, 26);
    ui_widget_base_init(&c3, UI_WIDGET_PLAIN, 0, 0, 70, 26);
    ui_box_init(&box, UI_HORIZONTAL, 16, 16, 288, 30);
    ui_box_pack(&box, &c1, false, false, 2);
    ui_box_pack(&box, &c2, true, true, 2);
    ui_box_pack(&box, &c3, false, false, 2);
    if (c1.x != 18 || c1.w != 70 || c2.x != 96 || c2.w != 128 ||
        c3.x != 232 || c3.w != 70 || c1.h != 30)
        return 1;

    ui_box_set_geometry(&box, 16, 16, 228, 30);
    if (c2.w != 68)
        return 2;
    ui_box_set_geometry(&box, 16, 16, 288, 30);
    if (c2.w != 128)
        return 2;

    ui_widget_base_init(&ga, UI_WIDGET_PLAIN, 0, 0, 10, 10);
    ui_widget_base_init(&gb, UI_WIDGET_PLAIN, 0, 0, 200, 10);
    ui_widget_base_init(&gc, UI_WIDGET_PLAIN, 0, 0, 55, 10);
    ui_box_init(&hbox, UI_HORIZONTAL, 0, 0, 300, 20);
    hbox.homogeneous = true;
    hbox.spacing = 0;
    ui_box_pack(&hbox, &ga, false, false, 0);
    ui_box_pack(&hbox, &gb, false, false, 0);
    ui_box_pack(&hbox, &gc, false, false, 0);
    if (ga.w != 100 || gb.w != 100 || gc.w != 100 ||
        ga.x != 0 || gb.x != 100 || gc.x != 200)
        return 3;

    ui_widget_base_init(&ga, UI_WIDGET_PLAIN, 0, 0, 10, 10);
    ui_widget_base_init(&gb, UI_WIDGET_PLAIN, 0, 0, 10, 10);
    ui_widget_base_init(&gc, UI_WIDGET_PLAIN, 0, 0, 10, 10);
    ui_grid_init(&grid, 2, 2, 4, 8, 8, 204, 104);
    ui_grid_attach(&grid, &ga, 0, 0);
    ui_grid_attach(&grid, &gb, 0, 1);
    ui_grid_attach(&grid, &gc, 1, 0);
    if (ga.x != 8 || ga.y != 8 || ga.w != 100 || ga.h != 50 ||
        gb.x != 112 || gb.y != 8 || gc.x != 8 || gc.y != 62)
        return 4;

    ui_widget_base_init(&root, UI_WIDGET_PLAIN, 0, 0, 100, 100);
    g16_sig_n = 0;
    if (ui_signal_connect(&root, UI_SIGNAL_CLICKED, g16_lis_a, 0) != 0 ||
        ui_signal_connect(&root, UI_SIGNAL_CLICKED, g16_lis_b, 0) != 0)
        return 5;
    if (ui_signal_emit(&root, UI_SIGNAL_CLICKED) != 2 ||
        g16_sig_n != 2 || g16_sig_log[0] != 1 || g16_sig_log[1] != 2)
        return 5;
    if (ui_signal_disconnect(&root, UI_SIGNAL_CLICKED, g16_lis_a) != 0)
        return 6;
    g16_sig_n = 0;
    if (ui_signal_emit(&root, UI_SIGNAL_CLICKED) != 1 ||
        g16_sig_n != 1 || g16_sig_log[0] != 2)
        return 6;

    /* Tree dispatch: press+release over a child button fires on_click AND the CLICKED signal through the same... */
    ui_widget_base_init(&root, UI_WIDGET_PLAIN, 0, 0, 200, 100);
    widget_button_init(&btn, 10, 10, 50, 20, "ok");
    btn.on_click = g16_btn_click;
    g16_sig_n = 0;
    if (ui_signal_connect(&btn.base, UI_SIGNAL_CLICKED, g16_lis_b, 0) != 0)
        return 7;
    if (ui_widget_add_child(&root, &btn.base) != 0)
        return 7;
    g16_btn_clicks = 0;
    ev.type = UI_TK_EV_POINTER;
    ev.x = 35; ev.y = 20;
    ev.buttons = 1; ev.down = true; ev.clicked = true;
    ev.keycode = 0; ev.pressed = false;
    if (!ui_widget_dispatch(&root, &ev) || !btn.pressed)
        return 7;
    ev.buttons = 0; ev.down = false; ev.clicked = false;
    if (!ui_widget_dispatch(&root, &ev) ||
        g16_btn_clicks != 1 || g16_sig_n != 1 || g16_sig_log[0] != 2)
        return 7;

    {
        static ui_gradient_t g;
        ui_gradient_init(&g, true);
        ui_gradient_add_stop(&g, 0, 0xFF000000u);
        ui_gradient_add_stop(&g, 100, 0xFF0000FFu);
        if (ui_gradient_sample(&g, 0) != 0xFF000000u ||
            ui_gradient_sample(&g, 100) != 0xFF0000FFu ||
            ui_gradient_sample(&g, 50) != 0xFF00007Fu)
            return 8;
    }
    return 0;
}

static void guide16_selftest_entry(void)
{
    int pass = 0;
    int fail = 0;

    while (g_phase2_done_w == 0) {
        if (kernel_futex_wait(&g_phase2_done_w, 0) == -EAGAIN)
            break;
    }
    while (g_guide15_done_w == 0) {
        if (kernel_futex_wait(&g_guide15_done_w, 0) == -EAGAIN)
            break;
    }

    kprintf("[guide16][test] start\n");

    /* M1 — kernel-side unit checks: libwidget.c is linked into the kernel, so the layout/signal/dispatch math is... */
    {
        int step = guide16_unit_checks();
        if (step == 0) {
            pass++;
            kprintf("[guide16] m1 layout+signals PASS "
                    "(box/homogeneous/grid/signals/dispatch/gradient)\n");
        } else {
            fail++;
            kprintf("[guide16] m1 layout+signals FAIL (step=%d)\n", step);
        }
    }

    /* M2 — the ring-3 toolkit demo on the display server: buttons + text field + modal dialog + gradient... */
    {
        int sc, ss, cc, cs;
        int r = guide15_run_pair("wsrv15 m2", "wsrv15-g16",
                                 "/fat12/wdemo16.elf",
                                 "wdemo16 selftest", "wdemo16-st", 42,
                                 &sc, &ss, &cc, &cs);
        if (r == 0 && sc == 20 && ss == 0) {
            kprintf("[guide16] m2 toolkit demo SKIPPED (no 32bpp framebuffer)\n");
        } else if (r == 0 && sc == 0 && ss == 0 && cc == 42 && cs == 0) {
            pass++;
            kprintf("[guide16] m2 toolkit demo PASS "
                    "(buttons/field/dialog/timer/clipboard/painter, "
                    "server readback byte-exact)\n");
        } else {
            fail++;
            kprintf("[guide16] m2 toolkit demo FAIL srv=%d/sig%d cli=%d/sig%d\n",
                    sc, ss, cc, cs);
        }
    }

    kprintf("[guide16][test] done pass=%d fail=%d\n", pass, fail);
    process_exit((fail == 0) ? 0 : 1);
}

void process_run_guide16_selftests(void)
{
    process_spawn_kernel("guide16-selftest", guide16_selftest_entry);
}
