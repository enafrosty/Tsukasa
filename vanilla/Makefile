#
# Project Tsukasa — Project Vanilla Display Server & Client SDK Makefile
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
         -fno-pie -fno-stack-protector -O2 \
         -I../tsukasa-libc/include -Iinclude

LDFLAGS = -m elf_x86_64 -T ../tsukasa-libc/tsukasa_app.ld -nostdlib

CRT0   = ../tsukasa-libc/crt/crt0.o
LIBC_A = ../tsukasa-libc/lib/libc.a
LIBM_A = ../tsukasa-libc/lib/libm.a

$(CRT0) $(LIBC_A) $(LIBM_A):
	$(MAKE) -C ../tsukasa-libc all

BIN_DIR = bin
LIB_DIR = lib

LIBVANILLA_OBJS = libvanilla/client.o \
                  libvanilla/surface.o

SERVER_OBJS     = server/main.o \
                  server/wm.o \
                  server/blitter.o \
                  server/compositor.o \
                  server/font.o \
                  server/default_font.o \
                  server/image.o \
                  server/shell.o \
                  server/launcher.o

TEST_IPC_OBJS   = tests/test_ipc.o \
                  server/wm.o \
                  server/blitter.o \
                  server/compositor.o \
                  server/font.o \
                  server/default_font.o \
                  server/image.o \
                  server/shell.o \
                  server/launcher.o

TEST_COMP_OBJS  = tests/test_compositor.o \
                  server/wm.o \
                  server/blitter.o \
                  server/compositor.o \
                  server/font.o \
                  server/default_font.o \
                  server/image.o \
                  server/shell.o \
                  server/launcher.o

TEST_TYPO_OBJS  = tests/test_typography.o \
                  server/blitter.o \
                  server/font.o \
                  server/default_font.o \
                  server/image.o

TEST_SHELL_OBJS = tests/test_shell.o \
                  server/wm.o \
                  server/blitter.o \
                  server/compositor.o \
                  server/font.o \
                  server/default_font.o \
                  server/image.o \
                  server/shell.o \
                  server/launcher.o

LIBVANILLA_A    = $(LIB_DIR)/libvanilla.a
SERVER_BIN      = $(BIN_DIR)/vanilla_srv.elf
TEST_IPC_BIN    = $(BIN_DIR)/test_ipc.elf
TEST_COMP_BIN   = $(BIN_DIR)/test_compositor.elf
TEST_TYPO_BIN   = $(BIN_DIR)/test_typography.elf
TEST_SHELL_BIN  = $(BIN_DIR)/test_shell.elf

APP_COMMON_OBJ  = apps/app_common.o
TERMINAL_BIN    = $(BIN_DIR)/terminal.elf
NOTEPAD_BIN     = $(BIN_DIR)/notepad.elf
CALC_BIN        = $(BIN_DIR)/calc.elf
FILEMGR_BIN     = $(BIN_DIR)/filemgr.elf
SETTINGS_BIN    = $(BIN_DIR)/settings.elf
TASKMGR_BIN     = $(BIN_DIR)/taskmgr.elf

APP_TARGETS     = $(TERMINAL_BIN) $(NOTEPAD_BIN) $(CALC_BIN) $(FILEMGR_BIN) $(SETTINGS_BIN) $(TASKMGR_BIN)

ALL_TARGETS     = $(LIBVANILLA_A) $(SERVER_BIN) $(TEST_IPC_BIN) $(TEST_COMP_BIN) $(TEST_TYPO_BIN) $(TEST_SHELL_BIN) $(APP_TARGETS)

.PHONY: all clean check host host-run host-clean

all: $(ALL_TARGETS)

$(LIBVANILLA_A): $(LIBVANILLA_OBJS)
	@mkdir -p $(LIB_DIR)
	$(AR) rcs $@ $^

$(SERVER_BIN): $(CRT0) $(SERVER_OBJS) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)
	@mkdir -p $(BIN_DIR)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) $(SERVER_OBJS) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)

$(TEST_IPC_BIN): $(CRT0) $(TEST_IPC_OBJS) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)
	@mkdir -p $(BIN_DIR)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) $(TEST_IPC_OBJS) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)

$(TEST_COMP_BIN): $(CRT0) $(TEST_COMP_OBJS) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)
	@mkdir -p $(BIN_DIR)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) $(TEST_COMP_OBJS) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)

$(TEST_TYPO_BIN): $(CRT0) $(TEST_TYPO_OBJS) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)
	@mkdir -p $(BIN_DIR)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) $(TEST_TYPO_OBJS) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)

$(TEST_SHELL_BIN): $(CRT0) $(TEST_SHELL_OBJS) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)
	@mkdir -p $(BIN_DIR)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) $(TEST_SHELL_OBJS) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)

$(TERMINAL_BIN): $(CRT0) apps/terminal.o $(APP_COMMON_OBJ) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)
	@mkdir -p $(BIN_DIR)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) apps/terminal.o $(APP_COMMON_OBJ) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)

$(NOTEPAD_BIN): $(CRT0) apps/notepad.o $(APP_COMMON_OBJ) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)
	@mkdir -p $(BIN_DIR)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) apps/notepad.o $(APP_COMMON_OBJ) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)

$(CALC_BIN): $(CRT0) apps/calc.o $(APP_COMMON_OBJ) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)
	@mkdir -p $(BIN_DIR)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) apps/calc.o $(APP_COMMON_OBJ) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)

$(FILEMGR_BIN): $(CRT0) apps/filemgr.o $(APP_COMMON_OBJ) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)
	@mkdir -p $(BIN_DIR)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) apps/filemgr.o $(APP_COMMON_OBJ) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)

$(SETTINGS_BIN): $(CRT0) apps/settings.o $(APP_COMMON_OBJ) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)
	@mkdir -p $(BIN_DIR)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) apps/settings.o $(APP_COMMON_OBJ) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)

$(TASKMGR_BIN): $(CRT0) apps/taskmgr.o $(APP_COMMON_OBJ) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)
	@mkdir -p $(BIN_DIR)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) apps/taskmgr.o $(APP_COMMON_OBJ) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)

%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

check: all
	@echo "[OK] Project Vanilla Step 4.6 binaries built successfully:"
	@echo "     - lib/libvanilla.a"
	@echo "     - bin/vanilla_srv.elf"
	@echo "     - bin/test_ipc.elf"
	@echo "     - bin/test_compositor.elf"
	@echo "     - bin/test_typography.elf"
	@echo "     - bin/test_shell.elf"
	@echo "     - bin/terminal.elf"
	@echo "     - bin/notepad.elf"
	@echo "     - bin/calc.elf"
	@echo "     - bin/filemgr.elf"
	@echo "     - bin/settings.elf"
	@echo "     - bin/taskmgr.elf"

clean:
	rm -rf libvanilla/*.o server/*.o tests/*.o apps/*.o $(BIN_DIR) $(LIB_DIR) host_emulator/*.host.o server/*.host.o libvanilla/*.host.o host_emulator/bin

host:
	$(MAKE) -f host_emulator/Makefile.host all

host-run:
	$(MAKE) -f host_emulator/Makefile.host run

host-clean:
	$(MAKE) -f host_emulator/Makefile.host clean
