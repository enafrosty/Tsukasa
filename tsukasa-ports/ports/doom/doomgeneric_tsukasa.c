/*
 * Project Tsukasa — DOOM Hardware Display & Input Driver (libvanilla + fb0)
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

#include "doomgeneric.h"

/* 1. Include DOOM internal key definitions first */
#include "doomkeys.h"

/* 2. Capture DOOM constants into compile-time enum constants */
enum {
    TSUKASA_DOOM_RIGHTARROW  = KEY_RIGHTARROW,
    TSUKASA_DOOM_LEFTARROW   = KEY_LEFTARROW,
    TSUKASA_DOOM_UPARROW     = KEY_UPARROW,
    TSUKASA_DOOM_DOWNARROW   = KEY_DOWNARROW,
    TSUKASA_DOOM_USE         = KEY_USE,
    TSUKASA_DOOM_FIRE        = KEY_FIRE,
    TSUKASA_DOOM_ESCAPE      = KEY_ESCAPE,      /* ASCII 27 */
    TSUKASA_DOOM_ENTER       = KEY_ENTER,       /* ASCII 13 */
    TSUKASA_DOOM_TAB         = KEY_TAB,         /* ASCII 9 */
    TSUKASA_DOOM_BACKSPACE   = KEY_BACKSPACE,   /* ASCII 127 */
    TSUKASA_DOOM_RSHIFT      = KEY_RSHIFT,
    TSUKASA_DOOM_RCTRL       = KEY_RCTRL,
    TSUKASA_DOOM_RALT        = KEY_RALT,
    TSUKASA_DOOM_LALT        = KEY_LALT,
    TSUKASA_DOOM_F1          = KEY_F1,
    TSUKASA_DOOM_F2          = KEY_F2,
    TSUKASA_DOOM_F3          = KEY_F3,
    TSUKASA_DOOM_F4          = KEY_F4,
    TSUKASA_DOOM_F5          = KEY_F5,
    TSUKASA_DOOM_F6          = KEY_F6,
    TSUKASA_DOOM_F7          = KEY_F7,
    TSUKASA_DOOM_F8          = KEY_F8,
    TSUKASA_DOOM_F9          = KEY_F9,
    TSUKASA_DOOM_F10         = KEY_F10,
    TSUKASA_DOOM_F11         = KEY_F11,
    TSUKASA_DOOM_F12         = KEY_F12,
    TSUKASA_DOOM_EQUALS      = KEY_EQUALS,
    TSUKASA_DOOM_MINUS       = KEY_MINUS,
    TSUKASA_DOOM_PAUSE       = KEY_PAUSE,
};

/* 3. Undefine all DOOM keys to prevent collision with evdev scancodes */
#undef KEY_RIGHTARROW
#undef KEY_LEFTARROW
#undef KEY_UPARROW
#undef KEY_DOWNARROW
#undef KEY_STRAFE_L
#undef KEY_STRAFE_R
#undef KEY_USE
#undef KEY_FIRE
#undef KEY_ESCAPE
#undef KEY_ENTER
#undef KEY_TAB
#undef KEY_F1
#undef KEY_F2
#undef KEY_F3
#undef KEY_F4
#undef KEY_F5
#undef KEY_F6
#undef KEY_F7
#undef KEY_F8
#undef KEY_F9
#undef KEY_F10
#undef KEY_F11
#undef KEY_F12
#undef KEY_BACKSPACE
#undef KEY_PAUSE
#undef KEY_EQUALS
#undef KEY_MINUS
#undef KEY_RSHIFT
#undef KEY_RCTRL
#undef KEY_RALT
#undef KEY_LALT
#undef KEY_CAPSLOCK
#undef KEY_NUMLOCK
#undef KEY_SCRLCK
#undef KEY_PRTSCR
#undef KEY_HOME
#undef KEY_END
#undef KEY_PGUP
#undef KEY_PGDN
#undef KEY_INS
#undef KEY_DEL
#undef KEYP_0
#undef KEYP_1
#undef KEYP_2
#undef KEYP_3
#undef KEYP_4
#undef KEYP_5
#undef KEYP_6
#undef KEYP_7
#undef KEYP_8
#undef KEYP_9
#undef KEYP_DIVIDE
#undef KEYP_PLUS
#undef KEYP_MINUS
#undef KEYP_MULTIPLY
#undef KEYP_PERIOD
#undef KEYP_EQUALS
#undef KEYP_ENTER

