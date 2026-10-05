/*
 * Project Tsukasa — Service Supervisor Implementation
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

#include "supervisor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

#ifndef SUPERVISOR_TEST_RUNNER
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

static svc_entry_t g_services[SUPERVISOR_MAX_SERVICES];
static int         g_service_count = 0;
static volatile int g_running = 1;

static uint64_t get_monotonic_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
    }
    return 0;
}
#endif

static char *trim_whitespace(char *str)
{
    while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n')
        str++;
    if (*str == '\0')
        return str;
    char *end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
        *end-- = '\0';
    return str;
}

int supervisor_parse_line(char *line, svc_entry_t *out)
{
    line = trim_whitespace(line);
    if (*line == '\0' || *line == '#')
        return 0;

    char *eq = strchr(line, '=');
    if (!eq)
        return -1;

    *eq = '\0';
    char *name = trim_whitespace(line);
    char *val = trim_whitespace(eq + 1);

    if (*name == '\0' || *val == '\0')
        return -1;

    memset(out, 0, sizeof(*out));

    char *p = val;
    while (*p && *p != ' ' && *p != '\t')
        p++;

    char *args = NULL;
    if (*p != '\0') {
        *p = '\0';
        args = trim_whitespace(p + 1);
    }

    strncpy(out->name, name, SUPERVISOR_NAME_MAX - 1);
    out->name[SUPERVISOR_NAME_MAX - 1] = '\0';

    strncpy(out->path, val, SUPERVISOR_PATH_MAX - 1);
    out->path[SUPERVISOR_PATH_MAX - 1] = '\0';

    if (args && *args != '\0') {
        strncpy(out->args, args, SUPERVISOR_PATH_MAX - 1);
        out->args[SUPERVISOR_PATH_MAX - 1] = '\0';
    }

    out->argv[0] = out->path;
    out->argc = 1;
    out->pid = -1;
    out->state = SVC_STATE_BACKOFF;
    out->restart_count = 0;
    out->last_start_ms = 0;
    out->backoff_ms = SUPERVISOR_BACKOFF_INIT_MS;
    out->restart_after_ms = 0;

    return 1;
}

uint32_t supervisor_compute_next_backoff(uint32_t current_backoff_ms, uint64_t runtime_ms)
{
    if (runtime_ms >= SUPERVISOR_BACKOFF_RESET_MS)
        return SUPERVISOR_BACKOFF_INIT_MS;

    uint32_t next = current_backoff_ms * 2;
    if (next > SUPERVISOR_BACKOFF_MAX_MS || next < current_backoff_ms)
        return SUPERVISOR_BACKOFF_MAX_MS;

    return next;
}

#ifndef SUPERVISOR_TEST_RUNNER
static int load_services_file(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    char buf[4096];
    ssize_t rd = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (rd <= 0)
        return 0;

    buf[rd] = '\0';

    char *cur = buf;
    while (*cur && g_service_count < SUPERVISOR_MAX_SERVICES) {
        char *eol = strchr(cur, '\n');
        if (eol)
            *eol = '\0';

        svc_entry_t entry;
        int res = supervisor_parse_line(cur, &entry);
        if (res > 0) {
            g_services[g_service_count++] = entry;
        }

        if (!eol)
            break;
        cur = eol + 1;
    }

    return g_service_count;
}

static int load_services(const char *specified_path)
{
    g_service_count = 0;

    if (specified_path) {
        if (load_services_file(specified_path) >= 0)
            return g_service_count;
    }

    if (load_services_file("/etc/services.conf") >= 0)
        return g_service_count;

    if (load_services_file("/SVCS.CFG") >= 0)
        return g_service_count;

    if (load_services_file("/fat12/SVCS.CFG") >= 0)
        return g_service_count;

    if (load_services_file("/bin/SVCS.CFG") >= 0)
        return g_service_count;

    if (load_services_file("/fat12/services.conf") >= 0)
        return g_service_count;

    if (load_services_file("services.conf") >= 0)
        return g_service_count;

    return -1;
}

/*
 * Future upgrade path:
 * When copy-on-write fork and execve primitives are available,
 * replace spawn_ex with fork() + execve(), and replace kill(pid, 0) status polling
 * with a SIGCHLD signal handler calling waitpid(-1, &status, WNOHANG).
 */
