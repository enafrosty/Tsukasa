/*
 * syscall.h - User syscall wrapper interface.
 */

#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

#include <stddef.h>
#include <stdint.h>

struct tsukasa_sigaction {
    uintptr_t sa_handler;
    uint64_t sa_mask;
    int sa_flags;
};

struct tsukasa_stat {
    uint64_t size;
    uint32_t type;
    uint32_t mode;
    uint64_t blocks;
};

struct tsukasa_pollfd {
    int fd;
    int16_t events;
    int16_t revents;
};

struct tsukasa_mmap_request {
    void *addr;
    size_t length;
    int prot;
    int flags;
    int fd;
    size_t offset;
};

struct tsukasa_fb_var_screeninfo {
    uint32_t xres;
    uint32_t yres;
    uint32_t xres_virtual;
    uint32_t yres_virtual;
    uint32_t bits_per_pixel;
};

struct tsukasa_fb_fix_screeninfo {
    char id[16];
    uintptr_t smem_start;
    uint32_t smem_len;
    uint32_t type;
    uint32_t visual;
    uint32_t line_length;
};

struct tsukasa_mem_stats {
    uint64_t total_pages;
    uint64_t used_pages;
    uint64_t free_pages;

    uint64_t heap_pool_bytes;
    uint64_t heap_used_bytes;
    uint64_t heap_peak_bytes;

    uint64_t process_count;
    uint64_t process_mapped_pages;
    uint64_t process_shm_pages;
    uint64_t process_shm_attachments;

    uint64_t shm_regions;
    uint64_t shm_attachments;
    uint64_t shm_reserved_pages;
};

struct tsukasa_net_ipv4 {
    uint8_t bytes[4];
};

struct tsukasa_net_mac {
    uint8_t bytes[6];
};

struct tsukasa_net_link_info {
    char nic_name[24];
    struct tsukasa_net_mac mac;
    struct tsukasa_net_ipv4 ip;
    struct tsukasa_net_ipv4 gateway;
    struct tsukasa_net_ipv4 dns;
    uint8_t link_up;
};

struct tsukasa_net_stats {
    uint64_t tx_packets;
    uint64_t tx_bytes;
    uint64_t rx_packets;
    uint64_t rx_bytes;
    uint64_t rx_dropped;
    uint64_t irq_count;
    uint64_t rx_poll_calls;
    uint8_t stack_initialized;
    uint8_t has_ip;
};

struct tsukasa_net_tcp_connect_req {
    struct tsukasa_net_ipv4 ip;
    uint16_t port;
};

struct tsukasa_spawn_request {
    const char *path;
    const char *args;
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;
    int tty_id;
};

struct tsukasa_time {
    uint8_t sec;
    uint8_t min;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
};

struct tsukasa_net_udp_send_req {
    struct tsukasa_net_ipv4 ip;
    uint16_t src_port;
    uint16_t dst_port;
    const void *buffer;
    uint32_t length;
};

struct tsukasa_gui_event {
    int32_t type;
    int32_t window;
    int32_t x;
    int32_t y;
    int32_t keycode;
    int32_t data1;
    int32_t data2;
};

struct tsukasa_theme_state {
    uint32_t accent_color;
    uint32_t background_mode;
    uint32_t solid_color;
    uint32_t wallpaper_style;
    uint32_t flags;
    char wallpaper_path[128];
};

#ifndef TSUKASA_THEME_BG_GRADIENT
#define TSUKASA_THEME_BG_GRADIENT      0u
#define TSUKASA_THEME_BG_SOLID         1u
#define TSUKASA_THEME_BG_WALLPAPER     2u
#endif

#ifndef TSUKASA_THEME_WP_SCALE_FILL
#define TSUKASA_THEME_WP_SCALE_FILL    0u
#define TSUKASA_THEME_WP_CENTER        1u
#endif

