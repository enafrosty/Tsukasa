/*
 * scheduler.c - Scheduler compatibility layer.
 */

#include "scheduler.h"

#ifdef __x86_64__

#include "process.h"

void scheduler_init(void)
{
    process_init();
}

uint64_t scheduler_tick(uint64_t current_rsp)
{
    return process_schedule_tick(current_rsp);
}

void scheduler_run(void)
{
    process_start_scheduler();
}

#else

#include "task.h"

extern void context_switch(uint32_t *save_esp, uint32_t next_esp);
extern void switch_to_user(uint32_t eip, uint32_t esp, uint32_t eflags);

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

#endif

