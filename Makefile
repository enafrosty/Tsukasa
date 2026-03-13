# Makefile for Tsukasa OS (Multiboot + GRUB).
# Usage: make iso   -> produces tsukasa.iso
#        make initrd -> produces initrd.img (FAT12 ramdisk)
#        make clean  -> removes all build artifacts

CC       = gcc
ASM      = nasm
LD       = ld
CFLAGS   = -m32 -ffreestanding -fno-pie -fno-stack-protector -Wall -Wextra -O2 -I. -Iinclude
ASMFLAGS = -f elf32
LDFLAGS  = -m elf_i386 -T linker.ld -nostdlib --build-id=none

KERNEL_BIN = tsukasa.bin
ISO_IMAGE  = tsukasa.iso
ISO_DIR    = iso
BOOT_DIR   = $(ISO_DIR)/boot
GRUB_DIR   = $(BOOT_DIR)/grub

INITRD_IMG   = initrd.img
INITRD_FILES = initrd_files

OBJS = boot.o isr.o idt.o vga.o kernel.o \
       mm/pmm.o mm/paging.o mm/heap.o mm/gdt.o \
       proc/task.o proc/scheduler.o proc/context.o proc/switch.o \
       syscall/syscall.o syscall/syscall_entry.o \
       ipc/shm.o user/user_stub.o \
       drv/fb.o drv/pic.o drv/ps2kbd.o drv/irq.o drv/ps2mouse.o \
       input/event.o \
       fs/vfs.o fs/initrd.o fs/fat12.o fs/memfs.o \
       loader/elf.o \
       gfx/blit.o gfx/font.o gfx/font_8x8.o \
       gfx/ui.o gfx/bmp.o \
       gfx/wm.o gfx/cursor.o gfx/desktop.o \
       gfx/apps/notepad.o gfx/apps/about.o gfx/apps/calc.o \
       gfx/apps/terminal.o gfx/apps/filemgr.o gfx/apps/settings.o

.PHONY: all iso initrd clean

all: $(KERNEL_BIN)

# ---------------------------------------------------------------------------
# Kernel compilation
# ---------------------------------------------------------------------------

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.s
	$(CC) $(CFLAGS) -c -o $@ $<

$(KERNEL_BIN): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

# ---------------------------------------------------------------------------
# FAT12 ramdisk (requires dosfstools: mformat, mcopy)
# Run this in WSL or Linux.
# Place files you want on the disk under initrd_files/.
# ---------------------------------------------------------------------------

$(INITRD_IMG):
	@mkdir -p $(INITRD_FILES)
	dd if=/dev/zero of=$(INITRD_IMG) bs=1024 count=2880
	mkfs.fat -F 12 $(INITRD_IMG)
	@if [ -n "$$(ls -A $(INITRD_FILES) 2>/dev/null)" ]; then \
	    mcopy -i $(INITRD_IMG) $(INITRD_FILES)/* ::; \
	fi
	@echo "[OK] $(INITRD_IMG) ready (FAT12, 1.44 MB)."

initrd: $(INITRD_IMG)

# ---------------------------------------------------------------------------
# Bootable ISO
# ---------------------------------------------------------------------------

check-multiboot: $(KERNEL_BIN)
	@python3 check_multiboot.py $(KERNEL_BIN) 2>/dev/null || \
	 python  check_multiboot.py $(KERNEL_BIN) || true

iso: $(KERNEL_BIN) check-multiboot
	@mkdir -p $(GRUB_DIR)
	cp $(KERNEL_BIN) $(BOOT_DIR)/
	cp grub.cfg $(GRUB_DIR)/
	@if [ -f $(INITRD_IMG) ]; then \
	    cp $(INITRD_IMG) $(BOOT_DIR)/; \
	    echo "[OK] initrd.img included in ISO."; \
	else \
	    echo "[WARN] No initrd.img found. Run 'make initrd' first for FAT12 support."; \
	fi
	grub-mkrescue -o $(ISO_IMAGE) $(ISO_DIR)
	@echo "[OK] $(ISO_IMAGE) ready."

# ---------------------------------------------------------------------------
# Clean
# ---------------------------------------------------------------------------

clean:
	rm -f $(OBJS) $(KERNEL_BIN) $(ISO_IMAGE) $(INITRD_IMG)
	rm -rf $(ISO_DIR)