#ifndef SUPERVISOR_TEST_RUNNER
static pid_t svc_spawn(svc_entry_t *svc)
{
    struct tsukasa_spawn_request req;
    memset(&req, 0, sizeof(req));
    req.path = svc->path;
    req.args = (svc->args[0] != '\0') ? svc->args : svc->path;
    req.stdin_fd = -1;
    req.stdout_fd = -1;
    req.stderr_fd = -1;
    req.tty_id = -1;

    int test_fd = open(req.path, O_RDONLY);
    if (test_fd >= 0) {
        close(test_fd);
    } else {
        /* Fallback for FAT12 8.3 filename truncation on root ramdisk */
        if (strcmp(svc->path, "/bin/registryd.elf") == 0) {
            test_fd = open("/bin/REGISTRD.ELF", O_RDONLY);
            if (test_fd >= 0) {
                close(test_fd);
                req.path = "/bin/REGISTRD.ELF";
            } else {
                test_fd = open("/fat12/REGISTRD.ELF", O_RDONLY);
                if (test_fd >= 0) {
                    close(test_fd);
                    req.path = "/fat12/REGISTRD.ELF";
                }
            }
        } else if (strcmp(svc->path, "/bin/shell.elf") == 0) {
            test_fd = open("/bin/SHELL.ELF", O_RDONLY);
            if (test_fd >= 0) {
                close(test_fd);
                req.path = "/bin/SHELL.ELF";
            } else {
                test_fd = open("/fat12/SHELL.ELF", O_RDONLY);
                if (test_fd >= 0) {
                    close(test_fd);
                    req.path = "/fat12/SHELL.ELF";
                }
            }
        }
    }

    return (pid_t)spawn_ex(&req);
}
#endif

