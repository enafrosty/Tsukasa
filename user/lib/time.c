#include "../include/time.h"

#include "../lib/syscall.h"
#include "../include/string.h"

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
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2)
        return is_leap(year) ? 29 : 28;
    if (month < 1 || month > 12)
        return 30;
    return days[month - 1];
}

static time_t rtc_to_epoch(const struct tsukasa_time *rt)
{
    time_t days = 0;
    time_t secs = 0;
    int y;
    int m;
    if (!rt)
        return 0;

    for (y = 1970; y < (int)rt->year; y++)
        days += is_leap(y) ? 366 : 365;
    for (m = 1; m < (int)rt->month; m++)
        days += days_in_month((int)rt->year, m);
    days += (time_t)rt->day - 1;

    secs = days * 86400;
    secs += (time_t)rt->hour * 3600;
    secs += (time_t)rt->min * 60;
    secs += (time_t)rt->sec;
    return secs;
}

time_t time(time_t *out)
{
    struct tsukasa_time rt;
    time_t now;
    if (system_time_get(&rt) != 0)
        return (time_t)-1;
    now = rtc_to_epoch(&rt);
    if (out)
        *out = now;
    return now;
}

struct tm *gmtime_r(const time_t *timer, struct tm *result)
{
    time_t t;
    int year = 1970;
    int month = 1;
    int yday = 0;
    int dpm;
    if (!timer || !result)
        return 0;
    t = *timer;
    if (t < 0)
        t = 0;

    result->tm_sec = (int)(t % 60);
    t /= 60;
    result->tm_min = (int)(t % 60);
    t /= 60;
    result->tm_hour = (int)(t % 24);
    t /= 24;

    while (1) {
        int diy = is_leap(year) ? 366 : 365;
        if (t < diy)
            break;
        t -= diy;
        year++;
    }
    yday = (int)t;
    while (1) {
        dpm = days_in_month(year, month);
        if (t < dpm)
            break;
        t -= dpm;
        month++;
    }

    result->tm_mday = (int)t + 1;
    result->tm_mon = month - 1;
    result->tm_year = year - 1900;
    result->tm_yday = yday;
    result->tm_wday = (int)((*timer / 86400 + 4) % 7); /* 1970-01-01 = Thu */
    result->tm_isdst = 0;
    return result;
}

struct tm *gmtime(const time_t *timer)
{
    static struct tm tmp;
    return gmtime_r(timer, &tmp);
}

static int append_num(char *out, size_t max, size_t *oi, int v, int width)
{
    char buf[16];
    int bi = width;
    if (!out || !oi || max == 0 || width <= 0 || width >= (int)sizeof(buf))
        return -1;
    while (bi > 0) {
        buf[bi - 1] = (char)('0' + (v % 10));
        v /= 10;
        bi--;
    }
    for (int i = 0; i < width; i++) {
        if (*oi + 1 >= max)
            return -1;
        out[(*oi)++] = buf[i];
    }
    out[*oi] = '\0';
    return 0;
}

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
            if (append_num(s, max, &oi, tm->tm_year + 1900, 4) != 0)
                return 0;
            break;
        case 'm':
            if (append_num(s, max, &oi, tm->tm_mon + 1, 2) != 0)
                return 0;
            break;
        case 'd':
            if (append_num(s, max, &oi, tm->tm_mday, 2) != 0)
                return 0;
            break;
        case 'H':
            if (append_num(s, max, &oi, tm->tm_hour, 2) != 0)
                return 0;
            break;
        case 'M':
            if (append_num(s, max, &oi, tm->tm_min, 2) != 0)
                return 0;
            break;
        case 'S':
            if (append_num(s, max, &oi, tm->tm_sec, 2) != 0)
                return 0;
            break;
        case '%':
            if (oi + 1 >= max)
                return 0;
            s[oi++] = '%';
            s[oi] = '\0';
            break;
        default:
            if (oi + 2 >= max)
                return 0;
            s[oi++] = '%';
            s[oi++] = fmt[i];
            s[oi] = '\0';
            break;
        }
    }
    return oi;
}
