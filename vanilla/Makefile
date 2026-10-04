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
         -fno-pie -fstack-protector-strong -O2 \
         -I../tsukasa-libc/include -Iinclude -Iinput -DSTBI_NO_THREAD_LOCALS

LDFLAGS = -m elf_x86_64 -T ../tsukasa-libc/tsukasa_app.ld -nostdlib

CRT0   = ../tsukasa-libc/crt/crt0.o
LIBC_A = ../tsukasa-libc/lib/libc.a
LIBM_A = ../tsukasa-libc/lib/libm.a

$(CRT0) $(LIBC_A) $(LIBM_A):
	$(MAKE) -C ../tsukasa-libc all

BIN_DIR = bin
LIB_DIR = lib

LIBVANILLA_OBJS = libvanilla/client.o \
                  libvanilla/surface.o \
                  server/theme.o \
                  input/keymap.o

LAYOUT_SRCS     = layout/layout.c \
                  layout/draw_consumer.c

LAYOUT_OBJS     = $(LAYOUT_SRCS:.c=.o)

LIBVANILLA_UI_SRCS = libvanilla-ui/ui_ctx.c \
                     libvanilla-ui/ui_tree.c \
                     libvanilla-ui/ui_event.c \
                     libvanilla-ui/ui_render.c \
                     libvanilla-ui/ui_text_field.c \
                     libvanilla-ui/ui_widgets.c

LIBVANILLA_UI_OBJS = $(LIBVANILLA_UI_SRCS:.c=.o)

SERVER_OBJS     = server/main.o \
                  server/wm.o \
                  server/blitter.o \
                  server/compositor.o \
                  server/font.o \
                  server/default_font.o \
                  server/image.o \
                  server/theme.o \
                  server/cursor.o

TEST_IPC_OBJS   = tests/test_ipc.o \
                  server/wm.o \
                  server/blitter.o \
                  server/compositor.o \
                  server/font.o \
                  server/default_font.o \
                  server/image.o \
                  server/theme.o \
                  server/cursor.o

TEST_COMP_OBJS  = tests/test_compositor.o \
                  server/wm.o \
                  server/blitter.o \
                  server/compositor.o \
                  server/font.o \
                  server/default_font.o \
                  server/image.o \
                  server/theme.o \
                  server/cursor.o

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
                  server/theme.o \
                  server/cursor.o \
                  shell-client/shell_client.test.o \
                  apps/app_common.o

TEST_LAYOUT_OBJS = layout/test_layout.o \
                   apps/app_common.o

LIBVANILLA_A       = $(LIB_DIR)/libvanilla.a
LIBVANILLA_UI_A    = $(LIB_DIR)/libvanilla-ui.a
LIBVANILLALAYOUT_A = $(LIB_DIR)/libvanillalayout.a
SERVER_BIN         = $(BIN_DIR)/vanilla_srv.elf
TEST_IPC_BIN       = $(BIN_DIR)/test_ipc.elf
TEST_COMP_BIN      = $(BIN_DIR)/test_compositor.elf
TEST_TYPO_BIN      = $(BIN_DIR)/test_typography.elf
TEST_SHELL_BIN     = $(BIN_DIR)/test_shell.elf
TEST_LAYOUT_BIN    = $(BIN_DIR)/test_layout.elf

APP_COMMON_OBJ  = apps/app_common.o
TERMINAL_BIN    = $(BIN_DIR)/terminal.elf
NOTEPAD_BIN     = $(BIN_DIR)/notepad.elf
CALC_BIN        = $(BIN_DIR)/calc.elf
FILEMGR_BIN     = $(BIN_DIR)/filemgr.elf
SETTINGS_BIN    = $(BIN_DIR)/settings.elf
TASKMGR_BIN     = $(BIN_DIR)/taskmgr.elf
SHELL_BIN       = $(BIN_DIR)/shell.elf

APP_TARGETS     = $(TERMINAL_BIN) $(NOTEPAD_BIN) $(CALC_BIN) $(FILEMGR_BIN) $(SETTINGS_BIN) $(TASKMGR_BIN) $(SHELL_BIN)

ALL_TARGETS     = $(LIBVANILLA_A) $(LIBVANILLA_UI_A) $(LIBVANILLALAYOUT_A) $(SERVER_BIN) $(TEST_IPC_BIN) $(TEST_COMP_BIN) $(TEST_TYPO_BIN) $(TEST_SHELL_BIN) $(TEST_LAYOUT_BIN) $(APP_TARGETS)

PYTHON ?= python3

.PHONY: all clean check host host-run host-clean layout ui gen-protocol

gen-protocol:
	$(PYTHON) tools/gen_protocol.py \
		--yaml-dir protocol/ \
		--out-header include/protocol_generated.h \
		--out-doc ../docs/agent/phase-2-protocol-motion/wire-format.md

all: gen-protocol $(ALL_TARGETS)

ui: $(LIBVANILLA_UI_A)

layout: $(LIBVANILLALAYOUT_A)

$(LIBVANILLA_A): $(LIBVANILLA_OBJS)
	@mkdir -p $(LIB_DIR)
	$(AR) rcs $@ $^

$(LIBVANILLA_UI_A): $(LIBVANILLA_UI_OBJS)
	@mkdir -p $(LIB_DIR)
	$(AR) rcs $@ $^

