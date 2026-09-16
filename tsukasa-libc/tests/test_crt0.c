/*
 * Project Tsukasa — CRT0 and Syscall Verification Suite
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

#include <sys/syscall.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include "../../scripts/test/tsk_test.h"

static size_t test_strlen(const char *s)
{
    size_t len = 0;
    while (s && s[len])
        len++;
    return len;
}

static uintptr_t s_entry_rsp;

static int real_main(int argc, char **argv, char **envp) __attribute__((used));

__attribute__((naked)) int main(void)
{
    __asm__ volatile (
        "movq %%rsp, %0\n\t"
        "jmp real_main\n\t"
        : "=m"(s_entry_rsp)
    );
}

static int real_main(int argc, char **argv, char **envp)
{
    (void)envp;
    int passed = 0;
    int total = 4;

    /* Verify 16-byte stack frame alignment on entry to main (rsp % 16 == 8 per AMD64 ABI). */
    if ((s_entry_rsp & 0xF) != 8) {
        TSK_TEST_FAIL("crt0", "stack_alignment", "rsp % 16 != 8");
        return 1;
    }
    TSK_TEST_PASS("crt0", "stack_alignment");
    passed++;

    /* Basic sanity check on argc and argv. */
    if (argc < 1 || !argv || !argv[0] || test_strlen(argv[0]) == 0) {
        TSK_TEST_FAIL("crt0", "argc_argv", "argc < 1 or null argv[0]");
        return 2;
    }
    TSK_TEST_PASS("crt0", "argc_argv");
    passed++;

    const char msg[] = "tsukasa-libc crt0: PASSED\n";
    ssize_t written = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    if (written != (ssize_t)(sizeof(msg) - 1)) {
        TSK_TEST_FAIL("crt0", "write_stdout", "write returned incorrect length");
        return 3;
    }
    TSK_TEST_PASS("crt0", "write_stdout");
    passed++;

    /* Check getpid syscall. */
    pid_t pid = getpid();
    if (pid < 0) {
        TSK_TEST_FAIL("crt0", "getpid", "getpid < 0");
        return 4;
    }
    TSK_TEST_PASS("crt0", "getpid");
    passed++;

    TSK_TEST_DONE("crt0", passed, total);
    return 0;
}
