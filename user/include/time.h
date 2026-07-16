#ifndef TSUKASA_TIME_H
#define TSUKASA_TIME_H

#include <stddef.h>
#include "sys/types.h"

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

time_t time(time_t *out);
struct tm *gmtime_r(const time_t *timer, struct tm *result);
struct tm *gmtime(const time_t *timer);
size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm);

#endif /* TSUKASA_TIME_H */
