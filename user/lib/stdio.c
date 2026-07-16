#include "../include/stdio.h"

#include "../include/unistd.h"
#include "../include/string.h"

#include <stdint.h>

typedef struct fmt_out {
    int fd;
    char *buf;
    size_t cap;
    size_t len;
    int to_buf;
    int count;
} fmt_out_t;

static void out_char(fmt_out_t *o, char c)
{
    if (!o)
        return;
    o->count++;
    if (o->to_buf) {
        if (o->cap > 0 && o->len + 1 < o->cap) {
            o->buf[o->len++] = c;
            o->buf[o->len] = '\0';
        } else if (o->cap > 0 && o->len + 1 == o->cap) {
            o->buf[o->len] = '\0';
        }
        return;
    }
    (void)write(o->fd, &c, 1);
}

static void out_str(fmt_out_t *o, const char *s)
{
    if (!s)
        s = "(null)";
    while (*s) {
        out_char(o, *s);
        s++;
    }
}

static void out_u(fmt_out_t *o, unsigned long long v, int base, int upper)
{
    char rev[32];
    int ri = 0;
    const char *alpha = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0) {
        out_char(o, '0');
        return;
    }
    while (v && ri < (int)sizeof(rev)) {
        rev[ri++] = alpha[v % (unsigned)base];
        v /= (unsigned)base;
    }
    while (ri > 0)
        out_char(o, rev[--ri]);
}

static void out_i(fmt_out_t *o, long long v)
{
    if (v < 0) {
        out_char(o, '-');
        out_u(o, (unsigned long long)(-v), 10, 0);
        return;
    }
    out_u(o, (unsigned long long)v, 10, 0);
}

static int vfmt(fmt_out_t *o, const char *fmt, va_list ap)
{
    for (size_t i = 0; fmt && fmt[i]; i++) {
        if (fmt[i] != '%') {
            out_char(o, fmt[i]);
            continue;
        }
        i++;
        if (!fmt[i])
            break;
        switch (fmt[i]) {
        case '%':
            out_char(o, '%');
            break;
        case 'c':
            out_char(o, (char)va_arg(ap, int));
            break;
        case 's':
            out_str(o, va_arg(ap, const char *));
            break;
        case 'd':
        case 'i':
            out_i(o, (long long)va_arg(ap, int));
            break;
        case 'u':
            out_u(o, (unsigned long long)va_arg(ap, unsigned int), 10, 0);
            break;
        case 'x':
            out_u(o, (unsigned long long)va_arg(ap, unsigned int), 16, 0);
            break;
        case 'X':
            out_u(o, (unsigned long long)va_arg(ap, unsigned int), 16, 1);
            break;
        case 'p':
            out_str(o, "0x");
            out_u(o, (unsigned long long)(uintptr_t)va_arg(ap, void *), 16, 0);
            break;
        default:
            out_char(o, '%');
            out_char(o, fmt[i]);
            break;
        }
    }
    return o ? o->count : 0;
}

int putchar(int c)
{
    char ch = (char)c;
    if (write(STDOUT_FILENO, &ch, 1) != 1)
        return EOF;
    return (unsigned char)ch;
}

int puts(const char *s)
{
    int n = 0;
    if (!s)
        s = "(null)";
    while (*s) {
        if (putchar((unsigned char)*s++) == EOF)
            return EOF;
        n++;
    }
    if (putchar('\n') == EOF)
        return EOF;
    return n + 1;
}

int vdprintf(int fd, const char *fmt, va_list ap)
{
    fmt_out_t out;
    out.fd = fd;
    out.buf = 0;
    out.cap = 0;
    out.len = 0;
    out.to_buf = 0;
    out.count = 0;
    return vfmt(&out, fmt, ap);
}

int dprintf(int fd, const char *fmt, ...)
{
    va_list ap;
    int rc;
    va_start(ap, fmt);
    rc = vdprintf(fd, fmt, ap);
    va_end(ap);
    return rc;
}

int vprintf(const char *fmt, va_list ap)
{
    return vdprintf(STDOUT_FILENO, fmt, ap);
}

int printf(const char *fmt, ...)
{
    va_list ap;
    int rc;
    va_start(ap, fmt);
    rc = vdprintf(STDOUT_FILENO, fmt, ap);
    va_end(ap);
    return rc;
}

int vsnprintf(char *out, size_t size, const char *fmt, va_list ap)
{
    fmt_out_t fo;
    if (!out || size == 0)
        return 0;
    out[0] = '\0';
    fo.fd = -1;
    fo.buf = out;
    fo.cap = size;
    fo.len = 0;
    fo.to_buf = 1;
    fo.count = 0;
    return vfmt(&fo, fmt, ap);
}

int snprintf(char *out, size_t size, const char *fmt, ...)
{
    va_list ap;
    int rc;
    va_start(ap, fmt);
    rc = vsnprintf(out, size, fmt, ap);
    va_end(ap);
    return rc;
}
