#ifndef LWIP_STDLIB_H
#define LWIP_STDLIB_H

#include "kutils.h"

#define atoi k_atoi
#define rand() 0
#define exit(x) while(1)

#endif
