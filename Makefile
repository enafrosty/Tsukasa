# Makefile for Tsukasa OS
#
# Architectures:
#   make ARCH=i386 iso      -> legacy GRUB/Multiboot i386 image
#   make ARCH=x86_64 iso    -> Limine x86_64 image

ARCH ?= x86_64

# Toolchains:
#   make TOOLCHAIN=llvm  (default) -> clang/ld.lld targeting bare-metal ELF
#                                     (works natively on Windows via MSYS2)
#   make TOOLCHAIN=gcc             -> host gcc/ld cross-capable of ELF (WSL/Linux)
TOOLCHAIN ?= llvm

ifeq ($(TOOLCHAIN),llvm)
CLANG_TARGET = $(if $(filter x86_64,$(ARCH)),x86_64-unknown-elf,i386-unknown-elf)
CC = clang --target=$(CLANG_TARGET)
LD = ld.lld
else
CC = gcc
LD = ld
endif

ASM = nasm
MAKE ?= $(MAKE)

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
    mm/pmm.o mm/heap.o mm/slab.o mm/tlsf.o mm/vmm_x64.o mm/vm_space.o \
    drv/fb.o drv/pic.o drv/pit.o drv/ps2kbd.o drv/irq.o drv/ps2mouse.o \
    drv/serial.o drv/ata.o drv/blockdev.o drv/diskmgr.o drv/rtc.o \
    input/event.o drv/input_dev.o \
    fs/vfs.o fs/devfs.o fs/initrd.o fs/fat12.o fs/fat32.o fs/mkfs_fat32.o fs/memfs.o fs/procfs.o fs/sysfs.o fs/bootfs.o \
    fs/tar.o fs/tar_testdata.o \
    loader/elf.o loader/exec.o loader/elf64.o \
    lib/kprintf.o lib/kutils.o lib/compiler_rt.o lib/ksymbols.o lib/backtrace.o \
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

# x64-only storage drivers (PCI-based; i386 keeps ATA PIO only).
X64_STORAGE_OBJS =

ifeq ($(ARCH),x86_64)
KERNEL_BIN = tsukasa_x64.elf
CFLAGS = -m64 -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
         -ffreestanding -fno-pie -fno-stack-protector -Wall -Wextra -O2 \
         -fno-omit-frame-pointer \
         -DTSUKASA_USERLIB_KERNEL \
         -I. -Iinclude -Iarch/x86_64 -Inet -Inet/lwip_arch \
         -Inet/third_party/lwip
ASMFLAGS = -f elf64
LDFLAGS = -m elf_x86_64 -T linker_x86_64.ld -nostdlib --build-id=none -z max-page-size=0x1000
OBJS = arch/x86_64/boot/entry.o \
       arch/x86_64/boot/boot_info.o \
       arch/x86_64/kernel_main.o \
       arch/x86_64/cpu/gdt.o arch/x86_64/cpu/idt.o arch/x86_64/cpu/isr.o \
       arch/x86_64/cpu/syscall_entry.o arch/x86_64/cpu/syscall_init.o \
       drv/acpi.o drv/ioapic.o drv/lapic.o sys/smp.o sys/wait_queue.o sys/work_queue.o \
       sys/futex.o sys/installer.o \
       sys/panic.o sys/kconsole.o \
       proc/process.o proc/scheduler.o proc/signal.o \
       tty/tty.o \
       syscall/syscall.o syscall/syscall_table.o \
       ipc/shm.o ipc/unix_socket.o \
       $(USER_LIB_OBJS) $(USER_APP_OBJS) \
       $(COMMON_OBJS) $(X64_NET_OBJS) $(X64_STORAGE_OBJS)
ifeq ($(GDBSTUB),1)
CFLAGS += -DCONFIG_GDBSTUB=1
OBJS += sys/gdbstub.o
endif
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
       ipc/shm.o ipc/unix_socket.o user/user_stub.o \
       $(COMMON_OBJS)
ISO_TARGET = iso-i386
endif

