/*
 * syscall.h - Syscall ABI definitions and dispatcher entry.
 */

#ifndef TSUKASA_SYSCALL_H
#define TSUKASA_SYSCALL_H

#include <stdint.h>

/*
 * Top-level syscall numbers.
 * Legacy values (0-5) are preserved for compatibility.
 */
#define SYS_YIELD          0
#define SYS_EXIT           1
#define SYS_SHM_CREATE     2
#define SYS_SHM_ATTACH     3
#define SYS_SHM_DETACH     4
#define SYS_SHM_DESTROY    5

#define SYS_GUI            6
#define SYS_FS             7
#define SYS_SYSTEM         8

/* FS command multiplexer (SYS_FS). */
#define FS_CMD_OPEN        1
#define FS_CMD_READ        2
#define FS_CMD_WRITE       3
#define FS_CMD_CLOSE       4
#define FS_CMD_SEEK        5
#define FS_CMD_SIZE        6
#define FS_CMD_CREATE      7
#define FS_CMD_LIST        8
#define FS_CMD_TELL        9
#define FS_CMD_STAT        10
#define FS_CMD_FSTAT       11
#define FS_CMD_DUP         12
#define FS_CMD_DUP2        13
#define FS_CMD_PIPE        14
#define FS_CMD_FCNTL       15
#define FS_CMD_GETCWD      16
#define FS_CMD_CHDIR       17
#define FS_CMD_IOCTL       18
#define FS_CMD_MMAP        19
#define FS_CMD_MUNMAP      20
#define FS_CMD_POLL        21

#define TSUKASA_O_RDONLY   0x0001
#define TSUKASA_O_WRONLY   0x0002
#define TSUKASA_O_RDWR     (TSUKASA_O_RDONLY | TSUKASA_O_WRONLY)
#define TSUKASA_O_APPEND   0x0004
#define TSUKASA_O_CREAT    0x0008
#define TSUKASA_O_TRUNC    0x0010
#define TSUKASA_O_NONBLOCK 0x0020

#define TSUKASA_F_GETFL    1
#define TSUKASA_F_SETFL    2

#define TSUKASA_POLLIN     0x0001
#define TSUKASA_POLLOUT    0x0004
#define TSUKASA_POLLERR    0x0008
#define TSUKASA_POLLHUP    0x0010

#define TSUKASA_PROT_READ  0x1
#define TSUKASA_PROT_WRITE 0x2
#define TSUKASA_MAP_SHARED 0x01
#define TSUKASA_MAP_PRIVATE 0x02

#define TSUKASA_FBIOGET_VSCREENINFO 0x4600
#define TSUKASA_FBIOGET_FSCREENINFO 0x4602

#define TSUKASA_KDSETMODE  0x4B3A
#define TSUKASA_KD_TEXT    0x00
#define TSUKASA_KD_GRAPHICS 0x01

#define TSUKASA_STAT_TYPE_UNKNOWN 0
#define TSUKASA_STAT_TYPE_FILE    1
#define TSUKASA_STAT_TYPE_DIR     2
#define TSUKASA_STAT_TYPE_PIPE    3
#define TSUKASA_STAT_TYPE_CHAR    4
#define TSUKASA_STAT_TYPE_BLOCK   5

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

/* GUI command multiplexer (SYS_GUI), Phase 2 ABI freeze. */
#define GUI_CMD_NOP                      0
#define GUI_CMD_WINDOW_CREATE            1
#define GUI_CMD_DRAW_RECT                2
#define GUI_CMD_DRAW_STRING              3
#define GUI_CMD_MARK_DIRTY               4
#define GUI_CMD_GET_EVENT                5
#define GUI_CMD_DRAW_ROUNDED_RECT_FILLED 6
#define GUI_CMD_DRAW_IMAGE               7
#define GUI_CMD_GET_STRING_WIDTH         8
#define GUI_CMD_GET_FONT_HEIGHT          9
#define GUI_CMD_DRAW_STRING_BITMAP       10
#define GUI_CMD_DRAW_STRING_SCALED       11
#define GUI_CMD_GET_STRING_WIDTH_SCALED  12
#define GUI_CMD_GET_FONT_HEIGHT_SCALED   13
#define GUI_CMD_WINDOW_SET_RESIZABLE     14
#define GUI_CMD_WINDOW_SET_TITLE         15
#define GUI_CMD_SET_FONT                 16
#define GUI_CMD_WINDOW_DESTROY           17
#define GUI_CMD_DRAW_STRING_SCALED_SLOPED 18
#define GUI_CMD_GET_SCREEN_SIZE          50

