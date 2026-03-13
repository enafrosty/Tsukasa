/*
 * ps2mouse.c - PS/2 mouse driver.
 * Reads 3-byte packets from IRQ 12, updates cursor, enqueues events.
 */

#include "ps2.h"
#include "pic.h"
#include "ps2mouse.h"
#include "../input/event.h"
#include "../gfx/cursor.h"
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
    outb(PS2_CMD, 0xD4);   /* Tell controller: next byte goes to mouse. */
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

    /* Enable the auxiliary (mouse) PS/2 port. */
    ps2_wait_write();
    outb(PS2_CMD, 0xA8);

    /* Enable IRQ 12 in PS/2 controller. Read config byte, set bit 1. */
    ps2_wait_write();
    outb(PS2_CMD, 0x20);   /* Read controller config byte. */
    ps2_wait_read();
    uint8_t config = inb(PS2_DATA);
    config |= 0x02;        /* Enable IRQ 12 (auxiliary port interrupt). */
    ps2_wait_write();
    outb(PS2_CMD, 0x60);   /* Write controller config byte. */
    ps2_wait_write();
    outb(PS2_DATA, config);

    /* Send "Set Defaults" to mouse. */
    ps2_mouse_write(0xF6);
    ps2_mouse_read();       /* ACK */

    /* Enable data reporting. */
    ps2_mouse_write(0xF4);
    ps2_mouse_read();       /* ACK */

    /* Unmask IRQ 12 on the slave PIC (IRQ 12 = slave bit 4). */
    uint8_t mask = inb(0xA1);
    mask &= ~(1 << 4);
    outb(0xA1, mask);

    /* Also make sure IRQ 2 (cascade) is unmasked on master. */
    mask = inb(0x21);
    mask &= ~(1 << 2);
    outb(0x21, mask);
}

void ps2mouse_handler(void)
{
    uint8_t data = inb(PS2_DATA);

    switch (mouse_cycle) {
    case 0:
        mouse_bytes[0] = (int8_t)data;
        /* Bit 3 should always be set in byte 0 of a valid packet. */
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

        /* Decode packet. */
        {
            uint8_t status = (uint8_t)mouse_bytes[0];
            int dx = mouse_bytes[1];
            int dy = mouse_bytes[2];

            /* Apply sign extension from status bits. */
            if (status & 0x10) dx |= 0xFFFFFF00;
            if (status & 0x20) dy |= 0xFFFFFF00;

            /* PS/2 Y is inverted (positive = up). */
            dy = -dy;

            /* Update cursor position. */
            cursor_move(dx, dy);

            uint8_t buttons = status & 0x07;

            /* Enqueue movement event. */
            if (dx != 0 || dy != 0) {
                struct input_event ev;
                ev.type = EVENT_MOUSE;
                ev.subtype = MOUSE_MOVE;
                ev.keycode = buttons;
                ev.x = (int16_t)cursor_x();
                ev.y = (int16_t)cursor_y();
                event_enqueue(&ev);
            }

            /* Enqueue button change events. */
            uint8_t changed = buttons ^ mouse_buttons_prev;
            if (changed) {
                struct input_event ev;
                ev.type = EVENT_MOUSE;
                ev.subtype = (buttons & changed) ? MOUSE_BTN_DOWN : MOUSE_BTN_UP;
                ev.keycode = buttons;
                ev.x = (int16_t)cursor_x();
                ev.y = (int16_t)cursor_y();
                event_enqueue(&ev);
            }
            mouse_buttons_prev = buttons;
        }
        break;
    }

    /* Send EOI to both slave and master PIC for IRQ 12. */
    pic_eoi(12);
}
