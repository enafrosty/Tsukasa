#include "../include/sys/mman.h"
#include "../lib/syscall.h"

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    if (offset < 0)
        return MAP_FAILED;
    return fs_mmap(addr, length, prot, flags, fd, (size_t)offset);
}

int munmap(void *addr, size_t length)
{
    return fs_munmap(addr, length);
}
