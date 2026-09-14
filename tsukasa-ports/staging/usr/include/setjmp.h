/*
 * Project Tsukasa — Non-Local Jumps Header (<setjmp.h>)
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

#ifndef _TSUKASA_SETJMP_H
#define _TSUKASA_SETJMP_H

#include <stdint.h>

typedef uint64_t jmp_buf[8];

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

#define _setjmp(env)       setjmp(env)
#define _longjmp(env, val) longjmp((env), (val))

#endif /* _TSUKASA_SETJMP_H */
