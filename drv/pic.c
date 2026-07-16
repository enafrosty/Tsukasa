/*
 * pic.c - PIC 8259 initialization and EOI.
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

    /* Unmask IRQ0 (timer), IRQ1 (keyboard), IRQ2 (cascade), IRQ12 (mouse). */
    g_pic1_mask = 0xF8u;
    g_pic2_mask = 0xEFu;
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
