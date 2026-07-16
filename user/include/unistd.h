#ifndef TSUKASA_UNISTD_H
#define TSUKASA_UNISTD_H

#include <stddef.h>
#include "sys/types.h"

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int close(int fd);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
off_t lseek(int fd, off_t offset, int whence);

int dup(int oldfd);
int dup2(int oldfd, int newfd);
int pipe(int pipefd[2]);

int chdir(const char *path);
char *getcwd(char *buf, size_t size);

int getpid(void);
pid_t waitpid(pid_t pid, int *status, int options);
int kill(pid_t pid, int sig);

void yield(void);
void _exit(int code);

#endif /* TSUKASA_UNISTD_H */
