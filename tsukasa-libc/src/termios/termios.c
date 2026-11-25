/*
 * Project Tsukasa — POSIX termios Userspace Wrappers
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

#include <termios.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stddef.h>

int tcgetattr(int fd, struct termios *termios_p)
{
    if (!termios_p) {
        errno = EFAULT;
        return -1;
    }
    return ioctl(fd, TCGETS, termios_p);
}

int tcsetattr(int fd, int optional_actions, const struct termios *termios_p)
{
    unsigned long req;
    if (!termios_p) {
        errno = EFAULT;
        return -1;
    }
    switch (optional_actions) {
    case TCSANOW:
        req = TCSETS;
        break;
    case TCSADRAIN:
        req = TCSETSW;
        break;
    case TCSAFLUSH:
        req = TCSETSF;
        break;
    default:
        errno = EINVAL;
        return -1;
    }
    return ioctl(fd, req, (void *)termios_p);
}

int tcsendbreak(int fd, int duration)
{
    (void)duration;
    struct termios t;
    return tcgetattr(fd, &t);
}

int tcdrain(int fd)
{
    struct termios t;
    return tcgetattr(fd, &t);
}

int tcflush(int fd, int queue_selector)
{
    struct termios t;
    if (queue_selector != TCIFLUSH && queue_selector != TCOFLUSH && queue_selector != TCIOFLUSH) {
        errno = EINVAL;
        return -1;
    }
    if (tcgetattr(fd, &t) < 0)
        return -1;
    if (queue_selector == TCIFLUSH || queue_selector == TCIOFLUSH)
        return tcsetattr(fd, TCSAFLUSH, &t);
    return 0;
}

int tcflow(int fd, int action)
{
    struct termios t;
    if (action < TCOOFF || action > TCION) {
        errno = EINVAL;
        return -1;
    }
    return tcgetattr(fd, &t);
}

speed_t cfgetispeed(const struct termios *termios_p)
{
    return termios_p ? termios_p->c_ispeed : B0;
}

speed_t cfgetospeed(const struct termios *termios_p)
{
    return termios_p ? termios_p->c_ospeed : B0;
}

int cfsetispeed(struct termios *termios_p, speed_t speed)
{
    if (!termios_p) {
        errno = EINVAL;
        return -1;
    }
    termios_p->c_ispeed = speed;
    return 0;
}

int cfsetospeed(struct termios *termios_p, speed_t speed)
{
    if (!termios_p) {
        errno = EINVAL;
        return -1;
    }
    termios_p->c_ospeed = speed;
    return 0;
}

void cfmakeraw(struct termios *termios_p)
{
    if (!termios_p)
        return;
    termios_p->c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    termios_p->c_oflag &= ~OPOST;
    termios_p->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    termios_p->c_cflag &= ~(CSIZE | PARENB);
    termios_p->c_cflag |= CS8;
    termios_p->c_cc[VMIN] = 1;
    termios_p->c_cc[VTIME] = 0;
}
