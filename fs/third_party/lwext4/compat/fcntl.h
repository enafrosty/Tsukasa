#ifndef _LWEXT4_COMPAT_FCNTL_H
#define _LWEXT4_COMPAT_FCNTL_H

#ifndef O_RDONLY
#define O_RDONLY 00
#endif
#ifndef O_WRONLY
#define O_WRONLY 01
#endif
#ifndef O_RDWR
#define O_RDWR   02
#endif
#ifndef O_CREAT
#define O_CREAT  0100
#endif
#ifndef O_EXCL
#define O_EXCL   0200
#endif
#ifndef O_TRUNC
#define O_TRUNC  01000
#endif
#ifndef O_APPEND
#define O_APPEND 02000
#endif

#endif /* _LWEXT4_COMPAT_FCNTL_H */
