/*
 * task.h - Task Control Block and process model.
 */

#ifndef TASK_H
#define TASK_H

#include <stdint.h>

/** Task states. */
#define TASK_READY   0
#define TASK_RUNNING 1
#define TASK_BLOCKED 2
#define TASK_DEAD    3

/** Task is user-mode (Ring 3). */
#define TASK_FLAG_USER (1u << 8)

/** Default kernel stack size per task (8 KiB). */
#define TASK_STACK_SIZE 8192

/** Task Control Block. */
struct task {
    uint32_t pid;
    uint32_t esp;           /**< Kernel stack pointer. */
    uintptr_t stack_base;   /**< Base of kernel stack (for free). */
    uintptr_t page_dir;     /**< Physical address of page directory. */
    uint32_t flags;         /**< TASK_FLAG_USER etc. */
    uint32_t user_eip;      /**< User entry (for first switch). */
    uint32_t user_esp;      /**< User stack pointer. */
    uint8_t state;
    struct task *next;
};

typedef struct task task_t;

/**
 * Initialize the task subsystem. Creates the idle task.
 */
void task_init(void);

/**
 * Create a new kernel task.
 *
 * @param entry Entry point (function pointer).
 * @return New task, or NULL on failure.
 */
task_t *task_create(void (*entry)(void));

/**
 * Create a user-mode (Ring 3) task.
 *
 * @param entry_addr Virtual address of user entry point.
 * @param stack_addr Virtual address of user stack (top).
 * @return New task, or NULL on failure.
 */
task_t *task_create_user(uint32_t entry_addr, uint32_t stack_addr);

/**
 * Get the currently running task.
 *
 * @return Current task, or NULL if none.
 */
task_t *task_current(void);

/**
 * Set the current task (called from scheduler).
 */
void task_set_current(task_t *t);

/**
 * Add task to the ready queue.
 */
void task_ready(task_t *t);

/**
 * Get the next ready task (round-robin).
 *
 * @return Next task, or NULL if none.
 */
task_t *task_next_ready(void);

/**
 * Yield to the next ready task.
 */
void task_yield(void);

#endif /* TASK_H */