#define KEY_PAUSE TSUKASA_DOOM_PAUSE

/* 4. Safely include Vanilla SDK, libc, and system evdev input headers */
#include "vanilla.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/input.h>
#include <time.h>

#define DOOM_WINDOW_WIDTH  640
#define DOOM_WINDOW_HEIGHT 400
#define KEY_QUEUE_CAPACITY 128

typedef struct {
    int           pressed;
    unsigned char key;
} key_event_t;

/* Vanilla display server client state */
static vanilla_client_t *g_client = NULL;
static vanilla_window_t *g_win    = NULL;

/* Direct framebuffer fallback state */
static int                       g_fb_fd    = -1;
static int                       g_input_fd = -1;
static uint32_t                 *g_fb_mem   = NULL;
static struct fb_var_screeninfo  g_vinfo;

/* Ring buffer for input event queueing */
static key_event_t g_key_queue[KEY_QUEUE_CAPACITY];
static int         g_key_head = 0;
static int         g_key_tail = 0;

static void enqueue_key(int pressed, unsigned char key)
{
    int next = (g_key_tail + 1) % KEY_QUEUE_CAPACITY;
    if (next != g_key_head) {
        g_key_queue[g_key_tail].pressed = pressed;
        g_key_queue[g_key_tail].key     = key;
        g_key_tail                      = next;
    }
}

static int dequeue_key(int *pressed, unsigned char *key)
{
    if (g_key_head == g_key_tail)
        return 0;

    *pressed   = g_key_queue[g_key_head].pressed;
    *key       = g_key_queue[g_key_head].key;
    g_key_head = (g_key_head + 1) % KEY_QUEUE_CAPACITY;
    return 1;
}

/*
 * Translate Tsukasa evdev scancodes to DOOM internal key constants.
 * Decouples mouse buttons and provides full alphanumeric and navigation mapping.
 */
static unsigned char translate_key(uint16_t code)
{
    switch (code) {
    /* Navigation: Arrow keys and WASD */
    case KEY_UP:
    case KEY_W:          return TSUKASA_DOOM_UPARROW;
    case KEY_DOWN:
    case KEY_S:          return TSUKASA_DOOM_DOWNARROW;
    case KEY_LEFT:
    case KEY_A:          return TSUKASA_DOOM_LEFTARROW;
    case KEY_RIGHT:
    case KEY_D:          return TSUKASA_DOOM_RIGHTARROW;

    /* Menu Confirmation, Cancel, and Navigation */
    case KEY_ENTER:
    case KEY_KPENTER:    return TSUKASA_DOOM_ENTER;      /* ASCII 13 */
    case KEY_ESC:        return TSUKASA_DOOM_ESCAPE;     /* ASCII 27 */
    case KEY_TAB:        return TSUKASA_DOOM_TAB;        /* ASCII 9 */
    case KEY_BACKSPACE:  return TSUKASA_DOOM_BACKSPACE;  /* ASCII 127 */
    case KEY_SPACE:      return ' ';                     /* ASCII 32 */

    /* Action Keys */
    case KEY_E:          return TSUKASA_DOOM_USE;
    case KEY_LEFTCTRL:
#ifdef KEY_RIGHTCTRL
    case KEY_RIGHTCTRL:
#endif
        return TSUKASA_DOOM_FIRE;
    case KEY_LEFTSHIFT:
    case KEY_RIGHTSHIFT: return TSUKASA_DOOM_RSHIFT;
    case KEY_LEFTALT:
#ifdef KEY_RIGHTALT
    case KEY_RIGHTALT:
#endif
        return TSUKASA_DOOM_LALT;

    /* Weapon Selection: Numbers 1 through 0 */
    case KEY_1:          return '1';
    case KEY_2:          return '2';
    case KEY_3:          return '3';
    case KEY_4:          return '4';
    case KEY_5:          return '5';
    case KEY_6:          return '6';
    case KEY_7:          return '7';
    case KEY_8:          return '8';
    case KEY_9:          return '9';
    case KEY_0:          return '0';

    /* Full Alphabetic Coverage for menus, shortcuts, and confirmations */
    case KEY_B:          return 'b';
    case KEY_C:          return 'c';
    case KEY_F:          return 'f';
    case KEY_G:          return 'g';
    case KEY_H:          return 'h';
    case KEY_I:          return 'i';
    case KEY_J:          return 'j';
    case KEY_K:          return 'k';
    case KEY_L:          return 'l';
    case KEY_M:          return 'm';
    case KEY_N:          return 'n';
    case KEY_O:          return 'o';
    case KEY_P:          return KEY_PAUSE;               /* DOOM pause constant 0xff */
    case KEY_Q:          return 'q';
    case KEY_R:          return 'r';
    case KEY_T:          return 't';
    case KEY_U:          return 'u';
    case KEY_V:          return 'v';
    case KEY_X:          return 'x';
    case KEY_Y:          return 'y';
    case KEY_Z:          return 'z';

    /* Function Keys */
    case KEY_F1:         return TSUKASA_DOOM_F1;
    case KEY_F2:         return TSUKASA_DOOM_F2;
    case KEY_F3:         return TSUKASA_DOOM_F3;
    case KEY_F4:         return TSUKASA_DOOM_F4;
    case KEY_F5:         return TSUKASA_DOOM_F5;
    case KEY_F6:         return TSUKASA_DOOM_F6;
    case KEY_F7:         return TSUKASA_DOOM_F7;
    case KEY_F8:         return TSUKASA_DOOM_F8;
    case KEY_F9:         return TSUKASA_DOOM_F9;
    case KEY_F10:        return TSUKASA_DOOM_F10;
    case KEY_F11:        return TSUKASA_DOOM_F11;
    case KEY_F12:        return TSUKASA_DOOM_F12;

    /* Punctuation and Screen Scaling */
    case KEY_MINUS:      return TSUKASA_DOOM_MINUS;
    case KEY_EQUAL:      return TSUKASA_DOOM_EQUALS;

    /* Mouse buttons ignored to prevent fake key injections */
    case BTN_LEFT:
    case BTN_RIGHT:
    case BTN_MIDDLE:
    default:
        return 0;
    }
}

