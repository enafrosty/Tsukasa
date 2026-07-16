#include "../include/sys/poll.h"
#include "../lib/syscall.h"

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    return fs_poll((struct tsukasa_pollfd *)fds, (size_t)nfds, timeout);
}
