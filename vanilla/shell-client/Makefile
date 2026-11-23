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
         -fno-pie -fstack-protector-strong -mstack-protector-guard=global -O2 \
         -I../../tsukasa-libc/include -I../include -I../input -I../apps -I. -DSTBI_NO_THREAD_LOCALS

LDFLAGS = -m elf_x86_64 -T ../../tsukasa-libc/tsukasa_app.ld -nostdlib

CRT0   = ../../tsukasa-libc/crt/crt0.o
LIBC_A = ../../tsukasa-libc/lib/libc.a
LIBM_A = ../../tsukasa-libc/lib/libm.a

LIBVANILLA_A = ../lib/libvanilla.a
APP_COMMON   = ../apps/app_common.o
FONT_OBJS    = ../server/font.o ../server/default_font.o

BIN = ../bin/shell.elf

OBJS = shell_client.o dock.o statusbar.o

all: $(BIN)

$(CRT0) $(LIBC_A) $(LIBM_A):
	$(MAKE) TOOLCHAIN=$(TOOLCHAIN) -C ../../tsukasa-libc all

$(LIBVANILLA_A):
	$(MAKE) TOOLCHAIN=$(TOOLCHAIN) -C .. $(LIBVANILLA_A)

$(APP_COMMON):
	$(MAKE) TOOLCHAIN=$(TOOLCHAIN) -C .. $(APP_COMMON)

$(FONT_OBJS):
	$(MAKE) TOOLCHAIN=$(TOOLCHAIN) -C .. server/font.o server/default_font.o

$(BIN): $(CRT0) $(OBJS) $(APP_COMMON) $(FONT_OBJS) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)
	@mkdir -p ../bin
	$(LD) $(LDFLAGS) -o $@ $(CRT0) $(OBJS) $(APP_COMMON) $(FONT_OBJS) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)

dock.o: dock.c dock.h
	$(CC) $(CFLAGS) -c -o $@ $<

statusbar.o: statusbar.c statusbar.h
	$(CC) $(CFLAGS) -c -o $@ $<

shell_client.o: shell_client.c shell_client.h dock.h statusbar.h
	$(CC) $(CFLAGS) -DSHELL_SEPARATE_UNITS -c -o $@ $<

clean:
	rm -f $(OBJS) $(BIN)

.PHONY: all clean
