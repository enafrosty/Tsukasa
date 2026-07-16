#ifndef LWIP_STRING_H
#define LWIP_STRING_H

#include "kutils.h"

#define memcpy k_memcpy
#define memset k_memset
#define strlen k_strlen
#define strcmp k_strcmp
#define strcpy k_strcpy

static inline int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    while (n--) {
        if (*p1 != *p2) return (int)*p1 - (int)*p2;
        p1++; p2++;
    }
    return 0;
}

static inline void *memmove(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

static inline char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    if (c == 0) return (char *)s;
    return (void*)0;
}

static inline int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return (int)*(const unsigned char *)s1 - (int)*(const unsigned char *)s2;
}

static inline char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++)
        dest[i] = src[i];
    for ( ; i < n; i++)
        dest[i] = '\0';
    return dest;
}

static inline char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char *h = haystack;
            const char *n = needle;
            while (*h && *n && *h == *n) {
                h++; n++;
            }
            if (!*n) return (char *)haystack;
        }
    }
    return (void*)0;
}

/* ctype.h macros for lwIP - avoid conflicting with system declarations */
#ifndef LWIP_CTYPE_H_COMPAT
#define LWIP_CTYPE_H_COMPAT

#undef isdigit
#undef isxdigit
#undef isalpha
#undef isalnum
#undef islower
#undef isupper
#undef isspace
#undef tolower
#undef toupper

#define isdigit(c)  ((c) >= '0' && (c) <= '9')
#define isxdigit(c) (((c) >= '0' && (c) <= '9') || ((c) >= 'a' && (c) <= 'f') || ((c) >= 'A' && (c) <= 'F'))
#define isalpha(c)  (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z'))
#define isalnum(c)  (isalpha(c) || isdigit(c))
#define islower(c)  ((c) >= 'a' && (c) <= 'z')
#define isupper(c)  ((c) >= 'A' && (c) <= 'Z')
#define isspace(c)  ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r' || (c) == '\v' || (c) == '\f')
#define tolower(c)  (((c) >= 'A' && (c) <= 'Z') ? ((c) + 32) : (c))
#define toupper(c)  (((c) >= 'a' && (c) <= 'z') ? ((c) - 32) : (c))

#endif

#endif