.PHONY: all iso initrd clean check-multiboot iso-i386 iso-x86_64 limine-artifacts arch-guard sdk apps vanilla coreutils disktools ports check libc-tests

all: arch-guard $(KERNEL_BIN)

iso: arch-guard $(ISO_TARGET)

arch-guard:
	@if [ -f $(ARCH_MARKER) ]; then \
	    PREV_ARCH=$$(awk '{print $$1}' $(ARCH_MARKER)); \
	    PREV_GDB=$$(awk '{print $$2}' $(ARCH_MARKER)); \
	    if [ "$$PREV_ARCH" != "$(ARCH)" ]; then \
	        echo "[build] ARCH switch detected: $$PREV_ARCH -> $(ARCH). Purging stale objects."; \
	        find . -name '*.o' -delete; \
	    elif [ "$$PREV_GDB" != "gdbstub=$(GDBSTUB)" ]; then \
	        echo "[build] GDBSTUB switch detected: $$PREV_GDB -> gdbstub=$(GDBSTUB). Purging stale objects."; \
	        find arch sys -name '*.o' -delete; \
	    fi; \
	fi
	@echo "$(ARCH) gdbstub=$(GDBSTUB)" > $(ARCH_MARKER)

%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.asm
	@mkdir -p $(dir $@)
	$(ASM) $(ASMFLAGS) -o $@ $<

lib/ksymbols_table.c:
	@mkdir -p lib
	@echo '/* Initial stub */' > $@
	@echo '#include "include/ksymbols.h"' >> $@
	@echo 'const struct ksym ksym_table[] = {};' >> $@
	@echo 'const unsigned long ksym_count = 0;' >> $@

lib/ksymbols_table.o: lib/ksymbols_table.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

ifeq ($(ARCH),x86_64)
$(KERNEL_BIN): arch-guard $(OBJS) lib/ksymbols_table.o
	$(LD) $(LDFLAGS) -o $@ $(OBJS) lib/ksymbols_table.o
	bash scripts/build/gen-ksymbols.sh $@ lib/ksymbols_table.c
	$(CC) $(CFLAGS) -c -o lib/ksymbols_table.o lib/ksymbols_table.c
	$(LD) $(LDFLAGS) -o $@ $(OBJS) lib/ksymbols_table.o
else
$(KERNEL_BIN): arch-guard $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
endif

$(INITRD_FILES)/hello.elf: user_elf/hello64.asm
	@mkdir -p $(INITRD_FILES)
	nasm -f elf64 -o user_elf/hello64.o user_elf/hello64.asm
	ld.lld -m elf_x86_64 -e _start --image-base=0x400000 -o $@ user_elf/hello64.o

$(INITRD_FILES)/hello_sc.elf: user_elf/hello_syscall64.asm
	@mkdir -p $(INITRD_FILES)
	nasm -f elf64 -o user_elf/hello_syscall64.o user_elf/hello_syscall64.asm
	ld.lld -m elf_x86_64 -e _start --image-base=0x400000 -o $@ user_elf/hello_syscall64.o

$(INITRD_FILES)/fault.elf: user_elf/fault64.asm
	@mkdir -p $(INITRD_FILES)
	nasm -f elf64 -o user_elf/fault64.o user_elf/fault64.asm
	ld.lld -m elf_x86_64 -e _start --image-base=0x400000 -o $@ user_elf/fault64.o

$(INITRD_FILES)/exit7.elf: user_elf/exit7.asm
	@mkdir -p $(INITRD_FILES)
	nasm -f elf64 -o user_elf/exit7.o user_elf/exit7.asm
	ld.lld -m elf_x86_64 -e _start --image-base=0x400000 -o $@ user_elf/exit7.o


SDK_DIR    = sdk
SDK_CC     = clang --target=x86_64-unknown-elf

SDK_CFLAGS = -ffreestanding -fno-pie -fno-stack-protector -mno-red-zone \
             -mno-mmx -mno-sse -mno-sse2 \
             -O2 -Wall -Wextra -Iuser/crt
