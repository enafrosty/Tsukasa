/*
 * Project Tsukasa — include "../include/unistd.h"
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

#include "../lib/syscall.h"
#include "../include/string.h"

#include <stdint.h>

int close(int fd)
{
    return fs_close(fd);
}

ssize_t read(int fd, void *buf, size_t count)
{
    size_t rc = fs_read(fd, buf, count);
    if (rc == (size_t)-1)
        return -1;
    return (ssize_t)rc;
}

ssize_t write(int fd, const void *buf, size_t count)
{
    size_t rc = fs_write(fd, buf, count);
    if (rc == (size_t)-1)
        return -1;
    return (ssize_t)rc;
}

off_t lseek(int fd, off_t offset, int whence)
{
    size_t rc = fs_seek(fd, (size_t)offset, whence);
    if (rc == (size_t)-1)
        return -1;
    return (off_t)rc;
}

int dup(int oldfd)
{
    return fs_dup(oldfd);
}

int dup2(int oldfd, int newfd)
{
    return fs_dup2(oldfd, newfd);
}

int pipe(int pipefd[2])
{
    return fs_pipe(pipefd);
}

int chdir(const char *path)
{
    return fs_chdir(path);
}

char *getcwd(char *buf, size_t size)
{
    if (!buf || size == 0)
        return 0;
    if (fs_getcwd(buf, size) != 0)
        return 0;
    return buf;
}

static int parse_field_from_status(const char *status, const char *prefix, size_t prefix_len)
{
    const char *p = status;
    int v = 0;
    while (p && *p) {
        if (strncmp(p, prefix, prefix_len) == 0) {
            p += prefix_len;
            while (*p >= '0' && *p <= '9') {
                v = v * 10 + (*p - '0');
                p++;
            }
            return v;
        }
        while (*p && *p != '\n')
            p++;
        if (*p == '\n')
            p++;
    }
    return -1;
}

int getpid(void)
{
    int fd = fs_open("/proc/self/status", 0);
    char buf[128];
    size_t n;
    if (fd < 0)
        return -1;
    n = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);
    if (n == 0 || n == (size_t)-1)
        return -1;
    buf[n] = '\0';
    return parse_field_from_status(buf, "pid: ", 5);
}

int getppid(void)
{
    int fd = fs_open("/proc/self/status", 0);
    char buf[128];
    size_t n;
    if (fd < 0)
        return 0;
    n = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);
    if (n == 0 || n == (size_t)-1)
        return 0;
    buf[n] = '\0';
    int ppid = parse_field_from_status(buf, "ppid: ", 6);
    return (ppid >= 0) ? ppid : 0;
}

int kill(pid_t pid, int sig)
{
    return kill_process((int)pid, sig);
}

void _exit(int code)
{
    exit(code);
}
