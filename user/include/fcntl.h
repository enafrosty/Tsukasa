#ifndef TSUKASA_FCNTL_H
#define TSUKASA_FCNTL_H

#include "sys/types.h"

#define O_RDONLY   0x0001
#define O_WRONLY   0x0002
#define O_RDWR     (O_RDONLY | O_WRONLY)
#define O_APPEND   0x0004
#define O_CREAT    0x0008
#define O_TRUNC    0x0010
#define O_NONBLOCK 0x0020

#define F_GETFL 1
#define F_SETFL 2

int open(const char *pathname, int flags, ...);
int fcntl(int fd, int cmd, ...);

#endif /* TSUKASA_FCNTL_H */
