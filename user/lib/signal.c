#include "../include/signal.h"
#include "../include/unistd.h"

sighandler_t signal(int sig, sighandler_t handler)
{
    struct sigaction sa;
    struct sigaction old;
    sa.sa_handler = handler;
    sa.sa_mask = 0;
    sa.sa_flags = 0;
    if (sigaction(sig, &sa, &old) != 0)
        return SIG_ERR;
    return old.sa_handler;
}

int raise(int sig)
{
    return kill(getpid(), sig);
}
