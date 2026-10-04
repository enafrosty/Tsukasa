#
# Project Tsukasa — Vanilla Desktop Shell Client Makefile
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

CFLAGS = -Wall -Wextra -Werror -ffreestanding -mno-red-zone \
         -fno-pie -fstack-protector-strong -O2 \
         -I../../tsukasa-libc/include -I../include -I../input -I../apps -I. -DSTBI_NO_THREAD_LOCALS

LDFLAGS = -m elf_x86_64 -T ../../tsukasa-libc/tsukasa_app.ld -nostdlib

CRT0   = ../../tsukasa-libc/crt/crt0.o
LIBC_A = ../../tsukasa-libc/lib/libc.a
LIBM_A = ../../tsukasa-libc/lib/libm.a

LIBVANILLA_A = ../lib/libvanilla.a
APP_COMMON   = ../apps/app_common.o

BIN = ../bin/shell.elf

all: $(BIN)

$(BIN): $(CRT0) shell_client.o $(APP_COMMON) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)
	@mkdir -p ../bin
	$(LD) $(LDFLAGS) -o $@ $(CRT0) shell_client.o $(APP_COMMON) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)

shell_client.o: shell_client.c shell_client.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f shell_client.o $(BIN)

.PHONY: all clean