void DG_Init(void)
{
    /* Attempt connection to Project Vanilla display server */
    g_client = vanilla_connect(NULL);
    if (g_client) {
        g_win = vanilla_create_window(g_client, "DOOM",
                                      40, 40,
                                      DOOM_WINDOW_WIDTH, DOOM_WINDOW_HEIGHT,
                                      WINDOW_FLAG_NONE);
        if (g_win) {
            vanilla_map_window(g_win);
            printf("[doom] Initialized in Project Vanilla window mode (640x400)\n");
            return;
        }
        vanilla_disconnect(g_client);
        g_client = NULL;
    }

    /* Fallback: Direct hardware framebuffer /dev/fb0 and /dev/input/events */
    printf("[doom] Vanilla display server unavailable; using direct framebuffer\n");
    g_fb_fd = open("/dev/fb0", O_RDWR);
    if (g_fb_fd < 0) {
        printf("[doom] Error: cannot open /dev/fb0\n");
        exit(1);
    }

    if (ioctl(g_fb_fd, FBIOGET_VSCREENINFO, &g_vinfo) < 0) {
        printf("[doom] Error: cannot read framebuffer info\n");
        close(g_fb_fd);
        exit(1);
    }

    size_t fb_size = (size_t)g_vinfo.xres * g_vinfo.yres *
                     (g_vinfo.bits_per_pixel ? (g_vinfo.bits_per_pixel / 8) : 4);
    g_fb_mem = (uint32_t *)mmap(NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, g_fb_fd, 0);
    if (g_fb_mem == MAP_FAILED || !g_fb_mem) {
        printf("[doom] Error: cannot map framebuffer\n");
        close(g_fb_fd);
        exit(1);
    }

    g_input_fd = open("/dev/input/events", O_RDONLY | O_NONBLOCK);
}

