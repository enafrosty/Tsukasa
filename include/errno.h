/*
 * Project Tsukasa — Core errno definitions
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

#ifndef CORE_ERRNO_H
#define CORE_ERRNO_H

#define EPERM        1   // Operation not permitted
#define ENOENT       2   // No such file or directory
#define ESRCH        3   // No such process
#define EINTR        4   // Interrupted system call
#define EIO          5   // I/O error
#define ENXIO        6   // No such device or address
#define EBADF        9   // Bad file descriptor
#define EAGAIN      11   // Try again
#define EWOULDBLOCK EAGAIN
#define ENOMEM      12   // Out of memory
#define EACCES      13   // Permission denied
#define EFAULT      14   // Bad address
#define EBUSY       16   // Device or resource busy
#define EEXIST      17   // File exists
#define EXDEV       18   // Cross-device link
#define ENODEV      19   // No such device
#define ENOTDIR     20   // Not a directory
#define EISDIR      21   // Is a directory
#define EINVAL      22   // Invalid argument
#define ENFILE      23   // File table overflow
#define EMFILE      24   // Too many open files
#ifndef ENOTTY
#define ENOTTY      25   // Not a typewriter
#endif
#define EFBIG       27   // File too large
#define ENOSPC      28   // No space left on device
#define ESPIPE      29   // Illegal seek
#define EROFS       30   // Read-only file system
#define EMLINK      31   // Too many links
#define EPIPE       32   // Broken pipe
#define EDEADLK     35   // Resource deadlock would occur
#define ENAMETOOLONG 36  // File name too long
#define ENOLCK      37   // No record locks available
#define ENOSYS      38   // Function not implemented
#define ENOTEMPTY   39   // Directory not empty
#define ELOOP       40   // Too many levels of symbolic links

#define EAFNOSUPPORT 97  // Address family not supported
#define EADDRINUSE   98  // Address already in use
#define ETIMEDOUT   110  // Connection timed out
#define ECONNREFUSED 111 // Connection refused

#endif // CORE_ERRNO_H
