/*
 * Project Tsukasa — Kernel Symbol Resolution and Stack Trace Subsystem
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

#ifndef TSUKASA_KSYMBOLS_H
#define TSUKASA_KSYMBOLS_H

#include <stdint.h>
#include <stddef.h>

struct ksym {
    unsigned long addr;
    const char *name;
};

extern const struct ksym ksym_table[];
extern const unsigned long ksym_count;

/*
 * Resolve addr to the nearest preceding symbol.
 * Returns the symbol name and writes byte offset to *off_out, or NULL if unresolvable.
 */
const char *ksym_lookup(unsigned long addr, unsigned long *off_out);

/*
 * Validates whether p points to plausible 64-bit kernel memory (aligned and canonical high half).
 */
int kptr_ok(uint64_t p);

/*
 * Standard x86_64 stack frame layout with frame pointer.
 */
struct stackframe {
    struct stackframe *rbp;
    unsigned long rip;
};

/*
 * Output callback type for custom backtrace emission (e.g. framebuffer panic screen).
 */
typedef void (*k_backtrace_emit_fn)(const char *line);

/*
 * Unwind call stack up to max_frames, resolving symbols.
 * If rbp is 0, defaults to current frame address.
 * If rip_hint is non-zero, it is reported as frame #0.
 */
void k_backtrace(unsigned long rbp, unsigned long rip_hint, int max_frames);
void k_backtrace_custom(unsigned long rbp, unsigned long rip_hint, int max_frames, k_backtrace_emit_fn emit);

#endif
