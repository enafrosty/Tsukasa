#ifndef _LWEXT4_COMPAT_STDLIB_H
#define _LWEXT4_COMPAT_STDLIB_H

#include <stddef.h>

void *ext4_user_malloc(size_t size);
void *ext4_user_calloc(size_t nmemb, size_t size);
void *ext4_user_realloc(void *ptr, size_t size);
void ext4_user_free(void *ptr);

void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *));

#endif /* _LWEXT4_COMPAT_STDLIB_H */
