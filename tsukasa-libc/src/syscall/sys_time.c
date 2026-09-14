/*
 * Project Tsukasa — Time and Sleep System Call Wrappers
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

#include "syscall_internal.h"
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    (void)rem;
    if (!req) {
        errno = EFAULT;
        return -1;
    }
    if (req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L) {
        errno = EINVAL;
        return -1;
    }
    long ms = (long)req->tv_sec * 1000 + (long)(req->tv_nsec / 1000000L);
    if (ms == 0 && req->tv_nsec > 0)
        ms = 1;
    return (int)__syscall_check(__syscall1(SYS_nanosleep, ms));
}

unsigned int sleep(unsigned int seconds)
{
    struct timespec req;
    req.tv_sec = (time_t)seconds;
    req.tv_nsec = 0;
    if (nanosleep(&req, NULL) < 0)
        return seconds;
    return 0;
}

int usleep(useconds_t usec)
{
    struct timespec req;
    req.tv_sec = (time_t)(usec / 1000000);
    req.tv_nsec = (long)((usec % 1000000) * 1000);
    return nanosleep(&req, NULL);
}

time_t time(time_t *out)
{
    int64_t t = __syscall1(SYS_time, (int64_t)out);
    if (t >= 0)
        return (time_t)t;

    int64_t ticks = __syscall0(SYS_ticks);
    time_t epoch = (ticks >= 0) ? (time_t)(ticks / 100) : 0;
    if (out)
        *out = epoch;
    return epoch;
}

clock_t clock(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (clock_t)ts.tv_sec * 1000000L + (clock_t)(ts.tv_nsec / 1000L);
    }
    return (clock_t)-1;
}

static const int days_before_month[12] = {
    0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
};

time_t mktime(struct tm *tm)
{
    if (!tm)
        return (time_t)-1;

    long year = tm->tm_year + 1900;
    long mon = tm->tm_mon;
    if (mon >= 12) {
        year += mon / 12;
        mon %= 12;
    } else if (mon < 0) {
        long adj = (-mon + 11) / 12;
        year -= adj;
        mon += adj * 12;
    }

    long days = (year - 1970) * 365;
    days += (year - 1969) / 4;
    days -= (year - 1901) / 100;
    days += (year - 1601) / 400;

    days += days_before_month[mon];
    int is_leap = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
    if (mon > 1 && is_leap)
        days++;

    days += (tm->tm_mday - 1);

    time_t t = (time_t)(days * 86400L + tm->tm_hour * 3600L + tm->tm_min * 60L + tm->tm_sec);
    return t;
}

int clock_gettime(int clk_id, struct timespec *tp)
{
    if (!tp) {
        errno = EFAULT;
        return -1;
    }
    if (clk_id == CLOCK_MONOTONIC) {
        int64_t ticks = __syscall0(SYS_ticks);
        if (ticks < 0)
            ticks = 0;
        tp->tv_sec = (time_t)(ticks / 100);
        tp->tv_nsec = (long)((ticks % 100) * 10000000L);
        return 0;
    } else if (clk_id == CLOCK_REALTIME) {
        time_t t = time(NULL);
        tp->tv_sec = t;
        tp->tv_nsec = 0;
        return 0;
    }
    errno = EINVAL;
    return -1;
}

int gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    if (!tv) {
        errno = EFAULT;
        return -1;
    }
    int64_t ticks = __syscall0(SYS_ticks);
    if (ticks < 0)
        ticks = 0;
    time_t t = time(NULL);
    tv->tv_sec = t;
    tv->tv_usec = (suseconds_t)((ticks % 100) * 10000);
    return 0;
}

int settimeofday(const struct timeval *tv, const struct timezone *tz)
{
    (void)tv;
    (void)tz;
    errno = EPERM;
    return -1;
}

static int is_leap(int year)
{
    if ((year % 4) != 0)
        return 0;
    if ((year % 100) != 0)
        return 1;
    return (year % 400) == 0;
}

static int days_in_month(int year, int month)
{
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2)
        return is_leap(year) ? 29 : 28;
    if (month < 1 || month > 12)
        return 30;
    return days[month - 1];
}

struct tm *gmtime_r(const time_t *timer, struct tm *result)
{
    if (!timer || !result)
        return NULL;
    time_t t = *timer;
    if (t < 0)
        t = 0;

    result->tm_sec = (int)(t % 60);
    t /= 60;
    result->tm_min = (int)(t % 60);
    t /= 60;
    result->tm_hour = (int)(t % 24);
    t /= 24;

    result->tm_wday = (int)((t + 4) % 7);

    int year = 1970;
    while (1) {
        int diy = is_leap(year) ? 366 : 365;
        if (t < diy)
            break;
        t -= diy;
        year++;
    }
    result->tm_year = year - 1900;
    result->tm_yday = (int)t;

    int month = 1;
    while (1) {
        int dpm = days_in_month(year, month);
        if (t < dpm)
            break;
        t -= dpm;
        month++;
    }
    result->tm_mon = month - 1;
    result->tm_mday = (int)t + 1;
    result->tm_isdst = 0;
    return result;
}

struct tm *gmtime(const time_t *timer)
{
    static struct tm static_tm;
    return gmtime_r(timer, &static_tm);
}

struct tm *localtime_r(const time_t *timer, struct tm *result)
{
    return gmtime_r(timer, result);
}

struct tm *localtime(const time_t *timer)
{
    return gmtime(timer);
}

static int append_str(char *s, size_t max, size_t *oi, const char *str)
{
    while (*str) {
        if (*oi + 1 >= max)
            return -1;
        s[(*oi)++] = *str++;
    }
    s[*oi] = '\0';
    return 0;
}

static int append_num(char *s, size_t max, size_t *oi, int v, int width, char pad)
{
    char buf[16];
    int bi = width;
    if (v < 0)
        v = 0;
    while (bi > 0) {
        buf[bi - 1] = (char)('0' + (v % 10));
        v /= 10;
        bi--;
    }
    if (pad != '0') {
        int i = 0;
        while (i < width - 1 && buf[i] == '0') {
            buf[i] = pad;
            i++;
        }
    }
    for (int i = 0; i < width; i++) {
        if (*oi + 1 >= max)
            return -1;
        s[(*oi)++] = buf[i];
    }
    s[*oi] = '\0';
    return 0;
}

static const char *const s_wday_short[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *const s_wday_long[]  = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char *const s_mon_short[]  = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static const char *const s_mon_long[]   = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm)
{
    size_t oi = 0;
    if (!s || max == 0 || !fmt || !tm)
        return 0;
    s[0] = '\0';

    for (size_t i = 0; fmt[i]; i++) {
        if (fmt[i] != '%') {
            if (oi + 1 >= max)
                return 0;
            s[oi++] = fmt[i];
            s[oi] = '\0';
            continue;
        }

        i++;
        if (!fmt[i])
            break;

        switch (fmt[i]) {
        case 'Y':
            if (append_num(s, max, &oi, tm->tm_year + 1900, 4, '0') != 0) return 0;
            break;
        case 'y':
            if (append_num(s, max, &oi, (tm->tm_year + 1900) % 100, 2, '0') != 0) return 0;
            break;
        case 'm':
            if (append_num(s, max, &oi, tm->tm_mon + 1, 2, '0') != 0) return 0;
            break;
        case 'd':
            if (append_num(s, max, &oi, tm->tm_mday, 2, '0') != 0) return 0;
            break;
        case 'e':
            if (append_num(s, max, &oi, tm->tm_mday, 2, ' ') != 0) return 0;
            break;
        case 'H':
            if (append_num(s, max, &oi, tm->tm_hour, 2, '0') != 0) return 0;
            break;
        case 'I':
        {
            int h = tm->tm_hour % 12;
            if (h == 0) h = 12;
            if (append_num(s, max, &oi, h, 2, '0') != 0) return 0;
            break;
        }
        case 'p':
            if (append_str(s, max, &oi, (tm->tm_hour >= 12) ? "PM" : "AM") != 0) return 0;
            break;
        case 'P':
            if (append_str(s, max, &oi, (tm->tm_hour >= 12) ? "pm" : "am") != 0) return 0;
            break;
        case 'M':
            if (append_num(s, max, &oi, tm->tm_min, 2, '0') != 0) return 0;
            break;
        case 'S':
            if (append_num(s, max, &oi, tm->tm_sec, 2, '0') != 0) return 0;
            break;
        case 'a':
            if (tm->tm_wday >= 0 && tm->tm_wday < 7) {
                if (append_str(s, max, &oi, s_wday_short[tm->tm_wday]) != 0) return 0;
            }
            break;
        case 'A':
            if (tm->tm_wday >= 0 && tm->tm_wday < 7) {
                if (append_str(s, max, &oi, s_wday_long[tm->tm_wday]) != 0) return 0;
            }
            break;
        case 'b':
        case 'h':
            if (tm->tm_mon >= 0 && tm->tm_mon < 12) {
                if (append_str(s, max, &oi, s_mon_short[tm->tm_mon]) != 0) return 0;
            }
            break;
        case 'B':
            if (tm->tm_mon >= 0 && tm->tm_mon < 12) {
                if (append_str(s, max, &oi, s_mon_long[tm->tm_mon]) != 0) return 0;
            }
            break;
        case 'F':
            if (strftime(s + oi, max - oi, "%Y-%m-%d", tm) == 0) return 0;
            while (s[oi]) oi++;
            break;
        case 'T':
        case 'X':
            if (strftime(s + oi, max - oi, "%H:%M:%S", tm) == 0) return 0;
            while (s[oi]) oi++;
            break;
        case 'R':
            if (strftime(s + oi, max - oi, "%H:%M", tm) == 0) return 0;
            while (s[oi]) oi++;
            break;
        case 'c':
            if (strftime(s + oi, max - oi, "%a %b %e %H:%M:%S %Y", tm) == 0) return 0;
            while (s[oi]) oi++;
            break;
        case 'D':
        case 'x':
            if (strftime(s + oi, max - oi, "%m/%d/%y", tm) == 0) return 0;
            while (s[oi]) oi++;
            break;
        case 'Z':
            if (append_str(s, max, &oi, "UTC") != 0) return 0;
            break;
        case 'z':
            if (append_str(s, max, &oi, "+0000") != 0) return 0;
            break;
        case 'j':
            if (append_num(s, max, &oi, tm->tm_yday + 1, 3, '0') != 0) return 0;
            break;
        case 'w':
            if (append_num(s, max, &oi, tm->tm_wday, 1, '0') != 0) return 0;
            break;
        case 'u':
            if (append_num(s, max, &oi, (tm->tm_wday == 0) ? 7 : tm->tm_wday, 1, '0') != 0) return 0;
            break;
        case '%':
            if (oi + 1 >= max) return 0;
            s[oi++] = '%';
            s[oi] = '\0';
            break;
        default:
            if (oi + 2 >= max) return 0;
            s[oi++] = '%';
            s[oi++] = fmt[i];
            s[oi] = '\0';
            break;
        }
    }

    return oi;
}
