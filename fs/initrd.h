/*
 * initrd.h - Initial RAM disk (Multiboot module).
 */

#ifndef INITRD_H
#define INITRD_H

#include <stddef.h>

/**
 * Initialize initrd from Multiboot modules.
 *
 * @param mb_info Multiboot info (may be NULL).
 */
void initrd_init_from_multiboot(const void *mb_info);

/**
 * Look up file in initrd. Minimal: single module as root file.
 *
 * @param path Path (e.g. "/" or "/initrd").
 * @param data Output: pointer to file data.
 * @param size Output: file size.
 * @return 0 on success, -1 on not found.
 */
int initrd_lookup(const char *path, const void **data, size_t *size);

#endif /* INITRD_H */