SDK_OBJS   = user/crt/crt0.o user/crt/syscalls.o user/crt/stdio_lite.o

user/crt/crt0.o: user/crt/crt0.asm
	nasm -f elf64 -o $@ $<

user/crt/syscalls.o: user/crt/syscalls.c user/crt/tsukasa_sdk.h
	$(SDK_CC) $(SDK_CFLAGS) -c -o $@ $<

user/crt/stdio_lite.o: user/crt/stdio_lite.c user/crt/tsukasa_sdk.h
	$(SDK_CC) $(SDK_CFLAGS) -c -o $@ $<


$(INITRD_FILES)/%.elf: user/cli/%.c $(SDK_OBJS) user/crt/tsukasa_app.ld
	@mkdir -p $(INITRD_FILES)
	$(SDK_CC) $(SDK_CFLAGS) -c -o user/cli/$*.o $<
	ld.lld -m elf_x86_64 -T user/crt/tsukasa_app.ld -nostdlib \
	    -o $@ user/crt/crt0.o user/cli/$*.o user/crt/syscalls.o user/crt/stdio_lite.o

$(INITRD_FILES)/hellosdk.elf: user/examples/hello.c $(SDK_OBJS) user/crt/tsukasa_app.ld
	@mkdir -p $(INITRD_FILES)
	$(SDK_CC) $(SDK_CFLAGS) -c -o user/examples/hello.o user/examples/hello.c
	ld.lld -m elf_x86_64 -T user/crt/tsukasa_app.ld -nostdlib \
	    -o $@ user/crt/crt0.o user/examples/hello.o user/crt/syscalls.o

TK_OBJS = user/tk/tk_client.sdk.o user/tk/tk_painter.sdk.o user/tk/tk_app.sdk.o \
          user/lib/libwidget.sdk.o gfx/font_8x8.sdk.o

user/tk/%.sdk.o: user/tk/%.c user/crt/tsukasa_sdk.h
	$(SDK_CC) $(SDK_CFLAGS) -c -o $@ $<

user/lib/libwidget.sdk.o: user/lib/libwidget.c user/include/libwidget.h
	$(SDK_CC) $(SDK_CFLAGS) -c -o $@ $<

gfx/font_8x8.sdk.o: gfx/font_8x8.c gfx/font_8x8.h
	$(SDK_CC) $(SDK_CFLAGS) -c -o $@ $<

$(INITRD_FILES)/wdemo16.elf: user/cli/wdemo16.c $(SDK_OBJS) $(TK_OBJS) user/crt/tsukasa_app.ld
	@mkdir -p $(INITRD_FILES)
	$(SDK_CC) $(SDK_CFLAGS) -c -o user/cli/wdemo16.o $<
	ld.lld -m elf_x86_64 -T user/crt/tsukasa_app.ld -nostdlib \
	    -o $@ user/crt/crt0.o user/cli/wdemo16.o $(TK_OBJS) user/crt/syscalls.o user/crt/stdio_lite.o

vanilla:
	$(MAKE) -C vanilla all

$(INITRD_FILES)/VSRV.ELF: vanilla
	@mkdir -p $(INITRD_FILES)
	cp vanilla/bin/vanilla_srv.elf $@

$(INITRD_FILES)/TESTIPC.ELF: vanilla
	@mkdir -p $(INITRD_FILES)
	cp vanilla/bin/test_ipc.elf $@

$(INITRD_FILES)/TESTCOMP.ELF: vanilla
	@mkdir -p $(INITRD_FILES)
	cp vanilla/bin/test_compositor.elf $@

$(INITRD_FILES)/TESTTYPO.ELF: vanilla
	@mkdir -p $(INITRD_FILES)
	cp vanilla/bin/test_typography.elf $@

$(INITRD_FILES)/TESTSHELL.ELF: vanilla
	@mkdir -p $(INITRD_FILES)
	cp vanilla/bin/test_shell.elf $@

$(INITRD_FILES)/TSHELL.ELF: vanilla
	@mkdir -p $(INITRD_FILES)
	cp vanilla/bin/test_shell.elf $@

