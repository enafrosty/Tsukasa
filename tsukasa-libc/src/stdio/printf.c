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
#include <limits.h>

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

static int big_digit_at(const uint32_t *big, int a, int z, int r, int pos)
{
    static const uint32_t pow10[9] = {
        1U, 10U, 100U, 1000U, 10000U, 100000U, 1000000U, 10000000U, 100000000U
    };
    int q = (pos >= 0) ? (pos / 9) : -((-pos + 8) / 9);
    int sub = pos - q * 9;
    int limb = r - q;
    if (limb < a || limb >= z)
        return 0;
    return (int)((big[limb] / pow10[sub]) % 10U);
}

static int big_has_nonzero_below(const uint32_t *big, int a, int z, int r, int pos)
{
    static const uint32_t pow10[9] = {
        1U, 10U, 100U, 1000U, 10000U, 100000U, 1000000U, 10000000U, 100000000U
    };
    int q = (pos >= 0) ? (pos / 9) : -((-pos + 8) / 9);
    int sub = pos - q * 9;
    int limb = r - q;
    if (limb >= z)
        return 0;
    if (limb >= a && (big[limb] % pow10[sub]) != 0U)
        return 1;
    int start_i = (limb + 1 > a) ? (limb + 1) : a;
    for (int i = start_i; i < z; i++) {
        if (big[i] != 0U)
            return 1;
    }
    return 0;
}

static int format_exp_str(char exp_char, int exp_val, char *out)
{
    int len = 0;
    out[len++] = exp_char;
    if (exp_val < 0) {
        out[len++] = '-';
        exp_val = -exp_val;
    } else {
        out[len++] = '+';
    }
    char rev[8];
    int rlen = 0;
    do {
        rev[rlen++] = (char)('0' + (exp_val % 10));
        exp_val /= 10;
    } while (exp_val > 0 && rlen < (int)sizeof(rev));
    while (rlen < 2)
        rev[rlen++] = '0';
    while (rlen > 0)
        out[len++] = rev[--rlen];
    return len;
}

