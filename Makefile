# Makefile for minimal x86 OS (Multiboot + GRUB).
# Usage: make iso   -> produces myos.iso

CC       = gcc
ASM      = nasm
LD       = ld
CFLAGS   = -m32 -ffreestanding -fno-pie -fno-stack-protector -Wall -Wextra -O2 -I.
ASMFLAGS = -f elf32
LDFLAGS  = -m elf_i386 -T linker.ld -nostdlib

KERNEL_BIN = myos.bin
ISO_IMAGE  = myos.iso
ISO_DIR    = iso
BOOT_DIR   = $(ISO_DIR)/boot
GRUB_DIR   = $(BOOT_DIR)/grub

OBJS = boot.o isr.o idt.o vga.o kernel.o mm/pmm.o mm/paging.o mm/heap.o mm/gdt.o \
       proc/task.o proc/scheduler.o proc/context.o proc/switch.o \
       syscall/syscall.o syscall/syscall_entry.o ipc/shm.o user/user_stub.o drv/fb.o drv/pic.o drv/ps2kbd.o drv/irq.o input/event.o fs/vfs.o fs/initrd.o loader/elf.o gfx/blit.o gfx/font.o gfx/font_8x8.o

.PHONY: all iso clean

all: $(KERNEL_BIN)

# Compile C sources.
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Assemble .s sources (GAS syntax; boot.s and isr.s).
%.o: %.s
	$(CC) $(CFLAGS) -c -o $@ $<

# Link kernel.
$(KERNEL_BIN): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

# Build bootable ISO using grub-mkrescue.
iso: $(KERNEL_BIN)
	@mkdir -p $(GRUB_DIR)
	cp $(KERNEL_BIN) $(BOOT_DIR)/
	cp grub.cfg $(GRUB_DIR)/
	grub-mkrescue -o $(ISO_IMAGE) $(ISO_DIR)
	@echo "[OK] $(ISO_IMAGE) ready. Load in VirtualBox as Other -> Other/Unknown."

clean:
	rm -f $(OBJS) $(KERNEL_BIN) $(ISO_IMAGE)
	rm -rf $(ISO_DIR)
