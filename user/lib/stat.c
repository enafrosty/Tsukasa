#include "../include/sys/stat.h"

#include "../lib/syscall.h"
#include "../include/syscall_nums.h"

static mode_t map_mode(const struct tsukasa_stat *in)
{
    mode_t mode = 0;
    if (!in)
        return 0;
    if (in->type == TSUKASA_STAT_TYPE_DIR)
        mode |= S_IFDIR;
    else if (in->type == TSUKASA_STAT_TYPE_CHAR)
        mode |= S_IFCHR;
    else if (in->type == TSUKASA_STAT_TYPE_PIPE)
        mode |= S_IFIFO;
    else
        mode |= S_IFREG;

    if (in->mode & TSUKASA_O_RDONLY)
        mode |= S_IRUSR | S_IRGRP | S_IROTH;
    if (in->mode & TSUKASA_O_WRONLY)
        mode |= S_IWUSR | S_IWGRP | S_IWOTH;
    return mode;
}

int stat(const char *pathname, struct stat *statbuf)
{
    struct tsukasa_stat st;
    if (!pathname || !statbuf)
        return -1;
    if (fs_stat(pathname, &st) != 0)
        return -1;
    statbuf->st_mode = map_mode(&st);
    statbuf->st_size = (off_t)st.size;
    statbuf->st_blocks = st.blocks;
    return 0;
}

int fstat(int fd, struct stat *statbuf)
{
    struct tsukasa_stat st;
    if (!statbuf)
        return -1;
    if (fs_fstat(fd, &st) != 0)
        return -1;
    statbuf->st_mode = map_mode(&st);
    statbuf->st_size = (off_t)st.size;
    statbuf->st_blocks = st.blocks;
    return 0;
}
