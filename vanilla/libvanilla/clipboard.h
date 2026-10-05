/*
 * Project Tsukasa — Clipboard Client API
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

#ifndef _LIBVANILLA_CLIPBOARD_H
#define _LIBVANILLA_CLIPBOARD_H

#include <stddef.h>
#include <stdint.h>

/* Maximum UTF-8 text size supported in single-slot clipboard (64 KiB). */
#define VCLIP_TEXT_MAX  65536

#ifndef EMSGSIZE
#define EMSGSIZE        90
#endif

/*
 * Phase A: synchronous single-slot UTF-8 clipboard.
 *
 * clipboard_set: Copy text (UTF-8, len bytes) to the clipboard.
 *   Returns 0 on success, negative errno on failure.
 *   text need not be NUL-terminated; len is the byte count.
 *
 * clipboard_get: Read current clipboard content into buf.
 *   Returns number of bytes written (not including NUL), or negative errno.
 *   The buffer is always NUL-terminated if the return value is >= 0.
 *
 * clipboard_clear: Discard current clipboard content. Returns 0 or errno.
 */
int clipboard_set(const char *text, size_t len);
int clipboard_get(char *buf, size_t buf_len);
int clipboard_clear(void);

/*
 * Phase B: MIME-typed offer/fetch (stubbed for future extension).
 *
 * clipboard_offer: Announce that the calling process can produce data in
 *   the given MIME types. The daemon records this; other processes may
 *   call clipboard_fetch to request the data.
 *
 * clipboard_fetch: Request data in the given MIME type from the current
 *   clipboard owner. Data is written to out_buf (up to out_len bytes).
 *   Returns byte count or negative errno.
 */
int clipboard_offer(const char * const *types, int type_count);
int clipboard_fetch(const char *mime_type, char *out_buf, size_t out_len);

#endif /* _LIBVANILLA_CLIPBOARD_H */