static void fmt_float(str_buf_t *b, double val, char spec,
                      int left_align, int show_sign, int space_sign,
                      int zero_pad, int alt_form, int width, int precision)
{
    uint64_t bits;
    memcpy(&bits, &val, sizeof(bits));

    int neg = (int)(bits >> 63);
    uint64_t abs_bits = bits & 0x7FFFFFFFFFFFFFFFULL;
    uint32_t e_bits = (uint32_t)(abs_bits >> 52);
    uint64_t mant = abs_bits & 0x000FFFFFFFFFFFFFULL;

    int is_upper = (spec == 'F' || spec == 'E' || spec == 'G');
    int g_mode = (spec == 'g' || spec == 'G');
    int e_mode = (spec == 'e' || spec == 'E');

    char sign_char = 0;
    if (neg)
        sign_char = '-';
    else if (show_sign)
        sign_char = '+';
    else if (space_sign)
        sign_char = ' ';

    if (e_bits == 0x7FFU) {
        const char *tag = (mant != 0ULL)
                              ? (is_upper ? "NAN" : "nan")
                              : (is_upper ? "INF" : "inf");
        int total_len = 3 + (sign_char ? 1 : 0);
        if (!left_align && width > total_len)
            buf_pad(b, ' ', width - total_len);
        if (sign_char)
            buf_putc(b, sign_char);
        buf_puts(b, tag, 3);
        if (left_align && width > total_len)
            buf_pad(b, ' ', width - total_len);
        return;
    }

    if (precision < 0)
        precision = 6;
    if (g_mode && precision == 0)
        precision = 1;

    if (abs_bits == 0ULL) {
        int use_exp = e_mode;
        int frac_prec = g_mode ? (alt_form ? (precision - 1) : 0) : precision;
        int emit_dot = (frac_prec > 0 || alt_form);
        char exp_buf[8];
        int exp_len = use_exp ? format_exp_str(is_upper ? 'E' : 'e', 0, exp_buf) : 0;
        int total_len = (sign_char ? 1 : 0) + 1 + (emit_dot ? 1 : 0) + frac_prec + exp_len;

        if (!left_align && !zero_pad && width > total_len)
            buf_pad(b, ' ', width - total_len);
        if (sign_char)
            buf_putc(b, sign_char);
        if (!left_align && zero_pad && width > total_len)
            buf_pad(b, '0', width - total_len);
        buf_putc(b, '0');
        if (emit_dot)
            buf_putc(b, '.');
        buf_pad(b, '0', frac_prec);
        for (int k = 0; k < exp_len; k++)
            buf_putc(b, exp_buf[k]);
        if (left_align && width > total_len)
            buf_pad(b, ' ', width - total_len);
        return;
    }

    int e2;
    if (e_bits == 0U) {
        e2 = -1074;
    } else {
        mant |= (1ULL << 52);
        e2 = (int)e_bits - 1075;
    }

    /* Represent exact value mant * 2^e2 in base-10^9 limbs so all IEEE-754 doubles fit. */
    uint32_t big[132];
    int a, z, r;
    if (e2 >= 0) {
        r = 131;
        a = 130;
        z = 132;
        big[130] = (uint32_t)(mant / 1000000000ULL);
        big[131] = (uint32_t)(mant % 1000000000ULL);
        int rem_e = e2;
        while (rem_e > 0) {
            int s = (rem_e > 29) ? 29 : rem_e;
            uint64_t carry = 0;
            for (int i = z - 1; i >= a; i--) {
                uint64_t cur = ((uint64_t)big[i] << (unsigned)s) + carry;
                big[i] = (uint32_t)(cur % 1000000000ULL);
                carry = cur / 1000000000ULL;
            }
            while (carry > 0) {
                big[--a] = (uint32_t)(carry % 1000000000ULL);
                carry /= 1000000000ULL;
            }
            rem_e -= s;
        }
    } else {
        r = 1;
        a = 0;
        z = 2;
        big[0] = (uint32_t)(mant / 1000000000ULL);
        big[1] = (uint32_t)(mant % 1000000000ULL);
        int rem_e = -e2;
        while (rem_e > 0) {
            int s = (rem_e > 9) ? 9 : rem_e;
            uint64_t rem = 0;
            uint64_t mask = (1ULL << (unsigned)s) - 1ULL;
            for (int i = a; i < z; i++) {
                uint64_t cur = (uint64_t)big[i] + rem * 1000000000ULL;
                big[i] = (uint32_t)(cur >> (unsigned)s);
                rem = cur & mask;
            }
            if (rem != 0ULL) {
                uint64_t cur = rem * 1000000000ULL;
                big[z++] = (uint32_t)(cur >> (unsigned)s);
            }
            while (a < z && big[a] == 0U)
                a++;
            rem_e -= s;
        }
    }

    while (a < z && big[a] == 0U)
        a++;
    while (z > a + 1 && big[z - 1] == 0U)
        z--;

    int d_top = 0;
    uint32_t top_val = big[a];
    while (top_val > 0U) {
        d_top++;
        top_val /= 10U;
    }
    int exp10 = (r - a) * 9 + (d_top - 1);
    int bottom_pos = (r - (z - 1)) * 9;

    int round_pos;
    if (e_mode)
        round_pos = exp10 - precision;
    else if (g_mode)
        round_pos = exp10 - (precision - 1);
    else
        round_pos = -precision;

    /* Fixed-point conversion with |x| < 0.05 * 10^round_pos rounds directly to signed zero. */
    if (!e_mode && !g_mode && round_pos > exp10 + 1) {
        int emit_dot = (precision > 0 || alt_form);
        int total_len = (sign_char ? 1 : 0) + 1 + (emit_dot ? 1 : 0) + precision;
        if (!left_align && !zero_pad && width > total_len)
            buf_pad(b, ' ', width - total_len);
        if (sign_char)
            buf_putc(b, sign_char);
        if (!left_align && zero_pad && width > total_len)
            buf_pad(b, '0', width - total_len);
        buf_putc(b, '0');
        if (emit_dot)
            buf_putc(b, '.');
        buf_pad(b, '0', precision);
        if (left_align && width > total_len)
            buf_pad(b, ' ', width - total_len);
        return;
    }

    int start_pos = (exp10 >= round_pos) ? exp10 : round_pos;
    int end_pos = (round_pos >= bottom_pos) ? round_pos : bottom_pos;
    int ndigits = start_pos - end_pos + 1;

    char digits[1120];
    digits[0] = '0';
    for (int k = 0; k < ndigits; k++)
        digits[1 + k] = (char)('0' + big_digit_at(big, a, z, r, start_pos - k));

    if (round_pos > bottom_pos) {
        int d_half = big_digit_at(big, a, z, r, round_pos - 1);
        int round_up = 0;
        if (d_half > 5) {
            round_up = 1;
        } else if (d_half == 5) {
            if (big_has_nonzero_below(big, a, z, r, round_pos - 1))
                round_up = 1;
            else if (((digits[ndigits] - '0') & 1) != 0)
                round_up = 1;
        }
        if (round_up) {
            int p_idx = ndigits;
            while (p_idx >= 0) {
                if (digits[p_idx] < '9') {
                    digits[p_idx]++;
                    break;
                }
                digits[p_idx] = '0';
                p_idx--;
            }
        }
    }

    int d_off = (digits[0] == '1') ? 0 : 1;
    const char *d = digits + d_off;
    int dlen = ndigits + (digits[0] == '1' ? 1 : 0);
    int lead_pos = start_pos + (digits[0] == '1' ? 1 : 0);

    int use_exp = e_mode;
    int frac_prec = precision;
    if (g_mode) {
        int X = lead_pos;
        if (X < -4 || X >= precision) {
            use_exp = 1;
            frac_prec = precision - 1;
            if (!alt_form) {
                while (frac_prec > 0) {
                    char ch = (frac_prec < dlen) ? d[frac_prec] : '0';
                    if (ch != '0')
                        break;
                    frac_prec--;
                }
            }
        } else {
            use_exp = 0;
            if (X >= 0) {
                frac_prec = precision - 1 - X;
                if (!alt_form) {
                    while (frac_prec > 0) {
                        int idx = X + frac_prec;
                        char ch = (idx < dlen) ? d[idx] : '0';
                        if (ch != '0')
                            break;
                        frac_prec--;
                    }
                }
            } else {
                int sig = precision;
                if (!alt_form) {
                    while (sig > 1) {
                        char ch = (sig - 1 < dlen) ? d[sig - 1] : '0';
                        if (ch != '0')
                            break;
                        sig--;
                    }
                }
                frac_prec = (-X - 1) + sig;
            }
        }
    }

    int emit_dot = (frac_prec > 0 || alt_form);
    char exp_buf[8];
    int exp_len = use_exp ? format_exp_str(is_upper ? 'E' : 'e', lead_pos, exp_buf) : 0;

    int int_len = (!use_exp && lead_pos >= 0) ? (lead_pos + 1) : 1;
    int total_len = (sign_char ? 1 : 0) + int_len + (emit_dot ? 1 : 0) + frac_prec + exp_len;

    if (!left_align && !zero_pad && width > total_len)
        buf_pad(b, ' ', width - total_len);
    if (sign_char)
        buf_putc(b, sign_char);
    if (!left_align && zero_pad && width > total_len)
        buf_pad(b, '0', width - total_len);

    if (use_exp) {
        buf_putc(b, d[0]);
        if (emit_dot)
            buf_putc(b, '.');
        for (int k = 0; k < frac_prec; k++) {
            int idx = 1 + k;
            buf_putc(b, (idx < dlen) ? d[idx] : '0');
        }
        for (int k = 0; k < exp_len; k++)
            buf_putc(b, exp_buf[k]);
    } else if (lead_pos >= 0) {
        for (int k = 0; k < int_len; k++)
            buf_putc(b, (k < dlen) ? d[k] : '0');
        if (emit_dot)
            buf_putc(b, '.');
        for (int k = 0; k < frac_prec; k++) {
            int idx = int_len + k;
            buf_putc(b, (idx < dlen) ? d[idx] : '0');
        }
    } else {
        buf_putc(b, '0');
        if (emit_dot)
            buf_putc(b, '.');
        int lead_zeros = -lead_pos - 1;
        if (lead_zeros > frac_prec)
            lead_zeros = frac_prec;
        buf_pad(b, '0', lead_zeros);
        int rem_frac = frac_prec - lead_zeros;
        for (int k = 0; k < rem_frac; k++)
            buf_putc(b, (k < dlen) ? d[k] : '0');
    }

    if (left_align && width > total_len)
        buf_pad(b, ' ', width - total_len);
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
            long w = (long)va_arg(ap, int);
            if (w < 0) {
                left_align = 1;
                zero_pad = 0;
                w = -w;
            }
            if (w > INT_MAX)
                w = INT_MAX;
            width = (int)w;
            i++;
        } else {
            long w = 0;
            while (fmt[i] >= '0' && fmt[i] <= '9') {
                if (w < INT_MAX) {
                    w = w * 10 + (fmt[i] - '0');
                    if (w > INT_MAX)
                        w = INT_MAX;
                }
                i++;
            }
            width = (int)w;
        }

        /* Precision */
        int precision = -1;
        if (fmt[i] == '.') {
            i++;
            if (fmt[i] == '*') {
                int p = va_arg(ap, int);
                precision = (p < 0) ? -1 : p;
                i++;
            } else {
                long p = 0;
                while (fmt[i] >= '0' && fmt[i] <= '9') {
                    if (p < INT_MAX) {
                        p = p * 10 + (fmt[i] - '0');
                        if (p > INT_MAX)
                            p = INT_MAX;
                    }
                    i++;
                }
                precision = (int)p;
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

        if (!fmt[i])
            break;

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
            unsigned long long uval = is_neg
                                          ? (0ULL - (unsigned long long)val)
                                          : (unsigned long long)val;

            char num_buf[32];
            int nlen = 0;
            if (uval == 0) {
                if (precision != 0)
                    num_buf[nlen++] = '0';
            } else {
                while (uval > 0 && nlen < 31) {
                    num_buf[nlen++] = (char)('0' + (uval % 10ULL));
                    uval /= 10ULL;
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
            if (precision >= 0) {
                if (precision > num_digits)
                    zeroes = precision - num_digits;
            } else if (zero_pad && width > num_digits + (sign_char ? 1 : 0)) {
                zeroes = width - num_digits - (sign_char ? 1 : 0);
            }

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

        if (spec == 'u' || spec == 'o' || spec == 'x' || spec == 'X' ||
            spec == 'b' || spec == 'p') {
            unsigned long long uval;
            int base = 10;
            int upper = (spec == 'X');

            if (spec == 'p') {
                uval = (uintptr_t)va_arg(ap, void *);
                base = 16;
                alt_form = 1;
            } else {
                if (spec == 'o')
                    base = 8;
                else if (spec == 'b')
                    base = 2;
                else if (spec == 'x' || spec == 'X')
                    base = 16;
                else
                    base = 10;

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

            int nonzero = (uval != 0ULL);
            const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
            char num_buf[64];
            int nlen = 0;
            if (uval == 0ULL) {
                if (precision != 0 || (spec == 'o' && alt_form))
                    num_buf[nlen++] = '0';
            } else {
                while (uval > 0ULL && nlen < 63) {
                    num_buf[nlen++] = digits[uval % (unsigned)base];
                    uval /= (unsigned)base;
                }
            }

            const char *prefix = "";
            if (spec == 'p') {
                prefix = "0x";
            } else if (alt_form && nonzero) {
                if (spec == 'x')
                    prefix = "0x";
                else if (spec == 'X')
                    prefix = "0X";
                else if (spec == 'b')
                    prefix = "0b";
                else if (spec == 'o' && precision <= nlen)
                    prefix = "0";
            }

            int plen = 0;
            while (prefix[plen])
                plen++;

            int zeroes = 0;
            if (precision >= 0) {
                if (precision > nlen)
                    zeroes = precision - nlen;
            } else if (zero_pad && width > nlen + plen) {
                zeroes = width - nlen - plen;
            }

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

        if (spec == 'f' || spec == 'F' || spec == 'e' || spec == 'E' ||
            spec == 'g' || spec == 'G') {
            double fval = va_arg(ap, double);
            fmt_float(&b, fval, spec, left_align, show_sign, space_sign,
                      zero_pad, alt_form, width, precision);
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
