/*
 * Project Tsukasa — usock20: guide-20 ring-3 acceptance binary
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

#include "tsukasa_sdk.h"
#include "../../include/socket_defs.h"

static int str_eq(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void addr_fill(struct tsk_sockaddr_un *ua, const char *path)
{
    int i = 0;
    ua->sun_family = TSK_AF_UNIX;
    while (path[i] && i < TSK_UNIX_PATH_MAX - 1) {
        ua->sun_path[i] = path[i];
        i++;
    }
    ua->sun_path[i] = '\0';
}

/* Bounded read loop: connect() buffers mean data can lag a schedule tick. */
static int read_exact(int fd, char *buf, int want)
{
    int got = 0;
    for (int spin = 0; spin < 20000 && got < want; spin++) {
        long r = read(fd, buf + got, (size_t)(want - got));
        if (r > 0)
            got += (int)r;
        else
            sched_yield();
    }
    return got;
}

static int run_validate(void)
{
    struct tsk_sockaddr_un ua;
    int fd;

    if (socket(2 /* AF_INET */, TSK_SOCK_STREAM, 0) != -97)
        return 21;

    fd = socket(TSK_AF_UNIX, TSK_SOCK_STREAM, 0);
    if (fd < 0)
        return 22;

    addr_fill(&ua, "/tmp/u20-absent.sock");
    if (connect(fd, &ua, (long)sizeof(ua)) != -111)
        return 23;

    ua.sun_family = 9;
    if (bind(fd, &ua, (long)sizeof(ua)) != -97)
        return 24;

    addr_fill(&ua, "/tmp/u20-idle.sock");
    if (bind(fd, &ua, (long)sizeof(ua)) != 0)
        return 25;
    if (listen(fd, 1) != 0)
        return 25;
    if (accept(fd) != -11)
        return 25;
    close(fd);
    return 0;
}

static int run_loopback(void)
{
    struct tsk_sockaddr_un ua;
    static const char ping[] = "PING3";
    static const char pong[] = "PONG3";
    char buf[8];
    int lfd, cfd, afd;

    lfd = socket(TSK_AF_UNIX, TSK_SOCK_STREAM, 0);
    if (lfd < 0)
        return 31;
    addr_fill(&ua, "/tmp/u20-ring3.sock");
    if (bind(lfd, &ua, (long)sizeof(ua)) != 0)
        return 32;
    if (listen(lfd, 1) != 0)
        return 33;

    cfd = socket(TSK_AF_UNIX, TSK_SOCK_STREAM, 0);
    if (cfd < 0 || connect(cfd, &ua, (long)sizeof(ua)) != 0)
        return 34;

    afd = -11;
    for (int spin = 0; spin < 20000 && afd == -11; spin++) {
        afd = accept(lfd);
        if (afd == -11)
            sched_yield();
    }
    if (afd < 0)
        return 35;

    if (write(cfd, ping, 5) != 5 || read_exact(afd, buf, 5) != 5)
        return 36;
    for (int i = 0; i < 5; i++)
        if (buf[i] != ping[i])
            return 36;

    if (write(afd, pong, 5) != 5 || read_exact(cfd, buf, 5) != 5)
        return 37;
    for (int i = 0; i < 5; i++)
        if (buf[i] != pong[i])
            return 37;

    close(afd);
    close(cfd);
    close(lfd);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return 20;
    if (str_eq(argv[1], "validate"))
        return run_validate();
    if (str_eq(argv[1], "loopback"))
        return run_loopback();
    return 20;
}
