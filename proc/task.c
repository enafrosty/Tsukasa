/*
 * task.c - Task creation and management.
 */

#include "task.h"
#include "../mm/heap.h"
#include "../mm/pmm.h"
#include "../include/paging.h"
#include "../vga.h"
#include "../drv/fb.h"
#include "../gfx/blit.h"
#include "../gfx/font.h"
#include <stddef.h>
#include <stdint.h>

static task_t *current_task;
static task_t *ready_list;
static uint32_t next_pid;

extern void context_switch(uint32_t *save_esp, uint32_t next_esp);
extern void switch_to_user(uint32_t eip, uint32_t esp, uint32_t eflags);

static void idle_task(void);
void main_kernel_task(void);

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

void main_kernel_task(void)
{
    /* Text banner in VGA. */
    vga_puts_row(0, "Tsukasa kernel main task");

    /* If we have a 32bpp framebuffer, draw a simple desktop-style UI. */
    if (fb_info.addr && fb_info.bpp == 32) {
        /* Dark background. */
        fb_fill_rect(0, 0, fb_info.width, fb_info.height, rgb(24, 24, 40));

        /* Taskbar at bottom. */
        int bar_h = 32;
        fb_fill_rect(0, fb_info.height - bar_h, fb_info.width, bar_h, rgb(16, 16, 28));

        /* Simple window in the center. */
        int win_w = 400;
        int win_h = 260;
        int win_x = (int)(fb_info.width - win_w) / 2;
        int win_y = (int)(fb_info.height - win_h) / 2;

        /* Window shadow and border. */
        fb_fill_rect(win_x + 6, win_y + 6, win_w, win_h, rgb(10, 10, 20));      /* shadow */
        fb_fill_rect(win_x, win_y, win_w, win_h, rgb(220, 220, 235));           /* body */
        fb_fill_rect(win_x, win_y, win_w, 28, rgb(60, 90, 160));                /* title bar */

        /* Title text. */
        fb_draw_string(win_x + 12, win_y + 8, "Tsukasa Desktop", rgb(255, 255, 255),
                       rgb(60, 90, 160));

        /* Content text. */
        fb_draw_string(win_x + 16, win_y + 48,
                       "Welcome to Tsukasa.\nThis is a simple framebuffer UI.",
                       rgb(20, 20, 40), rgb(220, 220, 235));
    }
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
