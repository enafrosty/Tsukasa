#ifndef TSUKASA_SYS_POLL_H
#define TSUKASA_SYS_POLL_H

#include "types.h"
#include "../syscall_nums.h"

typedef unsigned long nfds_t;

#define POLLIN  TSUKASA_POLLIN
#define POLLOUT TSUKASA_POLLOUT
#define POLLERR TSUKASA_POLLERR
#define POLLHUP TSUKASA_POLLHUP

struct pollfd {
    int fd;
    short events;
    short revents;
};

int poll(struct pollfd *fds, nfds_t nfds, int timeout);

#endif /* TSUKASA_SYS_POLL_H */
