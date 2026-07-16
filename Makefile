# Makefile for Tsukasa OS
#
# Architectures:
#   make ARCH=i386 iso      -> legacy GRUB/Multiboot i386 image
#   make ARCH=x86_64 iso    -> Limine x86_64 image

ARCH ?= i386

CC = gcc
ASM = nasm
LD = ld
MAKE = make

ISO_IMAGE = tsukasa.iso
ISO_DIR = iso
BOOT_DIR = $(ISO_DIR)/boot
GRUB_DIR = $(BOOT_DIR)/grub
LIMINE_BOOT_DIR = $(BOOT_DIR)/limine
EFI_BOOT_DIR = $(ISO_DIR)/EFI/BOOT

INITRD_IMG = initrd.img
INITRD_FILES = initrd_files

LIMINE_DIR = .limine
LIMINE_REPO = https://github.com/limine-bootloader/limine.git
LIMINE_BRANCH = v8.x-binary
ARCH_MARKER = .last_build_arch

COMMON_OBJS = vga.o \
    mm/pmm.o mm/heap.o mm/tlsf.o mm/vmm_x64.o mm/vm_space.o \
    drv/fb.o drv/pic.o drv/pit.o drv/ps2kbd.o drv/irq.o drv/ps2mouse.o \
    drv/serial.o drv/ata.o drv/rtc.o \
    input/event.o \
    fs/vfs.o fs/initrd.o fs/fat12.o fs/fat32.o fs/memfs.o fs/procfs.o fs/sysfs.o fs/bootfs.o \
    loader/elf.o loader/exec.o \
    lib/kprintf.o lib/kutils.o lib/compiler_rt.o \
    gfx/blit.o gfx/font.o gfx/font_8x8.o \
    gfx/ui.o gfx/bmp.o \
    gfx/wm.o gfx/cursor.o gfx/gui_srv.o gfx/desktop.o

