/*
 * Project Tsukasa — Freestanding kernel printf implementation
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

#include "../include/kprintf.h"
#include "../drv/serial.h"
#include "../include/spinlock.h"
#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>

/* The acquire is a BOUNDED spin, never a hard wait: a panicking core (or a same-core IRQ handler that... */
static spinlock_t g_kprintf_lock = SPINLOCK_INIT;

static int kprintf_lock_acquire(void)
{
    for (uint32_t spins = 0; spins < 4000000u; spins++) {
        if (spin_trylock(&g_kprintf_lock))
            return 1;
        __asm__ volatile ("pause");
    }
    return 0;
}

static void kprintf_lock_release(int owned)
{
    if (owned)
        spin_unlock(&g_kprintf_lock);
}

#define MAX_LOG_SUBSYSTEMS 16
typedef struct {
    char subsys[16];
    enum k_loglevel level;
    int active;
} k_subsys_log_t;

enum k_loglevel k_log_threshold = K_INFO;
static k_subsys_log_t g_subsys_logs[MAX_LOG_SUBSYSTEMS];

static int k_streq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return (*a == *b);
}

static void k_strlcpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    if (!n) return;
    while (i + 1 < n && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void k_log_set_level(const char *subsys, enum k_loglevel lvl)
{
    if (!subsys) return;
    for (int i = 0; i < MAX_LOG_SUBSYSTEMS; i++) {
        if (g_subsys_logs[i].active && k_streq(g_subsys_logs[i].subsys, subsys)) {
            g_subsys_logs[i].level = lvl;
            return;
        }
    }
    for (int i = 0; i < MAX_LOG_SUBSYSTEMS; i++) {
        if (!g_subsys_logs[i].active) {
            g_subsys_logs[i].active = 1;
            k_strlcpy(g_subsys_logs[i].subsys, subsys, sizeof(g_subsys_logs[i].subsys));
            g_subsys_logs[i].level = lvl;
            return;
        }
    }
}

int k_log_enabled(enum k_loglevel lvl, const char *subsys)
{
    if (subsys) {
        for (int i = 0; i < MAX_LOG_SUBSYSTEMS; i++) {
            if (g_subsys_logs[i].active && k_streq(g_subsys_logs[i].subsys, subsys)) {
                return (int)lvl <= (int)g_subsys_logs[i].level;
            }
        }
    }
    return (int)lvl <= (int)k_log_threshold;
}

static enum k_loglevel parse_level_name(const char *name, size_t len)
{
    if (len == 3 && name[0] == 'e' && name[1] == 'r' && name[2] == 'r') return K_ERR;
    if (len == 4 && name[0] == 'w' && name[1] == 'a' && name[2] == 'r' && name[3] == 'n') return K_WARN;
    if (len == 4 && name[0] == 'i' && name[1] == 'n' && name[2] == 'f' && name[3] == 'o') return K_INFO;
    if (len == 5 && name[0] == 'd' && name[1] == 'e' && name[2] == 'b' && name[3] == 'u' && name[4] == 'g') return K_DEBUG;
    if (len == 1) {
        if (name[0] == '0') return K_ERR;
        if (name[0] == '1') return K_WARN;
        if (name[0] == '2') return K_INFO;
        if (name[0] == '3') return K_DEBUG;
    }
    return K_INFO;
}

void k_log_init(const char *cmdline)
{
    if (!cmdline) return;

    const char *p = cmdline;
    while (*p) {
        if (p[0] == 't' && p[1] == 's' && p[2] == 'u' && p[3] == 'k' && p[4] == 'a' && p[5] == 's' && p[6] == 'a' && p[7] == '.' &&
            p[8] == 'l' && p[9] == 'o' && p[10] == 'g' && p[11] == 'l' && p[12] == 'e' && p[13] == 'v' && p[14] == 'e' && p[15] == 'l' && p[16] == '=') {
            p += 17;
            const char *start = p;
            while (*p && *p != ' ') p++;
            k_log_threshold = parse_level_name(start, (size_t)(p - start));
            continue;
        }

        if (p[0] == 't' && p[1] == 's' && p[2] == 'u' && p[3] == 'k' && p[4] == 'a' && p[5] == 's' && p[6] == 'a' && p[7] == '.' &&
            p[8] == 'l' && p[9] == 'o' && p[10] == 'g' && p[11] == 'f' && p[12] == 'i' && p[13] == 'l' && p[14] == 't' && p[15] == 'e' && p[16] == 'r' && p[17] == '=') {
            p += 18;
            while (*p && *p != ' ') {
                const char *tag_start = p;
                while (*p && *p != ':' && *p != ',' && *p != ' ') p++;
                if (*p != ':') break;
                size_t tag_len = (size_t)(p - tag_start);
                p++;
                const char *lvl_start = p;
                while (*p && *p != ',' && *p != ' ') p++;
                size_t lvl_len = (size_t)(p - lvl_start);

                char tag[16];
                if (tag_len >= sizeof(tag)) tag_len = sizeof(tag) - 1;
                for (size_t t = 0; t < tag_len; t++) tag[t] = tag_start[t];
                tag[tag_len] = '\0';

                enum k_loglevel lvl = parse_level_name(lvl_start, lvl_len);
                k_log_set_level(tag, lvl);

                if (*p == ',') p++;
            }
            continue;
        }
        p++;
    }
}

/* low-level output */

static void out_char(char c)
{
    serial_putc(c);
}

static const char hex_lower[] = "0123456789abcdef";
static const char hex_upper[] = "0123456789ABCDEF";

/* Write an unsigned integer in `base` into buf (reversed), return length. */
static int uint_to_buf(char *buf, uint64_t val, uint32_t base, const char *digits)
{
    if (val == 0) {
        buf[0] = '0';
        return 1;
    }
    int len = 0;
    while (val) {
        buf[len++] = digits[val % base];
        val /= base;
    }
    for (int i = 0, j = len - 1; i < j; i++, j--) {
        char tmp = buf[i]; buf[i] = buf[j]; buf[j] = tmp;
    }
    return len;
}

typedef void (*put_fn)(void *ctx, char c);

typedef struct {
    put_fn  put;
    void   *ctx;
    int     count;
} fmt_ctx_t;

static void fmt_putc(fmt_ctx_t *f, char c)
{
    f->put(f->ctx, c);
    f->count++;
}

static void fmt_pad(fmt_ctx_t *f, char pad, int width)
{
    while (width-- > 0)
        fmt_putc(f, pad);
}

static void fmt_str(fmt_ctx_t *f, const char *s, int width, char pad)
{
    if (!s) s = "(null)";
    int len = 0;
    const char *p = s;
    while (*p++) len++;
    if (len < width) fmt_pad(f, pad, width - len);
    while (*s) fmt_putc(f, *s++);
}

static int do_fmt(fmt_ctx_t *f, const char *fmt, va_list ap)
{
    char tmp[64];

    while (*fmt) {
        if (*fmt != '%') {
            fmt_putc(f, *fmt++);
            continue;
        }
        fmt++;

        char pad = ' ';
        int  width = 0;

        if (*fmt == '0') { pad = '0'; fmt++; }
        while (*fmt >= '1' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');

        int is_long = 0;
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
            if (*fmt == 'l') {
                is_long = 2;
                fmt++;
            }
        } else if (*fmt == 'z') {
            is_long = 1;
            fmt++;
        }

        char spec = *fmt++;
        switch (spec) {
        case 'd':
        case 'i': {
            int64_t val = is_long ? va_arg(ap, int64_t) : (int64_t)va_arg(ap, int32_t);
            if (val < 0) { fmt_putc(f, '-'); val = -val; }
            int len = uint_to_buf(tmp, (uint64_t)val, 10, hex_lower);
            if (len < width) fmt_pad(f, pad, width - len);
            for (int i = 0; i < len; i++) fmt_putc(f, tmp[i]);
            break;
        }
        case 'u': {
            uint64_t val = is_long ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, uint32_t);
            int len = uint_to_buf(tmp, val, 10, hex_lower);
            if (len < width) fmt_pad(f, pad, width - len);
            for (int i = 0; i < len; i++) fmt_putc(f, tmp[i]);
            break;
        }
        case 'x': {
            uint64_t val = is_long ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, uint32_t);
            int len = uint_to_buf(tmp, val, 16, hex_lower);
            if (len < width) fmt_pad(f, pad, width - len);
            for (int i = 0; i < len; i++) fmt_putc(f, tmp[i]);
            break;
        }
        case 'X': {
            uint64_t val = is_long ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, uint32_t);
            int len = uint_to_buf(tmp, val, 16, hex_upper);
            if (len < width) fmt_pad(f, pad, width - len);
            for (int i = 0; i < len; i++) fmt_putc(f, tmp[i]);
            break;
        }
        case 'p': {
            uint64_t val = (uint64_t)(uintptr_t)va_arg(ap, void *);
            int len = uint_to_buf(tmp, val, 16, hex_lower);
            if (len < width) fmt_pad(f, pad, width - len);
            for (int i = 0; i < len; i++) fmt_putc(f, tmp[i]);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            fmt_str(f, s, width, pad);
            break;
        }
        case 'c':
            fmt_putc(f, (char)va_arg(ap, int));
            break;
        case '%':
            fmt_putc(f, '%');
            break;
        default:
            fmt_putc(f, '%');
            fmt_putc(f, spec);
            break;
        }
    }
    return f->count;
}

