#
# Project Tsukasa — tsukasa-coreutils Makefile
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

CFLAGS = -Wall -Wextra -Werror -ffreestanding -mno-red-zone \
         -fno-pie -fno-stack-protector -O2 \
         -I../tsukasa-libc/include

LDFLAGS = -m elf_x86_64 -T ../tsukasa-libc/tsukasa_app.ld -nostdlib

CRT0   = ../tsukasa-libc/crt/crt0.o
LIBC_A = ../tsukasa-libc/lib/libc.a

$(CRT0) $(LIBC_A):
	$(MAKE) -C ../tsukasa-libc all

TSH_OBJS = src/tsh/main.o \
           src/tsh/line_editor.o \
           src/tsh/parser.o \
           src/tsh/builtins.o \
           src/tsh/exec.o \
           src/tsh/history.o

UTILS = ls cat mkdir rm cp mv touch head tail wc grep sort uniq cut \
        sysfetch ps kill free uname date poweroff reboot \
        net ping telnet fetch
BIN_DIR = bin
TSH_BIN = $(BIN_DIR)/tsh.elf
UTIL_BINS = $(patsubst %,$(BIN_DIR)/%.elf,$(UTILS))
ALL_BINS = $(TSH_BIN) $(UTIL_BINS)

.PHONY: all clean check

all: $(ALL_BINS)

$(TSH_BIN): $(CRT0) $(TSH_OBJS) $(LIBC_A)
	@mkdir -p $(BIN_DIR)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) $(TSH_OBJS) $(LIBC_A)

define BUILD_UTIL
$$(BIN_DIR)/$(1).elf: src/$(1)/$(1).o $$(CRT0) $$(LIBC_A)
	@mkdir -p $$(BIN_DIR)
	$$(LD) $$(LDFLAGS) -o $$@ $$(CRT0) $$< $$(LIBC_A)
endef

$(foreach u,$(UTILS),$(eval $(call BUILD_UTIL,$(u))))

src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

check: all
	@echo "[OK] tsukasa-coreutils binaries built successfully:"
	@echo "     - bin/tsh.elf"
	@echo "     - bin/ls.elf"
	@echo "     - bin/cat.elf"
	@echo "     - bin/mkdir.elf"
	@echo "     - bin/rm.elf"
	@echo "     - bin/cp.elf"
	@echo "     - bin/mv.elf"
	@echo "     - bin/touch.elf"
	@echo "     - bin/head.elf"
	@echo "     - bin/tail.elf"
	@echo "     - bin/wc.elf"
	@echo "     - bin/grep.elf"
	@echo "     - bin/sort.elf"
	@echo "     - bin/uniq.elf"
	@echo "     - bin/cut.elf"
	@echo "     - bin/sysfetch.elf"
	@echo "     - bin/ps.elf"
	@echo "     - bin/kill.elf"
	@echo "     - bin/free.elf"
	@echo "     - bin/uname.elf"
	@echo "     - bin/date.elf"
	@echo "     - bin/poweroff.elf"
	@echo "     - bin/reboot.elf"
	@echo "     - bin/net.elf"
	@echo "     - bin/ping.elf"
	@echo "     - bin/telnet.elf"
	@echo "     - bin/fetch.elf"

clean:
	rm -rf src/*/*.o $(BIN_DIR)

