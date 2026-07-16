/*
 * scheduler.h - Scheduler interface.
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

/**
 * Enter the scheduler and start multitasking. Never returns.
 */
void scheduler_run(void);

#ifdef __x86_64__
uint64_t scheduler_tick(uint64_t current_rsp);
void scheduler_init(void);
#endif

#endif /* SCHEDULER_H */
