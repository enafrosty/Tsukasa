#include "../include/stdlib.h"
#include "../include/string.h"

int abs(int x)
{
    return (x < 0) ? -x : x;
}

long strtol(const char *nptr, char **endptr, int base)
{
    long v = 0;
    int neg = 0;
    const char *p = nptr;
    if (!p) {
        if (endptr)
            *endptr = (char *)nptr;
        return 0;
    }

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    if (*p == '-') {
        neg = 1;
        p++;
    } else if (*p == '+') {
        p++;
    }

    if (base == 0) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            base = 16;
            p += 2;
        } else if (p[0] == '0') {
            base = 8;
            p++;
        } else {
            base = 10;
        }
    }

    while (*p) {
        int d;
        if (*p >= '0' && *p <= '9')
            d = *p - '0';
        else if (*p >= 'a' && *p <= 'z')
            d = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'Z')
            d = *p - 'A' + 10;
        else
            break;
        if (d >= base)
            break;
        v = v * base + d;
        p++;
    }
    if (endptr)
        *endptr = (char *)p;
    return neg ? -v : v;
}

int atoi(const char *nptr)
{
    return (int)strtol(nptr, 0, 10);
}

static unsigned g_rand_state = 1u;

void srand(unsigned int seed)
{
    g_rand_state = seed ? seed : 1u;
}

int rand(void)
{
    g_rand_state = g_rand_state * 1103515245u + 12345u;
    return (int)((g_rand_state >> 16) & 0x7FFFu);
}

static void swap_bytes(unsigned char *a, unsigned char *b, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        unsigned char t = a[i];
        a[i] = b[i];
        b[i] = t;
    }
}

void qsort(void *base,
           size_t nmemb,
           size_t size,
           int (*compar)(const void *, const void *))
{
    unsigned char *arr = (unsigned char *)base;
    if (!arr || !compar || size == 0 || nmemb < 2)
        return;

    for (size_t i = 0; i + 1 < nmemb; i++) {
        for (size_t j = 0; j + 1 < nmemb - i; j++) {
            unsigned char *a = arr + j * size;
            unsigned char *b = arr + (j + 1) * size;
            if (compar(a, b) > 0)
                swap_bytes(a, b, size);
        }
    }
}

void *bsearch(const void *key,
              const void *base,
              size_t nmemb,
              size_t size,
              int (*compar)(const void *, const void *))
{
    const unsigned char *arr = (const unsigned char *)base;
    size_t lo = 0;
    size_t hi = nmemb;
    if (!key || !arr || !compar || size == 0)
        return 0;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const void *slot = arr + mid * size;
        int cmp = compar(key, slot);
        if (cmp == 0)
            return (void *)slot;
        if (cmp < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return 0;
}
