#
# Project Tsukasa — Vanilla Service Supervisor Makefile
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
         -I../../tsukasa-libc/include -I../include -I.

LDFLAGS = -m elf_x86_64 -T ../../tsukasa-libc/tsukasa_app.ld -nostdlib

CRT0   = ../../tsukasa-libc/crt/crt0.o
LIBC_A = ../../tsukasa-libc/lib/libc.a

BIN = SUPERVSD.ELF

all: $(BIN)

$(BIN): $(CRT0) supervisor.o $(LIBC_A)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) supervisor.o $(LIBC_A)

supervisor.o: supervisor.c supervisor.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f supervisor.o $(BIN) test_supervisor test_supervisor.exe

test: test_supervisor
	./test_supervisor

ifeq ($(OS),Windows_NT)
test_supervisor: tests/test_supervisor.c supervisor.h supervisor.c
	gcc -O2 -Wall -Wextra -I. -I../../tsukasa-libc/include -DSUPERVISOR_TEST_RUNNER -o $@ tests/test_supervisor.c supervisor.c -lws2_32
else
test_supervisor: tests/test_supervisor.c supervisor.h supervisor.c
	gcc -O2 -Wall -Wextra -I. -I../../tsukasa-libc/include -DSUPERVISOR_TEST_RUNNER -o $@ tests/test_supervisor.c supervisor.c
endif

.PHONY: all clean test
