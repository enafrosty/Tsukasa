/*
 * Project Tsukasa — AHCI (SATA) PCIe/DMA storage driver
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

#ifndef TSUKASA_AHCI_H
#define TSUKASA_AHCI_H

/* Probe all PCI AHCI controllers, initialize active SATA ports, and register block devices. */
int ahci_init(void);

#endif /* TSUKASA_AHCI_H */
