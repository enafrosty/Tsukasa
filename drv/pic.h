/*
 * pic.h - Programmable Interrupt Controller.
 */

#ifndef PIC_H
#define PIC_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
#define PIC_H

/**
 * Initialize PIC: remap IRQs to vectors 32-47, mask all except keyboard.
 */
void pic_init(void);

/**
 * Send End-Of-Interrupt to PIC.
 *
 * @param irq IRQ number (0-15).
 */
void pic_eoi(unsigned int irq);

#endif /* PIC_H */
