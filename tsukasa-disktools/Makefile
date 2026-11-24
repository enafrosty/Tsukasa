#
# Project Tsukasa — Storage & Partitioning Suite (tsukasa-disktools) Makefile
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
         -fno-pie -fstack-protector-strong -mstack-protector-guard=global -O2 \
         -I../tsukasa-libc/include -Iinclude

LDFLAGS = -m elf_x86_64 -T ../tsukasa-libc/tsukasa_app.ld -nostdlib

CRT0   = ../tsukasa-libc/crt/crt0.o
LIBC_A = ../tsukasa-libc/lib/libc.a

$(CRT0) $(LIBC_A):
	$(MAKE) TOOLCHAIN=$(TOOLCHAIN) -C ../tsukasa-libc all

BIN_DIR = bin

COMMON_OBJS    = src/common.o src/gpt.o
FAT32_FMT_OBJ  = src/fat32_fmt.o
EXT4_FMT_OBJ   = src/ext4_fmt.o
FDISK_BIN      = $(BIN_DIR)/fdisk.elf
MKFS_FAT32_BIN = $(BIN_DIR)/mkfs.fat32.elf
MKFS_EXT2_BIN  = $(BIN_DIR)/mkfs.ext2.elf
MKFS_EXT4_BIN  = $(BIN_DIR)/mkfs.ext4.elf
INSTALLER_BIN  = $(BIN_DIR)/installer.elf

ALL_BINS = $(FDISK_BIN) $(MKFS_FAT32_BIN) $(MKFS_EXT2_BIN) $(MKFS_EXT4_BIN) $(INSTALLER_BIN)

.PHONY: all clean check host

all: $(ALL_BINS)

$(FDISK_BIN): $(CRT0) src/fdisk.o $(COMMON_OBJS) $(LIBC_A)
	@mkdir -p $(BIN_DIR)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) src/fdisk.o $(COMMON_OBJS) $(LIBC_A)

$(MKFS_FAT32_BIN): $(CRT0) src/mkfs_fat32.o $(FAT32_FMT_OBJ) $(COMMON_OBJS) $(LIBC_A)
	@mkdir -p $(BIN_DIR)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) src/mkfs_fat32.o $(FAT32_FMT_OBJ) $(COMMON_OBJS) $(LIBC_A)

$(MKFS_EXT2_BIN): $(CRT0) src/mkfs_ext2.o $(COMMON_OBJS) $(LIBC_A)
	@mkdir -p $(BIN_DIR)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) src/mkfs_ext2.o $(COMMON_OBJS) $(LIBC_A)

$(MKFS_EXT4_BIN): $(CRT0) src/mkfs_ext4.o $(EXT4_FMT_OBJ) $(COMMON_OBJS) $(LIBC_A)
	@mkdir -p $(BIN_DIR)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) src/mkfs_ext4.o $(EXT4_FMT_OBJ) $(COMMON_OBJS) $(LIBC_A)

$(INSTALLER_BIN): $(CRT0) src/installer.o $(FAT32_FMT_OBJ) $(EXT4_FMT_OBJ) $(COMMON_OBJS) $(LIBC_A)
	@mkdir -p $(BIN_DIR)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) src/installer.o $(FAT32_FMT_OBJ) $(EXT4_FMT_OBJ) $(COMMON_OBJS) $(LIBC_A)

src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

check: all
	@echo "[OK] tsukasa-disktools binaries built successfully:"
	@echo "     - bin/fdisk.elf"
	@echo "     - bin/mkfs.fat32.elf"
	@echo "     - bin/mkfs.ext2.elf"
	@echo "     - bin/mkfs.ext4.elf"
	@echo "     - bin/installer.elf"

clean:
	rm -rf src/*.o $(BIN_DIR)

HOST_CC ?= gcc
HOST_CFLAGS = -Wall -Wextra -O2 -Iinclude

ifeq ($(OS),Windows_NT)
HOST_EXT = .exe
else
HOST_EXT =
endif

host:
	@mkdir -p $(BIN_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -o $(BIN_DIR)/fdisk$(HOST_EXT) src/fdisk.c src/common.c src/gpt.c
	$(HOST_CC) $(HOST_CFLAGS) -o $(BIN_DIR)/mkfs.fat32$(HOST_EXT) src/mkfs_fat32.c src/fat32_fmt.c src/common.c src/gpt.c
	$(HOST_CC) $(HOST_CFLAGS) -o $(BIN_DIR)/mkfs.ext2$(HOST_EXT) src/mkfs_ext2.c src/common.c src/gpt.c
	$(HOST_CC) $(HOST_CFLAGS) -o $(BIN_DIR)/mkfs.ext4$(HOST_EXT) src/mkfs_ext4.c src/ext4_fmt.c src/common.c src/gpt.c
	$(HOST_CC) $(HOST_CFLAGS) -o $(BIN_DIR)/installer$(HOST_EXT) src/installer.c src/fat32_fmt.c src/ext4_fmt.c src/common.c src/gpt.c
	@echo "[OK] Host disktools binaries built in $(BIN_DIR)/"
