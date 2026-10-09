#
# Project Tsukasa — Vanilla Developer Tools Makefile
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
         -I../../tsukasa-libc/include -I../include -Ivendor/microui -I../apps

LDFLAGS = -m elf_x86_64 -T ../../tsukasa-libc/tsukasa_app.ld -nostdlib

CRT0           = ../../tsukasa-libc/crt/crt0.o
LIBC_A         = ../../tsukasa-libc/lib/libc.a
LIBM_A         = ../../tsukasa-libc/lib/libm.a
LIBVANILLA_A   = ../lib/libvanilla.a
APP_COMMON_OBJ = ../apps/app_common.o

BIN = DEVTOOLS.ELF

OBJS = inspector.o vendor/microui/microui.o

all: $(BIN)

$(BIN): $(CRT0) $(OBJS) $(APP_COMMON_OBJ) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) $(OBJS) $(APP_COMMON_OBJ) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)

inspector.o: inspector.c inspector.h
	$(CC) $(CFLAGS) -c -o $@ $<

vendor/microui/microui.o: vendor/microui/microui.c vendor/microui/microui.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(BIN)

.PHONY: all clean
