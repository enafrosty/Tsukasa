/*
 * syscall.c - Syscall dispatcher.
 */

#include "syscall.h"
#include "../proc/task.h"
#include "../ipc/shm.h"
#include <stdint.h>

uint32_t syscall_handler(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3)
{
    uint32_t result = 0;
    (void)arg2;
    (void)arg3;

    switch (num) {
    case SYS_YIELD:
        task_yield();
        break;
    case SYS_EXIT:
        /* Mark task dead, yield to next. */
        if (task_current())
            task_current()->state = TASK_DEAD;
        task_yield();
        break;
    case SYS_SHM_CREATE:
        result = (uint32_t)shm_create(arg1);
        break;
    case SYS_SHM_ATTACH:
        result = (uint32_t)(uintptr_t)shm_attach((int)arg1);
        break;
    case SYS_SHM_DETACH:
        shm_detach((void *)(uintptr_t)arg1);
        break;
    case SYS_SHM_DESTROY:
        result = (uint32_t)shm_destroy((int)arg1);
        break;
    default:
        result = (uint32_t)-1;
        break;
    }
    return result;
}
