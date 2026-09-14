#
# Project Tsukasa - Common Ports Toolchain Definitions
#
# Copyright (C) 2025-2026 frosty (@enafrosty) and Project Tsukasa contributors.
#
# Project Tsukasa was created and is maintained by frosty (@enafrosty).
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation, either version 3 of the License, or (at your
# option) any later version. See the top-level LICENSE file.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#

TOP ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
PORTS_DIR ?= $(abspath $(TOP)/tsukasa-ports)
STAGING_DIR ?= $(PORTS_DIR)/staging

TOOLCHAIN ?= llvm

ifeq ($(TOOLCHAIN),llvm)
CC = clang --target=x86_64-unknown-elf
LD = ld.lld
AR = ar
else
CC = gcc
LD = ld
AR = ar
endif

# Freestanding CFLAGS matching Tsukasa user ABI
PORTS_CFLAGS = -Wall -Wextra -ffreestanding -mno-red-zone \
               -fno-pie -fno-stack-protector -O2 \
               -I$(TOP)/tsukasa-libc/include

PORTS_LDFLAGS = -m elf_x86_64 -T $(TOP)/tsukasa-libc/tsukasa_app.ld -nostdlib

CRT0   = $(TOP)/tsukasa-libc/crt/crt0.o
LIBC_A = $(TOP)/tsukasa-libc/lib/libc.a
LIBM_A = $(TOP)/tsukasa-libc/lib/libm.a

$(CRT0) $(LIBC_A) $(LIBM_A):
	$(MAKE) -C $(TOP)/tsukasa-libc all