$(INITRD_FILES)/TERMINAL.ELF: vanilla
	@mkdir -p $(INITRD_FILES)
	cp vanilla/bin/terminal.elf $@

$(INITRD_FILES)/NOTEPAD.ELF: vanilla
	@mkdir -p $(INITRD_FILES)
	cp vanilla/bin/notepad.elf $@

$(INITRD_FILES)/CALC.ELF: vanilla
	@mkdir -p $(INITRD_FILES)
	cp vanilla/bin/calc.elf $@

$(INITRD_FILES)/FILEMGR.ELF: vanilla
	@mkdir -p $(INITRD_FILES)
	cp vanilla/bin/filemgr.elf $@

$(INITRD_FILES)/SETTINGS.ELF: vanilla
	@mkdir -p $(INITRD_FILES)
	cp vanilla/bin/settings.elf $@

$(INITRD_FILES)/TASKMGR.ELF: vanilla
	@mkdir -p $(INITRD_FILES)
	cp vanilla/bin/taskmgr.elf $@

coreutils:
	$(MAKE) -C tsukasa-coreutils all

$(INITRD_FILES)/TSH.ELF: coreutils
	@mkdir -p $(INITRD_FILES)
	cp tsukasa-coreutils/bin/tsh.elf $@

disktools:
	$(MAKE) -C tsukasa-disktools all

$(INITRD_FILES)/FDISK.ELF: disktools
	@mkdir -p $(INITRD_FILES)
	cp tsukasa-disktools/bin/fdisk.elf $@

$(INITRD_FILES)/MKFSFAT.ELF: disktools
	@mkdir -p $(INITRD_FILES)
	cp tsukasa-disktools/bin/mkfs.fat32.elf $@

$(INITRD_FILES)/MKFSEXT2.ELF: disktools
	@mkdir -p $(INITRD_FILES)
	cp tsukasa-disktools/bin/mkfs.ext2.elf $@

$(INITRD_FILES)/INSTALL.ELF: disktools
	@mkdir -p $(INITRD_FILES)
	cp tsukasa-disktools/bin/installer.elf $@
 
ports:
	$(MAKE) -C tsukasa-ports all

$(INITRD_FILES)/DOOM.ELF: ports
	@mkdir -p $(INITRD_FILES)
	cp tsukasa-ports/staging/bin/doomgeneric.elf $@

$(INITRD_FILES)/DOOM1.WAD: ports
	@mkdir -p $(INITRD_FILES)
	cp tsukasa-ports/staging/usr/share/doom/doom1.wad $@

$(INITRD_FILES)/TCC.ELF: ports
	@mkdir -p $(INITRD_FILES)
	cp tsukasa-ports/staging/bin/tcc.elf $@

$(INITRD_FILES)/LUA.ELF: ports
	@mkdir -p $(INITRD_FILES)
	cp tsukasa-ports/staging/bin/lua.elf $@

$(INITRD_FILES)/LUAC.ELF: ports
	@mkdir -p $(INITRD_FILES)
	cp tsukasa-ports/staging/bin/luac.elf $@

libc-tests:
	$(MAKE) -C tsukasa-libc all check

$(INITRD_FILES)/TCRT0.ELF: libc-tests
	@mkdir -p $(INITRD_FILES)
	cp tsukasa-libc/tests/test_crt0.elf $@

$(INITRD_FILES)/TSYSC.ELF: libc-tests
	@mkdir -p $(INITRD_FILES)
	cp tsukasa-libc/tests/test_syscalls.elf $@

$(INITRD_FILES)/TLIBC.ELF: libc-tests
	@mkdir -p $(INITRD_FILES)
	cp tsukasa-libc/tests/test_libc.elf $@

$(INITRD_FILES)/TMATH.ELF: libc-tests
	@mkdir -p $(INITRD_FILES)
	cp tsukasa-libc/tests/test_math.elf $@

$(INITRD_FILES)/TSTDIO.ELF: libc-tests
	@mkdir -p $(INITRD_FILES)
	cp tsukasa-libc/tests/test_stdio.elf $@