/* GUI command return codes (stable ABI). */
#define GUI_OK             0
#define GUI_ERR_INVALID   -1
#define GUI_ERR_NOTFOUND  -2
#define GUI_ERR_NOMEM     -3
#define GUI_ERR_PERM      -4
#define GUI_ERR_FULL      -5
#define GUI_ERR_AGAIN     -6

/* GUI event IDs used by SYS_GUI GET_EVENT (Phase 2 ABI freeze). */
#define GUI_EVENT_NONE        0
#define GUI_EVENT_PAINT       1
#define GUI_EVENT_CLICK       2
#define GUI_EVENT_RIGHT_CLICK 3
#define GUI_EVENT_CLOSE       4
#define GUI_EVENT_KEY         5
#define GUI_EVENT_MOUSE_DOWN  6
#define GUI_EVENT_MOUSE_UP    7
#define GUI_EVENT_MOUSE_MOVE  8
#define GUI_EVENT_MOUSE_WHEEL 9
#define GUI_EVENT_KEYUP       10
#define GUI_EVENT_RESIZE      11

struct tsukasa_gui_event {
    int32_t type;
    int32_t window;
    int32_t x;
    int32_t y;
    int32_t keycode;
    int32_t data1;
    int32_t data2;
};

/* System command multiplexer (SYS_SYSTEM). */
#define SYSTEM_CMD_YIELD           1
#define SYSTEM_CMD_SPAWN           2
#define SYSTEM_CMD_EXEC            3
#define SYSTEM_CMD_WAITPID         4
#define SYSTEM_CMD_KILL            5
#define SYSTEM_CMD_SIGACTION       6
#define SYSTEM_CMD_SIGPROCMASK     7
#define SYSTEM_CMD_SIGPENDING      8
#define SYSTEM_CMD_TTY_CREATE      9
#define SYSTEM_CMD_TTY_SET_FG      10
#define SYSTEM_CMD_TTY_GET_FG      11
#define SYSTEM_CMD_TTY_KILL_FG     12
#define SYSTEM_CMD_SHM_CREATE      13
#define SYSTEM_CMD_SHM_ATTACH      14
#define SYSTEM_CMD_SHM_DETACH      15
#define SYSTEM_CMD_SHM_DESTROY     16
#define SYSTEM_CMD_MEM_STATS       17
#define SYSTEM_CMD_MEM_DUMP        18
#define SYSTEM_CMD_NET_INIT        19
#define SYSTEM_CMD_NET_IS_INIT     20
#define SYSTEM_CMD_NET_HAS_IP      21
#define SYSTEM_CMD_NET_GET_LINK    22
#define SYSTEM_CMD_NET_GET_STATS   23
#define SYSTEM_CMD_NET_DHCP        24
#define SYSTEM_CMD_NET_DNS_LOOKUP  25
#define SYSTEM_CMD_NET_PING        26
#define SYSTEM_CMD_NET_TCP_CONNECT 27
#define SYSTEM_CMD_NET_TCP_SEND    28
#define SYSTEM_CMD_NET_TCP_RECV    29
#define SYSTEM_CMD_NET_TCP_CLOSE   30
#define SYSTEM_CMD_NET_UDP_SEND    31
#define SYSTEM_CMD_NET_POLL        32
#define SYSTEM_CMD_NET_GET_MAC     33
#define SYSTEM_CMD_NET_GET_IP      34
#define SYSTEM_CMD_NET_GET_GATEWAY 35
#define SYSTEM_CMD_NET_GET_DNS     36
#define SYSTEM_CMD_GET_CMDLINE     37
#define SYSTEM_CMD_SPAWN_EX        38
#define SYSTEM_CMD_TIME_GET        39

/* Reserved v2 desktop customization command range. */
#define SYSTEM_CMD_THEME_SET_ACCENT    100
#define SYSTEM_CMD_THEME_SET_BG_MODE   101
#define SYSTEM_CMD_THEME_SET_WALLPAPER 102
#define SYSTEM_CMD_THEME_GET_STATE     103

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

struct tsukasa_net_udp_send_req {
    struct tsukasa_net_ipv4 ip;
    uint16_t src_port;
    uint16_t dst_port;
    const void *buffer;
    uint32_t length;
};

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

struct tsukasa_theme_state {
    uint32_t accent_color;
    uint32_t background_mode;
    uint32_t solid_color;
    uint32_t wallpaper_style;
    uint32_t flags;
    char wallpaper_path[128];
};

uintptr_t syscall_handler(uintptr_t num,
                          uintptr_t arg1,
                          uintptr_t arg2,
                          uintptr_t arg3,
                          uintptr_t arg4,
                          uintptr_t arg5);

#endif /* TSUKASA_SYSCALL_H */
