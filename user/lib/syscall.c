/*
 * syscall.c - User wrappers for int 0x80 syscall ABI.
 */

#include "syscall.h"
#include "../include/syscall_nums.h"

#ifdef TSUKASA_USERLIB_KERNEL
extern uintptr_t syscall_handler(uintptr_t num,
                                 uintptr_t arg1,
                                 uintptr_t arg2,
                                 uintptr_t arg3,
                                 uintptr_t arg4,
                                 uintptr_t arg5);
#endif

static long syscall5(long n, long a1, long a2, long a3, long a4, long a5)
{
#ifdef TSUKASA_USERLIB_KERNEL
    return (long)syscall_handler((uintptr_t)n,
                                 (uintptr_t)a1,
                                 (uintptr_t)a2,
                                 (uintptr_t)a3,
                                 (uintptr_t)a4,
                                 (uintptr_t)a5);
#else
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
        : "memory"
    );
    return ret;
#endif
}

static long syscall1(long n, long a1)
{
    return syscall5(n, a1, 0, 0, 0, 0);
}

static long sys_system(long cmd, long a1, long a2, long a3, long a4)
{
    return syscall5(SYS_SYSTEM, cmd, a1, a2, a3, a4);
}

static long sys_fs(long cmd, long a1, long a2, long a3, long a4)
{
    return syscall5(SYS_FS, cmd, a1, a2, a3, a4);
}

void exit(int code)
{
    syscall1(SYS_EXIT, (long)code);
    __builtin_unreachable();
}

void yield(void)
{
    syscall1(SYS_YIELD, 0);
}

int spawn(const char *path)
{
    return (int)sys_system(SYSTEM_CMD_SPAWN, (long)path, 0, 0, 0);
}

int spawn_ex(const struct tsukasa_spawn_request *req)
{
    return (int)sys_system(SYSTEM_CMD_SPAWN_EX, (long)req, 0, 0, 0);
}

int exec_process(int pid, const char *path)
{
    return (int)sys_system(SYSTEM_CMD_EXEC, (long)pid, (long)path, 0, 0);
}

int waitpid(int pid, int *status, int options)
{
    return (int)sys_system(SYSTEM_CMD_WAITPID, (long)pid, (long)status, (long)options, 0);
}

int kill_process(int pid, int sig)
{
    return (int)sys_system(SYSTEM_CMD_KILL, (long)pid, (long)sig, 0, 0);
}

int system_get_cmdline(char *buf, size_t size)
{
    return (int)sys_system(SYSTEM_CMD_GET_CMDLINE, (long)buf, (long)size, 0, 0);
}

int system_time_get(struct tsukasa_time *out)
{
    return (int)sys_system(SYSTEM_CMD_TIME_GET, (long)out, 0, 0, 0);
}

int sigaction(int sig, const struct tsukasa_sigaction *act, struct tsukasa_sigaction *oldact)
{
    return (int)sys_system(SYSTEM_CMD_SIGACTION, (long)sig, (long)act, (long)oldact, 0);
}

int sigprocmask(int how, const uint64_t *set, uint64_t *oldset)
{
    return (int)sys_system(SYSTEM_CMD_SIGPROCMASK, (long)how, (long)set, (long)oldset, 0);
}

int sigpending(uint64_t *set)
{
    return (int)sys_system(SYSTEM_CMD_SIGPENDING, (long)set, 0, 0, 0);
}

int u_tty_create(void)
{
    return (int)sys_system(SYSTEM_CMD_TTY_CREATE, 0, 0, 0, 0);
}

int u_tty_set_fg(int tty_id, int pgid)
{
    return (int)sys_system(SYSTEM_CMD_TTY_SET_FG, tty_id, pgid, 0, 0);
}

int u_tty_get_fg(int tty_id)
{
    return (int)sys_system(SYSTEM_CMD_TTY_GET_FG, tty_id, 0, 0, 0);
}

int u_tty_kill_fg(int tty_id, int sig)
{
    return (int)sys_system(SYSTEM_CMD_TTY_KILL_FG, tty_id, sig, 0, 0);
}

/* Legacy-compatible wrappers. */
int u_shm_create(size_t size)
{
    return (int)syscall1(SYS_SHM_CREATE, (long)size);
}

void *u_shm_attach(int id)
{
    return (void *)(long)syscall1(SYS_SHM_ATTACH, id);
}

int u_shm_detach(void *addr)
{
    return (int)syscall1(SYS_SHM_DETACH, (long)addr);
}

int u_shm_destroy(int id)
{
    return (int)syscall1(SYS_SHM_DESTROY, id);
}

