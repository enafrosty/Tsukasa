/*
 * Project Tsukasa — Kernel termios line discipline implementation
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

#include "termios.h"

#include "../drv/pit.h"
#include "../fs/vfs.h"
#include "../include/errno.h"
#include "../proc/process.h"

static inline uint64_t ldisc_irq_save_disable(void)
{
    uint64_t flags = 0;
#ifdef __x86_64__
    __asm__ volatile ("pushfq\n\tpopq %0\n\tcli" : "=r"(flags) : : "memory");
#endif
    return flags;
}

static inline void ldisc_irq_restore(uint64_t flags)
{
#ifdef __x86_64__
    if (flags & (1ULL << 9))
        __asm__ volatile ("sti" : : : "memory");
    else
        __asm__ volatile ("cli" : : : "memory");
#else
    (void)flags;
#endif
}

static void rx_push_locked(tty_ldisc_t *ld, char ch)
{
    if (ld->rx_count >= TTY_LDISC_BUF_SIZE)
        return;
    ld->rx_buf[ld->rx_tail] = ch;
    ld->rx_tail = (ld->rx_tail + 1U) % TTY_LDISC_BUF_SIZE;
    ld->rx_count++;
}

static char rx_pop_locked(tty_ldisc_t *ld)
{
    char ch = ld->rx_buf[ld->rx_head];
    ld->rx_head = (ld->rx_head + 1U) % TTY_LDISC_BUF_SIZE;
    ld->rx_count--;
    return ch;
}

static void flush_line_to_rx_locked(tty_ldisc_t *ld)
{
    for (size_t i = 0; i < ld->line_len; i++)
        rx_push_locked(ld, ld->line_buf[i]);
    ld->line_len = 0;
}

void tty_ldisc_init(tty_ldisc_t *ld)
{
    if (!ld)
        return;
    ld->lock = SPINLOCK_INIT;
    for (size_t i = 0; i < sizeof(ld->termios); i++)
        ((uint8_t *)&ld->termios)[i] = 0;

    ld->termios.c_iflag = VFS_ICRNL | VFS_IXON;
    ld->termios.c_oflag = VFS_OPOST | VFS_ONLCR;
    ld->termios.c_cflag = VFS_CS8 | VFS_CREAD | VFS_CLOCAL | VFS_B38400;
    ld->termios.c_lflag = VFS_ISIG | VFS_ICANON | VFS_ECHO | VFS_ECHOE |
                          VFS_ECHOK | VFS_ECHOCTL | VFS_IEXTEN;
    ld->termios.c_ispeed = VFS_B38400;
    ld->termios.c_ospeed = VFS_B38400;

    ld->termios.c_cc[VFS_VINTR] = 3;
    ld->termios.c_cc[VFS_VQUIT] = 28;
    ld->termios.c_cc[VFS_VERASE] = 127;
    ld->termios.c_cc[VFS_VKILL] = 21;
    ld->termios.c_cc[VFS_VEOF] = 4;
    ld->termios.c_cc[VFS_VTIME] = 0;
    ld->termios.c_cc[VFS_VMIN] = 1;
    ld->termios.c_cc[VFS_VSTART] = 17;
    ld->termios.c_cc[VFS_VSTOP] = 19;
    ld->termios.c_cc[VFS_VSUSP] = 26;
    ld->termios.c_cc[VFS_VEOL] = 0;

    ld->line_len = 0;
    ld->rx_head = 0;
    ld->rx_tail = 0;
    ld->rx_count = 0;
    ld->eof_pending = 0;
}

void tty_ldisc_flush_input(tty_ldisc_t *ld)
{
    if (!ld)
        return;
    unsigned long flags = spin_lock_irqsave(&ld->lock);
    ld->line_len = 0;
    ld->rx_head = 0;
    ld->rx_tail = 0;
    ld->rx_count = 0;
    ld->eof_pending = 0;
    spin_unlock_irqrestore(&ld->lock, flags);
}

static size_t format_echo_char(const struct tsk_termios *t, char ch, char *out, size_t cap)
{
    size_t n = 0;
    uint8_t uc = (uint8_t)ch;

    if (ch == '\n') {
        if ((t->c_oflag & VFS_OPOST) && (t->c_oflag & VFS_ONLCR)) {
            if (cap >= 2) {
                out[0] = '\r';
                out[1] = '\n';
                return 2;
            }
        }
        if (cap >= 1)
            out[n++] = '\n';
        return n;
    }

    if (uc < 32 && ch != '\t' && ch != '\r' && (t->c_lflag & VFS_ECHOCTL)) {
        if (cap >= 2) {
            out[0] = '^';
            out[1] = (char)(uc + '@');
            return 2;
        }
    }

    if (cap >= 1)
        out[n++] = ch;
    return n;
}

static void emit_echo_append(char *echo_buf, size_t *echo_len, size_t echo_cap,
                             const char *src, size_t slen)
{
    for (size_t i = 0; i < slen && *echo_len < echo_cap; i++)
        echo_buf[(*echo_len)++] = src[i];
}

void tty_ldisc_input_bytes(tty_ldisc_t *ld, const char *data, size_t len,
                           tty_echo_fn_t echo_fn, tty_sig_fn_t sig_fn,
                           void *ctx, wait_queue_head_t *waitq)
{
    char echo_buf[256];
    size_t echo_len = 0;
    int sig_list[16];
    int sig_cnt = 0;
    int wake_readers = 0;

    if (!ld || !data || len == 0)
        return;

    unsigned long flags = spin_lock_irqsave(&ld->lock);
    for (size_t i = 0; i < len; i++) {
        uint8_t uc = (uint8_t)data[i];
        char c = (char)uc;
        vfs_tcflag_t iflag = ld->termios.c_iflag;
        vfs_tcflag_t lflag = ld->termios.c_lflag;

        if (c == '\r') {
            if (iflag & VFS_IGNCR)
                continue;
            if (iflag & VFS_ICRNL)
                c = '\n';
        } else if (c == '\n' && (iflag & VFS_INLCR)) {
            c = '\r';
        }

        if (lflag & VFS_ISIG) {
            int sig = 0;
            uint8_t vintr = ld->termios.c_cc[VFS_VINTR];
            uint8_t vquit = ld->termios.c_cc[VFS_VQUIT];
            uint8_t vsusp = ld->termios.c_cc[VFS_VSUSP];

            if (vintr != 0 && (uint8_t)c == vintr)
                sig = PROCESS_SIGINT;
            else if (vquit != 0 && (uint8_t)c == vquit)
                sig = PROCESS_SIGQUIT;
            else if (vsusp != 0 && (uint8_t)c == vsusp)
                sig = PROCESS_SIGTSTP;

            if (sig != 0) {
                if (!(lflag & VFS_NOFLSH)) {
                    ld->line_len = 0;
                    ld->rx_head = 0;
                    ld->rx_tail = 0;
                    ld->rx_count = 0;
                    ld->eof_pending = 0;
                }
                if ((lflag & VFS_ECHO) && (lflag & VFS_ECHOCTL)) {
                    char seq[4];
                    size_t n = format_echo_char(&ld->termios, c, seq, sizeof(seq));
                    emit_echo_append(echo_buf, &echo_len, sizeof(echo_buf), seq, n);
                }
                if (sig_cnt < 16)
                    sig_list[sig_cnt++] = sig;
                wake_readers = 1;
                continue;
            }
        }

        if (lflag & VFS_ICANON) {
            uint8_t verase = ld->termios.c_cc[VFS_VERASE];
            uint8_t vkill = ld->termios.c_cc[VFS_VKILL];
            uint8_t veof = ld->termios.c_cc[VFS_VEOF];
            uint8_t veol = ld->termios.c_cc[VFS_VEOL];

            if (verase != 0 && ((uint8_t)c == verase || (verase == 127 && c == '\b'))) {
                if (ld->line_len > 0) {
                    ld->line_len--;
                    if ((lflag & VFS_ECHO) && (lflag & VFS_ECHOE))
                        emit_echo_append(echo_buf, &echo_len, sizeof(echo_buf), "\b \b", 3);
                }
                continue;
            }

            if (vkill != 0 && (uint8_t)c == vkill) {
                if (ld->line_len > 0) {
                    if ((lflag & VFS_ECHO) && (lflag & VFS_ECHOE)) {
                        while (ld->line_len > 0) {
                            ld->line_len--;
                            emit_echo_append(echo_buf, &echo_len, sizeof(echo_buf), "\b \b", 3);
                        }
                    } else {
                        ld->line_len = 0;
                        if ((lflag & VFS_ECHO) && (lflag & VFS_ECHOK)) {
                            char seq[2];
                            size_t n = format_echo_char(&ld->termios, '\n', seq, sizeof(seq));
                            emit_echo_append(echo_buf, &echo_len, sizeof(echo_buf), seq, n);
                        }
                    }
                }
                continue;
            }

            if (veof != 0 && (uint8_t)c == veof) {
                if (ld->line_len > 0)
                    flush_line_to_rx_locked(ld);
                else
                    ld->eof_pending++;
                wake_readers = 1;
                continue;
            }

            if (c == '\n' || (veol != 0 && (uint8_t)c == veol)) {
                if ((lflag & VFS_ECHO) || (lflag & VFS_ECHONL)) {
                    char seq[4];
                    size_t n = format_echo_char(&ld->termios, c, seq, sizeof(seq));
                    emit_echo_append(echo_buf, &echo_len, sizeof(echo_buf), seq, n);
                }
                if (ld->line_len < TTY_LDISC_BUF_SIZE)
                    ld->line_buf[ld->line_len++] = c;
                flush_line_to_rx_locked(ld);
                wake_readers = 1;
                continue;
            }

            if (ld->line_len < TTY_LDISC_BUF_SIZE) {
                ld->line_buf[ld->line_len++] = c;
                if (lflag & VFS_ECHO) {
                    char seq[4];
                    size_t n = format_echo_char(&ld->termios, c, seq, sizeof(seq));
                    emit_echo_append(echo_buf, &echo_len, sizeof(echo_buf), seq, n);
                }
            }
        } else {
            rx_push_locked(ld, c);
            wake_readers = 1;
            if (lflag & VFS_ECHO) {
                char seq[4];
                size_t n = format_echo_char(&ld->termios, c, seq, sizeof(seq));
                emit_echo_append(echo_buf, &echo_len, sizeof(echo_buf), seq, n);
            }
        }
    }
    spin_unlock_irqrestore(&ld->lock, flags);

    if (echo_len > 0 && echo_fn)
        echo_fn(ctx, echo_buf, echo_len);

    if (sig_fn) {
        for (int i = 0; i < sig_cnt; i++)
            sig_fn(ctx, sig_list[i]);
    }

    if (wake_readers && waitq)
        wait_queue_wake_all(waitq);
}

size_t tty_ldisc_process_output(tty_ldisc_t *ld, const char *in, size_t len,
                                char *out, size_t out_cap)
{
    if (!ld || !in || !out || out_cap == 0)
        return 0;

    unsigned long flags = spin_lock_irqsave(&ld->lock);
    vfs_tcflag_t oflag = ld->termios.c_oflag;
    spin_unlock_irqrestore(&ld->lock, flags);

    size_t w = 0;
    for (size_t i = 0; i < len && w < out_cap; i++) {
        char ch = in[i];
        if (oflag & VFS_OPOST) {
            if (ch == '\n' && (oflag & VFS_ONLCR)) {
                if (w + 2 > out_cap)
                    break;
                out[w++] = '\r';
                out[w++] = '\n';
                continue;
            }
            if (ch == '\r' && (oflag & VFS_OCRNL))
                ch = '\n';
            else if (ch == '\r' && (oflag & VFS_ONOCR))
                continue;
        }
        out[w++] = ch;
    }
    return w;
}

int tty_ldisc_can_read(tty_ldisc_t *ld)
{
    if (!ld)
        return 0;
    unsigned long flags = spin_lock_irqsave(&ld->lock);
    int ready = (ld->rx_count > 0 || ld->eof_pending > 0);
    spin_unlock_irqrestore(&ld->lock, flags);
    return ready;
}

int tty_ldisc_fionread(tty_ldisc_t *ld)
{
    if (!ld)
        return 0;
    unsigned long flags = spin_lock_irqsave(&ld->lock);
    int n = (int)ld->rx_count;
    spin_unlock_irqrestore(&ld->lock, flags);
    return n;
}

long tty_ldisc_read(tty_ldisc_t *ld, void *buf, size_t count, int flags,
                    int (*is_hup)(void *), void *ctx, wait_queue_head_t *waitq)
{
    uint8_t *dst = (uint8_t *)buf;
    uint64_t deadline = 0;
    int deadline_armed = 0;
    size_t last_rx_seen = 0;

    if (!ld || !dst)
        return -EINVAL;
    if (count == 0)
        return 0;

    for (;;) {
        process_t *cur = process_current();
        if (cur && (cur->signal_pending & ~cur->signal_mask))
            return -EINTR;

        uint64_t irqf = ldisc_irq_save_disable();
        spin_lock(&ld->lock);

        int icanon = (ld->termios.c_lflag & VFS_ICANON) ? 1 : 0;
        if (icanon) {
            if (ld->rx_count > 0) {
                size_t got = 0;
                uint8_t veol = ld->termios.c_cc[VFS_VEOL];
                while (got < count && ld->rx_count > 0) {
                    char ch = rx_pop_locked(ld);
                    dst[got++] = (uint8_t)ch;
                    if (ch == '\n' || (veol != 0 && (uint8_t)ch == veol))
                        break;
                }
                spin_unlock(&ld->lock);
                ldisc_irq_restore(irqf);
                return (long)got;
            }
            if (ld->eof_pending > 0) {
                ld->eof_pending--;
                spin_unlock(&ld->lock);
                ldisc_irq_restore(irqf);
                return 0;
            }
            if (is_hup && is_hup(ctx)) {
                spin_unlock(&ld->lock);
                ldisc_irq_restore(irqf);
                return 0;
            }
        } else {
            uint8_t vmin = ld->termios.c_cc[VFS_VMIN];
            uint8_t vtime = ld->termios.c_cc[VFS_VTIME];
            size_t target = ((size_t)vmin < count) ? (size_t)vmin : count;

            if (ld->rx_count > 0) {
                int enough = 0;
                if (vmin == 0)
                    enough = 1;
                else if (ld->rx_count >= target)
                    enough = 1;
                else if (flags & VFS_O_NONBLOCK)
                    enough = 1;
                else if (vtime > 0 && deadline_armed && pit_ticks_ms() >= deadline)
                    enough = 1;

                if (enough) {
                    size_t got = 0;
                    while (got < count && ld->rx_count > 0)
                        dst[got++] = (uint8_t)rx_pop_locked(ld);
                    spin_unlock(&ld->lock);
                    ldisc_irq_restore(irqf);
                    return (long)got;
                }

                /* Inter-byte timer starts once first byte arrives when VMIN > 0 and VTIME > 0. */
                if (vtime > 0 && (!deadline_armed || ld->rx_count != last_rx_seen)) {
                    deadline = pit_ticks_ms() + (uint64_t)vtime * 100ULL;
                    deadline_armed = 1;
                    last_rx_seen = ld->rx_count;
                }
            } else {
                if (is_hup && is_hup(ctx)) {
                    spin_unlock(&ld->lock);
                    ldisc_irq_restore(irqf);
                    return 0;
                }
                if (vmin == 0 && vtime == 0) {
                    spin_unlock(&ld->lock);
                    ldisc_irq_restore(irqf);
                    return 0;
                }
                if (vmin == 0 && vtime > 0) {
                    if (!deadline_armed) {
                        deadline = pit_ticks_ms() + (uint64_t)vtime * 100ULL;
                        deadline_armed = 1;
                    }
                    if (pit_ticks_ms() >= deadline) {
                        spin_unlock(&ld->lock);
                        ldisc_irq_restore(irqf);
                        return 0;
                    }
                }
            }
        }

        if (flags & VFS_O_NONBLOCK) {
            spin_unlock(&ld->lock);
            ldisc_irq_restore(irqf);
            return -EAGAIN;
        }

        if (!cur || !waitq) {
            spin_unlock(&ld->lock);
            ldisc_irq_restore(irqf);
            return -EAGAIN;
        }

        wait_queue_entry_t wqe;
        wqe.proc = cur;
        wqe.next = NULL;
        wait_queue_add(waitq, &wqe);
        process_set_poll_blocked(deadline_armed ? deadline : 0ULL);
        spin_unlock(&ld->lock);
        process_yield();
        ldisc_irq_restore(irqf);
        process_clear_poll_deadline();
        wait_queue_remove(waitq, &wqe);
    }
}

