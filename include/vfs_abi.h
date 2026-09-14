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
#define VFS_PROT_READ  0x1
#define VFS_PROT_WRITE 0x2

/* mmap flags (VFS_MAP_*) */
#define VFS_MAP_SHARED     0x01
#define VFS_MAP_PRIVATE    0x02
#define VFS_MAP_ANONYMOUS  0x20
#define VFS_MAP_ANON       VFS_MAP_ANONYMOUS

/* framebuffer fix-info classification */
#define VFS_FB_TYPE_PACKED_PIXELS 0
#define VFS_FB_VISUAL_TRUECOLOR   2

/* framebuffer ioctls (Linux fbdev request numbers) */
#define VFS_FBIOGET_VSCREENINFO 0x4600
#define VFS_FBIOGET_FSCREENINFO 0x4602

/* KD console mode ioctl (Linux KDSETMODE) */
#define VFS_KDSETMODE   0x4B3A
#define VFS_KD_TEXT     0x00
#define VFS_KD_GRAPHICS 0x01

/* poll event bits */
#define VFS_POLLIN   0x0001
#define VFS_POLLOUT  0x0004
#define VFS_POLLERR  0x0008
#define VFS_POLLHUP  0x0010

typedef struct vfs_pollfd {
    int fd;
    int16_t events;
    int16_t revents;
} vfs_pollfd_t;

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

#endif /* TSUKASA_VFS_ABI_H */
