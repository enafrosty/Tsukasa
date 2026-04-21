/*
 * serial.c - COM1 serial port driver (8N1, 115200 baud).
 *
 * x86 PC UART register map (base = 0x3F8 = COM1):
 *   +0  THR (write) / RBR (read) / DLL (baud divisor low, DLAB=1)
 *   +1  IER (interrupt enable) / DLH (baud divisor high, DLAB=1)
 *   +2  IIR (read) / FCR (write)
 *   +3  LCR (line control, bit7 = DLAB)
 *   +4  MCR (modem control)
 *   +5  LSR (line status, bit5 = TX empty)
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
    /* Disable interrupts. */
    outb(COM1_BASE + 1u, 0x00u);

    /* Enable DLAB to set baud divisor. */
    outb(COM1_BASE + 3u, 0x80u);

    /* Divisor = 1 → 115200 baud (clock / 16 / divisor = 1843200/16/1). */
    outb(COM1_BASE + 0u, 0x01u);   /* DLL */
    outb(COM1_BASE + 1u, 0x00u);   /* DLH */

    /* 8 data bits, no parity, 1 stop bit, DLAB=0. */
    outb(COM1_BASE + 3u, 0x03u);

    /* Enable FIFO, clear them, 14-byte threshold. */
    outb(COM1_BASE + 2u, 0xC7u);

    /* RTS/DTR active, IRQ disabled. */
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
