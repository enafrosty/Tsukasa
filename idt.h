/*
 * idt.h - Interrupt Descriptor Table (IDT) setup for x86.
 * Prevents triple fault on CPU exceptions by routing them to stub handlers.
 */

#ifndef IDT_H
#define IDT_H

#include <stdint.h>

#define IDT_ENTRIES 256

/* Initialize IDT and load it with lidt. Call early from kernel_main. */
void idt_init(void);

/* Called from assembly stub with vector number and error code (0 if none). */
void idt_handler(uint32_t vector, uint32_t error_code);

#endif /* IDT_H */
