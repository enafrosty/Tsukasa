#include "../include/string.h"

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s && s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    size_t i = 0;
    if (!a || !b)
        return (a == b) ? 0 : 1;
    while (a[i] && b[i] && a[i] == b[i])
        i++;
    return (unsigned char)a[i] - (unsigned char)b[i];
}

int strncmp(const char *a, const char *b, size_t n)
{
    size_t i;
    if (!a || !b)
        return (a == b) ? 0 : 1;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i] || a[i] == '\0' || b[i] == '\0')
            return (unsigned char)a[i] - (unsigned char)b[i];
    }
    return 0;
}

char *strcpy(char *dst, const char *src)
{
    size_t i = 0;
    if (!dst)
        return dst;
    if (!src) {
        dst[0] = '\0';
        return dst;
    }
    while (src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    if (!dst || n == 0)
        return dst;
    if (!src) {
        dst[0] = '\0';
        return dst;
    }
    while (i < n && src[i]) {
        dst[i] = src[i];
        i++;
    }
    while (i < n) {
        dst[i] = '\0';
        i++;
    }
    return dst;
}

char *strcat(char *dst, const char *src)
{
    size_t i = strlen(dst);
    size_t j = 0;
    if (!dst || !src)
        return dst;
    while (src[j]) {
        dst[i + j] = src[j];
        j++;
    }
    dst[i + j] = '\0';
    return dst;
}

char *strncat(char *dst, const char *src, size_t n)
{
    size_t i = strlen(dst);
    size_t j = 0;
    if (!dst || !src)
        return dst;
    while (src[j] && j < n) {
        dst[i + j] = src[j];
        j++;
    }
    dst[i + j] = '\0';
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    unsigned char *p = (unsigned char *)dst;
    for (size_t i = 0; i < n; i++)
        p[i] = (unsigned char)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; i++)
        d[i] = s[i];
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d <= s) {
        for (size_t i = 0; i < n; i++)
            d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; i--)
            d[i - 1] = s[i - 1];
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i])
            return (int)pa[i] - (int)pb[i];
    }
    return 0;
}

char *strchr(const char *s, int c)
{
    if (!s)
        return 0;
    while (*s) {
        if (*s == (char)c)
            return (char *)s;
        s++;
    }
    return (c == 0) ? (char *)s : 0;
}

char *strrchr(const char *s, int c)
{
    const char *last = 0;
    if (!s)
        return 0;
    while (*s) {
        if (*s == (char)c)
            last = s;
        s++;
    }
    if (c == 0)
        return (char *)s;
    return (char *)last;
}
