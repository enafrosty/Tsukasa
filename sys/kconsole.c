/*
 * Project Tsukasa — Kernel console implementation
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

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "sys/kconsole.h"
#include "include/spinlock.h"
#include "include/kprintf.h"
#include "drv/fb.h"
#include "gfx/blit.h"
#include "gfx/font.h"

static spinlock_t console_lock = SPINLOCK_INIT;
static int cursor_x = 0;
static int cursor_y = 0;
static bool kconsole_active = false;
static uint32_t text_color = 0xFFFFFFFF;
static uint32_t bg_color = 0xFF000000;

#define CHAR_WIDTH  8
#define CHAR_HEIGHT 10

static bool fb_ok(void) {
    return fb_info.addr && fb_info.bpp == 32;
}

void kconsole_init(void) {
    cursor_x = 10;
    cursor_y = 10;
    kconsole_active = true;
    if (fb_ok())
        fb_fill_rect(0, 0, (int)fb_info.width, (int)fb_info.height, bg_color);
}

void kconsole_set_active(bool active) {
    kconsole_active = active;
}

void kconsole_set_color(uint32_t color) {
    spin_lock(&console_lock);
    text_color = color;
    spin_unlock(&console_lock);
}

static void kconsole_scroll(void) {
    if (cursor_y + CHAR_HEIGHT >= (int)fb_info.height - 10) {
        cursor_y = 10;
        fb_fill_rect(0, 0, (int)fb_info.width, (int)fb_info.height, bg_color);
    }
}

static void kconsole_putc_nolock(char c) {
    if (!kconsole_active || !fb_ok()) return;

    if (c == '\n') {
        cursor_x = 10;
        cursor_y += CHAR_HEIGHT;
        kconsole_scroll();
        return;
    }

    if (c == '\r') {
        cursor_x = 10;
        return;
    }

    if (c == '\t') {
        cursor_x += CHAR_WIDTH * 4;
        return;
    }

    char s[2] = { c, 0 };
    fb_draw_string(cursor_x, cursor_y, s, text_color, bg_color);

    cursor_x += CHAR_WIDTH;
    if (cursor_x + CHAR_WIDTH >= (int)fb_info.width - 10) {
        cursor_x = 10;
        cursor_y += CHAR_HEIGHT;
        kconsole_scroll();
    }
}

void kconsole_putc(char c) {
    spin_lock(&console_lock);
    kconsole_putc_nolock(c);
    spin_unlock(&console_lock);
}

void kconsole_write(const char *s) {
    if (!s) return;

    spin_lock(&console_lock);
    if (!kconsole_active) {
        spin_unlock(&console_lock);
        return;
    }

    while (*s) {
        kconsole_putc_nolock(*s++);
    }
    spin_unlock(&console_lock);
}

void log_ok(const char *msg) {
    kprintf("[ OK ] %s\n", msg);
    kconsole_set_color(0xFF00CC44);
    kconsole_write("[ OK ] ");
    kconsole_set_color(0xFFFFFFFF);
    kconsole_write(msg);
    kconsole_write("\n");
}

void log_fail(const char *msg) {
    kprintf("[FAIL] %s\n", msg);
    kconsole_set_color(0xFFDD2222);
    kconsole_write("[FAIL] ");
    kconsole_set_color(0xFFFFFFFF);
    kconsole_write(msg);
    kconsole_write("\n");
}