int system_mem_stats(struct tsukasa_mem_stats *out)
{
    return (int)sys_system(SYSTEM_CMD_MEM_STATS, (long)out, 0, 0, 0);
}

int system_mem_dump(void)
{
    return (int)sys_system(SYSTEM_CMD_MEM_DUMP, 0, 0, 0, 0);
}

struct tsukasa_net_dns_req {
    const char *name;
    struct tsukasa_net_ipv4 *out_ip;
};

struct tsukasa_net_ping_req {
    struct tsukasa_net_ipv4 ip;
    uint32_t timeout_ms;
};

struct tsukasa_net_tcp_recv_req {
    void *buffer;
    uint32_t max_len;
    int wait;
};

int net_init(void)
{
    return (int)sys_system(SYSTEM_CMD_NET_INIT, 0, 0, 0, 0);
}

int net_is_init(void)
{
    return (int)sys_system(SYSTEM_CMD_NET_IS_INIT, 0, 0, 0, 0);
}

int net_has_ip(void)
{
    return (int)sys_system(SYSTEM_CMD_NET_HAS_IP, 0, 0, 0, 0);
}

int net_get_link(struct tsukasa_net_link_info *out)
{
    return (int)sys_system(SYSTEM_CMD_NET_GET_LINK, (long)out, 0, 0, 0);
}

int net_get_mac(struct tsukasa_net_mac *out)
{
    return (int)sys_system(SYSTEM_CMD_NET_GET_MAC, (long)out, 0, 0, 0);
}

int net_get_ip(struct tsukasa_net_ipv4 *out)
{
    return (int)sys_system(SYSTEM_CMD_NET_GET_IP, (long)out, 0, 0, 0);
}

int net_get_gateway(struct tsukasa_net_ipv4 *out)
{
    return (int)sys_system(SYSTEM_CMD_NET_GET_GATEWAY, (long)out, 0, 0, 0);
}

int net_get_dns(struct tsukasa_net_ipv4 *out)
{
    return (int)sys_system(SYSTEM_CMD_NET_GET_DNS, (long)out, 0, 0, 0);
}

int net_get_stats(struct tsukasa_net_stats *out)
{
    return (int)sys_system(SYSTEM_CMD_NET_GET_STATS, (long)out, 0, 0, 0);
}

int net_dhcp(void)
{
    return (int)sys_system(SYSTEM_CMD_NET_DHCP, 0, 0, 0, 0);
}

int net_dns_lookup(const char *name, struct tsukasa_net_ipv4 *out)
{
    struct tsukasa_net_dns_req req;
    req.name = name;
    req.out_ip = out;
    return (int)sys_system(SYSTEM_CMD_NET_DNS_LOOKUP, (long)&req, 0, 0, 0);
}

int net_ping(const struct tsukasa_net_ipv4 *ip, uint32_t timeout_ms)
{
    struct tsukasa_net_ping_req req;
    if (!ip)
        return -1;
    req.ip = *ip;
    req.timeout_ms = timeout_ms;
    return (int)sys_system(SYSTEM_CMD_NET_PING, (long)&req, 0, 0, 0);
}

int net_tcp_connect(const struct tsukasa_net_tcp_connect_req *req)
{
    return (int)sys_system(SYSTEM_CMD_NET_TCP_CONNECT, (long)req, 0, 0, 0);
}

int net_tcp_send(const void *buffer, size_t len)
{
    return (int)sys_system(SYSTEM_CMD_NET_TCP_SEND, (long)buffer, (long)len, 0, 0);
}

int net_tcp_recv(void *buffer, size_t max_len, int wait)
{
    struct tsukasa_net_tcp_recv_req req;
    req.buffer = buffer;
    req.max_len = (uint32_t)max_len;
    req.wait = wait;
    return (int)sys_system(SYSTEM_CMD_NET_TCP_RECV, (long)&req, 0, 0, 0);
}

int net_tcp_close(void)
{
    return (int)sys_system(SYSTEM_CMD_NET_TCP_CLOSE, 0, 0, 0, 0);
}

int net_udp_send(const struct tsukasa_net_udp_send_req *req)
{
    return (int)sys_system(SYSTEM_CMD_NET_UDP_SEND, (long)req, 0, 0, 0);
}

int net_poll(void)
{
    return (int)sys_system(SYSTEM_CMD_NET_POLL, 0, 0, 0, 0);
}

int theme_set_accent(uint32_t color_argb)
{
    return (int)sys_system(SYSTEM_CMD_THEME_SET_ACCENT, (long)color_argb, 0, 0, 0);
}

