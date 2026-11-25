/*
 * Project Tsukasa — POSIX Terminal I/O Interface (<termios.h>)
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

#ifndef _TSUKASA_TERMIOS_H
#define _TSUKASA_TERMIOS_H

#include <stdint.h>

typedef uint32_t tcflag_t;
typedef uint8_t  cc_t;
typedef uint32_t speed_t;

#define NCCS 32

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t     c_line;
    cc_t     c_cc[NCCS];
    speed_t  c_ispeed;
    speed_t  c_ospeed;
};

/* c_cc subscripts */
#define VINTR    0
#define VQUIT    1
#define VERASE   2
#define VKILL    3
#define VEOF     4
#define VTIME    5
#define VMIN     6
#define VSWTC    7
#define VSTART   8
#define VSTOP    9
#define VSUSP    10
#define VEOL     11
#define VREPRINT 12
#define VDISCARD 13
#define VWERASE  14
#define VLNEXT   15
#define VEOL2    16

/* c_iflag bits */
#define IGNBRK   0000001U
#define BRKINT   0000002U
#define IGNPAR   0000004U
#define PARMRK   0000010U
#define INPCK    0000020U
#define ISTRIP   0000040U
#define INLCR    0000100U
#define IGNCR    0000200U
#define ICRNL    0000400U
#define IUCLC    0001000U
#define IXON     0002000U
#define IXANY    0004000U
#define IXOFF    0010000U
#define IMAXBEL  0020000U
#define IUTF8    0040000U

/* c_oflag bits */
#define OPOST    0000001U
#define OLCUC    0000002U
#define ONLCR    0000004U
#define OCRNL    0000010U
#define ONOCR    0000020U
#define ONLRET   0000040U
#define OFILL    0000100U
#define OFDEL    0000200U

/* c_cflag bits */
#define CSIZE    0000060U
#define CS5      0000000U
#define CS6      0000020U
#define CS7      0000040U
#define CS8      0000060U
#define CSTOPB   0000100U
#define CREAD    0000200U
#define PARENB   0000400U
#define PARODD   0001000U
#define HUPCL    0002000U
#define CLOCAL   0004000U

#define B0       0000000U
#define B50      0000001U
#define B75      0000002U
#define B110     0000003U
#define B134     0000004U
#define B150     0000005U
#define B200     0000006U
#define B300     0000007U
#define B600     0000010U
#define B1200    0000011U
#define B1800    0000012U
#define B2400    0000013U
#define B4800    0000014U
#define B9600    0000015U
#define B19200   0000016U
#define B38400   0000017U
#define B57600   0010001U
#define B115200  0010002U

/* c_lflag bits */
#define ISIG     0000001U
#define ICANON   0000002U
#define XCASE    0000004U
#define ECHO     0000010U
#define ECHOE    0000020U
#define ECHOK    0000040U
#define ECHONL   0000100U
#define NOFLSH   0000200U
#define TOSTOP   0000400U
#define ECHOCTL  0001000U
#define IEXTEN   0100000U

/* tcsetattr actions */
#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

/* tcflush selectors */
#define TCIFLUSH  0
#define TCOFLUSH  1
#define TCIOFLUSH 2

/* tcflow actions */
#define TCOOFF 0
#define TCOON  1
#define TCIOFF 2
#define TCION  3

int tcgetattr(int fd, struct termios *termios_p);
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);
int tcsendbreak(int fd, int duration);
int tcdrain(int fd);
int tcflush(int fd, int queue_selector);
int tcflow(int fd, int action);
speed_t cfgetispeed(const struct termios *termios_p);
speed_t cfgetospeed(const struct termios *termios_p);
int cfsetispeed(struct termios *termios_p, speed_t speed);
int cfsetospeed(struct termios *termios_p, speed_t speed);
void cfmakeraw(struct termios *termios_p);

#endif /* _TSUKASA_TERMIOS_H */
