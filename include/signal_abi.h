/*
 * Project Tsukasa — Signal ABI structures and user signal frame definitions
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

#ifndef TSUKASA_SIGNAL_ABI_H
#define TSUKASA_SIGNAL_ABI_H

#include <stddef.h>
#include <stdint.h>

#define TSK_SA_NOCLDSTOP 0x00000001u
#define TSK_SA_NOCLDWAIT 0x00000002u
#define TSK_SA_SIGINFO   0x00000004u
#define TSK_SA_RESTORER  0x04000000u
#define TSK_SA_ONSTACK   0x08000000u
#define TSK_SA_RESTART   0x10000000u
#define TSK_SA_NODEFER   0x40000000u
#define TSK_SA_RESETHAND 0x80000000u

enum {
    TSK_REG_R8 = 0,
    TSK_REG_R9,
    TSK_REG_R10,
    TSK_REG_R11,
    TSK_REG_R12,
    TSK_REG_R13,
    TSK_REG_R14,
    TSK_REG_R15,
    TSK_REG_RDI,
    TSK_REG_RSI,
    TSK_REG_RBP,
    TSK_REG_RBX,
    TSK_REG_RDX,
    TSK_REG_RAX,
    TSK_REG_RCX,
    TSK_REG_RSP,
    TSK_REG_RIP,
    TSK_REG_EFL,
    TSK_REG_CSGSFS,
    TSK_NGREG
};

typedef struct tsk_siginfo {
    int32_t si_signo;
    int32_t si_errno;
    int32_t si_code;
    int32_t si_pid;
    uint32_t si_uid;
    int32_t si_status;
    uint64_t si_addr;
    int64_t si_value;
} tsk_siginfo_t;

typedef struct tsk_mcontext {
    uint64_t gregs[TSK_NGREG];
    uint64_t fpu_flags;
    uint8_t fpregs[512] __attribute__((aligned(16)));
} tsk_mcontext_t;

typedef struct tsk_ucontext {
    uint64_t uc_flags;
    uint64_t uc_link;
    uint64_t uc_sigmask;
    uint64_t uc_pad;
    tsk_mcontext_t uc_mcontext;
} tsk_ucontext_t;

/* Placed at 16-byte aligned sigframe_va; pretcode sits at sigframe_va - 8. */
typedef struct tsk_sigframe {
    tsk_ucontext_t uc;
    tsk_siginfo_t info;
} tsk_sigframe_t;

_Static_assert((offsetof(tsk_ucontext_t, uc_mcontext.fpregs) % 16) == 0,
               "tsk_ucontext_t fpregs must be 16-byte aligned");
_Static_assert((sizeof(tsk_ucontext_t) % 16) == 0,
               "tsk_ucontext_t size must be a multiple of 16 bytes");
_Static_assert(offsetof(tsk_sigframe_t, uc) == 0,
               "tsk_sigframe_t uc must sit at offset 0 for rt_sigreturn");

#endif /* TSUKASA_SIGNAL_ABI_H */
