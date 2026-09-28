/*
 * Project Tsukasa — Kernel Stack Smashing Protection Runtime
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
#include <stddef.h>
#include "include/kprintf.h"
#include "include/ksymbols.h"

/*
 * Compile-time constant canary value protects against accidental buffer overruns.
 * A fixed constant is guessable and does not protect against deliberate exploit payloads.
 * True randomisation requires a cryptographic entropy source once a CSPRNG is available.
 */
unsigned long __stack_chk_guard = 0x00000aff595e2f0aUL;

void kernel_panic(void *regs, const char *msg);

__attribute__((noreturn, no_stack_protector))
void __stack_chk_fail(void)
{
    kprintf("[stack] canary corrupted - stack smashing detected\n");
    k_backtrace(0, 0, 8);
    kernel_panic(NULL, "stack smashing detected");
    for (;;) {
        __asm__ volatile("hlt");
    }
}
