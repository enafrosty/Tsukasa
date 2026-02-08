/*
 * ps2.h - PS/2 keyboard and mouse constants.
 */

#ifndef PS2_H
#define PS2_H

#define PS2_DATA  0x60
#define PS2_STATUS 0x64

static inline unsigned char inb(unsigned short port)
{
    unsigned char val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

#endif /* PS2_H */
