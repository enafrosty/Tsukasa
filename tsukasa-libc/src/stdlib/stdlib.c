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

static char *g_default_environ[] = {
    "HOME=/",
    "PATH=/bin:/usr/bin:/sbin",
    "TERM=xterm",
    NULL
};
char **environ = g_default_environ;
static int g_environ_allocated = 0;

static size_t env_key_len(const char *name)
{
    size_t len = 0;
    if (!name || !name[0])
        return 0;
    while (name[len]) {
        if (name[len] == '=')
            return 0;
        len++;
    }
    return len;
}

static int env_match_entry(const char *entry, const char *name, size_t nlen)
{
    if (!entry || !name || nlen == 0)
        return 0;
    for (size_t i = 0; i < nlen; i++) {
        if (entry[i] != name[i])
            return 0;
    }
    return entry[nlen] == '=';
}

char *getenv(const char *name)
{
    size_t nlen = env_key_len(name);
    if (nlen == 0 || !environ)
        return NULL;
    for (size_t i = 0; environ[i]; i++) {
        if (env_match_entry(environ[i], name, nlen))
            return environ[i] + nlen + 1;
    }
    return NULL;
}

int setenv(const char *name, const char *value, int overwrite)
{
    size_t nlen = env_key_len(name);
    if (nlen == 0 || !value)
        return -1;

    size_t count = 0;
    size_t found_idx = (size_t)-1;
    if (environ) {
        while (environ[count]) {
            if (found_idx == (size_t)-1 && env_match_entry(environ[count], name, nlen))
                found_idx = count;
            count++;
        }
    }

    if (found_idx != (size_t)-1 && !overwrite)
        return 0;

    size_t vlen = strlen(value);
    char *kv = (char *)malloc(nlen + 1 + vlen + 1);
    if (!kv)
        return -1;
    memcpy(kv, name, nlen);
    kv[nlen] = '=';
    memcpy(kv + nlen + 1, value, vlen + 1);

    if (found_idx != (size_t)-1) {
        if (!g_environ_allocated) {
            char **copy = (char **)malloc((count + 1) * sizeof(char *));
            if (!copy) {
                free(kv);
                return -1;
            }
            for (size_t i = 0; i < count; i++)
                copy[i] = environ[i];
            copy[count] = NULL;
            environ = copy;
            g_environ_allocated = 1;
        }
        environ[found_idx] = kv;
        return 0;
    }

    char **new_env = (char **)malloc((count + 2) * sizeof(char *));
    if (!new_env) {
        free(kv);
        return -1;
    }
    for (size_t i = 0; i < count; i++)
        new_env[i] = environ[i];
    new_env[count] = kv;
    new_env[count + 1] = NULL;
    if (g_environ_allocated)
        free(environ);
    environ = new_env;
    g_environ_allocated = 1;
    return 0;
}

int unsetenv(const char *name)
{
    size_t nlen = env_key_len(name);
    if (nlen == 0)
        return -1;
    if (!environ)
        return 0;

    size_t count = 0;
    while (environ[count])
        count++;

    if (!g_environ_allocated) {
        char **copy = (char **)malloc((count + 1) * sizeof(char *));
        if (!copy)
            return -1;
        for (size_t i = 0; i <= count; i++)
            copy[i] = environ[i];
        environ = copy;
        g_environ_allocated = 1;
    }

    size_t w = 0;
    for (size_t r = 0; r < count; r++) {
        if (env_match_entry(environ[r], name, nlen))
            continue;
        environ[w++] = environ[r];
    }
    environ[w] = NULL;
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

static void heap_sift_down(unsigned char *arr, size_t start, size_t end,
                           size_t size,
                           int (*compar)(const void *, const void *))
{
    size_t root = start;
    while (end >= 2 && root <= (end - 2) / 2) {
        size_t child = 2 * root + 1;
        size_t swap_idx = root;

        if (compar(arr + swap_idx * size, arr + child * size) < 0)
            swap_idx = child;
        if (child + 1 < end &&
            compar(arr + swap_idx * size, arr + (child + 1) * size) < 0)
            swap_idx = child + 1;
        if (swap_idx == root)
            return;

        swap_bytes(arr + root * size, arr + swap_idx * size, size);
        root = swap_idx;
    }
}

void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *))
{
    unsigned char *arr = (unsigned char *)base;
    if (!arr || !compar || size == 0 || nmemb < 2)
        return;

    for (size_t start = nmemb / 2; start > 0; start--)
        heap_sift_down(arr, start - 1, nmemb, size, compar);

    for (size_t end = nmemb; end > 1; end--) {
        swap_bytes(arr, arr + (end - 1) * size, size);
        heap_sift_down(arr, 0, end - 1, size, compar);
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