int tty_ldisc_ioctl(tty_ldisc_t *ld, unsigned long req, void *arg)
{
    if (!ld)
        return -EINVAL;

    switch (req) {
    case VFS_TCGETS: {
        if (!arg)
            return -EFAULT;
        unsigned long flags = spin_lock_irqsave(&ld->lock);
        *(struct tsk_termios *)arg = ld->termios;
        spin_unlock_irqrestore(&ld->lock, flags);
        return 0;
    }
    case VFS_TCSETS:
    case VFS_TCSETSW:
    case VFS_TCSETSF: {
        if (!arg)
            return -EFAULT;
        struct tsk_termios nt = *(const struct tsk_termios *)arg;
        unsigned long flags = spin_lock_irqsave(&ld->lock);
        ld->termios = nt;
        if (req == VFS_TCSETSF) {
            ld->line_len = 0;
            ld->rx_head = 0;
            ld->rx_tail = 0;
            ld->rx_count = 0;
            ld->eof_pending = 0;
        }
        spin_unlock_irqrestore(&ld->lock, flags);
        return 0;
    }
    case VFS_FIONREAD: {
        if (!arg)
            return -EFAULT;
        *(int *)arg = tty_ldisc_fionread(ld);
        return 0;
    }
    default:
        return -ENOTTY;
    }
}
