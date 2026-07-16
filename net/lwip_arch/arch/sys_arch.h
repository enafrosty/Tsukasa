#ifndef TSUKASA_LWIP_ARCH_SYS_ARCH_H
#define TSUKASA_LWIP_ARCH_SYS_ARCH_H

#include "lwip/opt.h"

typedef int sys_sem_t;
typedef int sys_mutex_t;
typedef int sys_mbox_t;
typedef int sys_thread_t;

#define SYS_SEM_NULL 0
#define SYS_MBOX_NULL 0

#endif /* TSUKASA_LWIP_ARCH_SYS_ARCH_H */
