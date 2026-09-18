/*
 * Project Tsukasa — COM1 serial port driver (x86 port I/O)
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

#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

#define COM1_BASE  0x3F8u
#define COM2_BASE  0x2F8u

/* Port-parameterized serial functions */
void serial_init_port(uint16_t base);
void serial_putc_port(uint16_t base, char c);
void serial_puts_port(uint16_t base, const char *s);
void serial_write_byte_raw_port(uint16_t base, uint8_t b);
int  serial_has_rx_port(uint16_t base);
char serial_getc_port(uint16_t base);

/* Legacy COM1 helpers */
void serial_init(void);
void serial_putc(char c);
void serial_puts(const char *s);

#endif /* SERIAL_H */