void DG_DrawFrame(void)
{
    /* Project Vanilla window surface presentation with alpha enforcement */
    if (g_win && g_win->surface.pixels) {
        uint32_t *dst = g_win->surface.pixels;
        if (DOOMGENERIC_RESX == DOOM_WINDOW_WIDTH && DOOMGENERIC_RESY == DOOM_WINDOW_HEIGHT) {
            for (int i = 0; i < DOOM_WINDOW_WIDTH * DOOM_WINDOW_HEIGHT; i++)
                dst[i] = DG_ScreenBuffer[i] | 0xFF000000u;
        } else {
            for (int y = 0; y < DOOMGENERIC_RESY && (y * 2 + 1) < DOOM_WINDOW_HEIGHT; y++) {
                const uint32_t *src_row = &DG_ScreenBuffer[y * DOOMGENERIC_RESX];
                uint32_t *dst_row0 = &dst[(y * 2) * DOOM_WINDOW_WIDTH];
                uint32_t *dst_row1 = &dst[(y * 2 + 1) * DOOM_WINDOW_WIDTH];
                for (int x = 0; x < DOOMGENERIC_RESX && (x * 2 + 1) < DOOM_WINDOW_WIDTH; x++) {
                    uint32_t px = src_row[x] | 0xFF000000u;
                    dst_row0[x * 2]     = px;
                    dst_row0[x * 2 + 1] = px;
                    dst_row1[x * 2]     = px;
                    dst_row1[x * 2 + 1] = px;
                }
            }
        }
        vanilla_present(g_win, NULL);
        return;
    }

    /* Fallback direct framebuffer rendering with alpha enforcement */
    if (!g_fb_mem)
        return;

    uint32_t pitch_px = g_vinfo.xres;
    int scale_x = (int)g_vinfo.xres / DOOMGENERIC_RESX;
    int scale_y = (int)g_vinfo.yres / DOOMGENERIC_RESY;
    int scale = (scale_x < scale_y) ? scale_x : scale_y;
    if (scale < 1)
        scale = 1;

    int offset_x = ((int)g_vinfo.xres - (DOOMGENERIC_RESX * scale)) / 2;
    int offset_y = ((int)g_vinfo.yres - (DOOMGENERIC_RESY * scale)) / 2;

    for (int y = 0; y < DOOMGENERIC_RESY; y++) {
        const uint32_t *src_row = &DG_ScreenBuffer[y * DOOMGENERIC_RESX];
        for (int sy = 0; sy < scale; sy++) {
            uint32_t *dst_row = &g_fb_mem[(offset_y + y * scale + sy) * pitch_px + offset_x];
            for (int x = 0; x < DOOMGENERIC_RESX; x++) {
                uint32_t pixel = src_row[x] | 0xFF000000u;
                for (int sx = 0; sx < scale; sx++) {
                    dst_row[x * scale + sx] = pixel;
                }
            }
        }
    }
}

void DG_SleepMs(uint32_t ms)
{
    usleep((useconds_t)ms * 1000);
}

uint32_t DG_GetTicksMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

int DG_GetKey(int *pressed, unsigned char *doomKey)
{
    /* Drain any pre-queued events first */
    if (dequeue_key(pressed, doomKey))
        return 1;

    /* Poll events from Project Vanilla display server */
    if (g_client && g_win) {
        vanilla_event_t ev;
        while (vanilla_poll_event(g_client, &ev) > 0) {
            if (ev.type == VANILLA_EVENT_CLOSE_REQ) {
                vanilla_destroy_window(g_win);
                vanilla_disconnect(g_client);
                g_win = NULL;
                g_client = NULL;
                exit(0);
            } else if (ev.type == VANILLA_EVENT_INPUT) {
                if (ev.input.type == EV_KEY) {
                    unsigned char dk = translate_key((uint16_t)ev.input.code);
                    if (dk != 0) {
                        enqueue_key((ev.input.value != 0) ? 1 : 0, dk);
                    }
                }
            }
        }
    } else if (g_input_fd >= 0) {
        /* Fallback: read directly from /dev/input/events */
        struct input_event ev;
        while (read(g_input_fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
            if (ev.type == EV_KEY) {
                unsigned char dk = translate_key((uint16_t)ev.code);
                if (dk != 0) {
                    enqueue_key((ev.value != 0) ? 1 : 0, dk);
                }
            }
        }
    }

    return dequeue_key(pressed, doomKey);
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}

int main(int argc, char **argv)
{
    doomgeneric_Create(argc, argv);

    while (1) {
        doomgeneric_Tick();
    }

    if (g_win) {
        vanilla_destroy_window(g_win);
        g_win = NULL;
    }
    if (g_client) {
        vanilla_disconnect(g_client);
        g_client = NULL;
    }

    return 0;
}