static int setup_query_socket(void)
{
    unlink(VSUP_SOCKET_PATH);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, VSUP_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    if (listen(fd, 8) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static void svc_accept_query(int listen_fd)
{
    if (listen_fd < 0)
        return;

    int client_fd = accept(listen_fd, NULL, NULL);
    if (client_fd < 0)
        return;

    vsup_req_t req;
    ssize_t n = read(client_fd, &req, sizeof(req));
    if (n == (ssize_t)sizeof(req) && req.magic == VSUP_MAGIC && req.op == VSUP_OP_LIST) {
        vsup_resp_hdr_t hdr;
        hdr.magic = VSUP_MAGIC;
        hdr.service_count = g_service_count;
        write(client_fd, &hdr, sizeof(hdr));

        for (int i = 0; i < g_service_count; i++) {
            vsup_svc_entry_t entry;
            memset(&entry, 0, sizeof(entry));
            strncpy(entry.name, g_services[i].name, sizeof(entry.name) - 1);
            entry.pid = (int32_t)g_services[i].pid;
            entry.restart_count = g_services[i].restart_count;
            entry.state = (uint8_t)g_services[i].state;
            write(client_fd, &entry, sizeof(entry));
        }
    }
    close(client_fd);
}

static int run_query_client(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "[supervisord] failed to create socket: %d\n", errno);
        return 1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, VSUP_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[supervisord] failed to connect to %s: %d\n", VSUP_SOCKET_PATH, errno);
        close(fd);
        return 1;
    }

    vsup_req_t req;
    memset(&req, 0, sizeof(req));
    req.magic = VSUP_MAGIC;
    req.op = VSUP_OP_LIST;
    if (write(fd, &req, sizeof(req)) != sizeof(req)) {
        fprintf(stderr, "[supervisord] failed to send query request\n");
        close(fd);
        return 1;
    }

    vsup_resp_hdr_t hdr;
    if (read(fd, &hdr, sizeof(hdr)) != sizeof(hdr) || hdr.magic != VSUP_MAGIC) {
        fprintf(stderr, "[supervisord] invalid response from %s\n", VSUP_SOCKET_PATH);
        close(fd);
        return 1;
    }

    printf("%-16s %-8s %-10s %s\n", "NAME", "PID", "RESTARTS", "STATE");
    printf("%-16s %-8s %-10s %s\n", "----------------", "--------", "----------", "-----");
    for (int i = 0; i < hdr.service_count; i++) {
        vsup_svc_entry_t entry;
        if (read(fd, &entry, sizeof(entry)) != sizeof(entry))
            break;
        const char *st_str = "unknown";
        switch (entry.state) {
        case SVC_STATE_STARTING: st_str = "starting"; break;
        case SVC_STATE_RUNNING:  st_str = "running"; break;
        case SVC_STATE_BACKOFF:  st_str = "backoff"; break;
        case SVC_STATE_STOPPED:  st_str = "stopped"; break;
        }
        printf("%-16s %-8d %-10u %s\n", entry.name, (int)entry.pid, entry.restart_count, st_str);
    }

    close(fd);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && (strcmp(argv[1], "--list") == 0 || strcmp(argv[1], "-l") == 0)) {
        return run_query_client();
    }

    const char *cfg_path = (argc > 1 && argv[1][0] != '-') ? argv[1] : NULL;

    printf("[supervisord] Starting service supervisor\n");
    fflush(stdout);

    int count = load_services(cfg_path);
    if (count <= 0) {
        printf("[supervisord] WARN: no services loaded from configuration\n");
        fflush(stdout);
    }

    int query_fd = setup_query_socket();
    if (query_fd >= 0) {
        printf("[supervisord] Query socket listening on %s\n", VSUP_SOCKET_PATH);
        fflush(stdout);
    }

    uint64_t now_ms = get_monotonic_ms();
    for (int i = 0; i < g_service_count; i++) {
        svc_entry_t *s = &g_services[i];
        s->pid = svc_spawn(s);
        if (s->pid > 0) {
            s->state = SVC_STATE_RUNNING;
            s->last_start_ms = get_monotonic_ms();
            s->backoff_ms = SUPERVISOR_BACKOFF_INIT_MS;
            printf("[supervisord] starting %s (%s) pid=%d\n", s->name, s->path, (int)s->pid);
            fflush(stdout);
        } else {
            s->state = SVC_STATE_BACKOFF;
            s->backoff_ms = SUPERVISOR_BACKOFF_INIT_MS;
            s->restart_after_ms = get_monotonic_ms() + s->backoff_ms;
            printf("[supervisord] could not start %s (%s), retry in %us\n",
                   s->name, s->path, s->backoff_ms / 1000u);
            fflush(stdout);
        }
        struct timespec ts_step = { 0, 100000000 }; /* 100ms yield between spawns */
        nanosleep(&ts_step, NULL);
    }

    while (g_running) {
        now_ms = get_monotonic_ms();

        for (int i = 0; i < g_service_count; i++) {
            svc_entry_t *s = &g_services[i];
            switch (s->state) {
            case SVC_STATE_RUNNING: {
                int status = 0;
                int reaped = waitpid(s->pid, &status, WNOHANG);
                int is_dead = 0;

                if (reaped == s->pid) {
                    is_dead = 1;
                } else if (reaped <= 0) {
                    if (kill(s->pid, 0) < 0) {
                        is_dead = 1;
                    }
                }

                if (is_dead) {
                    uint64_t runtime_ms = now_ms - s->last_start_ms;
                    s->backoff_ms = supervisor_compute_next_backoff(s->backoff_ms, runtime_ms);
                    s->restart_after_ms = now_ms + s->backoff_ms;
                    s->state = SVC_STATE_BACKOFF;
                    s->pid = -1;
                    s->restart_count++;
                    printf("[supervisord] %s exited, restart in %us\n",
                           s->name, s->backoff_ms / 1000u);
                    fflush(stdout);
                }
                break;
            }

            case SVC_STATE_BACKOFF: {
                if (now_ms >= s->restart_after_ms) {
                    s->pid = svc_spawn(s);
                    if (s->pid > 0) {
                        s->state = SVC_STATE_RUNNING;
                        s->last_start_ms = now_ms;
                        printf("[supervisord] starting %s (%s) pid=%d\n",
                               s->name, s->path, (int)s->pid);
                        fflush(stdout);
                    } else {
                        if (s->backoff_ms < SUPERVISOR_BACKOFF_MAX_MS) {
                            uint32_t next = s->backoff_ms * 2;
                            s->backoff_ms = (next < SUPERVISOR_BACKOFF_MAX_MS) ? next : SUPERVISOR_BACKOFF_MAX_MS;
                        }
                        s->restart_after_ms = now_ms + s->backoff_ms;
                        printf("[supervisord] could not start %s (%s), retry in %us\n",
                               s->name, s->path, s->backoff_ms / 1000u);
                        fflush(stdout);
                    }
                }
                break;
            }

            default:
                break;
            }
        }

        svc_accept_query(query_fd);

        struct timespec ts = { 0, 100000000 }; /* 100ms polling interval */
        nanosleep(&ts, NULL);
    }

    if (query_fd >= 0) {
        close(query_fd);
        unlink(VSUP_SOCKET_PATH);
    }

    return 0;
}
#endif
