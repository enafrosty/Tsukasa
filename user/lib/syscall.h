/*
 * syscall.h - Syscall wrapper interface for user apps.
 */

#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

#include <stddef.h>

void exit(int code);
void yield(void);
int shm_create(size_t size);
void *shm_attach(int id);
void shm_detach(void *addr);
int shm_destroy(int id);

#endif /* USER_SYSCALL_H */
