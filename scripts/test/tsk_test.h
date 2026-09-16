/*
 * Project Tsukasa — Automated Test Protocol and Reporting Definitions
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

#ifndef TSK_TEST_H
#define TSK_TEST_H

#include <stdio.h>

#define TSK_TEST_PASS(suite, name) \
    printf("[TEST] %s.%s PASS\n", suite, name)

#define TSK_TEST_FAIL(suite, name, reason) \
    printf("[TEST] %s.%s FAIL: %s\n", suite, name, reason)

#define TSK_TEST_SKIP(suite, name, reason) \
    printf("[TEST] %s.%s SKIP: %s\n", suite, name, reason)

#define TSK_EXPECT(suite, name, cond) \
    do { \
        if (cond) { \
            TSK_TEST_PASS(suite, name); \
        } else { \
            TSK_TEST_FAIL(suite, name, #cond); \
        } \
    } while (0)

#define TSK_TEST_DONE(suite, passed, total) \
    printf("[TEST] %s DONE %d/%d\n", suite, (int)(passed), (int)(total))

#endif /* TSK_TEST_H */
