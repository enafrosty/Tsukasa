/*
 * Project Tsukasa — Helper binary verifying execve argv, envp, auxv, and CLOEXEC
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

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define AT_NULL   0ULL
#define AT_PHDR   3ULL
#define AT_PHENT  4ULL
#define AT_PHNUM  5ULL
#define AT_PAGESZ 6ULL
#define AT_ENTRY  9ULL
#define AT_RANDOM 25ULL
#define AT_EXECFN 31ULL

static int parse_int_simple(const char *s)
{
    int v = 0;
    if (!s || !*s)
        return -1;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return (*s == '\0') ? v : -1;
}

static int verify_auxv(char **envp)
{
    int envc = 0;
    while (envp && envp[envc])
        envc++;

    uint64_t *auxv = (uint64_t *)(uintptr_t)&envp[envc + 1];
    uint64_t pagesz = 0, phent = 0, phnum = 0, entry = 0, rnd = 0, execfn = 0;

    for (size_t i = 0; auxv[i] != AT_NULL; i += 2) {
        uint64_t k = auxv[i];
        uint64_t v = auxv[i + 1];
        if (k == AT_PAGESZ)
            pagesz = v;
        else if (k == AT_PHENT)
            phent = v;
        else if (k == AT_PHNUM)
            phnum = v;
        else if (k == AT_ENTRY)
            entry = v;
        else if (k == AT_RANDOM)
            rnd = v;
        else if (k == AT_EXECFN)
            execfn = v;
    }

    if (pagesz != 4096ULL || phent != 56ULL || phnum == 0 || entry == 0 || rnd == 0 || execfn == 0)
        return -1;

    const uint8_t *rb = (const uint8_t *)(uintptr_t)rnd;
    int any_nz = 0;
    for (int i = 0; i < 16; i++) {
        if (rb[i] != 0)
            any_nz = 1;
    }
    if (!any_nz)
        return -1;

    const char *fn = (const char *)(uintptr_t)execfn;
    if (!fn || fn[0] == '\0')
        return -1;
    return 0;
}

int main(int argc, char **argv, char **envp)
{
    if (verify_auxv(envp) != 0)
        return 10;

    if (argc >= 4 && strcmp(argv[1], "shebang_arg") == 0) {
        if (strcmp(argv[2], "/tmp/test_shebang.sh") != 0)
            return 11;
        if (strcmp(argv[3], "user_arg") != 0)
            return 12;
        const char *sv = getenv("TSK_SHEBANG_ENV");
        if (!sv || strcmp(sv, "shebang_ok") != 0)
            return 13;
        return 55;
    }

    if (argc < 5 || strcmp(argv[1], "--verify-exec") != 0)
        return 14;

    const char *ev = getenv("TSK_TEST_ENV");
    if (!ev || strcmp(ev, "cow_exec_ok") != 0)
        return 15;
    if (getenv("TSK_NOT_PRESENT") != NULL)
        return 16;

    if (setenv("TSK_LOCAL_ENV", "abc123", 1) != 0)
        return 17;
    const char *lv = getenv("TSK_LOCAL_ENV");
    if (!lv || strcmp(lv, "abc123") != 0)
        return 18;
    if (unsetenv("TSK_LOCAL_ENV") != 0 || getenv("TSK_LOCAL_ENV") != NULL)
        return 19;

    int keep_fd = parse_int_simple(argv[2]);
    int clo_fd = parse_int_simple(argv[3]);
    int dup_clo_fd = parse_int_simple(argv[4]);
    if (keep_fd < 0 || clo_fd < 0 || dup_clo_fd < 0)
        return 20;

    char buf[16];
    memset(buf, 0, sizeof(buf));
    if (lseek(keep_fd, 0, SEEK_SET) != 0 || read(keep_fd, buf, 7) != 7 ||
        memcmp(buf, "KEEP_ME", 7) != 0)
        return 21;

    errno = 0;
    if (fcntl(clo_fd, F_GETFD, 0) != -1 || errno != EBADF)
        return 22;

    errno = 0;
    if (fcntl(dup_clo_fd, F_GETFD, 0) != -1 || errno != EBADF)
        return 23;

    close(keep_fd);
    return 33;
}
