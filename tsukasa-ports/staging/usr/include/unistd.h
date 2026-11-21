/*
 * Project Tsukasa — Standard Symbolic Constants and Types
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

#ifndef _TSUKASA_UNISTD_H
#define _TSUKASA_UNISTD_H

#include <sys/types.h>

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

extern char **environ;

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
int fsync(int fd);
off_t lseek(int fd, off_t offset, int whence);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
int pipe(int pipefd[2]);
char *getcwd(char *buf, size_t size);
int chdir(const char *path);
int unlink(const char *pathname);
int rmdir(const char *pathname);
int list_dir(const char *dir, char names[][64], int max);
int isatty(int fd);


struct tsukasa_spawn_request {
    const char *path;
    const char *args;
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;
    int tty_id;
};

pid_t fork(void);
pid_t spawn(const char *path, char *const argv[], char *const envp[]);
int spawn_ex(const struct tsukasa_spawn_request *req);
int execve(const char *pathname, char *const argv[], char *const envp[]);
int execvp(const char *file, char *const argv[]);
pid_t getpid(void);
pid_t getppid(void);
int kill(pid_t pid, int sig);
int sched_yield(void);
int reboot(int cmd);

unsigned int sleep(unsigned int seconds);
int usleep(useconds_t usec);

void _exit(int status);
void exit(int status);

#endif /* _TSUKASA_UNISTD_H */
