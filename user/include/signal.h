#ifndef TSUKASA_SIGNAL_H
#define TSUKASA_SIGNAL_H

#include "sys/types.h"

typedef void (*sighandler_t)(int);
typedef uint64_t sigset_t;

struct sigaction {
    sighandler_t sa_handler;
    sigset_t sa_mask;
    int sa_flags;
};

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)

#define SIGINT  2
#define SIGKILL 9
#define SIGUSR1 10

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int sigpending(sigset_t *set);
sighandler_t signal(int sig, sighandler_t handler);
int raise(int sig);
int kill(pid_t pid, int sig);

#endif /* TSUKASA_SIGNAL_H */