USER_LIB_OBJS = $(patsubst user/%.c,user/%.o,$(wildcard user/lib/*.c))
USER_APP_OBJS = $(patsubst user/%.c,user/%.o,$(wildcard user/apps/*.c))

X64_NET_OBJS = dev/pci.o \
    net/network.o net/lwip_port.o \
    net/nic/nic.o net/nic/nic_netif.o net/nic/virtio_net.o net/nic/e1000.o \
    net/third_party/lwip/core/def.o \
    net/third_party/lwip/core/dns.o \
    net/third_party/lwip/core/inet_chksum.o \
    net/third_party/lwip/core/init.o \
    net/third_party/lwip/core/ip.o \
    net/third_party/lwip/core/mem.o \
    net/third_party/lwip/core/memp.o \
    net/third_party/lwip/core/netif.o \
    net/third_party/lwip/core/pbuf.o \
    net/third_party/lwip/core/raw.o \
    net/third_party/lwip/core/stats.o \
    net/third_party/lwip/core/sys.o \
    net/third_party/lwip/core/tcp.o \
    net/third_party/lwip/core/tcp_in.o \
    net/third_party/lwip/core/tcp_out.o \
    net/third_party/lwip/core/timeouts.o \
    net/third_party/lwip/core/udp.o \
    net/third_party/lwip/core/ipv4/dhcp.o \
    net/third_party/lwip/core/ipv4/etharp.o \
    net/third_party/lwip/core/ipv4/icmp.o \
    net/third_party/lwip/core/ipv4/ip4.o \
    net/third_party/lwip/core/ipv4/ip4_addr.o \
    net/third_party/lwip/core/ipv4/ip4_frag.o \
    net/third_party/lwip/netif/ethernet.o

ifeq ($(ARCH),x86_64)
KERNEL_BIN = tsukasa_x64.elf
CFLAGS = -m64 -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
         -ffreestanding -fno-pie -fno-stack-protector -Wall -Wextra -O2 \
         -DTSUKASA_USERLIB_KERNEL \
         -I. -Iinclude -Iarch/x86_64 -Inet -Inet/lwip_arch \
         -Inet/third_party/lwip
ASMFLAGS = -f elf64
LDFLAGS = -m elf_x86_64 -T linker_x86_64.ld -nostdlib --build-id=none -z max-page-size=0x1000
OBJS = arch/x86_64/boot/entry.o \
       arch/x86_64/boot/boot_info.o \
       arch/x86_64/kernel_main.o \
       arch/x86_64/cpu/gdt.o arch/x86_64/cpu/idt.o arch/x86_64/cpu/isr.o \
       drv/lapic.o sys/smp.o \
       proc/process.o proc/scheduler.o proc/signal.o \
       tty/tty.o \
       syscall/syscall.o \
       ipc/shm.o \
       $(USER_LIB_OBJS) $(USER_APP_OBJS) \
       $(COMMON_OBJS) $(X64_NET_OBJS)
ISO_TARGET = iso-x86_64
else
KERNEL_BIN = tsukasa.bin
CFLAGS = -m32 -ffreestanding -fno-pie -fno-stack-protector -Wall -Wextra -O2 -I. -Iinclude
ASMFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib --build-id=none
OBJS = boot.o isr.o idt.o kernel.o \
       mm/gdt.o \
       mm/paging.o \
       proc/task.o proc/scheduler.o proc/context.o proc/switch.o \
       syscall/syscall.o syscall/syscall_entry.o \
       ipc/shm.o user/user_stub.o \
       $(COMMON_OBJS)
ISO_TARGET = iso-i386
endif

.PHONY: all iso initrd clean check-multiboot iso-i386 iso-x86_64 limine-artifacts arch-guard

all: arch-guard $(KERNEL_BIN)

iso: arch-guard $(ISO_TARGET)

arch-guard:
	@if [ -f $(ARCH_MARKER) ] && [ "$$(cat $(ARCH_MARKER))" != "$(ARCH)" ]; then \
	    echo "[build] ARCH switch detected: $$(cat $(ARCH_MARKER)) -> $(ARCH). Purging stale objects."; \
	    find . -name '*.o' -delete; \
	fi
	@echo "$(ARCH)" > $(ARCH_MARKER)

%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.asm
	@mkdir -p $(dir $@)
	$(ASM) $(ASMFLAGS) -o $@ $<

$(KERNEL_BIN): arch-guard $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(INITRD_IMG):
	@mkdir -p $(INITRD_FILES)
	dd if=/dev/zero of=$(INITRD_IMG) bs=1024 count=2880
	mkfs.fat -F 12 $(INITRD_IMG)
	@if [ -n "$$(ls -A $(INITRD_FILES) 2>/dev/null)" ]; then \
	    mcopy -i $(INITRD_IMG) $(INITRD_FILES)/* ::; \
	fi
	@echo "[OK] $(INITRD_IMG) ready (FAT12, 1.44 MB)."

initrd: $(INITRD_IMG)

check-multiboot: $(KERNEL_BIN)
	@if [ "$(ARCH)" = "i386" ]; then \
	    python3 check_multiboot.py $(KERNEL_BIN) 2>/dev/null || \
	    python  check_multiboot.py $(KERNEL_BIN) || true; \
	fi

iso-i386: $(KERNEL_BIN) check-multiboot
	@mkdir -p $(GRUB_DIR)
	cp $(KERNEL_BIN) $(BOOT_DIR)/
	cp grub.cfg $(GRUB_DIR)/
	@if [ -f $(INITRD_IMG) ]; then \
	    cp $(INITRD_IMG) $(BOOT_DIR)/; \
	    echo "[OK] initrd.img included in ISO."; \
	else \
	    echo "[WARN] No initrd.img found. Continuing without optional FAT12 compatibility ramdisk."; \
	fi
	grub-mkrescue -o $(ISO_IMAGE) $(ISO_DIR)
	@echo "[OK] $(ISO_IMAGE) ready (i386/GRUB)."

$(LIMINE_DIR)/limine:
	@if [ ! -d $(LIMINE_DIR) ]; then \
	    git clone --depth=1 --branch=$(LIMINE_BRANCH) $(LIMINE_REPO) $(LIMINE_DIR); \
	fi
	$(MAKE) -C $(LIMINE_DIR)

limine-artifacts: $(LIMINE_DIR)/limine

iso-x86_64: $(KERNEL_BIN) limine-artifacts
	@mkdir -p $(BOOT_DIR) $(LIMINE_BOOT_DIR) $(EFI_BOOT_DIR)
	cp $(KERNEL_BIN) $(BOOT_DIR)/tsukasa_x64.elf
	cp limine.conf $(ISO_DIR)/
	@if [ -f $(INITRD_IMG) ]; then \
	    cp $(INITRD_IMG) $(BOOT_DIR)/; \
	    echo "[OK] initrd.img included in ISO."; \
	else \
	    echo "[WARN] No initrd.img found. Continuing without optional FAT12 compatibility ramdisk."; \
	fi
	cp $(LIMINE_DIR)/limine-bios.sys $(LIMINE_BOOT_DIR)/
	cp $(LIMINE_DIR)/limine-bios-cd.bin $(LIMINE_BOOT_DIR)/
	cp $(LIMINE_DIR)/limine-uefi-cd.bin $(LIMINE_BOOT_DIR)/
	cp $(LIMINE_DIR)/BOOTX64.EFI $(EFI_BOOT_DIR)/
	cp $(LIMINE_DIR)/BOOTIA32.EFI $(EFI_BOOT_DIR)/
	xorriso -as mkisofs \
	    -b boot/limine/limine-bios-cd.bin \
	    -no-emul-boot -boot-load-size 4 -boot-info-table \
	    --efi-boot boot/limine/limine-uefi-cd.bin \
	    -efi-boot-part --efi-boot-image --protective-msdos-label \
	    $(ISO_DIR) -o $(ISO_IMAGE)
	$(LIMINE_DIR)/limine bios-install $(ISO_IMAGE)
	@echo "[OK] $(ISO_IMAGE) ready (x86_64/Limine)."

clean:
	find . -name '*.o' -delete
	rm -f tsukasa.bin tsukasa_x64.elf $(ISO_IMAGE) $(INITRD_IMG) $(ARCH_MARKER)
	rm -rf $(ISO_DIR)
