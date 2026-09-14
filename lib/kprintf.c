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

/* low-level output */

static void out_char(char c)
{
    serial_putc(c);
}

static const char hex_lower[] = "0123456789abcdef";
static const char hex_upper[] = "0123456789ABCDEF";

/* Write an unsigned integer in `base` into buf (reversed), return length. */
static int uint_to_buf(char *buf, uint32_t val, uint32_t base, const char *digits)
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
    char tmp[32];

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

        char spec = *fmt++;
        switch (spec) {
        case 'd':
        case 'i': {
            int32_t val = va_arg(ap, int32_t);
            if (val < 0) { fmt_putc(f, '-'); val = -val; }
            int len = uint_to_buf(tmp, (uint32_t)val, 10, hex_lower);
            if (len < width) fmt_pad(f, pad, width - len);
            for (int i = 0; i < len; i++) fmt_putc(f, tmp[i]);
            break;
        }
        case 'u': {
            uint32_t val = va_arg(ap, uint32_t);
            int len = uint_to_buf(tmp, val, 10, hex_lower);
            if (len < width) fmt_pad(f, pad, width - len);
            for (int i = 0; i < len; i++) fmt_putc(f, tmp[i]);
            break;
        }
        case 'x': {
            uint32_t val = va_arg(ap, uint32_t);
            int len = uint_to_buf(tmp, val, 16, hex_lower);
            if (len < width) fmt_pad(f, pad, width - len);
            for (int i = 0; i < len; i++) fmt_putc(f, tmp[i]);
            break;
        }
        case 'X': {
            uint32_t val = va_arg(ap, uint32_t);
            int len = uint_to_buf(tmp, val, 16, hex_upper);
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
