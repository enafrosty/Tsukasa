/*
 * Project Tsukasa — Service Discovery Client API
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

#ifndef _LIBVANILLA_SERVICE_H
#define _LIBVANILLA_SERVICE_H

#include <stddef.h>

/*
 * Register a service name and socket path with the registry.
 * Returns 0 on success, negative errno on failure.
 * Connects to /tmp/vreg.sock, sends REGISTER, reads response, closes.
 */
int service_register(const char *name, const char *socket_path);

/*
 * Remove a service from the registry (call on clean exit).
 * Returns 0 on success, negative errno on failure.
 */
int service_unregister(const char *name);

/*
 * Look up a service by name. On success, path_buf receives the socket path
 * (NUL-terminated, at most VREG_PATH_MAX bytes) and returns 0.
 * Returns -ENOENT if the name is not registered, other negative errno on
 * transport error.
 */
int service_connect(const char *name, char *path_buf, size_t path_buf_len);

#endif /* _LIBVANILLA_SERVICE_H */
