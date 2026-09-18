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

void serial_init_port(uint16_t base)
{
    outb(base + 1u, 0x00u); /* Disable interrupts */
    outb(base + 3u, 0x80u); /* Enable DLAB (set baud rate divisor) */
    outb(base + 0u, 0x01u); /* Set divisor to 1 (115200 baud): low byte */
    outb(base + 1u, 0x00u); /* High byte */
    outb(base + 3u, 0x03u); /* 8 bits, no parity, one stop bit (8N1) */
    outb(base + 2u, 0xC7u); /* Enable FIFO, clear them, with 14-byte threshold */
    outb(base + 4u, 0x03u); /* RTS/DSR set */
}

static void serial_wait_tx_port(uint16_t base)
{
    while ((inb(base + 5u) & 0x20u) == 0u)
        __asm__ volatile ("pause");
}

void serial_write_byte_raw_port(uint16_t base, uint8_t b)
{
    serial_wait_tx_port(base);
    outb(base + 0u, b);
}

void serial_putc_port(uint16_t base, char c)
{
    if (c == '\n') {
        serial_wait_tx_port(base);
        outb(base + 0u, '\r');
    }
    serial_wait_tx_port(base);
    outb(base + 0u, (uint8_t)c);
}

void serial_puts_port(uint16_t base, const char *s)
{
    if (!s) return;
    while (*s)
        serial_putc_port(base, *s++);
}

int serial_has_rx_port(uint16_t base)
{
    return (inb(base + 5u) & 0x01u) != 0;
}

char serial_getc_port(uint16_t base)
{
    while (!serial_has_rx_port(base))
        __asm__ volatile ("pause");
    return (char)inb(base + 0u);
}

void serial_init(void)
{
    serial_init_port(COM1_BASE);
}

void serial_putc(char c)
{
    serial_putc_port(COM1_BASE, c);
}

void serial_puts(const char *s)
{
    serial_puts_port(COM1_BASE, s);
}