#ifndef TSUKASA_THEME_FLAG_EPHEMERAL
#define TSUKASA_THEME_FLAG_EPHEMERAL   0x1u
#endif

void exit(int code);
void yield(void);

int spawn(const char *path);
int spawn_ex(const struct tsukasa_spawn_request *req);
int exec_process(int pid, const char *path);
int waitpid(int pid, int *status, int options);
int kill_process(int pid, int sig);
int system_get_cmdline(char *buf, size_t size);
int system_time_get(struct tsukasa_time *out);

int sigaction(int sig, const struct tsukasa_sigaction *act, struct tsukasa_sigaction *oldact);
int sigprocmask(int how, const uint64_t *set, uint64_t *oldset);
int sigpending(uint64_t *set);

int u_tty_create(void);
int u_tty_set_fg(int tty_id, int pgid);
int u_tty_get_fg(int tty_id);
int u_tty_kill_fg(int tty_id, int sig);

int u_shm_create(size_t size);
void *u_shm_attach(int id);
int u_shm_detach(void *addr);
int u_shm_destroy(int id);

#define tty_create  u_tty_create
#define tty_set_fg  u_tty_set_fg
#define tty_get_fg  u_tty_get_fg
#define tty_kill_fg u_tty_kill_fg
#define shm_create  u_shm_create
#define shm_attach  u_shm_attach
#define shm_detach  u_shm_detach
#define shm_destroy u_shm_destroy

int system_mem_stats(struct tsukasa_mem_stats *out);
int system_mem_dump(void);

int net_init(void);
int net_is_init(void);
int net_has_ip(void);
int net_get_link(struct tsukasa_net_link_info *out);
int net_get_mac(struct tsukasa_net_mac *out);
int net_get_ip(struct tsukasa_net_ipv4 *out);
int net_get_gateway(struct tsukasa_net_ipv4 *out);
int net_get_dns(struct tsukasa_net_ipv4 *out);
int net_get_stats(struct tsukasa_net_stats *out);
int net_dhcp(void);
int net_dns_lookup(const char *name, struct tsukasa_net_ipv4 *out);
int net_ping(const struct tsukasa_net_ipv4 *ip, uint32_t timeout_ms);
int net_tcp_connect(const struct tsukasa_net_tcp_connect_req *req);
int net_tcp_send(const void *buffer, size_t len);
int net_tcp_recv(void *buffer, size_t max_len, int wait);
int net_tcp_close(void);
int net_udp_send(const struct tsukasa_net_udp_send_req *req);
int net_poll(void);

int theme_set_accent(uint32_t color_argb);
int theme_set_bg_mode(uint32_t mode);
int theme_set_bg_mode_ex(uint32_t mode, uint32_t aux);
int theme_set_wallpaper(const char *path);
int theme_get_state(struct tsukasa_theme_state *out);

int fs_open(const char *path, int flags);
int fs_create(const char *path);
size_t fs_read(int fd, void *buf, size_t count);
size_t fs_write(int fd, const void *buf, size_t count);
int fs_close(int fd);
size_t fs_seek(int fd, size_t offset, int whence);
size_t fs_tell(int fd);
size_t fs_size(int fd);
int fs_list(const char *dir, char names[][64], int max);
int fs_stat(const char *path, struct tsukasa_stat *out);
int fs_fstat(int fd, struct tsukasa_stat *out);
int fs_dup(int fd);
int fs_dup2(int oldfd, int newfd);
int fs_pipe(int pipefd[2]);
int fs_fcntl(int fd, int cmd, int arg);
int fs_getcwd(char *buf, size_t size);
int fs_chdir(const char *path);
int fs_ioctl(int fd, unsigned long request, void *arg);
void *fs_mmap(void *addr, size_t length, int prot, int flags, int fd, size_t offset);
int fs_munmap(void *addr, size_t length);
int fs_poll(struct tsukasa_pollfd *fds, size_t nfds, int timeout_ms);

#endif /* USER_SYSCALL_H */
