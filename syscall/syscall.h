/*
 * syscall.h - Syscall mechanism and numbers.
 */

#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

/** Syscall numbers. */
#define SYS_YIELD   0
#define SYS_EXIT    1
#define SYS_SHM_CREATE  2
#define SYS_SHM_ATTACH  3
#define SYS_SHM_DETACH  4
#define SYS_SHM_DESTROY 5

/**
 * Syscall handler. Called from assembly with eax=num, ebx=arg1, ecx=arg2, edx=arg3.
 * Returns value in eax (via C return value).
 */
uint32_t syscall_handler(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3);

#endif /* SYSCALL_H */
