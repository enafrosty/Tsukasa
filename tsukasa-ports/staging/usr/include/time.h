/*
 * Project Tsukasa — Date and Time Operations Header
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

#ifndef _TSUKASA_TIME_H
#define _TSUKASA_TIME_H

#include <sys/types.h>
#include <stddef.h>

typedef long clock_t;
#define CLOCKS_PER_SEC 1000000L

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

struct timespec {
    time_t tv_sec;
    long   tv_nsec;
};

time_t time(time_t *out);
clock_t clock(void);
time_t mktime(struct tm *tm);
static inline double difftime(time_t time1, time_t time0)
{
    return (double)(time1 - time0);
}
struct tm *gmtime_r(const time_t *timer, struct tm *result);
struct tm *gmtime(const time_t *timer);
struct tm *localtime_r(const time_t *timer, struct tm *result);
struct tm *localtime(const time_t *timer);
size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm);

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

int clock_gettime(int clk_id, struct timespec *tp);

int nanosleep(const struct timespec *req, struct timespec *rem);
unsigned int sleep(unsigned int seconds);
int usleep(useconds_t usec);

#endif /* _TSUKASA_TIME_H */
