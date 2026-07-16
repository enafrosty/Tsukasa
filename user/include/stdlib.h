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
