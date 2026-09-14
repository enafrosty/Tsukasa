/*
 * Project Tsukasa — Standard C library general utilities header
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

#ifndef TSUKASA_STDLIB_H
#define TSUKASA_STDLIB_H

#include <stddef.h>
#include "sys/types.h"

int abs(int x);
long strtol(const char *nptr, char **endptr, int base);
int atoi(const char *nptr);

void srand(unsigned int seed);
int rand(void);

void qsort(void *base,
           size_t nmemb,
           size_t size,
           int (*compar)(const void *, const void *));

void *bsearch(const void *key,
              const void *base,
              size_t nmemb,
              size_t size,
              int (*compar)(const void *, const void *));

void exit(int code);
void _exit(int code);

#endif /* TSUKASA_STDLIB_H */
