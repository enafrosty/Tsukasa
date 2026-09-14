/*
 * Project Tsukasa — /bin/install: disk installer command (guide 13)
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

#include "user/include/app_runtime.h"

#include "../include/app_runtime.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"
#include "../include/string.h"

#include "../../drv/blockdev.h"
#include "../../sys/installer.h"

/* Case-insensitive token match: the PS/2 keyboard driver has no Shift/Caps handling yet (lowercase-only... */
static int token_is_erase(const char *s)
{
    static const char t[] = "erase";
    int i;
    if (!s)
        return 0;
    for (i = 0; t[i]; i++) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + 32);
        if (c != t[i])
            return 0;
    }
    return s[i] == '\0';
}

static int disk_has_data(block_dev_t *bd)
{
    if (bd->is_fat32)
        return 1;
    for (int i = 0; i < blockdev_count(); i++) {
        block_dev_t *c = blockdev_at(i);
        if (c && c->is_partition && c->parent == bd)
            return 1;
    }
    return 0;
}

static void list_disks(void)
{
    dprintf(1, "Available disks:\n");
    for (int i = 0; i < blockdev_count(); i++) {
        block_dev_t *bd = blockdev_at(i);
        if (!bd || bd->is_partition)
            continue;
        /* NOTE: user/lib stdio's printf supports NO width/flag modifiers (%% c s d i u x X p only) — %-4s here... */
        dprintf(1, "  /dev/%s  %u MiB  %s%s\n", bd->name,
                (unsigned)(bd->sector_count / 2048u),
                bd->label[0] ? bd->label : "(no label)",
                disk_has_data(bd) ? "  ** CONTAINS DATA **" : "");
    }
}

static void progress(const char *stage, int percent)
{
    dprintf(1, "  [%d%%] %s\n", percent, stage);
}

static int cmd_install_main(int argc, char **argv)
{
    block_dev_t *bd;
    char err[96];

    dprintf(1, "Tsukasa Installer (created by frosty / @enafrosty)\n");
    dprintf(1, "DEVELOPER BUILD: may corrupt data or fail to boot.\n\n");

    if (argc < 2) {
        list_disks();
        dprintf(1, "\nUsage: install <disk> ERASE\n");
        dprintf(1, "  e.g. install %s ERASE\n",
                (blockdev_count() > 0 && blockdev_at(0)) ?
                    blockdev_at(0)->name : "sda");
        dprintf(1, "The disk is ONLY erased when the ERASE keyword is given.\n");
        return 0;
    }

    bd = blockdev_find(argv[1]);
    if (!bd) {
        const char *n = argv[1];
        if (n[0] == '/' && n[1] == 'd' && n[2] == 'e' && n[3] == 'v' &&
            n[4] == '/')
            bd = blockdev_find(n + 5);
    }
    if (!bd || bd->is_partition) {
        dprintf(2, "install: '%s' is not a whole disk\n", argv[1]);
        dprintf(1, "\n");
        list_disks();
        return 1;
    }

    if (installer_preflight(bd, err, sizeof(err)) != 0) {
        dprintf(2, "install: cannot install to /dev/%s: %s\n", bd->name, err);
        return 1;
    }

    if (argc < 3 || !token_is_erase(argv[2])) {
        dprintf(1, "Target: /dev/%s (%u MiB)%s\n", bd->name,
                (unsigned)(bd->sector_count / 2048u),
                disk_has_data(bd) ? "  ** CONTAINS DATA **" : "");
        dprintf(1, "This will ERASE ALL DATA on /dev/%s and install Tsukasa.\n",
                bd->name);
        dprintf(1, "Nothing has been written. To proceed, re-run:\n");
        dprintf(1, "  install %s ERASE\n", bd->name);
        return 1;
    }

    dprintf(1, "Installing to /dev/%s ...\n", bd->name);
    if (installer_run(bd, progress) != 0) {
        dprintf(2, "install: FAILED (see [installer] serial log for the "
                   "failing stage)\n");
        return 1;
    }

    dprintf(1, "\nInstallation complete. Power off the VM, remove the CD,\n");
    dprintf(1, "and boot from /dev/%s (gate g7: qemu ... -boot c, no -cdrom).\n",
            bd->name);
    return 0;
}

void app_cmd_install_entry(void)
{
    _exit(app_run_main(cmd_install_main));
}
