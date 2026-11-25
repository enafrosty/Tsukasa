/*
 * Project Tsukasa — shared kernel/user VFS ABI (framebuffer, KD console
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

#ifndef TSUKASA_VFS_ABI_H
#define TSUKASA_VFS_ABI_H

#include <stdint.h>

/* mmap prot bits (VFS_PROT_*) */
#define VFS_PROT_NONE  0x0
#define VFS_PROT_READ  0x1
#define VFS_PROT_WRITE 0x2
#define VFS_PROT_EXEC  0x4

/* mmap flags (VFS_MAP_*) */
#define VFS_MAP_SHARED     0x01
#define VFS_MAP_PRIVATE    0x02
#define VFS_MAP_FIXED      0x10
#define VFS_MAP_ANONYMOUS  0x20
#define VFS_MAP_ANON       VFS_MAP_ANONYMOUS

/* msync flags (VFS_MS_*) */
#define VFS_MS_ASYNC       1
#define VFS_MS_INVALIDATE  2
#define VFS_MS_SYNC        4

/* framebuffer fix-info classification */
#define VFS_FB_TYPE_PACKED_PIXELS 0
#define VFS_FB_VISUAL_TRUECOLOR   2

/* framebuffer ioctls (Linux fbdev request numbers) */
#define VFS_FBIOGET_VSCREENINFO 0x4600
#define VFS_FBIOGET_FSCREENINFO 0x4602

/* KD console mode ioctls (Linux KDSETMODE / KDGETMODE) */
#define VFS_KDSETMODE   0x4B3A
#define VFS_KDGETMODE   0x4B3B
#define VFS_KD_TEXT     0x00
#define VFS_KD_GRAPHICS 0x01

/* VT console ioctls (Linux VT_*) */
#define VFS_VT_GETSTATE   0x5603
#define VFS_VT_ACTIVATE   0x5606
#define VFS_VT_WAITACTIVE 0x5607

struct vfs_vt_stat {
    uint16_t v_active;
    uint16_t v_signal;
    uint16_t v_state;
};

/* TTY / PTY ioctls (Linux x86_64 compatible) */
#define VFS_TCGETS     0x5401UL
#define VFS_TCSETS     0x5402UL
#define VFS_TCSETSW    0x5403UL
#define VFS_TCSETSF    0x5404UL
#define VFS_TIOCSCTTY  0x540EUL
#define VFS_TIOCGPGRP  0x540FUL
#define VFS_TIOCSPGRP  0x5410UL
#define VFS_TIOCGWINSZ 0x5413UL
#define VFS_TIOCSWINSZ 0x5414UL
#define VFS_FIONREAD   0x541BUL
#define VFS_TIOCGPTN   0x80045430UL
#define VFS_TIOCSPTLCK 0x40045431UL

#define VFS_NCCS 32

typedef uint32_t vfs_tcflag_t;
typedef uint8_t  vfs_cc_t;
typedef uint32_t vfs_speed_t;

struct tsk_winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};

struct tsk_termios {
    vfs_tcflag_t c_iflag;
    vfs_tcflag_t c_oflag;
    vfs_tcflag_t c_cflag;
    vfs_tcflag_t c_lflag;
    vfs_cc_t     c_line;
    vfs_cc_t     c_cc[VFS_NCCS];
    vfs_speed_t  c_ispeed;
    vfs_speed_t  c_ospeed;
};

/* c_cc indices */
#define VFS_VINTR    0
#define VFS_VQUIT    1
#define VFS_VERASE   2
#define VFS_VKILL    3
#define VFS_VEOF     4
#define VFS_VTIME    5
#define VFS_VMIN     6
#define VFS_VSWTC    7
#define VFS_VSTART   8
#define VFS_VSTOP    9
#define VFS_VSUSP    10
#define VFS_VEOL     11
#define VFS_VREPRINT 12
#define VFS_VDISCARD 13
#define VFS_VWERASE  14
#define VFS_VLNEXT   15
#define VFS_VEOL2    16

/* c_iflag bits */
#define VFS_IGNBRK  0000001U
#define VFS_BRKINT  0000002U
#define VFS_IGNPAR  0000004U
#define VFS_PARMRK  0000010U
#define VFS_INPCK   0000020U
#define VFS_ISTRIP  0000040U
#define VFS_INLCR   0000100U
#define VFS_IGNCR   0000200U
#define VFS_ICRNL   0000400U
#define VFS_IXON    0002000U
#define VFS_IXANY   0004000U
#define VFS_IXOFF   0010000U
#define VFS_IUTF8   0040000U

