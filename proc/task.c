/*
 * task.c - Task creation and management.
 */

#include "task.h"
#include "../mm/heap.h"
#include "../mm/pmm.h"
#include "../include/paging.h"
#include <stddef.h>
#include <stdint.h>

static task_t *current_task;
static task_t *ready_list;
static uint32_t next_pid;

extern void context_switch(uint32_t *save_esp, uint32_t next_esp);
extern void switch_to_user(uint32_t eip, uint32_t esp, uint32_t eflags);

static void idle_task(void);

void task_init(void)
{
    current_task = NULL;
    ready_list = NULL;
    next_pid = 1;

    /* Create idle task. */
    task_t *idle = task_create(idle_task);
    if (idle) {
        idle->pid = 0;
        task_ready(idle);
    }
}

void idle_task(void)
{
    for (;;) {
        __asm__ volatile ("hlt");
        task_yield();
    }
}

task_t *task_create(void (*entry)(void))
{
    uintptr_t stack_phys = pmm_alloc_pages(TASK_STACK_SIZE / PAGE_SIZE);
    if (stack_phys == 0)
        return NULL;

    task_t *t = (task_t *)kmalloc(sizeof(task_t));
    if (!t) {
        pmm_free_pages(stack_phys, TASK_STACK_SIZE / PAGE_SIZE);
        return NULL;
    }

    t->pid = next_pid++;
    t->stack_base = stack_phys;
    t->page_dir = paging_get_current_pd();
    t->flags = 0;
    t->state = TASK_READY;
    t->next = NULL;

    /* Set up stack: at top of stack, push entry point so ret jumps there. */
    uint32_t *stack_top = (uint32_t *)(stack_phys + TASK_STACK_SIZE - 4);
    stack_top[0] = (uint32_t)entry;
    t->esp = (uint32_t)stack_top;

    return t;
}

task_t *task_create_user(uint32_t entry_addr, uint32_t stack_addr)
{
    uintptr_t stack_phys = pmm_alloc_pages(TASK_STACK_SIZE / PAGE_SIZE);
    if (stack_phys == 0)
        return NULL;

    task_t *t = (task_t *)kmalloc(sizeof(task_t));
    if (!t) {
        pmm_free_pages(stack_phys, TASK_STACK_SIZE / PAGE_SIZE);
        return NULL;
    }

    t->pid = next_pid++;
    t->stack_base = stack_phys;
    t->page_dir = paging_get_current_pd();
    t->flags = TASK_FLAG_USER;
    t->user_eip = entry_addr;
    t->user_esp = stack_addr;
    t->state = TASK_READY;
    t->next = NULL;

    /* ESP for first switch: point to top of kernel stack with iret frame. */
    uint32_t *stack_top = (uint32_t *)(stack_phys + TASK_STACK_SIZE);
    stack_top -= 5;  /* iret frame: eip, cs, eflags, esp, ss */
    stack_top[0] = entry_addr;
    stack_top[1] = 0x1B;
    stack_top[2] = 0x202;   /* eflags with IF=1 */
    stack_top[3] = stack_addr;
    stack_top[4] = 0x23;
    t->esp = (uint32_t)stack_top;

    return t;
}

task_t *task_current(void)
{
    return current_task;
}

void task_set_current(task_t *t)
{
    current_task = t;
    if (t)
        t->state = TASK_RUNNING;
}

void task_ready(task_t *t)
{
    if (!t)
        return;
    t->state = TASK_READY;
    t->next = ready_list;
    ready_list = t;
}

task_t *task_next_ready(void)
{
    if (!ready_list)
        return NULL;
    task_t *t = ready_list;
    ready_list = ready_list->next;
    t->next = NULL;
    return t;
}

void task_yield(void)
{
    task_t *cur = current_task;
    task_t *next = task_next_ready();
    if (!next)
        return;
    if (cur)
        task_ready(cur);
    task_set_current(next);
    context_switch(&cur->esp, next->esp);
}
