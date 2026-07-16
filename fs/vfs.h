/*
 * vfs.h - Virtual File System interface.
 *
 * Phase 4 adds:
 *   - Per-process file-descriptor tables
 *   - Open flags / access modes
 *   - dup/dup2/pipe/fcntl
 *   - stat/fstat
 *   - process-aware cwd and normalized path handling
 *   - explicit mount table introspection
 */

#ifndef VFS_H
#define VFS_H

#include <stddef.h>
#include <stdint.h>

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

#define VFS_POLLIN   0x0001
#define VFS_POLLOUT  0x0004
#define VFS_POLLERR  0x0008
#define VFS_POLLHUP  0x0010

#define VFS_PROT_READ  0x1
#define VFS_PROT_WRITE 0x2

#define VFS_MAP_SHARED  0x01
#define VFS_MAP_PRIVATE 0x02

#define VFS_FB_TYPE_PACKED_PIXELS 0
#define VFS_FB_VISUAL_TRUECOLOR   2

#define VFS_FBIOGET_VSCREENINFO 0x4600
#define VFS_FBIOGET_FSCREENINFO 0x4602

#define VFS_KDSETMODE 0x4B3A
#define VFS_KD_TEXT   0x00
#define VFS_KD_GRAPHICS 0x01

/* File type and mode bits for vfs_stat_t. */
#define VFS_TYPE_UNKNOWN 0
#define VFS_TYPE_FILE    1
#define VFS_TYPE_DIR     2
#define VFS_TYPE_PIPE    3
#define VFS_TYPE_CHAR    4
#define VFS_TYPE_BLOCK   5

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

typedef struct vfs_pollfd {
    int fd;
    int16_t events;
    int16_t revents;
} vfs_pollfd_t;

typedef struct vfs_mmap_request {
    void *addr;
    size_t length;
    int prot;
    int flags;
    int fd;
    size_t offset;
} vfs_mmap_request_t;

typedef struct vfs_fb_var_screeninfo {
    uint32_t xres;
    uint32_t yres;
    uint32_t xres_virtual;
    uint32_t yres_virtual;
    uint32_t bits_per_pixel;
} vfs_fb_var_screeninfo_t;

typedef struct vfs_fb_fix_screeninfo {
    char id[16];
    uintptr_t smem_start;
    uint32_t smem_len;
    uint32_t type;
    uint32_t visual;
    uint32_t line_length;
} vfs_fb_fix_screeninfo_t;

struct process;

void vfs_init(const void *boot_info);

int vfs_open(const char *path);
int vfs_open_flags(const char *path, int flags);
int vfs_create(const char *path);

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

int vfs_getcwd(char *buf, size_t size);
int vfs_chdir(const char *path);

int vfs_list(const char *dir, char names[][VFS_NAME_MAX], int max);
int vfs_get_mounts(vfs_mount_info_t *out, int max);

int vfs_process_inherit(struct process *dst, const struct process *src);
int vfs_process_dup2(struct process *dst,
                     int dst_fd,
                     const struct process *src,
                     int src_fd);
void vfs_process_cleanup(struct process *proc);

#endif /* VFS_H */
