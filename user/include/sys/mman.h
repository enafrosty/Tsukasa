#ifndef TSUKASA_SYS_MMAN_H
#define TSUKASA_SYS_MMAN_H

#include "types.h"
#include "../syscall_nums.h"

#define PROT_READ  TSUKASA_PROT_READ
#define PROT_WRITE TSUKASA_PROT_WRITE

#define MAP_SHARED  TSUKASA_MAP_SHARED
#define MAP_PRIVATE TSUKASA_MAP_PRIVATE
#define MAP_FAILED  ((void *)-1)

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int munmap(void *addr, size_t length);

#endif /* TSUKASA_SYS_MMAN_H */
