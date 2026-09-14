/*
 * Project Tsukasa — Formatted String Printing Engine Implementation
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

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    char  *buf;
    size_t cap;
    size_t len;
    int    count;
} str_buf_t;

static void buf_putc(str_buf_t *b, char c)
{
    if (!b)
        return;
    b->count++;
    if (b->buf && b->cap > 0) {
        if (b->len + 1 < b->cap) {
            b->buf[b->len++] = c;
            b->buf[b->len] = '\0';
        }
    }
}

static void buf_puts(str_buf_t *b, const char *s, int max_len)
{
    if (!s)
        s = "(null)";
    int written = 0;
    while (*s && (max_len < 0 || written < max_len)) {
        buf_putc(b, *s);
        s++;
        written++;
    }
}

static void buf_pad(str_buf_t *b, char pad_char, int count)
{
    for (int i = 0; i < count; i++)
        buf_putc(b, pad_char);
}

int vsnprintf(char *out, size_t size, const char *fmt, va_list ap)
{
    str_buf_t b;
    b.buf = out;
    b.cap = size;
    b.len = 0;
    b.count = 0;

    if (out && size > 0)
        out[0] = '\0';

    if (!fmt)
        return 0;

    for (size_t i = 0; fmt[i]; i++) {
        if (fmt[i] != '%') {
            buf_putc(&b, fmt[i]);
            continue;
        }

        i++;
        if (!fmt[i])
            break;

        /* Flags */
        int left_align = 0;
        int show_sign = 0;
        int space_sign = 0;
        int zero_pad = 0;
        int alt_form = 0;

        while (1) {
            if (fmt[i] == '-')
                left_align = 1;
            else if (fmt[i] == '+')
                show_sign = 1;
            else if (fmt[i] == ' ')
                space_sign = 1;
            else if (fmt[i] == '0')
                zero_pad = 1;
            else if (fmt[i] == '#')
                alt_form = 1;
            else
                break;
            i++;
        }
        if (left_align)
            zero_pad = 0;

        /* Field width */
        int width = 0;
        if (fmt[i] == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                left_align = 1;
                zero_pad = 0;
                width = -width;
            }
            i++;
        } else {
            while (fmt[i] >= '0' && fmt[i] <= '9') {
                width = width * 10 + (fmt[i] - '0');
                i++;
            }
        }

        /* Precision */
        int precision = -1;
        if (fmt[i] == '.') {
            i++;
            precision = 0;
            if (fmt[i] == '*') {
                precision = va_arg(ap, int);
                i++;
            } else {
                while (fmt[i] >= '0' && fmt[i] <= '9') {
                    precision = precision * 10 + (fmt[i] - '0');
                    i++;
                }
            }
        }

        /* Length modifiers */
        int len_mod = 0; /* 0=int, 1=l, 2=ll, 3=z, 4=h, 5=hh */
        if (fmt[i] == 'l') {
            if (fmt[i + 1] == 'l') {
                len_mod = 2;
                i += 2;
            } else {
                len_mod = 1;
                i++;
            }
        } else if (fmt[i] == 'z') {
            len_mod = 3;
            i++;
        } else if (fmt[i] == 'h') {
            if (fmt[i + 1] == 'h') {
                len_mod = 5;
                i += 2;
            } else {
                len_mod = 4;
                i++;
            }
        }

        char spec = fmt[i];
        if (spec == '%') {
            buf_putc(&b, '%');
            continue;
        }

        if (spec == 'c') {
            char c = (char)va_arg(ap, int);
            if (!left_align && width > 1)
                buf_pad(&b, ' ', width - 1);
            buf_putc(&b, c);
            if (left_align && width > 1)
                buf_pad(&b, ' ', width - 1);
            continue;
        }

        if (spec == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            int slen = 0;
            while (s[slen])
                slen++;
            if (precision >= 0 && slen > precision)
                slen = precision;

            if (!left_align && width > slen)
                buf_pad(&b, ' ', width - slen);
            buf_puts(&b, s, slen);
            if (left_align && width > slen)
                buf_pad(&b, ' ', width - slen);
            continue;
        }

        if (spec == 'd' || spec == 'i') {
            long long val;
            if (len_mod == 2)
                val = va_arg(ap, long long);
            else if (len_mod == 1)
                val = va_arg(ap, long);
            else if (len_mod == 3)
                val = (long long)va_arg(ap, ssize_t);
            else if (len_mod == 4)
                val = (short)va_arg(ap, int);
            else if (len_mod == 5)
                val = (signed char)va_arg(ap, int);
            else
                val = va_arg(ap, int);

            int is_neg = (val < 0);
            unsigned long long uval = is_neg ? (unsigned long long)(-val) : (unsigned long long)val;

            char num_buf[32];
            int nlen = 0;
            if (uval == 0) {
                num_buf[nlen++] = '0';
            } else {
                while (uval > 0 && nlen < 31) {
                    num_buf[nlen++] = '0' + (uval % 10);
                    uval /= 10;
                }
            }

            char sign_char = 0;
            if (is_neg)
                sign_char = '-';
            else if (show_sign)
                sign_char = '+';
            else if (space_sign)
                sign_char = ' ';

            int num_digits = nlen;
            int zeroes = 0;
            if (precision > num_digits)
                zeroes = precision - num_digits;
            else if (zero_pad && width > num_digits + (sign_char ? 1 : 0))
                zeroes = width - num_digits - (sign_char ? 1 : 0);

            int total_len = num_digits + zeroes + (sign_char ? 1 : 0);

            if (!left_align && width > total_len)
                buf_pad(&b, ' ', width - total_len);
            if (sign_char)
                buf_putc(&b, sign_char);
            buf_pad(&b, '0', zeroes);
            while (nlen > 0)
                buf_putc(&b, num_buf[--nlen]);
            if (left_align && width > total_len)
                buf_pad(&b, ' ', width - total_len);
            continue;
        }

        if (spec == 'u' || spec == 'x' || spec == 'X' || spec == 'b' || spec == 'p') {
            unsigned long long uval;
            int base = 10;
            int upper = (spec == 'X');

            if (spec == 'p') {
                uval = (uintptr_t)va_arg(ap, void *);
                base = 16;
                alt_form = 1;
            } else {
                base = (spec == 'b') ? 2 : ((spec == 'x' || spec == 'X') ? 16 : 10);
                if (len_mod == 2)
                    uval = va_arg(ap, unsigned long long);
                else if (len_mod == 1)
                    uval = va_arg(ap, unsigned long);
                else if (len_mod == 3)
                    uval = va_arg(ap, size_t);
                else if (len_mod == 4)
                    uval = (unsigned short)va_arg(ap, unsigned int);
                else if (len_mod == 5)
                    uval = (unsigned char)va_arg(ap, unsigned int);
                else
                    uval = va_arg(ap, unsigned int);
            }

            const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
            char num_buf[64];
            int nlen = 0;
            if (uval == 0) {
                num_buf[nlen++] = '0';
            } else {
                while (uval > 0 && nlen < 63) {
                    num_buf[nlen++] = digits[uval % (unsigned)base];
                    uval /= (unsigned)base;
                }
            }

            const char *prefix = "";
            if (alt_form && (spec == 'x' || spec == 'p'))
                prefix = "0x";
            else if (alt_form && spec == 'X')
                prefix = "0X";
            else if (alt_form && spec == 'b')
                prefix = "0b";

            int plen = 0;
            while (prefix[plen])
                plen++;

            int zeroes = 0;
            if (precision > nlen)
                zeroes = precision - nlen;
            else if (zero_pad && width > nlen + plen)
                zeroes = width - nlen - plen;

            int total_len = nlen + zeroes + plen;

            if (!left_align && width > total_len)
                buf_pad(&b, ' ', width - total_len);
            for (int p = 0; p < plen; p++)
                buf_putc(&b, prefix[p]);
            buf_pad(&b, '0', zeroes);
            while (nlen > 0)
                buf_putc(&b, num_buf[--nlen]);
            if (left_align && width > total_len)
                buf_pad(&b, ' ', width - total_len);
            continue;
        }

        /* Fallback for unrecognized specifier */
        buf_putc(&b, '%');
        buf_putc(&b, spec);
    }

    return b.count;
}

int snprintf(char *out, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int rc = vsnprintf(out, size, fmt, ap);
    va_end(ap);
    return rc;
}

int sprintf(char *out, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int rc = vsnprintf(out, (size_t)-1, fmt, ap);
    va_end(ap);
    return rc;
}
