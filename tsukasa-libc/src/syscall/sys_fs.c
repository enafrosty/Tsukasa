/*
 * Project Tsukasa — File and Directory System Call Wrappers
 *
 * Copyright (C) 2025-2026 frosty (@enafrosty) and Project Tsukasa contributors.
 *
 * Project Tsukasa was created and is maintained by frosty (@enafrosty).
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. See the top-level LICENSE file.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include "syscall_internal.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

ssize_t read(int fd, void *buf, size_t count)
{
    return (ssize_t)__syscall_check(__syscall3(SYS_read, fd, (int64_t)(uintptr_t)buf, (int64_t)count));
}

ssize_t write(int fd, const void *buf, size_t count)
{
    return (ssize_t)__syscall_check(__syscall3(SYS_write, fd, (int64_t)(uintptr_t)buf, (int64_t)count));
}

int open(const char *pathname, int flags, ...)
{
    return (int)__syscall_check(__syscall2(SYS_open, (int64_t)(uintptr_t)pathname, (int64_t)flags));
}

int close(int fd)
{
    return (int)__syscall_check(__syscall1(SYS_close, fd));
}

int fsync(int fd)
{
    (void)fd;
    return 0;
}

off_t lseek(int fd, off_t offset, int whence)
{
    return (off_t)__syscall_check(__syscall3(SYS_lseek, fd, offset, whence));
}

int dup(int oldfd)
{
    return (int)__syscall_check(__syscall1(SYS_dup, oldfd));
}

int dup2(int oldfd, int newfd)
{
    return (int)__syscall_check(__syscall2(SYS_dup2, oldfd, newfd));
}

int pipe(int pipefd[2])
{
    return (int)__syscall_check(__syscall1(SYS_pipe, (int64_t)(uintptr_t)pipefd));
}

int fcntl(int fd, int cmd, ...)
{
    void *arg = NULL;
    __builtin_va_list ap;
    __builtin_va_start(ap, cmd);
    arg = __builtin_va_arg(ap, void *);
    __builtin_va_end(ap);
    return (int)__syscall_check(__syscall3(SYS_fcntl, (int64_t)fd, (int64_t)cmd, (int64_t)(uintptr_t)arg));
}

int stat(const char *pathname, struct stat *statbuf)
{
    struct tsukasa_stat kst;
    if (!pathname || !statbuf) {
        errno = EFAULT;
        return -1;
    }
    int64_t ret = __syscall2(SYS_stat, (int64_t)(uintptr_t)pathname, (int64_t)(uintptr_t)&kst);
    if (ret < 0) {
        errno = (int)(-ret);
        return -1;
    }
    statbuf->st_dev = 0;
    statbuf->st_ino = 0;
    statbuf->st_nlink = 1;
    statbuf->st_uid = 0;
    statbuf->st_gid = 0;
    statbuf->st_rdev = 0;
    statbuf->st_size = (off_t)kst.size;
    statbuf->st_blocks = kst.blocks;
    statbuf->st_atime = 0;
    statbuf->st_mtime = 0;
    statbuf->st_ctime = 0;

    mode_t mode = 0;
    if (kst.type == 2)
        mode |= S_IFDIR;
    else if (kst.type == 4)
        mode |= S_IFCHR;
    else if (kst.type == 3)
        mode |= S_IFIFO;
    else if (kst.type == 6)
        mode |= S_IFSOCK;
    else
        mode |= S_IFREG;

    if (kst.mode & 0x0001)
        mode |= S_IRUSR | S_IRGRP | S_IROTH;
    if (kst.mode & 0x0002)
        mode |= S_IWUSR | S_IWGRP | S_IWOTH;
    statbuf->st_mode = mode;
    return 0;
}

int fstat(int fd, struct stat *statbuf)
{
    if (!statbuf) {
        errno = EFAULT;
        return -1;
    }
    int64_t sz = __syscall1(SYS_fsize, fd);
    if (sz < 0) {
        errno = (int)(-sz);
        return -1;
    }
    statbuf->st_dev = 0;
    statbuf->st_ino = 0;
    statbuf->st_mode = S_IFREG | S_IRUSR | S_IWUSR;
    statbuf->st_nlink = 1;
    statbuf->st_uid = 0;
    statbuf->st_gid = 0;
    statbuf->st_rdev = 0;
    statbuf->st_size = (off_t)sz;
    statbuf->st_blocks = (sz + 511) / 512;
    statbuf->st_atime = 0;
    statbuf->st_mtime = 0;
    statbuf->st_ctime = 0;
    return 0;
}

char *getcwd(char *buf, size_t size)
{
    if (!buf || size == 0) {
        errno = EINVAL;
        return NULL;
    }
    int64_t ret = __syscall2(SYS_getcwd, (int64_t)(uintptr_t)buf, (int64_t)size);
    if (ret < 0) {
        errno = (int)(-ret);
        return NULL;
    }
    return buf;
}

int chdir(const char *path)
{
    return (int)__syscall_check(__syscall1(SYS_chdir, (int64_t)(uintptr_t)path));
}

int unlink(const char *pathname)
{
    return (int)__syscall_check(__syscall1(SYS_unlink, (int64_t)(uintptr_t)pathname));
}

int rmdir(const char *pathname)
{
    return (int)__syscall_check(__syscall1(SYS_rmdir, (int64_t)(uintptr_t)pathname));
}

int mkdir(const char *pathname, mode_t mode)
{
    return (int)__syscall_check(__syscall2(SYS_mkdir, (int64_t)(uintptr_t)pathname, (int64_t)mode));
}

int rename(const char *oldpath, const char *newpath)
{
    return (int)__syscall_check(__syscall2(SYS_rename, (int64_t)(uintptr_t)oldpath, (int64_t)(uintptr_t)newpath));
}

int list_dir(const char *dir, char names[][64], int max)
{
    return (int)__syscall_check(__syscall3(SYS_list, (int64_t)(uintptr_t)dir,
                                           (int64_t)(uintptr_t)names, (int64_t)max));
}

