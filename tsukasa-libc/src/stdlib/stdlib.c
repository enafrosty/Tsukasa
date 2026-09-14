/*
 * Project Tsukasa — Standard General Utilities Implementation
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

#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

int abs(int x)
{
    return (x < 0) ? -x : x;
}

long labs(long x)
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
    } else if (base == 16 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
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

unsigned long strtoul(const char *nptr, char **endptr, int base)
{
    unsigned long v = 0;
    const char *p = nptr;
    if (!p) {
        if (endptr)
            *endptr = (char *)nptr;
        return 0;
    }

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    if (*p == '+')
        p++;

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
    } else if (base == 16 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
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
        v = v * (unsigned long)base + (unsigned long)d;
        p++;
    }
    if (endptr)
        *endptr = (char *)p;
    return v;
}

long long strtoll(const char *nptr, char **endptr, int base)
{
    long long v = 0;
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
    } else if (base == 16 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
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

unsigned long long strtoull(const char *nptr, char **endptr, int base)
{
    unsigned long long v = 0;
    const char *p = nptr;
    if (!p) {
        if (endptr)
            *endptr = (char *)nptr;
        return 0;
    }

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    if (*p == '+')
        p++;

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
    } else if (base == 16 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
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
        v = v * (unsigned long long)base + (unsigned long long)d;
        p++;
    }
    if (endptr)
        *endptr = (char *)p;
    if (endptr)
        *endptr = (char *)p;
    return v;
}

static char g_env_home[] = "/";
static char g_env_path[] = "/bin:/fat12";
static char g_env_term[] = "xterm";

static char *g_default_environ[] = {
    "HOME=/",
    "PATH=/bin:/fat12",
    "TERM=xterm",
    NULL
};
char **environ = g_default_environ;

char *getenv(const char *name)
{
    if (!name)
        return NULL;
    if (name[0] == 'H' && name[1] == 'O' && name[2] == 'M' && name[3] == 'E' && name[4] == '\0')
        return g_env_home;
    if (name[0] == 'P' && name[1] == 'A' && name[2] == 'T' && name[3] == 'H' && name[4] == '\0')
        return g_env_path;
    if (name[0] == 'T' && name[1] == 'E' && name[2] == 'R' && name[3] == 'M' && name[4] == '\0')
        return g_env_term;
    return NULL;
}

int setenv(const char *name, const char *value, int overwrite)
{
    (void)name; (void)value; (void)overwrite;
    return 0;
}

int unsetenv(const char *name)
{
    (void)name;
    return 0;
}

int system(const char *command)
{
    if (!command)
        return 1;
    char *argv[4];
    argv[0] = (char *)"/bin/tsh.elf";
    argv[1] = (char *)"-c";
    argv[2] = (char *)command;
    argv[3] = NULL;
    pid_t pid = spawn("/bin/tsh.elf", argv, NULL);
    if (pid < 0)
        return -1;
    int status = 0;
    waitpid(pid, &status, 0);
    return status;
}

char *realpath(const char *path, char *resolved_path)
{
    if (!path) {
        errno = EINVAL;
        return NULL;
    }
    if (!resolved_path) {
        resolved_path = (char *)malloc(1024);
        if (!resolved_path) {
            errno = ENOMEM;
            return NULL;
        }
    }
    strncpy(resolved_path, path, 1024);
    resolved_path[1023] = '\0';
    return resolved_path;
}

int mkstemp(char *template)
{
    if (!template) {
        errno = EINVAL;
        return -1;
    }

    size_t len = strlen(template);
    if (len < 6 || strcmp(template + len - 6, "XXXXXX") != 0) {
        errno = EINVAL;
        return -1;
    }

    static unsigned int counter = 100000;
    for (int retry = 0; retry < 100; retry++) {
        unsigned int val = ++counter;
        for (int i = 0; i < 6; i++) {
            template[len - 6 + i] = '0' + (char)(val % 10);
            val /= 10;
        }

        int fd = open(template, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0)
            return fd;
    }

    return -1;
}

int atoi(const char *nptr)
{
    return (int)strtol(nptr, NULL, 10);
}

long atol(const char *nptr)
{
    return strtol(nptr, NULL, 10);
}

static unsigned int g_rand_state = 1u;

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

void qsort(void *base, size_t nmemb, size_t size,
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

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *))
{
    const unsigned char *arr = (const unsigned char *)base;
    size_t lo = 0;
    size_t hi = nmemb;
    if (!key || !arr || !compar || size == 0)
        return NULL;

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
    return NULL;
}

void abort(void)
{
    const char msg[] = "Abort: process terminated by abort()\n";
    write(STDERR_FILENO, msg, sizeof(msg) - 1);
    _exit(134);
}
