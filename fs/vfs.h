/*
 * Project Tsukasa — Virtual File System interface
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

#ifndef VFS_H
#define VFS_H

#include <stddef.h>
#include <stdint.h>

/* Shared kernel/user ABI: fb ioctls + screeninfo, KD console mode, mmap prot/flags, poll bits + pollfd. */
#include "../include/vfs_abi.h"

#define VFS_NAME_MAX  64
#define VFS_PATH_MAX  256

#define VFS_SEEK_SET 0
#define VFS_SEEK_CUR 1
#define VFS_SEEK_END 2

/* Open flags. */
#define VFS_O_RDONLY  0x0001
#define VFS_O_WRONLY  0x0002
#define VFS_O_RDWR    (VFS_O_RDONLY | VFS_O_WRONLY)
#define VFS_O_APPEND  0x0004
#define VFS_O_CREAT   0x0008
#define VFS_O_TRUNC   0x0010
#define VFS_O_NONBLOCK 0x0020

#define VFS_F_GETFL 1
#define VFS_F_SETFL 2

/* The poll bits, mmap prot and map flags, fb ioctls plus their screeninfo structs, and the KD console mode... */

/* File type and mode bits for vfs_stat_t. */
#define VFS_TYPE_UNKNOWN 0
#define VFS_TYPE_FILE    1
#define VFS_TYPE_DIR     2
#define VFS_TYPE_PIPE    3
#define VFS_TYPE_CHAR    4
#define VFS_TYPE_BLOCK   5
#define VFS_TYPE_SOCKET  6

#define VFS_MODE_READ  0x01
#define VFS_MODE_WRITE 0x02

typedef struct vfs_stat {
    uint64_t size;
    uint32_t type;
    uint32_t mode;
    uint64_t blocks;
} vfs_stat_t;

typedef struct vfs_mount_info {
    char path[VFS_PATH_MAX];
    char fs_name[16];
    int read_only;
} vfs_mount_info_t;

typedef struct vfs_mmap_request {
    void *addr;
    size_t length;
    int prot;
    int flags;
    int fd;
    size_t offset;
} vfs_mmap_request_t;

struct process;

void vfs_init(const void *boot_info);

int vfs_open(const char *path);
int vfs_open_flags(const char *path, int flags);
int vfs_create(const char *path);
int vfs_mkdir(const char *path, int mode);
int vfs_unlink(const char *path);
int vfs_rmdir(const char *path);
int vfs_rename(const char *old_path, const char *new_path);


size_t vfs_read(int fd, void *buf, size_t count);
size_t vfs_write(int fd, const void *buf, size_t count);
size_t vfs_seek(int fd, size_t offset, int whence);
size_t vfs_tell(int fd);
size_t vfs_size(int fd);
void vfs_close(int fd);

int vfs_dup(int oldfd);
int vfs_dup2(int oldfd, int newfd);
int vfs_pipe(int pipefd[2]);
int vfs_fcntl(int fd, int cmd, int arg);
int vfs_ioctl(int fd, unsigned long request, void *arg);
void *vfs_mmap(void *addr, size_t length, int prot, int flags, int fd, size_t offset);
int vfs_munmap(void *addr, size_t length);
int vfs_poll(vfs_pollfd_t *fds, size_t nfds, int timeout_ms);

int vfs_stat(const char *path, vfs_stat_t *out);
int vfs_fstat(int fd, vfs_stat_t *out);

int vfs_socket_create(void);
int vfs_socket_bind(int fd, const char *path);
int vfs_socket_listen(int fd);
int vfs_socket_connect(int fd, const char *path);
int vfs_socket_accept(int fd);

int vfs_kd_mode(void);

int vfs_getcwd(char *buf, size_t size);
int vfs_chdir(const char *path);

int vfs_list(const char *dir, char names[][VFS_NAME_MAX], int max);
int vfs_get_mounts(vfs_mount_info_t *out, int max);

void vfs_run_guide12_selftests(void);

void vfs_run_guide13_selftests(void);

void vfs_run_guide14_selftests(void);

int vfs_process_inherit(struct process *dst, const struct process *src);
int vfs_process_dup2(struct process *dst,
                     int dst_fd,
                     const struct process *src,
                     int src_fd);
void vfs_process_cleanup(struct process *proc);

#endif /* VFS_H */
