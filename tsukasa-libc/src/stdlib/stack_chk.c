/*
 * Project Tsukasa — Userland Stack Smashing Protection Runtime
 *
 * Copyright (C) 2025-2026 frosty (@enafrosty) and Project Tsukasa contributors.
 *
 * Project Tsukasa was created and is maintained by frosty (@enafrosty).
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at you
 * option) any later version. See the top-level LICENSE file.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>

/*
 * Initial compile-time constant canary for userspace binaries.
 * Accidental overrun protection only; CSPRNG randomisation planned for future entropy integration.
 */
uintptr_t __stack_chk_guard = 0x00000aff595e2f0aUL;

__attribute__((noreturn, no_stack_protector))
void __stack_chk_fail(void)
{
    const char msg[] = "[stack] canary corrupted - stack smashing detected\n";
    write(STDERR_FILENO, msg, sizeof(msg) - 1);
    abort();
    for (;;) {
        _exit(134);
    }
}
