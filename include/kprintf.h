/*
 * Project Tsukasa — Freestanding kernel printf
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

#ifndef KPRINTF_H
#define KPRINTF_H

#include <stddef.h>
#include <stdarg.h>

/* Print a formatted string to the serial port. Returns the number of characters written. */
int kprintf(const char *fmt, ...);

/* Format a string into buf (at most n bytes including NUL terminator). */
int ksprintf(char *buf, size_t n, const char *fmt, ...);

/* Bare string output to serial (no formatting). */
void kputs(const char *s);

/* Kernel log levels */
enum k_loglevel {
    K_ERR = 0,
    K_WARN = 1,
    K_INFO = 2,
    K_DEBUG = 3
};

/* Global threshold; messages above it are dropped. Default K_INFO. */
extern enum k_loglevel k_log_threshold;

/* Per-subsystem override: subsystem tag -> threshold. */
void k_log_set_level(const char *subsys, enum k_loglevel lvl);
int k_log_enabled(enum k_loglevel lvl, const char *subsys);
void k_log_init(const char *cmdline);

#define klog(lvl, subsys, fmt, ...) \
    do { if (k_log_enabled((lvl), (subsys))) \
            kprintf("[%s] " fmt, (subsys), ##__VA_ARGS__); } while (0)

#define kerr(s, ...)   klog(K_ERR,   s, __VA_ARGS__)
#define kwarn(s, ...)  klog(K_WARN,  s, __VA_ARGS__)
#define kinfo(s, ...)  klog(K_INFO,  s, __VA_ARGS__)
#define kdebug(s, ...) klog(K_DEBUG, s, __VA_ARGS__)

#endif /* KPRINTF_H */
