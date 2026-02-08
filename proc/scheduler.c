/*
 * scheduler.c - Round-robin scheduler.
 */

#include "task.h"
#include <stddef.h>

extern void context_switch(uint32_t *save_esp, uint32_t next_esp);

/**
 * Enter the scheduler. Switches to the first ready task and never returns.
 * Call with interrupts disabled. The bootstrap context is saved and we
 * switch to the first task in the ready queue.
 */
void scheduler_run(void)
{
    task_t *next = task_next_ready();
    if (!next) {
        for (;;)
            __asm__ volatile ("hlt");
    }
    task_set_current(next);
    if (next->flags & TASK_FLAG_USER) {
        switch_to_user(next->user_eip, next->user_esp, 0x202);
    } else {
        uint32_t bootstrap_esp;
        __asm__ volatile ("movl %%esp, %0" : "=m"(bootstrap_esp));
        context_switch(&bootstrap_esp, next->esp);
    }
    __builtin_unreachable();
}
