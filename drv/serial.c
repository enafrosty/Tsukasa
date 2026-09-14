/*
 * Project Tsukasa — COM1 serial port driver (8N1, 115200 baud)
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

#include "../drv/serial.h"
#include <stdint.h>

#define COM1_BASE  0x3F8u

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

void serial_init(void)
{
    outb(COM1_BASE + 1u, 0x00u);

    outb(COM1_BASE + 3u, 0x80u);

    outb(COM1_BASE + 0u, 0x01u);
    outb(COM1_BASE + 1u, 0x00u);

    outb(COM1_BASE + 3u, 0x03u);

    outb(COM1_BASE + 2u, 0xC7u);

    outb(COM1_BASE + 4u, 0x03u);
}

/* Block until the Transmit Holding Register is empty. */
static void serial_wait_tx(void)
{
    while ((inb(COM1_BASE + 5u) & 0x20u) == 0u)
        __asm__ volatile ("pause");
}

void serial_putc(char c)
{
    if (c == '\n') {
        serial_wait_tx();
        outb(COM1_BASE, '\r');
    }
    serial_wait_tx();
    outb(COM1_BASE, (uint8_t)c);
}

void serial_puts(const char *s)
{
    if (!s) return;
    while (*s)
        serial_putc(*s++);
}
