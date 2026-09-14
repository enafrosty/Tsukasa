#
# Project Tsukasa — tsukasa-libc Makefile
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
else
CC = gcc
LD = ld
endif

ASM ?= nasm
AR ?= ar

CFLAGS_BASE = -Wall -Wextra -Werror -ffreestanding -mno-red-zone \
              -fno-pie -fno-stack-protector -O2 -Iinclude

CFLAGS      = $(CFLAGS_BASE) -mno-mmx -mno-sse -mno-sse2
MATH_CFLAGS = $(CFLAGS_BASE)

SFLAGS = -Wall -Wextra -Werror -Wno-unused-command-line-argument

ASMFLAGS = -f elf64

CRT_OBJS = crt/crt0.o crt/crt0.s.o

SYSCALL_OBJS = src/syscall/errno.o \
               src/syscall/sys_fs.o \
               src/syscall/sys_mem.o \
               src/syscall/sys_proc.o \
               src/syscall/sys_time.o \
               src/syscall/sys_ipc.o \
               src/syscall/sys_dev.o \
               src/syscall/sys_net.o

STRING_OBJS  = src/string/string.o

STDLIB_OBJS  = src/stdlib/stdlib.o \
               src/stdlib/malloc.o \
               src/stdlib/locale.o \
               src/stdlib/assert.o \
               src/stdlib/strtod.o

STDIO_OBJS   = src/stdio/printf.o \
               src/stdio/stdio.o \
               src/stdio/sscanf.o

SETJMP_OBJS  = src/setjmp/setjmp.o

LIBC_OBJS    = $(SYSCALL_OBJS) $(STRING_OBJS) $(STDLIB_OBJS) $(STDIO_OBJS) $(SETJMP_OBJS)
MATH_OBJS    = src/math/math.o

LIBC_A       = lib/libc.a
LIBM_A       = lib/libm.a

TEST_BINS    = tests/test_crt0.elf \
               tests/test_syscalls.elf \
               tests/test_libc.elf \
               tests/test_math.elf \
               tests/test_stdio.elf

.PHONY: all clean check

all: $(CRT_OBJS) $(LIBC_A) $(LIBM_A)

crt/crt0.o: crt/crt0.asm
	@mkdir -p $(dir $@)
	$(ASM) $(ASMFLAGS) -o $@ $<

crt/crt0.s.o: crt/crt0.s
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SFLAGS) -c -o $@ $<

src/setjmp/%.o: src/setjmp/%.s
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SFLAGS) -c -o $@ $<

src/stdlib/strtod.o: src/stdlib/strtod.c
	@mkdir -p $(dir $@)
	$(CC) $(MATH_CFLAGS) -c -o $@ $<

src/stdio/sscanf.o: src/stdio/sscanf.c
	@mkdir -p $(dir $@)
	$(CC) $(MATH_CFLAGS) -c -o $@ $<

src/math/%.o: src/math/%.c
	@mkdir -p $(dir $@)
	$(CC) $(MATH_CFLAGS) -c -o $@ $<

src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(LIBC_A): $(LIBC_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

$(LIBM_A): $(MATH_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

tests/test_math.o: tests/test_math.c
	@mkdir -p $(dir $@)
	$(CC) $(MATH_CFLAGS) -c -o $@ $<

tests/%.o: tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

tests/test_math.elf: crt/crt0.o tests/test_math.o $(LIBM_A) $(LIBC_A) tsukasa_app.ld
	$(LD) -m elf_x86_64 -T tsukasa_app.ld -nostdlib -o $@ crt/crt0.o tests/test_math.o $(LIBM_A) $(LIBC_A)

tests/%.elf: crt/crt0.o tests/%.o $(LIBC_A) tsukasa_app.ld
	$(LD) -m elf_x86_64 -T tsukasa_app.ld -nostdlib -o $@ crt/crt0.o tests/$*.o $(LIBC_A)

check: $(TEST_BINS)
	@echo "[OK] tsukasa-libc verification binaries built successfully:"
	@echo "     - tests/test_crt0.elf"
	@echo "     - tests/test_syscalls.elf"
	@echo "     - tests/test_libc.elf"
	@echo "     - tests/test_math.elf"
	@echo "     - tests/test_stdio.elf"

clean:
	rm -f crt/*.o src/*/*.o tests/*.o tests/*.elf $(LIBC_A) $(LIBM_A)