/* c_oflag bits */
#define VFS_OPOST   0000001U
#define VFS_OLCUC   0000002U
#define VFS_ONLCR   0000004U
#define VFS_OCRNL   0000010U
#define VFS_ONOCR   0000020U
#define VFS_ONLRET  0000040U

/* c_cflag bits */
#define VFS_CSIZE   0000060U
#define VFS_CS8     0000060U
#define VFS_CREAD   0000200U
#define VFS_HUPCL   0002000U
#define VFS_CLOCAL  0004000U
#define VFS_B0      0000000U
#define VFS_B9600   0000015U
#define VFS_B38400  0000017U
#define VFS_B115200 0010002U

/* c_lflag bits */
#define VFS_ISIG    0000001U
#define VFS_ICANON  0000002U
#define VFS_ECHO    0000010U
#define VFS_ECHOE   0000020U
#define VFS_ECHOK   0000040U
#define VFS_ECHONL  0000100U
#define VFS_NOFLSH  0000200U
#define VFS_TOSTOP  0000400U
#define VFS_ECHOCTL 0001000U
#define VFS_IEXTEN  0100000U

/* Block device ioctls (Linux-compatible) */
#define BLKRRPART    0x125F
#define BLKSSZGET    0x1268
#define BLKGETSIZE64 0x80081272UL

/* poll event bits */
#define VFS_POLLIN   0x0001
#define VFS_POLLPRI  0x0002
#define VFS_POLLOUT  0x0004
#define VFS_POLLERR  0x0008
#define VFS_POLLHUP  0x0010
#define VFS_POLLNVAL 0x0020

typedef struct vfs_pollfd {
    int fd;
    int16_t events;
    int16_t revents;
} vfs_pollfd_t;

#ifndef _TIMEVAL_DEFINED
#define _TIMEVAL_DEFINED
struct timeval {
    int64_t tv_sec;
    int64_t tv_usec;
};
#endif

/* epoll constants and structures */
#define EPOLLIN      0x0001
#define EPOLLPRI     0x0002
#define EPOLLOUT     0x0004
#define EPOLLERR     0x0008
#define EPOLLHUP     0x0010
#define EPOLLRDHUP   0x2000
#define EPOLLONESHOT (1U << 30)
#define EPOLLET      (1U << 31)

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

#define EPOLL_CLOEXEC 0x80000

typedef union epoll_data {
    void    *ptr;
    int      fd;
    uint32_t u32;
    uint64_t u64;
} epoll_data_t;

typedef struct epoll_event {
    uint32_t     events;
    epoll_data_t data;
} __attribute__((packed)) epoll_event_t;
typedef struct epoll_event vfs_epoll_event_t;

typedef struct vfs_fb_bitfield {
    uint32_t offset;
    uint32_t length;
    uint32_t msb_right;
} vfs_fb_bitfield_t;

typedef struct vfs_fb_var_screeninfo {
    uint32_t xres;
    uint32_t yres;
    uint32_t xres_virtual;
    uint32_t yres_virtual;
    uint32_t xoffset;
    uint32_t yoffset;
    uint32_t bits_per_pixel;
    uint32_t grayscale;
    vfs_fb_bitfield_t red;
    vfs_fb_bitfield_t green;
    vfs_fb_bitfield_t blue;
    vfs_fb_bitfield_t transp;
    uint32_t nonstd;
    uint32_t activate;
    uint32_t height;
    uint32_t width;
    uint32_t accel_flags;
    uint32_t pixclock;
    uint32_t left_margin;
    uint32_t right_margin;
    uint32_t upper_margin;
    uint32_t lower_margin;
    uint32_t hsync_len;
    uint32_t vsync_len;
    uint32_t sync;
    uint32_t vmode;
    uint32_t rotate;
    uint32_t colorspace;
    uint32_t reserved[4];
} vfs_fb_var_screeninfo_t;

typedef struct vfs_fb_fix_screeninfo {
    char id[16];
    uintptr_t smem_start;
    uint32_t smem_len;
    uint32_t type;
    uint32_t visual;
    uint32_t line_length;
} vfs_fb_fix_screeninfo_t;

/* Input event types (EV_*) */
#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_ABS 0x03
#define EV_MSC 0x04

/* Synchronization events */
#define SYN_REPORT 0x00

/* Relative axes (REL_*) */
#define REL_X      0x00
#define REL_Y      0x01
#define REL_Z      0x02
#define REL_WHEEL  0x08

/* Mouse buttons (BTN_*) */
#define BTN_MOUSE   0x110
#define BTN_LEFT    0x110
#define BTN_RIGHT   0x111
#define BTN_MIDDLE  0x112
#define BTN_SIDE    0x113
#define BTN_EXTRA   0x114

