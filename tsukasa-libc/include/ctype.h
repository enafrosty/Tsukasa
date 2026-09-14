/*
 * Project Tsukasa — Standard Character Classification and Mapping Header
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

#ifndef _TSUKASA_CTYPE_H
#define _TSUKASA_CTYPE_H

static inline int isspace(int c)
{
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v');
}

static inline int isdigit(int c)
{
    return (c >= '0' && c <= '9');
}

static inline int isalpha(int c)
{
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
}

static inline int isalnum(int c)
{
    return (isalpha(c) || isdigit(c));
}

static inline int islower(int c)
{
    return (c >= 'a' && c <= 'z');
}

static inline int isupper(int c)
{
    return (c >= 'A' && c <= 'Z');
}

static inline int isprint(int c)
{
    return (c >= 0x20 && c <= 0x7E);
}

static inline int isxdigit(int c)
{
    return (isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
}

static inline int tolower(int c)
{
    if (c >= 'A' && c <= 'Z')
        return c + ('a' - 'A');
    return c;
}

static inline int toupper(int c)
{
    if (c >= 'a' && c <= 'z')
        return c - ('a' - 'A');
    return c;
}

static inline int iscntrl(int c)
{
    return ((c >= 0 && c < 0x20) || c == 0x7F);
}

static inline int isgraph(int c)
{
    return (c > 0x20 && c <= 0x7E);
}

static inline int ispunct(int c)
{
    return (isgraph(c) && !isalnum(c));
}

#endif /* _TSUKASA_CTYPE_H */
