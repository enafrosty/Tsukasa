#include "../include/sys/ioctl.h"
#include "../lib/syscall.h"

#include <stdarg.h>

int ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    uintptr_t arg;
    va_start(ap, request);
    arg = va_arg(ap, uintptr_t);
    va_end(ap);
    return fs_ioctl(fd, request, (void *)arg);
}
