/*
 * Project Tsukasa — Application runtime helper header
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

#ifndef TSUKASA_APP_RUNTIME_H
#define TSUKASA_APP_RUNTIME_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "user/include/sys/types.h"
#include "user/include/sys/stat.h"
#include "user/include/sys/mman.h"
#include "user/include/sys/poll.h"
#include "user/include/sys/ioctl.h"
#include "user/include/fcntl.h"
#include "user/include/stdio.h"
#include "user/include/stdlib.h"
#include "user/include/string.h"
#include "user/include/unistd.h"
#include "user/include/signal.h"
#include "user/include/time.h"
#include "user/include/libui.h"
#include "user/include/libwidget.h"
#include "user/include/shell.h"

int app_get_cmdline(char *buf, size_t cap);
int app_tokenize(char *line, char *argv[], int max_args);
int app_run_main(int (*main_fn)(int argc, char **argv));

#endif /* TSUKASA_APP_RUNTIME_H */
