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

static size_t test_strlen(const char *s)
{
    size_t len = 0;
    while (s && s[len])
        len++;
    return len;
}

int main(int argc, char **argv, char **envp)
{
    (void)envp;

    /* Verify 16-byte stack frame alignment on entry to main (rsp % 16 == 8 per AMD64 ABI). */
    uintptr_t rsp_val;
    __asm__ volatile ("mov %%rsp, %0" : "=r"(rsp_val));
    if ((rsp_val & 0xF) != 8)
        return 1;

    /* Basic sanity check on argc and argv. */
    if (argc < 1 || !argv || !argv[0] || test_strlen(argv[0]) == 0)
        return 2;

    const char msg[] = "tsukasa-libc crt0: PASSED\n";
    ssize_t written = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    if (written != (ssize_t)(sizeof(msg) - 1))
        return 3;

    /* Check getpid syscall. */
    pid_t pid = getpid();
    if (pid < 0)
        return 4;

    return 0;
}
