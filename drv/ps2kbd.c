/*
 * Project Tsukasa — PS/2 keyboard driver
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

#include "ps2.h"
#include "pic.h"
#include "irq.h"
#include "input_dev.h"
#include "../input/event.h"
#include "../include/vfs_abi.h"
#ifdef __x86_64__
#include "../tty/tty.h"
#endif

/* Scan code to ASCII (make codes, set 1). High bit = release. */
static const unsigned char scan_to_key[] = {
    0, 0x1B, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ',
};

/* Standard Linux evdev keycode table for PS/2 set 1 */
static const uint16_t ps2_set1_to_linux[89] = {
    [0x00] = KEY_RESERVED,
    [0x01] = KEY_ESC,
    [0x02] = KEY_1,
    [0x03] = KEY_2,
    [0x04] = KEY_3,
    [0x05] = KEY_4,
    [0x06] = KEY_5,
    [0x07] = KEY_6,
    [0x08] = KEY_7,
    [0x09] = KEY_8,
    [0x0A] = KEY_9,
    [0x0B] = KEY_0,
    [0x0C] = KEY_MINUS,
    [0x0D] = KEY_EQUAL,
    [0x0E] = KEY_BACKSPACE,
    [0x0F] = KEY_TAB,
    [0x10] = KEY_Q,
    [0x11] = KEY_W,
    [0x12] = KEY_E,
    [0x13] = KEY_R,
    [0x14] = KEY_T,
    [0x15] = KEY_Y,
    [0x16] = KEY_U,
    [0x17] = KEY_I,
    [0x18] = KEY_O,
    [0x19] = KEY_P,
    [0x1A] = KEY_LEFTBRACE,
    [0x1B] = KEY_RIGHTBRACE,
    [0x1C] = KEY_ENTER,
    [0x1D] = KEY_LEFTCTRL,
    [0x1E] = KEY_A,
    [0x1F] = KEY_S,
    [0x20] = KEY_D,
    [0x21] = KEY_F,
    [0x22] = KEY_G,
    [0x23] = KEY_H,
    [0x24] = KEY_J,
    [0x25] = KEY_K,
    [0x26] = KEY_L,
    [0x27] = KEY_SEMICOLON,
    [0x28] = KEY_APOSTROPHE,
    [0x29] = KEY_GRAVE,
    [0x2A] = KEY_LEFTSHIFT,
    [0x2B] = KEY_BACKSLASH,
    [0x2C] = KEY_Z,
    [0x2D] = KEY_X,
    [0x2E] = KEY_C,
    [0x2F] = KEY_V,
    [0x30] = KEY_B,
    [0x31] = KEY_N,
    [0x32] = KEY_M,
    [0x33] = KEY_COMMA,
    [0x34] = KEY_DOT,
    [0x35] = KEY_SLASH,
    [0x36] = KEY_RIGHTSHIFT,
    [0x37] = KEY_KPASTERISK,
    [0x38] = KEY_LEFTALT,
    [0x39] = KEY_SPACE,
    [0x3A] = KEY_CAPSLOCK,
    [0x3B] = KEY_F1,
    [0x3C] = KEY_F2,
    [0x3D] = KEY_F3,
    [0x3E] = KEY_F4,
    [0x3F] = KEY_F5,
    [0x40] = KEY_F6,
    [0x41] = KEY_F7,
    [0x42] = KEY_F8,
    [0x43] = KEY_F9,
    [0x44] = KEY_F10,
    [0x45] = KEY_NUMLOCK,
    [0x46] = KEY_SCROLLLOCK,
    [0x57] = KEY_F11,
    [0x58] = KEY_F12,
};

static uint16_t ps2_ext_to_linux(uint8_t sc)
{
    switch (sc) {
    case 0x1C: return KEY_ENTER;
    case 0x1D: return KEY_LEFTCTRL;
    case 0x35: return KEY_SLASH;
    case 0x38: return KEY_LEFTALT;
    case 0x47: return 102;
    case 0x48: return KEY_UP;
    case 0x49: return 104;
    case 0x4B: return KEY_LEFT;
    case 0x4D: return KEY_RIGHT;
    case 0x4F: return 107;
    case 0x50: return KEY_DOWN;
    case 0x51: return 109;
    case 0x52: return 110;
    case 0x53: return 111;
    default: return KEY_RESERVED;
    }
}

static uint8_t g_ext_prefix;

void ps2kbd_handler(void)
{
    unsigned char sc = inb(PS2_DATA);
    unsigned char key = 0;
    int press;
    unsigned int code;
    uint32_t keycode;
    uint16_t linux_key;
    struct gui_event e;

    if (sc == 0xE0) {
        g_ext_prefix = 1;
        irq_ack(1);
        return;
    }

    press = ((sc & 0x80u) == 0u);
    code = sc & 0x7Fu;

    if (g_ext_prefix && code == 0x2A) {
        g_ext_prefix = 0;
        irq_ack(1);
        return;
    }

    /* Translate to Linux evdev keycode and push to /dev/input/events */
    if (g_ext_prefix)
        linux_key = ps2_ext_to_linux((uint8_t)code);
    else if (code < sizeof(ps2_set1_to_linux) / sizeof(ps2_set1_to_linux[0]))
        linux_key = ps2_set1_to_linux[code];
    else
        linux_key = KEY_RESERVED;

    if (linux_key != KEY_RESERVED) {
        input_dev_push(EV_KEY, linux_key, press ? 1 : 0);
        input_dev_push(EV_SYN, SYN_REPORT, 0);
    }

    if (g_ext_prefix && code == 0x37) {
        keycode = INPUT_KEY_PRINTSCREEN;
    } else {
        if (code < sizeof(scan_to_key) / sizeof(scan_to_key[0]))
            key = scan_to_key[code];
        keycode = key ? (uint32_t)key : (uint32_t)(g_ext_prefix ? (0xE000u | code) : code);
    }

    e.event_id = press ? INPUT_EVENT_KEY : INPUT_EVENT_KEYUP;
    e.type = EVENT_KEY;
    e.subtype = press ? KEY_PRESS : KEY_RELEASE;
    e.keycode = keycode;
    e.x = 0;
    e.y = 0;
    e.wheel_delta = 0;
    e.width = 0;
    e.height = 0;
    e.modifiers = 0;
    e.window_id = -1;

#ifdef __x86_64__
    if (!g_ext_prefix)
        tty_handle_scancode((uint8_t)code, press);
#endif
    event_enqueue(&e);
    g_ext_prefix = 0;
    irq_ack(1);
}
