/*
 * gdt.h - Global Descriptor Table with user segments for Ring 3.
 */

#ifndef GDT_H
#define GDT_H

/** GDT segment selectors. */
#define GDT_KERNEL_CS  0x08
#define GDT_KERNEL_DS  0x10
#define GDT_USER_CS    0x18
#define GDT_USER_DS    0x20

/**
 * Initialize GDT with kernel and user segments.
 * Call after IDT, before creating user tasks.
 */
void gdt_init(void);

#endif /* GDT_H */