$(LIBVANILLALAYOUT_A): $(LAYOUT_OBJS) server/blitter.o
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

$(TEST_LAYOUT_BIN): $(CRT0) $(TEST_LAYOUT_OBJS) $(LIBVANILLALAYOUT_A) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)
	@mkdir -p $(BIN_DIR)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) $(TEST_LAYOUT_OBJS) $(LIBVANILLALAYOUT_A) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)

build/test_layout: layout/test_layout.c layout/layout.c layout/draw_consumer.c apps/app_common.c server/blitter.c server/theme.c
	@mkdir -p build
	gcc -O2 -Iinclude -I../tsukasa-libc/include -Iinput -Iserver -Iapps -DVANILLA_HOST -o $@ $^ -lm

build/test_ui: libvanilla-ui/test_ui.c $(LIBVANILLA_UI_SRCS) layout/layout.c layout/draw_consumer.c apps/app_common.c server/blitter.c server/theme.c input/keymap.c
	@mkdir -p build
	gcc -O2 -Iinclude -I../tsukasa-libc/include -Iinput -Iserver -Iapps -Ilibvanilla-ui -DVANILLA_HOST -o $@ $^ -lm

$(TERMINAL_BIN): $(CRT0) apps/terminal.o $(APP_COMMON_OBJ) $(LIBVANILLALAYOUT_A) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)
	@mkdir -p $(BIN_DIR)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) apps/terminal.o $(APP_COMMON_OBJ) $(LIBVANILLALAYOUT_A) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)

$(NOTEPAD_BIN): $(CRT0) apps/notepad.o $(APP_COMMON_OBJ) $(LIBVANILLALAYOUT_A) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)
	@mkdir -p $(BIN_DIR)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) apps/notepad.o $(APP_COMMON_OBJ) $(LIBVANILLALAYOUT_A) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)

$(CALC_BIN): $(CRT0) apps/calc.o $(APP_COMMON_OBJ) $(LIBVANILLA_UI_A) $(LIBVANILLALAYOUT_A) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)
	@mkdir -p $(BIN_DIR)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) apps/calc.o $(APP_COMMON_OBJ) $(LIBVANILLA_UI_A) $(LIBVANILLALAYOUT_A) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)

$(FILEMGR_BIN): $(CRT0) apps/filemgr.o $(APP_COMMON_OBJ) $(LIBVANILLALAYOUT_A) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)
	@mkdir -p $(BIN_DIR)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) apps/filemgr.o $(APP_COMMON_OBJ) $(LIBVANILLALAYOUT_A) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)

$(SETTINGS_BIN): $(CRT0) apps/settings.o $(APP_COMMON_OBJ) $(LIBVANILLA_UI_A) $(LIBVANILLALAYOUT_A) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)
	@mkdir -p $(BIN_DIR)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) apps/settings.o $(APP_COMMON_OBJ) $(LIBVANILLA_UI_A) $(LIBVANILLALAYOUT_A) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)

$(TASKMGR_BIN): $(CRT0) apps/taskmgr.o $(APP_COMMON_OBJ) $(LIBVANILLALAYOUT_A) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)
	@mkdir -p $(BIN_DIR)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) apps/taskmgr.o $(APP_COMMON_OBJ) $(LIBVANILLALAYOUT_A) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)

$(SHELL_BIN): $(CRT0) shell-client/shell_client.o $(APP_COMMON_OBJ) $(LIBVANILLALAYOUT_A) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)
	@mkdir -p $(BIN_DIR)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) shell-client/shell_client.o $(APP_COMMON_OBJ) $(LIBVANILLALAYOUT_A) $(LIBVANILLA_A) $(LIBM_A) $(LIBC_A)

shell-client/shell_client.test.o: shell-client/shell_client.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DTEST_SHELL_RUNNER -c -o $@ $<

%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

check: all
	@echo "[OK] Project Vanilla Step 4.6 binaries built successfully:"
	@echo "     - lib/libvanilla.a"
	@echo "     - lib/libvanilla-ui.a"
	@echo "     - lib/libvanillalayout.a"
	@echo "     - bin/vanilla_srv.elf"
	@echo "     - bin/test_ipc.elf"
	@echo "     - bin/test_compositor.elf"
	@echo "     - bin/test_typography.elf"
	@echo "     - bin/test_shell.elf"
	@echo "     - bin/test_layout.elf"
	@echo "     - bin/terminal.elf"
	@echo "     - bin/notepad.elf"
	@echo "     - bin/calc.elf"
	@echo "     - bin/filemgr.elf"
	@echo "     - bin/settings.elf"
	@echo "     - bin/taskmgr.elf"
	@echo "     - bin/shell.elf"

clean:
	rm -rf libvanilla/*.o libvanilla-ui/*.o server/*.o tests/*.o apps/*.o input/*.o layout/*.o shell-client/*.o $(BIN_DIR) $(LIB_DIR) host_emulator/*.host.o server/*.host.o libvanilla/*.host.o input/*.host.o host_emulator/bin build

host: gen-protocol
	$(MAKE) -f host_emulator/Makefile.host all

host-run: gen-protocol
	$(MAKE) -f host_emulator/Makefile.host run

host-clean:
	$(MAKE) -f host_emulator/Makefile.host clean

