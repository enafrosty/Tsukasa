/*
 * Project Tsukasa — Vanilla Display Server Daemon Entry Point
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

#include "server.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    const char *sock_path = VANILLA_SOCKET_PATH;
    vanilla_server_t srv;

    if (argc > 1 && argv[1] && argv[1][0] != '\0')
        sock_path = argv[1];

    printf("[vanilla] Initializing display server at %s...\n", sock_path);
    if (vanilla_server_init(&srv, sock_path) < 0) {
        printf("[vanilla] Fatal: Failed to initialize display server socket\n");
        return 1;
    }

    printf("[vanilla] Listening for client connections.\n");
    while (srv.running)
        vanilla_server_poll(&srv, 50);

    printf("[vanilla] Shutting down display server...\n");
    vanilla_server_close(&srv);

    return 0;
}
