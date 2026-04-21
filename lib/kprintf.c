/*
 * kprintf.c - Freestanding kernel printf implementation.
 * Outputs via serial_putc (COM1).
 *
 * Supported format specifiers:
 *   %d / %i  - signed decimal
 *   %u       - unsigned decimal
 *   %x       - unsigned hex (lowercase)
 *   %X       - unsigned hex (uppercase)
 *   %s       - C string
 *   %c       - character
 *   %%       - literal '%'
 *   Width / zero-padding: e.g. %08x  %5d
 */

#include "../include/kprintf.h"
#include "../drv/serial.h"
#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>

/* ---- low-level output -------------------------------------------------- */

static void out_char(char c)
{
    serial_putc(c);
}

/* ---- integer rendering ------------------------------------------------- */

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
    /* Reverse. */
    for (int i = 0, j = len - 1; i < j; i++, j--) {
        char tmp = buf[i]; buf[i] = buf[j]; buf[j] = tmp;
    }
    return len;
}

/* ---- core formatter ---------------------------------------------------- */

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
        fmt++;  /* skip '%' */

        /* Parse flags / width. */
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

/* ---- serial output context -------------------------------------------- */

static void serial_put(void *ctx, char c)
{
    (void)ctx;
    out_char(c);
}

int kprintf(const char *fmt, ...)
{
    fmt_ctx_t f;
    f.put   = serial_put;
    f.ctx   = NULL;
    f.count = 0;
    va_list ap;
    va_start(ap, fmt);
    int n = do_fmt(&f, fmt, ap);
    va_end(ap);
    return n;
}

void kputs(const char *s)
{
    if (!s) return;
    while (*s) out_char(*s++);
    out_char('\n');
}

/* ---- ksprintf ---------------------------------------------------------- */

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
