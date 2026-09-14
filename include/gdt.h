/*
 * Project Tsukasa — Global Descriptor Table with user segments for Ring 3
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

#ifndef GDT_H
#define GDT_H

/* GDT segment selectors. */
#define GDT_KERNEL_CS  0x08
#define GDT_KERNEL_DS  0x10
#define GDT_USER_CS    0x18
#define GDT_USER_DS    0x20
#define GDT_TSS        0x28  /* GDT index 5 */

/* Initialize GDT with kernel and user segments and load a 32-bit TSS. Call after IDT, before creating user... */
void gdt_init(void);

#endif /* GDT_H */
