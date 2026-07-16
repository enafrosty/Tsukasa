/*
 * syscall_nums.h - User-visible syscall IDs and multiplexer commands.
 */

#ifndef SYSCALL_NUMS_H
#define SYSCALL_NUMS_H

/* Top-level syscall numbers (legacy + expanded ABI). */
#define SYS_YIELD          0
#define SYS_EXIT           1
#define SYS_SHM_CREATE     2
#define SYS_SHM_ATTACH     3
#define SYS_SHM_DETACH     4
#define SYS_SHM_DESTROY    5
#define SYS_GUI            6
#define SYS_FS             7
#define SYS_SYSTEM         8

/* FS commands (SYS_FS). */
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

#define TSUKASA_STAT_TYPE_FILE 1
#define TSUKASA_STAT_TYPE_DIR  2
#define TSUKASA_STAT_TYPE_PIPE 3
#define TSUKASA_STAT_TYPE_CHAR 4
#define TSUKASA_STAT_TYPE_BLOCK 5

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

/* GUI commands (SYS_GUI), Phase 2 ABI freeze. */
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

#define GUI_OK             0
#define GUI_ERR_INVALID   -1
#define GUI_ERR_NOTFOUND  -2
#define GUI_ERR_NOMEM     -3
#define GUI_ERR_PERM      -4
#define GUI_ERR_FULL      -5
#define GUI_ERR_AGAIN     -6

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

/* System commands (SYS_SYSTEM). */
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

#endif /* SYSCALL_NUMS_H */
