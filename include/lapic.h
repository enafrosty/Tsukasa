#ifndef LAPIC_H
#define LAPIC_H

#include <stdint.h>

void lapic_init(void);
void lapic_enable(void);
void lapic_eoi(void);
uint32_t lapic_read_id(void);
void lapic_send_ipi_all(void);
void lapic_send_ipi(uint32_t lapic_id, uint8_t vector);

#endif /* LAPIC_H */