/* Standard Linux keycodes */
#define KEY_RESERVED    0
#define KEY_ESC         1
#define KEY_1           2
#define KEY_2           3
#define KEY_3           4
#define KEY_4           5
#define KEY_5           6
#define KEY_6           7
#define KEY_7           8
#define KEY_8           9
#define KEY_9           10
#define KEY_0           11
#define KEY_MINUS       12
#define KEY_EQUAL       13
#define KEY_BACKSPACE   14
#define KEY_TAB         15
#define KEY_Q           16
#define KEY_W           17
#define KEY_E           18
#define KEY_R           19
#define KEY_T           20
#define KEY_Y           21
#define KEY_U           22
#define KEY_I           23
#define KEY_O           24
#define KEY_P           25
#define KEY_LEFTBRACE   26
#define KEY_RIGHTBRACE  27
#define KEY_ENTER       28
#define KEY_LEFTCTRL    29
#define KEY_A           30
#define KEY_S           31
#define KEY_D           32
#define KEY_F           33
#define KEY_G           34
#define KEY_H           35
#define KEY_J           36
#define KEY_K           37
#define KEY_L           38
#define KEY_SEMICOLON   39
#define KEY_APOSTROPHE  40
#define KEY_GRAVE       41
#define KEY_LEFTSHIFT   42
#define KEY_BACKSLASH   43
#define KEY_Z           44
#define KEY_X           45
#define KEY_C           46
#define KEY_V           47
#define KEY_B           48
#define KEY_N           49
#define KEY_M           50
#define KEY_COMMA       51
#define KEY_DOT         52
#define KEY_SLASH       53
#define KEY_RIGHTSHIFT  54
#define KEY_KPASTERISK  55
#define KEY_LEFTALT     56
#define KEY_SPACE       57
#define KEY_CAPSLOCK    58
#define KEY_F1          59
#define KEY_F2          60
#define KEY_F3          61
#define KEY_F4          62
#define KEY_F5          63
#define KEY_F6          64
#define KEY_F7          65
#define KEY_F8          66
#define KEY_F9          67
#define KEY_F10         68
#define KEY_NUMLOCK     69
#define KEY_SCROLLLOCK  70
#define KEY_F11         87
#define KEY_F12         88
#define KEY_UP          103
#define KEY_LEFT        105
#define KEY_RIGHT       106
#define KEY_DOWN        108

/* Standard evdev input packet (Phase 1 specification) */
struct input_event {
    uint32_t type;    /* EV_KEY, EV_REL, EV_ABS */
    uint16_t code;    /* Keycode or Button (BTN_LEFT, BTN_RIGHT) */
    int32_t  value;   /* 1: Press, 0: Release, dx, dy, scroll */
    uint64_t timestamp_ms;
};

/* POSIX mode/type bits for vfs_stat_t */
#define VFS_S_IFMT   0170000U
#define VFS_S_IFSOCK 0140000U
#define VFS_S_IFLNK  0120000U
#define VFS_S_IFREG  0100000U
#define VFS_S_IFBLK  0060000U
#define VFS_S_IFDIR  0040000U
#define VFS_S_IFCHR  0020000U
#define VFS_S_IFIFO  0010000U
#define VFS_S_ISUID  04000U
#define VFS_S_ISGID  02000U
#define VFS_S_ISVTX  01000U

/* flock(2) operations */
#ifndef LOCK_SH
#define LOCK_SH 1
#define LOCK_EX 2
#define LOCK_NB 4
#define LOCK_UN 8
#endif

/* access(2) mode flags */
#ifndef F_OK
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
#endif

/* mount(2) / umount2(2) flags */
#ifndef MS_RDONLY
#define MS_RDONLY       1
#define MS_NOSUID       2
#define MNT_FORCE       1
#define MNT_DETACH      2
#define MNT_EXPIRE      4
#define UMOUNT_NOFOLLOW 8
#endif

/* getdents64(2) directory entry types and ABI struct */
#ifndef DT_UNKNOWN
#define DT_UNKNOWN 0
#define DT_FIFO    1
#define DT_CHR     2
#define DT_DIR     4
#define DT_BLK     6
#define DT_REG     8
#define DT_LNK     10
#define DT_SOCK    12
#endif

#ifndef _LINUX_DIRENT64_DEFINED
#define _LINUX_DIRENT64_DEFINED
struct linux_dirent64 {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[];
};
#endif

#endif /* TSUKASA_VFS_ABI_H */
