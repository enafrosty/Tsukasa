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
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/mount.h>
#include <sys/stat.h>

#define TSUKASA_DIR_BUFSZ 2048

struct __tsukasa_dir {
    int fd;
    size_t pos;
    size_t len;
    uint8_t buf[TSUKASA_DIR_BUFSZ];
    struct dirent cur;
};

static void fill_stat_from_tsukasa(struct stat *statbuf, const struct tsukasa_stat *kst)
{
    statbuf->st_dev = (dev_t)kst->st_dev;
    statbuf->st_ino = (ino_t)kst->st_ino;
    statbuf->st_nlink = (nlink_t)(kst->st_nlink ? kst->st_nlink : 1U);
    statbuf->st_uid = (uid_t)kst->st_uid;
    statbuf->st_gid = (gid_t)kst->st_gid;
    statbuf->st_rdev = (dev_t)kst->st_rdev;
    statbuf->st_size = (off_t)kst->size;
    statbuf->st_blksize = kst->st_blksize ? kst->st_blksize : 4096U;
    statbuf->st_blocks = kst->blocks;
    statbuf->st_atime = (time_t)kst->st_atime;
    statbuf->st_mtime = (time_t)kst->st_mtime;
    statbuf->st_ctime = (time_t)kst->st_ctime;

    if (kst->st_mode != 0) {
        statbuf->st_mode = (mode_t)kst->st_mode;
        return;
    }

    mode_t mode = 0;
    if (kst->type == 2)
        mode |= S_IFDIR;
    else if (kst->type == 4)
        mode |= S_IFCHR;
    else if (kst->type == 3)
        mode |= S_IFIFO;
    else if (kst->type == 6)
        mode |= S_IFSOCK;
    else if (kst->type == 7)
        mode |= S_IFLNK;
    else
        mode |= S_IFREG;

    if (kst->mode & 0x0001)
        mode |= S_IRUSR | S_IRGRP | S_IROTH;
    if (kst->mode & 0x0002)
        mode |= S_IWUSR | S_IWGRP | S_IWOTH;
    statbuf->st_mode = mode;
}

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
    int mode = 0;
    if (flags & O_CREAT) {
        __builtin_va_list ap;
        __builtin_va_start(ap, flags);
        mode = __builtin_va_arg(ap, int);
        __builtin_va_end(ap);
    }
    return (int)__syscall_check(__syscall3(SYS_open, (int64_t)(uintptr_t)pathname, (int64_t)flags, (int64_t)mode));
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
    fill_stat_from_tsukasa(statbuf, &kst);
    return 0;
}

int fstat(int fd, struct stat *statbuf)
{
    struct tsukasa_stat kst;
    if (!statbuf) {
        errno = EFAULT;
        return -1;
    }
    int64_t ret = __syscall2(SYS_fstat, (int64_t)fd, (int64_t)(uintptr_t)&kst);
    if (ret < 0) {
        errno = (int)(-ret);
        return -1;
    }
    fill_stat_from_tsukasa(statbuf, &kst);
    return 0;
}

int lstat(const char *pathname, struct stat *statbuf)
{
    struct tsukasa_stat kst;
    if (!pathname || !statbuf) {
        errno = EFAULT;
        return -1;
    }
    int64_t ret = __syscall2(SYS_lstat, (int64_t)(uintptr_t)pathname, (int64_t)(uintptr_t)&kst);
    if (ret < 0) {
        errno = (int)(-ret);
        return -1;
    }
    fill_stat_from_tsukasa(statbuf, &kst);
    return 0;
}

int access(const char *pathname, int mode)
{
    return (int)__syscall_check(__syscall2(SYS_access, (int64_t)(uintptr_t)pathname, (int64_t)mode));
}

int chmod(const char *pathname, mode_t mode)
{
    return (int)__syscall_check(__syscall2(SYS_chmod, (int64_t)(uintptr_t)pathname, (int64_t)mode));
}

int fchmod(int fd, mode_t mode)
{
    return (int)__syscall_check(__syscall2(SYS_fchmod, (int64_t)fd, (int64_t)mode));
}

int chown(const char *pathname, uid_t owner, gid_t group)
{
    return (int)__syscall_check(__syscall3(SYS_chown, (int64_t)(uintptr_t)pathname,
                                           (int64_t)(uint32_t)owner, (int64_t)(uint32_t)group));
}

int fchown(int fd, uid_t owner, gid_t group)
{
    return (int)__syscall_check(__syscall3(SYS_fchown, (int64_t)fd,
                                           (int64_t)(uint32_t)owner, (int64_t)(uint32_t)group));
}

