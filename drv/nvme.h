/*
 * Project Tsukasa — NVMe 2.0 PCIe storage driver
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

#ifndef TSUKASA_NVME_H
#define TSUKASA_NVME_H

/* Probe all PCI NVMe controllers (class 0x01, subclass 0x08, prog_if 0x02) and register namespaces. */
int nvme_init(void);

#endif /* TSUKASA_NVME_H */
