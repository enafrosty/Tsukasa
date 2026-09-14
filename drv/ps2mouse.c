/*
 * Project Tsukasa — PS/2 mouse driver
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
#include "ps2mouse.h"
#include "input_dev.h"
#include "../input/event.h"
#include "../gfx/cursor.h"
#include "../include/vfs_abi.h"
#include <stdint.h>

/* Mouse packet state machine. */
static uint8_t mouse_cycle;
static int8_t  mouse_bytes[3];
static uint8_t mouse_buttons_prev;

/* Wait for PS/2 controller to be ready for a command byte. */
static void ps2_wait_write(void)
{
    int timeout = 100000;
    while (timeout-- > 0) {
        if (!(inb(PS2_CMD) & 0x02))
            return;
    }
}

/* Wait for PS/2 controller to have data ready to read. */
static void ps2_wait_read(void)
{
    int timeout = 100000;
    while (timeout-- > 0) {
        if (inb(PS2_CMD) & 0x01)
            return;
    }
}

/* Write a byte to the mouse (via PS/2 controller port 0x64). */
static void ps2_mouse_write(uint8_t data)
{
    ps2_wait_write();
    outb(PS2_CMD, 0xD4);
    ps2_wait_write();
    outb(PS2_DATA, data);
}

/* Read response from mouse. */
static uint8_t ps2_mouse_read(void)
{
    ps2_wait_read();
    return inb(PS2_DATA);
}

void ps2mouse_init(void)
{
    mouse_cycle = 0;
    mouse_buttons_prev = 0;

    ps2_wait_write();
    outb(PS2_CMD, 0xA8);

    ps2_wait_write();
    outb(PS2_CMD, 0x20);
    ps2_wait_read();
    uint8_t config = inb(PS2_DATA);
    config |= 0x02;
    ps2_wait_write();
    outb(PS2_CMD, 0x60);
    ps2_wait_write();
    outb(PS2_DATA, config);

    ps2_mouse_write(0xF6);
    ps2_mouse_read();

    ps2_mouse_write(0xF4);
    ps2_mouse_read();

    pic_unmask_irq(12);
    pic_unmask_irq(2);
}

void ps2mouse_handler(void)
{
    uint8_t data = inb(PS2_DATA);

    switch (mouse_cycle) {
    case 0:
        mouse_bytes[0] = (int8_t)data;
        if (data & 0x08)
            mouse_cycle = 1;
        break;
    case 1:
        mouse_bytes[1] = (int8_t)data;
        mouse_cycle = 2;
        break;
    case 2:
        mouse_bytes[2] = (int8_t)data;
        mouse_cycle = 0;

        {
            uint8_t status = (uint8_t)mouse_bytes[0];
            int dx = mouse_bytes[1];
            int dy = mouse_bytes[2];

            if (status & 0x10) dx |= 0xFFFFFF00;
            if (status & 0x20) dy |= 0xFFFFFF00;

            dy = -dy;

            cursor_move(dx, dy);

            uint8_t buttons = status & 0x07;
            uint8_t changed = buttons ^ mouse_buttons_prev;

            /* Push normalized evdev motion events */
            if (dx != 0)
                input_dev_push(EV_REL, REL_X, dx);
            if (dy != 0)
                input_dev_push(EV_REL, REL_Y, dy);

            /* Push normalized evdev button events */
            if (changed & MOUSE_BUTTON_LEFT)
                input_dev_push(EV_KEY, BTN_LEFT, (buttons & MOUSE_BUTTON_LEFT) ? 1 : 0);
            if (changed & MOUSE_BUTTON_RIGHT)
                input_dev_push(EV_KEY, BTN_RIGHT, (buttons & MOUSE_BUTTON_RIGHT) ? 1 : 0);
            if (changed & MOUSE_BUTTON_MIDDLE)
                input_dev_push(EV_KEY, BTN_MIDDLE, (buttons & MOUSE_BUTTON_MIDDLE) ? 1 : 0);

            if (dx != 0 || dy != 0 || changed != 0)
                input_dev_push(EV_SYN, SYN_REPORT, 0);

            /* Feed legacy desktop queue */
            if (dx != 0 || dy != 0) {
                struct gui_event ev;
                ev.event_id = INPUT_EVENT_MOUSE_MOVE;
                ev.type = EVENT_MOUSE;
                ev.subtype = MOUSE_MOVE;
                ev.keycode = buttons;
                ev.x = cursor_x();
                ev.y = cursor_y();
                ev.wheel_delta = 0;
                ev.width = 0;
                ev.height = 0;
                ev.modifiers = 0;
                ev.window_id = -1;
                event_enqueue(&ev);
            }

            if (changed & MOUSE_BUTTON_LEFT) {
                struct gui_event ev;
                int pressed = (buttons & MOUSE_BUTTON_LEFT) != 0;
                ev.event_id = pressed ? INPUT_EVENT_MOUSE_DOWN : INPUT_EVENT_MOUSE_UP;
                ev.type = EVENT_MOUSE;
                ev.subtype = pressed ? MOUSE_BTN_DOWN : MOUSE_BTN_UP;
                ev.keycode = buttons;
                ev.x = cursor_x();
                ev.y = cursor_y();
                ev.wheel_delta = 0;
                ev.width = 0;
                ev.height = 0;
                ev.modifiers = MOUSE_BUTTON_LEFT;
                ev.window_id = -1;
                event_enqueue(&ev);
            }
            if (changed & MOUSE_BUTTON_RIGHT) {
                struct gui_event ev;
                int pressed = (buttons & MOUSE_BUTTON_RIGHT) != 0;
                ev.event_id = pressed ? INPUT_EVENT_RIGHT_CLICK : INPUT_EVENT_MOUSE_UP;
                ev.type = EVENT_MOUSE;
                ev.subtype = pressed ? MOUSE_BTN_DOWN : MOUSE_BTN_UP;
                ev.keycode = buttons;
                ev.x = cursor_x();
                ev.y = cursor_y();
                ev.wheel_delta = 0;
                ev.width = 0;
                ev.height = 0;
                ev.modifiers = MOUSE_BUTTON_RIGHT;
                ev.window_id = -1;
                event_enqueue(&ev);
            }
            if (changed & MOUSE_BUTTON_MIDDLE) {
                struct gui_event ev;
                int pressed = (buttons & MOUSE_BUTTON_MIDDLE) != 0;
                ev.event_id = pressed ? INPUT_EVENT_MOUSE_DOWN : INPUT_EVENT_MOUSE_UP;
                ev.type = EVENT_MOUSE;
                ev.subtype = pressed ? MOUSE_BTN_DOWN : MOUSE_BTN_UP;
                ev.keycode = buttons;
                ev.x = cursor_x();
                ev.y = cursor_y();
                ev.wheel_delta = 0;
                ev.width = 0;
                ev.height = 0;
                ev.modifiers = MOUSE_BUTTON_MIDDLE;
                ev.window_id = -1;
                event_enqueue(&ev);
            }
            mouse_buttons_prev = buttons;
        }
        break;
    }

    irq_ack(12);
}
