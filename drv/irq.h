/*
 * irq.h - IRQ callback registration for PIC IRQ lines.
 */

#ifndef TSUKASA_IRQ_H
#define TSUKASA_IRQ_H

#include <stdint.h>

typedef void (*irq_callback_t)(uint8_t irq, void *ctx);

int irq_register_handler(uint8_t irq, irq_callback_t callback, void *ctx);
void irq_unregister_handler(uint8_t irq);

#endif /* TSUKASA_IRQ_H */
