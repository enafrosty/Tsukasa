/*
 * Project Tsukasa — futex10: guide-10 milestone-5 acceptance binary
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

#include "tsukasa_sdk.h"

/* Same virtual address in every spawned instance of this image (see the header comment). */
static volatile unsigned int g_word;

static int str_eq(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int run_probe(void)
{
    long r;

    r = futex(&g_word, 99, 0);
    if (r != -38)
        return 21;

    /* Misaligned uaddr -> -EINVAL (22). */
    r = futex((volatile unsigned int *)((volatile char *)&g_word + 1),
              FUTEX_WAIT, 0);
    if (r != -22)
        return 22;

    /* Kernel-space uaddr -> -EFAULT (14). */
    r = futex((volatile unsigned int *)(unsigned long)0xffff800000000000ULL,
              FUTEX_WAKE, 1);
    if (r != -14)
        return 23;

    g_word = 5;
    r = futex(&g_word, FUTEX_WAIT, 4);
    if (r != -11)
        return 24;

    r = futex(&g_word, FUTEX_WAKE, 1);
    if (r != 0)
        return 25;

    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2 || !argv || !argv[1])
        return 20;

    if (str_eq(argv[1], "probe"))
        return run_probe();

    if (str_eq(argv[1], "waiter")) {
        long r = futex(&g_word, FUTEX_WAIT, 0);
        return (r == 0) ? 0 : 30;
    }

    if (str_eq(argv[1], "waker")) {
        /* Publish-then-wake, the textbook order — even though only the ADDRESS is shared here, keeping the... */
        g_word = 1;
        {
            long r = futex(&g_word, FUTEX_WAKE, 1);
            return (r == 1) ? 0 : 31;
        }
    }

    return 20;
}
