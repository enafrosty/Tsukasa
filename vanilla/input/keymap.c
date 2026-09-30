/*
 * Project Tsukasa — Unified Evdev to ASCII Keymap Implementation
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

#include "keymap.h"
#include <sys/input.h>

char vanilla_evdev_to_ascii(uint16_t code, int shift)
{
    if (code >= KEY_1 && code <= KEY_9) {
        static const char num_normal[] = "123456789";
        static const char num_shift[]  = "!@#$%^&*(";
        return shift ? num_shift[code - KEY_1] : num_normal[code - KEY_1];
    }
    if (code == KEY_0)
        return shift ? ')' : '0';

    if (code >= KEY_Q && code <= KEY_P) {
        static const char row1[] = "qwertyuiop";
        char c = row1[code - KEY_Q];
        return shift ? (char)(c - 32) : c;
    }
    if (code >= KEY_A && code <= KEY_L) {
        static const char row2[] = "asdfghjkl";
        char c = row2[code - KEY_A];
        return shift ? (char)(c - 32) : c;
    }
    if (code >= KEY_Z && code <= KEY_M) {
        static const char row3[] = "zxcvbnm";
        char c = row3[code - KEY_Z];
        return shift ? (char)(c - 32) : c;
    }

    switch (code) {
    case KEY_SPACE:      return ' ';
    case KEY_ENTER:      return '\n';
    case KEY_BACKSPACE:  return '\b';
    case KEY_TAB:        return '\t';
    case KEY_MINUS:      return shift ? '_' : '-';
    case KEY_EQUAL:      return shift ? '+' : '=';
    case KEY_LEFTBRACE:  return shift ? '{' : '[';
    case KEY_RIGHTBRACE: return shift ? '}' : ']';
    case KEY_SEMICOLON:  return shift ? ':' : ';';
    case KEY_APOSTROPHE: return shift ? '"' : '\'';
    case KEY_GRAVE:      return shift ? '~' : '`';
    case KEY_BACKSLASH:  return shift ? '|' : '\\';
    case KEY_COMMA:      return shift ? '<' : ',';
    case KEY_DOT:        return shift ? '>' : '.';
    case KEY_SLASH:      return shift ? '?' : '/';
    case KEY_KP0:        return '0';
    case KEY_KP1:        return '1';
    case KEY_KP2:        return '2';
    case KEY_KP3:        return '3';
    case KEY_KP4:        return '4';
    case KEY_KP5:        return '5';
    case KEY_KP6:        return '6';
    case KEY_KP7:        return '7';
    case KEY_KP8:        return '8';
    case KEY_KP9:        return '9';
    case KEY_KPDOT:      return '.';
    case KEY_KPENTER:    return '\n';
    case KEY_KPPLUS:     return '+';
    case KEY_KPMINUS:    return '-';
    case KEY_KPASTERISK: return '*';
    case KEY_KPSLASH:    return '/';
    default:             return 0;
    }
}