$(INITRD_FILES)/TESTDRV.ELF: user/cli/testdrv.c $(SDK_OBJS) user/crt/tsukasa_app.ld
	@mkdir -p $(INITRD_FILES)
	$(SDK_CC) $(SDK_CFLAGS) -c -o user/cli/testdrv.o $<
	ld.lld -m elf_x86_64 -T user/crt/tsukasa_app.ld -nostdlib \
	    -o $@ user/crt/crt0.o user/cli/testdrv.o user/crt/syscalls.o user/crt/stdio_lite.o

apps: $(INITRD_FILES)/hellosdk.elf $(INITRD_FILES)/echo.elf $(INITRD_FILES)/cat.elf $(INITRD_FILES)/argvchk.elf $(INITRD_FILES)/futex10.elf $(INITRD_FILES)/usock20.elf $(INITRD_FILES)/wsrv15.elf $(INITRD_FILES)/wapp15.elf $(INITRD_FILES)/wdemo16.elf $(INITRD_FILES)/VSRV.ELF $(INITRD_FILES)/TESTIPC.ELF $(INITRD_FILES)/TESTCOMP.ELF $(INITRD_FILES)/TESTTYPO.ELF $(INITRD_FILES)/TESTSHELL.ELF $(INITRD_FILES)/TSHELL.ELF $(INITRD_FILES)/TERMINAL.ELF $(INITRD_FILES)/NOTEPAD.ELF $(INITRD_FILES)/CALC.ELF $(INITRD_FILES)/FILEMGR.ELF $(INITRD_FILES)/SETTINGS.ELF $(INITRD_FILES)/TASKMGR.ELF $(INITRD_FILES)/TSH.ELF $(INITRD_FILES)/FDISK.ELF $(INITRD_FILES)/MKFSFAT.ELF $(INITRD_FILES)/MKFSEXT2.ELF $(INITRD_FILES)/INSTALL.ELF $(INITRD_FILES)/DOOM.ELF $(INITRD_FILES)/DOOM1.WAD $(INITRD_FILES)/TCC.ELF $(INITRD_FILES)/LUA.ELF $(INITRD_FILES)/LUAC.ELF $(INITRD_FILES)/TCRT0.ELF $(INITRD_FILES)/TSYSC.ELF $(INITRD_FILES)/TLIBC.ELF $(INITRD_FILES)/TMATH.ELF $(INITRD_FILES)/TSTDIO.ELF $(INITRD_FILES)/TESTDRV.ELF

sdk: $(SDK_OBJS) user/crt/tsukasa_app.ld user/crt/tsukasa_sdk.h
	@mkdir -p $(SDK_DIR)/include $(SDK_DIR)/lib $(SDK_DIR)/crt
	cp user/crt/tsukasa_sdk.h $(SDK_DIR)/include/
	cp user/crt/crt0.o user/crt/tsukasa_app.ld $(SDK_DIR)/crt/
	cp user/crt/syscalls.o $(SDK_DIR)/lib/
	@echo "[OK] SDK staged in $(SDK_DIR)/ (crt0.o, tsukasa_app.ld, syscalls.o, tsukasa_sdk.h)."

