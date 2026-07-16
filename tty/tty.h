/*
 * tty.h - Minimal tty foreground process-group controls.
 */

#ifndef TSUKASA_TTY_H
#define TSUKASA_TTY_H

#include <stdint.h>

void tty_init(void);

int tty_create(void);
int tty_destroy(int tty_id);

int tty_get_active(void);
int tty_set_active(int tty_id);

int tty_set_foreground_pgid(int tty_id, int pgid);
int tty_get_foreground_pgid(int tty_id);
int tty_kill_foreground(int tty_id, int sig);

void tty_handle_scancode(uint8_t scancode, int pressed);

#endif /* TSUKASA_TTY_H */

