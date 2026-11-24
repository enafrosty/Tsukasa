/*
 * Project Tsukasa — FAT32 Filesystem Formatter (mkfs.fat32)
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

#include "../include/disktools.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-n label] [-s sectors_per_cluster] <device_or_image_file>\n", prog);
}

int main(int argc, char *argv[])
{
    const char *dev_path = NULL;
    const char *label_arg = "TSUKASA";
    uint32_t user_spc = 0;
    int verbose = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "-L") == 0) {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            label_arg = argv[++i];
        } else if (strcmp(argv[i], "-s") == 0) {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            user_spc = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "-F") == 0) {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            i++;
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else {
            dev_path = argv[i];
        }
    }

    if (!dev_path) {
        print_usage(argv[0]);
        return 1;
    }

    device_handle_t dev;
    if (device_open(dev_path, O_RDWR, &dev) != 0) {
        fprintf(stderr, "mkfs.fat32: cannot open %s\n", dev_path);
        return 1;
    }

    int rc = fat32_format_device(&dev, label_arg, 0, user_spc, verbose);
    device_close(&dev);
    return (rc == 0) ? 0 : 1;
}