mode_t umask(mode_t mask)
{
    return (mode_t)__syscall1(SYS_umask, (int64_t)mask);
}

int truncate(const char *path, off_t length)
{
    return (int)__syscall_check(__syscall2(SYS_truncate, (int64_t)(uintptr_t)path, (int64_t)length));
}

int ftruncate(int fd, off_t length)
{
    return (int)__syscall_check(__syscall2(SYS_ftruncate, (int64_t)fd, (int64_t)length));
}

int symlink(const char *target, const char *linkpath)
{
    return (int)__syscall_check(__syscall2(SYS_symlink, (int64_t)(uintptr_t)target,
                                           (int64_t)(uintptr_t)linkpath));
}

ssize_t readlink(const char *pathname, char *buf, size_t bufsiz)
{
    return (ssize_t)__syscall_check(__syscall3(SYS_readlink, (int64_t)(uintptr_t)pathname,
                                               (int64_t)(uintptr_t)buf, (int64_t)bufsiz));
}

int flock(int fd, int operation)
{
    return (int)__syscall_check(__syscall2(SYS_flock, (int64_t)fd, (int64_t)operation));
}

int mount(const char *source, const char *target, const char *filesystemtype,
          unsigned long mountflags, const void *data)
{
    return (int)__syscall_check(__syscall5(SYS_mount, (int64_t)(uintptr_t)source,
                                           (int64_t)(uintptr_t)target,
                                           (int64_t)(uintptr_t)filesystemtype,
                                           (int64_t)mountflags,
                                           (int64_t)(uintptr_t)data));
}

int umount(const char *target)
{
    return umount2(target, 0);
}

int umount2(const char *target, int flags)
{
    return (int)__syscall_check(__syscall2(SYS_umount2, (int64_t)(uintptr_t)target, (int64_t)flags));
}

ssize_t getdents64(int fd, void *dirp, size_t count)
{
    return (ssize_t)__syscall_check(__syscall3(SYS_getdents64, (int64_t)fd,
                                               (int64_t)(uintptr_t)dirp, (int64_t)count));
}

DIR *fdopendir(int fd)
{
    struct stat st;
    if (fstat(fd, &st) != 0)
        return NULL;
    if (!S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return NULL;
    }
    DIR *d = (DIR *)malloc(sizeof(DIR));
    if (!d) {
        errno = ENOMEM;
        return NULL;
    }
    d->fd = fd;
    d->pos = 0;
    d->len = 0;
    return d;
}

DIR *opendir(const char *name)
{
    if (!name) {
        errno = EFAULT;
        return NULL;
    }
    int fd = open(name, O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        return NULL;
    DIR *d = fdopendir(fd);
    if (!d)
        close(fd);
    return d;
}

struct dirent *readdir(DIR *dirp)
{
    if (!dirp) {
        errno = EBADF;
        return NULL;
    }
    if (dirp->pos >= dirp->len) {
        ssize_t n = getdents64(dirp->fd, dirp->buf, sizeof(dirp->buf));
        if (n <= 0)
            return NULL;
        dirp->pos = 0;
        dirp->len = (size_t)n;
    }
    struct linux_dirent64 *ld = (struct linux_dirent64 *)(dirp->buf + dirp->pos);
    if (ld->d_reclen == 0 || dirp->pos + ld->d_reclen > dirp->len)
        return NULL;
    dirp->pos += ld->d_reclen;

    dirp->cur.d_ino = (ino_t)ld->d_ino;
    dirp->cur.d_off = (off_t)ld->d_off;
    dirp->cur.d_reclen = ld->d_reclen;
    dirp->cur.d_type = ld->d_type;
    strncpy(dirp->cur.d_name, ld->d_name, sizeof(dirp->cur.d_name) - 1);
    dirp->cur.d_name[sizeof(dirp->cur.d_name) - 1] = '\0';
    return &dirp->cur;
}

int closedir(DIR *dirp)
{
    if (!dirp) {
        errno = EBADF;
        return -1;
    }
    int rc = close(dirp->fd);
    free(dirp);
    return rc;
}

void rewinddir(DIR *dirp)
{
    if (!dirp)
        return;
    dirp->pos = 0;
    dirp->len = 0;
    (void)lseek(dirp->fd, 0, SEEK_SET);
}

int dirfd(DIR *dirp)
{
    if (!dirp) {
        errno = EINVAL;
        return -1;
    }
    return dirp->fd;
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
