/*
 * Project Tsukasa — Standard String and Memory Operations Implementation
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

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

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

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    unsigned char uc = (unsigned char)c;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == uc)
            return (void *)(p + i);
    }
    return NULL;
}

int strcoll(const char *s1, const char *s2)
{
    return strcmp(s1, s2);
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s && s[n])
        n++;
    return n;
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

int strcmp(const char *a, const char *b)
{
    size_t i = 0;
    if (!a || !b)
        return (a == b) ? 0 : (a ? 1 : -1);
    while (a[i] && b[i] && a[i] == b[i])
        i++;
    return (unsigned char)a[i] - (unsigned char)b[i];
}

int strncmp(const char *a, const char *b, size_t n)
{
    if (n == 0)
        return 0;
    if (!a || !b)
        return (a == b) ? 0 : (a ? 1 : -1);
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || a[i] == '\0' || b[i] == '\0')
            return (unsigned char)a[i] - (unsigned char)b[i];
    }
    return 0;
}

int strcasecmp(const char *s1, const char *s2)
{
    if (!s1 || !s2)
        return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    while (*s1 && *s2) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2)
            return c1 - c2;
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

int strncasecmp(const char *s1, const char *s2, size_t n)
{
    if (n == 0)
        return 0;
    if (!s1 || !s2)
        return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    for (size_t i = 0; i < n; i++) {
        int c1 = tolower((unsigned char)s1[i]);
        int c2 = tolower((unsigned char)s2[i]);
        if (c1 != c2 || s1[i] == '\0' || s2[i] == '\0')
            return c1 - c2;
    }
    return 0;
}

char *strchr(const char *s, int c)
{
    if (!s)
        return NULL;
    while (*s) {
        if (*s == (char)c)
            return (char *)s;
        s++;
    }
    return (c == 0) ? (char *)s : NULL;
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    if (!s)
        return NULL;
    while (*s) {
        if (*s == (char)c)
            last = s;
        s++;
    }
    if (c == 0)
        return (char *)s;
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle)
{
    if (!haystack || !needle)
        return NULL;
    if (*needle == '\0')
        return (char *)haystack;
    size_t nlen = strlen(needle);
    while (*haystack) {
        if (*haystack == *needle && strncmp(haystack, needle, nlen) == 0)
            return (char *)haystack;
        haystack++;
    }
    return NULL;
}

char *strcasestr(const char *haystack, const char *needle)
{
    if (!haystack || !needle)
        return NULL;
    if (*needle == '\0')
        return (char *)haystack;
    size_t nlen = strlen(needle);
    while (*haystack) {
        if (strncasecmp(haystack, needle, nlen) == 0)
            return (char *)haystack;
        haystack++;
    }
    return NULL;
}

char *strdup(const char *s)
{
    if (!s)
        return NULL;
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (!copy)
        return NULL;
    memcpy(copy, s, len + 1);
    return copy;
}

char *strerror(int errnum)
{
    switch (errnum) {
    case 0:            return "Success";
    case EPERM:        return "Operation not permitted";
    case ENOENT:       return "No such file or directory";
    case ESRCH:        return "No such process";
    case EINTR:        return "Interrupted system call";
    case EIO:          return "Input/output error";
    case ENXIO:        return "No such device or address";
    case E2BIG:        return "Argument list too long";
    case ENOEXEC:      return "Exec format error";
    case EBADF:        return "Bad file descriptor";
    case ECHILD:       return "No child processes";
    case EAGAIN:       return "Resource temporarily unavailable";
    case ENOMEM:       return "Cannot allocate memory";
    case EACCES:       return "Permission denied";
    case EFAULT:       return "Bad address";
    case EBUSY:        return "Device or resource busy";
    case EEXIST:       return "File exists";
    case EXDEV:        return "Invalid cross-device link";
    case ENODEV:       return "No such device";
    case ENOTDIR:      return "Not a directory";
    case EISDIR:       return "Is a directory";
    case EINVAL:       return "Invalid argument";
    case ENFILE:       return "Too many open files in system";
    case EMFILE:       return "Too many open files";
    case ENOTTY:       return "Inappropriate ioctl for device";
    case EFBIG:        return "File too large";
    case ENOSPC:       return "No space left on device";
    case ESPIPE:       return "Illegal seek";
    case EROFS:        return "Read-only file system";
    case EMLINK:       return "Too many links";
    case EPIPE:        return "Broken pipe";
    case EDOM:         return "Numerical argument out of domain";
    case ERANGE:       return "Numerical result out of range";
    case EDEADLK:      return "Resource deadlock avoided";
    case ENAMETOOLONG: return "File name too long";
    case ENOLCK:       return "No locks available";
    case ENOSYS:       return "Function not implemented";
    case ENOTEMPTY:    return "Directory not empty";
    case EAFNOSUPPORT: return "Address family not supported by protocol";
    case EADDRINUSE:   return "Address already in use";
    case ETIMEDOUT:    return "Connection timed out";
    case ECONNREFUSED: return "Connection refused";
    default:           return "Unknown error";
    }
}

char *strpbrk(const char *s, const char *accept)
{
    if (!s || !accept)
        return NULL;
    while (*s) {
        const char *a = accept;
        while (*a) {
            if (*s == *a)
                return (char *)s;
            a++;
        }
        s++;
    }
    return NULL;
}

size_t strspn(const char *s, const char *accept)
{
    if (!s || !accept)
        return 0;
    size_t count = 0;
    while (*s) {
        const char *a = accept;
        int found = 0;
        while (*a) {
            if (*s == *a) {
                found = 1;
                break;
            }
            a++;
        }
        if (!found)
            break;
        count++;
        s++;
    }
    return count;
}

size_t strcspn(const char *s, const char *reject)
{
    if (!s || !reject)
        return 0;
    size_t count = 0;
    while (*s) {
        const char *r = reject;
        int found = 0;
        while (*r) {
            if (*s == *r) {
                found = 1;
                break;
            }
            r++;
        }
        if (found)
            break;
        count++;
        s++;
    }
    return count;
}

char *strtok_r(char *str, const char *delim, char **saveptr)
{
    if (!saveptr || (!str && !*saveptr) || !delim)
        return NULL;
    char *s = str ? str : *saveptr;
    while (*s && strchr(delim, *s))
        s++;
    if (*s == '\0') {
        *saveptr = s;
        return NULL;
    }
    char *token = s;
    while (*s && !strchr(delim, *s))
        s++;
    if (*s) {
        *s = '\0';
        *saveptr = s + 1;
    } else {
        *saveptr = s;
    }
    return token;
}

static char *g_strtok_save = NULL;

char *strtok(char *str, const char *delim)
{
    return strtok_r(str, delim, &g_strtok_save);
}
