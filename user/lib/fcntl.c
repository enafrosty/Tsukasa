#include "../include/fcntl.h"

#include "../lib/syscall.h"

#include <stdarg.h>

int open(const char *pathname, int flags, ...)
{
    /*
     * Tsukasa VFS currently ignores mode_t for O_CREAT. Keep varargs ABI
     * compatible while forwarding flags directly.
     */
    return fs_open(pathname, flags);
}

int fcntl(int fd, int cmd, ...)
{
    va_list ap;
    int arg = 0;
    va_start(ap, cmd);
    arg = va_arg(ap, int);
    va_end(ap);
    return fs_fcntl(fd, cmd, arg);
}
