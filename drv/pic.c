/*
 * Project Tsukasa — PIC 8259 initialization and EOI
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

#include "pic.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define ICW1_ICW4  0x01
#define ICW1_INIT  0x10
#define ICW4_8086  0x01

static uint8_t g_pic1_mask = 0xFFu;
static uint8_t g_pic2_mask = 0xFFu;

static void pic_write_masks(void)
{
    outb(PIC1_DATA, g_pic1_mask);
    outb(PIC2_DATA, g_pic2_mask);
}

void pic_init(void)
{
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);
    outb(PIC1_DATA, 32);
    outb(PIC1_DATA, 1 << 2);
    outb(PIC1_DATA, ICW4_8086);

    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_DATA, 40);
    outb(PIC2_DATA, 2);
    outb(PIC2_DATA, ICW4_8086);

    g_pic1_mask = 0xF8u;
    g_pic2_mask = 0xEFu;
    pic_write_masks();
}

void pic_disable(void)
{
    g_pic1_mask = 0xFFu;
    g_pic2_mask = 0xFFu;
    pic_write_masks();
}

void pic_eoi(unsigned int irq)
{
    if (irq >= 8)
        outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}

void pic_mask_irq(uint8_t irq)
{
    if (irq < 8) {
        g_pic1_mask |= (uint8_t)(1u << irq);
    } else if (irq < 16) {
        g_pic2_mask |= (uint8_t)(1u << (irq - 8));
    } else {
        return;
    }
    pic_write_masks();
}

void pic_unmask_irq(uint8_t irq)
{
    if (irq < 8) {
        g_pic1_mask &= (uint8_t)~(1u << irq);
    } else if (irq < 16) {
        g_pic2_mask &= (uint8_t)~(1u << (irq - 8));
    } else {
        return;
    }
    pic_write_masks();
}
