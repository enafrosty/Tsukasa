/*
 * Project Tsukasa — Kernel utility functions
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

#ifndef KUTILS_H
#define KUTILS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Kernel string utilities
void k_memset(void *dest, int val, size_t len);
void k_memcpy(void *dest, const void *src, size_t len);
size_t k_strlen(const char *str);
int k_strcmp(const char *s1, const char *s2);
int k_strncmp(const char *s1, const char *s2, size_t n);
void k_strcpy(char *dest, const char *src);
int k_atoi(const char *str);
void k_itoa(int n, char *buf);
void k_itoa_hex(uint64_t n, char *buf);

// Kernel timing utilities
void k_delay(int iterations);
void k_sleep(int ms);
void k_reboot(void);
void k_shutdown(void);
void k_beep(int freq, int ms);
void k_beep_process(void);
char *k_strstr(const char *haystack, const char *needle);

#endif
