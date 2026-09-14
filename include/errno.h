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

#define EINVAL      22  // Invalid argument
#define ENOENT      2   // No such file or directory
#define EIO         5   // I/O error
#define ENXIO       6   // No such device or address
#define ENODEV      19  // No such device
#define EEXIST      17  // File exists
#define ENOSPC      28  // No space left on device
#define ENOSYS      38  // Function not implemented
#define ETIMEDOUT   110 // Connection timed out

#define EFAULT      14  // Bad address (Tsukasa addition for SPEC-C01 acceptance)
#define EAGAIN      11
#define ENOMEM      12

#define EAFNOSUPPORT 97  // Address family not supported
#define EADDRINUSE   98  // Address already in use
#define ECONNREFUSED 111 // Connection refused
#define EPIPE        32  // Broken pipe

#endif // CORE_ERRNO_H
