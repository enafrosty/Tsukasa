/*
 * syscall.c - Syscall wrappers (int 0x80).
 */

#include "syscall.h"
#include "../include/syscall_nums.h"

static long syscall1(long n, long a1)
{
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "b"(a1)
        : "memory"
    );
    return ret;
}

static long syscall2(long n, long a1, long a2)
{
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "b"(a1), "c"(a2)
        : "memory"
    );
    return ret;
}

void exit(int code)
{
    (void)code;
    syscall1(SYS_EXIT, 0);
    __builtin_unreachable();
}

void yield(void)
{
    syscall1(SYS_YIELD, 0);
}

int shm_create(size_t size)
{
    return (int)syscall1(SYS_SHM_CREATE, (long)size);
}

void *shm_attach(int id)
{
    return (void *)(long)syscall1(SYS_SHM_ATTACH, id);
}

void shm_detach(void *addr)
{
    syscall1(SYS_SHM_DETACH, (long)addr);
}

int shm_destroy(int id)
{
    return (int)syscall1(SYS_SHM_DESTROY, id);
}
