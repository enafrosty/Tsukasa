#ifndef TSUKASA_SYS_STAT_H
#define TSUKASA_SYS_STAT_H

#include "types.h"

#define S_IFMT   0170000
#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000

#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IROTH 0004
#define S_IWOTH 0002

#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)

struct stat {
    mode_t st_mode;
    off_t st_size;
    uint64_t st_blocks;
};

int stat(const char *pathname, struct stat *statbuf);
int fstat(int fd, struct stat *statbuf);

#endif /* TSUKASA_SYS_STAT_H */