$(INITRD_IMG): $(INITRD_FILES)/hello.elf $(INITRD_FILES)/hello_sc.elf $(INITRD_FILES)/fault.elf $(INITRD_FILES)/exit7.elf $(INITRD_FILES)/hellosdk.elf $(INITRD_FILES)/echo.elf $(INITRD_FILES)/cat.elf $(INITRD_FILES)/argvchk.elf $(INITRD_FILES)/futex10.elf $(INITRD_FILES)/usock20.elf $(INITRD_FILES)/wsrv15.elf $(INITRD_FILES)/wapp15.elf $(INITRD_FILES)/wdemo16.elf $(INITRD_FILES)/VSRV.ELF $(INITRD_FILES)/TESTIPC.ELF $(INITRD_FILES)/TESTCOMP.ELF $(INITRD_FILES)/TESTTYPO.ELF $(INITRD_FILES)/TESTSHELL.ELF $(INITRD_FILES)/TSHELL.ELF $(INITRD_FILES)/TERMINAL.ELF $(INITRD_FILES)/NOTEPAD.ELF $(INITRD_FILES)/CALC.ELF $(INITRD_FILES)/FILEMGR.ELF $(INITRD_FILES)/SETTINGS.ELF $(INITRD_FILES)/TASKMGR.ELF $(INITRD_FILES)/TSH.ELF $(INITRD_FILES)/FDISK.ELF $(INITRD_FILES)/MKFSFAT.ELF $(INITRD_FILES)/MKFSEXT2.ELF $(INITRD_FILES)/INSTALL.ELF $(INITRD_FILES)/DOOM.ELF $(INITRD_FILES)/DOOM1.WAD $(INITRD_FILES)/TCC.ELF $(INITRD_FILES)/LUA.ELF $(INITRD_FILES)/LUAC.ELF $(INITRD_FILES)/TCRT0.ELF $(INITRD_FILES)/TSYSC.ELF $(INITRD_FILES)/TLIBC.ELF $(INITRD_FILES)/TMATH.ELF $(INITRD_FILES)/TSTDIO.ELF $(INITRD_FILES)/TESTDRV.ELF

	@mkdir -p $(INITRD_FILES)
	dd if=/dev/zero of=$(INITRD_IMG) bs=1024 count=16384
	mkfs.fat -F 12 -s 16 $(INITRD_IMG)
	@if [ -n "$$(ls -A $(INITRD_FILES) 2>/dev/null)" ]; then \
	    mcopy -i $(INITRD_IMG) $(INITRD_FILES)/* ::; \
	fi
	@echo "[OK] $(INITRD_IMG) ready (FAT12, 16 MB)."

initrd: $(INITRD_IMG)

check:
	@scripts/test/run-host-tests.sh
	@scripts/test/run-guest-tests.sh
	@scripts/test/summarize.sh

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

bootblob.bin: limine-artifacts
	dd if=/dev/zero of=.bootblob-template.img bs=512 count=8192 2>/dev/null
	printf '\200\000\002\000\014\377\377\377\000\010\000\000\000\030\000\000' | \
	    dd of=.bootblob-template.img bs=1 seek=446 conv=notrunc 2>/dev/null
	printf '\125\252' | dd of=.bootblob-template.img bs=1 seek=510 conv=notrunc 2>/dev/null
	$(LIMINE_DIR)/limine bios-install .bootblob-template.img
	dd if=.bootblob-template.img of=$@ bs=512 count=2048 2>/dev/null
	rm -f .bootblob-template.img

iso-x86_64: $(KERNEL_BIN) limine-artifacts bootblob.bin $(INITRD_IMG)
	@mkdir -p $(BOOT_DIR) $(LIMINE_BOOT_DIR) $(EFI_BOOT_DIR)
	cp $(KERNEL_BIN) $(BOOT_DIR)/tsukasa_x64.elf
	cp bootblob.bin $(BOOT_DIR)/
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

qemu run: iso-x86_64
	@if [ ! -f disk.img ]; then \
	    echo "[*] Creating 64MB disk.img..."; \
	    qemu-img create -f raw disk.img 64M 2>/dev/null || dd if=/dev/zero of=disk.img bs=1M count=64 2>/dev/null; \
	fi
	qemu-system-x86_64 -cdrom $(ISO_IMAGE) -hda disk.img -boot d -m 256 -smp 2 -vga std -serial stdio -netdev user,id=u1 -device e1000,netdev=u1


clean:
	find . -name '*.o' -delete
	rm -f tsukasa.bin tsukasa_x64.elf $(ISO_IMAGE) $(INITRD_IMG) $(ARCH_MARKER) lib/ksymbols_table.c
	rm -rf $(ISO_DIR) $(SDK_DIR) $(INITRD_FILES)
	$(MAKE) -C vanilla clean
	$(MAKE) -C tsukasa-disktools clean