static void serial_put(void *ctx, char c)
{
    (void)ctx;
    out_char(c);
}

int kprintf(const char *fmt, ...)
{
    fmt_ctx_t f;
    int owned;
    f.put   = serial_put;
    f.ctx   = NULL;
    f.count = 0;
    va_list ap;
    va_start(ap, fmt);
    owned = kprintf_lock_acquire();
    int n = do_fmt(&f, fmt, ap);
    kprintf_lock_release(owned);
    va_end(ap);
    return n;
}

void kputs(const char *s)
{
    int owned;
    if (!s) return;
    owned = kprintf_lock_acquire();
    while (*s) out_char(*s++);
    out_char('\n');
    kprintf_lock_release(owned);
}

typedef struct {
    char  *buf;
    size_t cap;
    size_t pos;
} sbuf_t;

static void sbuf_put(void *ctx, char c)
{
    sbuf_t *s = (sbuf_t *)ctx;
    if (s->pos + 1 < s->cap)
        s->buf[s->pos++] = c;
}

int ksprintf(char *buf, size_t n, const char *fmt, ...)
{
    if (!buf || n == 0) return 0;
    sbuf_t sb = { buf, n, 0 };
    fmt_ctx_t f;
    f.put   = sbuf_put;
    f.ctx   = &sb;
    f.count = 0;
    va_list ap;
    va_start(ap, fmt);
    int ret = do_fmt(&f, fmt, ap);
    va_end(ap);
    buf[sb.pos] = '\0';
    return ret;
}