int theme_set_bg_mode(uint32_t mode)
{
    return (int)sys_system(SYSTEM_CMD_THEME_SET_BG_MODE, (long)mode, 0, 0, 0);
}

int theme_set_bg_mode_ex(uint32_t mode, uint32_t aux)
{
    return (int)sys_system(SYSTEM_CMD_THEME_SET_BG_MODE, (long)mode, (long)aux, 1, 0);
}

int theme_set_wallpaper(const char *path)
{
    return (int)sys_system(SYSTEM_CMD_THEME_SET_WALLPAPER, (long)path, 0, 0, 0);
}

int theme_get_state(struct tsukasa_theme_state *out)
{
    return (int)sys_system(SYSTEM_CMD_THEME_GET_STATE, (long)out, 0, 0, 0);
}

int fs_open(const char *path, int flags)
{
    return (int)sys_fs(FS_CMD_OPEN, (long)path, (long)flags, 0, 0);
}

int fs_create(const char *path)
{
    return (int)sys_fs(FS_CMD_CREATE, (long)path, 0, 0, 0);
}

size_t fs_read(int fd, void *buf, size_t count)
{
    return (size_t)sys_fs(FS_CMD_READ, (long)fd, (long)buf, (long)count, 0);
}

size_t fs_write(int fd, const void *buf, size_t count)
{
    return (size_t)sys_fs(FS_CMD_WRITE, (long)fd, (long)buf, (long)count, 0);
}

int fs_close(int fd)
{
    return (int)sys_fs(FS_CMD_CLOSE, (long)fd, 0, 0, 0);
}

size_t fs_seek(int fd, size_t offset, int whence)
{
    return (size_t)sys_fs(FS_CMD_SEEK, (long)fd, (long)offset, (long)whence, 0);
}

size_t fs_tell(int fd)
{
    return (size_t)sys_fs(FS_CMD_TELL, (long)fd, 0, 0, 0);
}

size_t fs_size(int fd)
{
    return (size_t)sys_fs(FS_CMD_SIZE, (long)fd, 0, 0, 0);
}

int fs_list(const char *dir, char names[][64], int max)
{
    return (int)sys_fs(FS_CMD_LIST, (long)dir, (long)names, (long)max, 0);
}

int fs_stat(const char *path, struct tsukasa_stat *out)
{
    return (int)sys_fs(FS_CMD_STAT, (long)path, (long)out, 0, 0);
}

int fs_fstat(int fd, struct tsukasa_stat *out)
{
    return (int)sys_fs(FS_CMD_FSTAT, (long)fd, (long)out, 0, 0);
}

int fs_dup(int fd)
{
    return (int)sys_fs(FS_CMD_DUP, (long)fd, 0, 0, 0);
}

int fs_dup2(int oldfd, int newfd)
{
    return (int)sys_fs(FS_CMD_DUP2, (long)oldfd, (long)newfd, 0, 0);
}

int fs_pipe(int pipefd[2])
{
    return (int)sys_fs(FS_CMD_PIPE, (long)pipefd, 0, 0, 0);
}

int fs_fcntl(int fd, int cmd, int arg)
{
    return (int)sys_fs(FS_CMD_FCNTL, (long)fd, (long)cmd, (long)arg, 0);
}

int fs_getcwd(char *buf, size_t size)
{
    return (int)sys_fs(FS_CMD_GETCWD, (long)buf, (long)size, 0, 0);
}

int fs_chdir(const char *path)
{
    return (int)sys_fs(FS_CMD_CHDIR, (long)path, 0, 0, 0);
}

int fs_ioctl(int fd, unsigned long request, void *arg)
{
    return (int)sys_fs(FS_CMD_IOCTL, (long)fd, (long)request, (long)arg, 0);
}

void *fs_mmap(void *addr, size_t length, int prot, int flags, int fd, size_t offset)
{
    struct tsukasa_mmap_request req;
    req.addr = addr;
    req.length = length;
    req.prot = prot;
    req.flags = flags;
    req.fd = fd;
    req.offset = offset;
    return (void *)(long)sys_fs(FS_CMD_MMAP, (long)&req, 0, 0, 0);
}

int fs_munmap(void *addr, size_t length)
{
    return (int)sys_fs(FS_CMD_MUNMAP, (long)addr, (long)length, 0, 0);
}

int fs_poll(struct tsukasa_pollfd *fds, size_t nfds, int timeout_ms)
{
    return (int)sys_fs(FS_CMD_POLL, (long)fds, (long)nfds, (long)timeout_ms, 0);
}
