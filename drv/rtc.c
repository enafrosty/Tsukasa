/*
 * rtc.c - CMOS Real-Time Clock driver.
 *
 * Reads via CMOS ports 0x70 (index write) / 0x71 (data read).
 * Handles BCD encoding and the "Update In Progress" flag.
 * Supports century register (register 0x32) if the BIOS sets it.
 */

#include "../drv/rtc.h"
#include <stdint.h>

#define CMOS_ADDR  0x70u
#define CMOS_DATA  0x71u

/* CMOS register indices. */
#define CMOS_SEC       0x00u
#define CMOS_MIN       0x02u
#define CMOS_HOUR      0x04u
#define CMOS_DAY       0x07u
#define CMOS_MONTH     0x08u
#define CMOS_YEAR      0x09u
#define CMOS_STATUS_B  0x0Bu
#define CMOS_STATUS_A  0x0Au
#define CMOS_CENTURY   0x32u

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static uint8_t cmos_read(uint8_t reg)
{
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

/* Returns 1 while the RTC update-in-progress bit is set. */
static int rtc_updating(void)
{
    return (cmos_read(CMOS_STATUS_A) & 0x80u) ? 1 : 0;
}

/* Convert BCD byte to binary. */
static uint8_t bcd2bin(uint8_t bcd)
{
    return (uint8_t)(((bcd >> 4) * 10u) + (bcd & 0x0Fu));
}

static rtc_time_t g_cached;

/** Read until two consecutive identical reads — avoids mid-update glitches. */
static void rtc_read_raw(rtc_time_t *t)
{
    rtc_time_t prev;
    uint8_t status_b;

    /* Wait for any update-in-progress to clear. */
    while (rtc_updating())
        __asm__ volatile ("pause");

    /* First read. */
    prev.sec   = cmos_read(CMOS_SEC);
    prev.min   = cmos_read(CMOS_MIN);
    prev.hour  = cmos_read(CMOS_HOUR);
    prev.day   = cmos_read(CMOS_DAY);
    prev.month = cmos_read(CMOS_MONTH);
    prev.year  = cmos_read(CMOS_YEAR);

    /* Repeat until stable. */
    do {
        *t = prev;
        while (rtc_updating())
            __asm__ volatile ("pause");
        prev.sec   = cmos_read(CMOS_SEC);
        prev.min   = cmos_read(CMOS_MIN);
        prev.hour  = cmos_read(CMOS_HOUR);
        prev.day   = cmos_read(CMOS_DAY);
        prev.month = cmos_read(CMOS_MONTH);
        prev.year  = cmos_read(CMOS_YEAR);
    } while (t->sec   != prev.sec   || t->min   != prev.min  ||
             t->hour  != prev.hour  || t->day   != prev.day  ||
             t->month != prev.month || t->year  != prev.year);

    status_b = cmos_read(CMOS_STATUS_B);

    /* If bit 2 of Status B is clear, values are BCD-encoded. */
    if (!(status_b & 0x04u)) {
        t->sec   = bcd2bin(t->sec);
        t->min   = bcd2bin(t->min);
        t->hour  = bcd2bin(t->hour & 0x7Fu) | (t->hour & 0x80u);
        t->day   = bcd2bin(t->day);
        t->month = bcd2bin(t->month);
        t->year  = bcd2bin((uint8_t)t->year);
    }

    /* Convert 12-h to 24-h if needed. */
    if (!(status_b & 0x02u) && (t->hour & 0x80u)) {
        t->hour = (uint8_t)(((t->hour & 0x7Fu) % 12u) + 12u);
    }

    /* Determine full year. */
    uint8_t century = cmos_read(CMOS_CENTURY);
    uint16_t full_year;
    if (century != 0 && century != 0xFF) {
        uint8_t c = (status_b & 0x04u) ? century : bcd2bin(century);
        full_year = (uint16_t)(c * 100u + t->year);
    } else {
        /* BIOS doesn't expose century — assume 2000s. */
        full_year = (uint16_t)(((uint16_t)t->year < 70u) ?
                               2000u + t->year : 1900u + t->year);
    }
    t->year = full_year;
}

void rtc_init(void)
{
    rtc_read_raw(&g_cached);
}

void rtc_read(rtc_time_t *t)
{
    if (!t) return;
    rtc_read_raw(t);
}
