/*
 * Project Tsukasa — Kernel Stack Backtrace Implementation
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

#include "include/ksymbols.h"
#include "include/kprintf.h"

int kptr_ok(uint64_t p)
{
    return p >= 0xffff800000000000ULL && (p & 7) == 0;
}

void k_backtrace_custom(unsigned long rbp, unsigned long rip_hint, int max_frames, k_backtrace_emit_fn emit)
{
    if (max_frames <= 0)
        max_frames = 16;
    if (max_frames > 64)
        max_frames = 64;

    if (!rbp) {
        rbp = (unsigned long)__builtin_frame_address(0);
    }
    if (!rip_hint) {
        rip_hint = (unsigned long)__builtin_return_address(0);
    }

    char buf[128];
    unsigned long off = 0;
    const char *name = NULL;
    int frame_idx = 0;

    if (rip_hint) {
        name = ksym_lookup(rip_hint, &off);
        ksprintf(buf, sizeof(buf), "  #%d  0x%016lx  %s+0x%lx",
                 frame_idx++, rip_hint, name ? name : "??", off);
        if (emit)
            emit(buf);
        else
            kprintf("%s\n", buf);
    }

    struct stackframe *f = (struct stackframe *)rbp;
    while (f && frame_idx < max_frames) {
        if (!kptr_ok((unsigned long)f))
            break;
        if (!kptr_ok((unsigned long)f->rbp))
            break;
        if (f->rip == 0)
            break;
        if ((unsigned long)f->rbp <= (unsigned long)f)
            break;

        if (f->rip != rip_hint) {
            name = ksym_lookup(f->rip, &off);
            ksprintf(buf, sizeof(buf), "  #%d  0x%016lx  %s+0x%lx",
                     frame_idx++, f->rip, name ? name : "??", off);
            if (emit)
                emit(buf);
            else
                kprintf("%s\n", buf);
        }

        f = f->rbp;
    }
}

void k_backtrace(unsigned long rbp, unsigned long rip_hint, int max_frames)
{
    k_backtrace_custom(rbp, rip_hint, max_frames, NULL);
}
