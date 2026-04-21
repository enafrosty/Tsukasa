/*
 * rtc.h - CMOS Real-Time Clock driver (x86 ports 0x70 / 0x71).
 */

#ifndef RTC_H
#define RTC_H

#include <stdint.h>

typedef struct {
    uint8_t  sec;
    uint8_t  min;
    uint8_t  hour;
    uint8_t  day;
    uint8_t  month;
    uint16_t year;
} rtc_time_t;

/** Read and cache the initial RTC time.  Must be called once during boot. */
void rtc_init(void);

/**
 * Fill *t with the current RTC date/time.
 * Values are all decoded from BCD and represent real calendar numbers.
 */
void rtc_read(rtc_time_t *t);

#endif /* RTC_H */
