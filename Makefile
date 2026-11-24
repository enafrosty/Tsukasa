# Makefile for Tsukasa OS
# Single architecture: x86_64.

# Toolchains:
#   make TOOLCHAIN=llvm  (default) -> clang/ld.lld targeting bare-metal ELF
#                                     (works natively on Windows via MSYS2)
#   make TOOLCHAIN=gcc             -> host gcc/ld cross-capable of ELF (WSL/Linux)
TOOLCHAIN ?= llvm
export TOOLCHAIN

ifeq ($(TOOLCHAIN),llvm)
CC = clang --target=x86_64-unknown-elf
LD = ld.lld
USER_ELF_LDFLAGS = -m elf_x86_64 -e _start --image-base=0x400000
else
CC = gcc
LD = ld
USER_ELF_LDFLAGS = -m elf_x86_64 -e _start -Ttext=0x400000 -z max-page-size=0x1000
endif

ASM = nasm
MAKE ?= $(MAKE)

ISO_IMAGE = tsukasa.iso
ISO_DIR = iso
BOOT_DIR = $(ISO_DIR)/boot
LIMINE_BOOT_DIR = $(BOOT_DIR)/limine
EFI_BOOT_DIR = $(ISO_DIR)/EFI/BOOT

INITRD_DIR = build/initrd
INITRD_BIN = $(INITRD_DIR)/bin
INITRD_TAR = build/initrd.tar
INITRD_IMG = build/initrd.tar.lz4
HOST_CC ?= cc
LZ4PACK = build/tools/lz4pack

LIMINE_DIR = .limine
LIMINE_REPO = https://github.com/limine-bootloader/limine.git
LIMINE_BRANCH = v8.x-binary

COMMON_OBJS = vga.o \
    mm/pmm.o mm/heap.o mm/slab.o mm/tlsf.o mm/vmm_x64.o mm/vm_space.o mm/pagecache.o \
    drv/fb.o drv/pic.o drv/pit.o drv/ps2kbd.o drv/irq.o drv/ps2mouse.o \
    drv/serial.o drv/ata.o drv/blockdev.o drv/diskmgr.o drv/rtc.o \
    drv/input_dev.o \
    fs/vfs.o fs/devfs.o fs/initrd.o fs/fat32.o fs/mkfs_fat32.o fs/tmpfs.o fs/procfs.o fs/sysfs.o fs/bootfs.o \
    fs/tar.o fs/tar_testdata.o \
    loader/elf.o loader/exec.o loader/elf64.o \
    lib/kprintf.o lib/kutils.o lib/lz4.o lib/compiler_rt.o lib/ksymbols.o lib/backtrace.o lib/stack_chk.o \
    gfx/blit.o gfx/font.o gfx/font_8x8.o

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

# x64 storage drivers (PCI-based).
X64_STORAGE_OBJS = drv/ahci.o drv/virtio_blk.o drv/nvme.o

KERNEL_BIN = tsukasa_x64.elf
CFLAGS = -m64 -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
         -ffreestanding -fno-pie -fstack-protector-strong -mstack-protector-guard=global -Wall -Wextra -O2 \
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
ifeq ($(UBSAN),1)
CFLAGS += -fsanitize=signed-integer-overflow,shift,integer-divide-by-zero,bounds,unreachable -fno-sanitize=alignment -DCONFIG_UBSAN=1
OBJS += lib/ubsan.o
endif
ISO_TARGET = iso-x86_64

.PHONY: all iso initrd clean iso-x86_64 limine-artifacts sdk apps vanilla coreutils disktools ports check libc libc-tests

all: $(KERNEL_BIN)

iso: $(ISO_TARGET)

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

$(KERNEL_BIN): $(OBJS) lib/ksymbols_table.o
	$(LD) $(LDFLAGS) -o $@ $(OBJS) lib/ksymbols_table.o
	bash scripts/build/gen-ksymbols.sh $@ lib/ksymbols_table.c
	$(CC) $(CFLAGS) -c -o lib/ksymbols_table.o lib/ksymbols_table.c
	$(LD) $(LDFLAGS) -o $@ $(OBJS) lib/ksymbols_table.o

$(INITRD_BIN)/hello.elf: user_elf/hello64.asm
	@mkdir -p $(INITRD_BIN)
	nasm -f elf64 -o user_elf/hello64.o user_elf/hello64.asm
	$(LD) $(USER_ELF_LDFLAGS) -o $@ user_elf/hello64.o

$(INITRD_BIN)/hello_sc.elf: user_elf/hello_syscall64.asm
	@mkdir -p $(INITRD_BIN)
	nasm -f elf64 -o user_elf/hello_syscall64.o user_elf/hello_syscall64.asm
	$(LD) $(USER_ELF_LDFLAGS) -o $@ user_elf/hello_syscall64.o

$(INITRD_BIN)/fault.elf: user_elf/fault64.asm
	@mkdir -p $(INITRD_BIN)
	nasm -f elf64 -o user_elf/fault64.o user_elf/fault64.asm
	$(LD) $(USER_ELF_LDFLAGS) -o $@ user_elf/fault64.o

$(INITRD_BIN)/exit7.elf: user_elf/exit7.asm
	@mkdir -p $(INITRD_BIN)
	nasm -f elf64 -o user_elf/exit7.o user_elf/exit7.asm
	$(LD) $(USER_ELF_LDFLAGS) -o $@ user_elf/exit7.o


SDK_DIR    = sdk
SDK_CC     = $(CC)

SDK_CFLAGS = -ffreestanding -fno-pie -fstack-protector-strong -mstack-protector-guard=global -mno-red-zone \
             -mno-mmx -mno-sse -mno-sse2 \
             -O2 -Wall -Wextra -Iuser/crt
SDK_OBJS   = user/crt/crt0.o user/crt/syscalls.o user/crt/stdio_lite.o

user/crt/crt0.o: user/crt/crt0.asm
	nasm -f elf64 -o $@ $<

user/crt/syscalls.o: user/crt/syscalls.c user/crt/tsukasa_sdk.h
	$(SDK_CC) $(SDK_CFLAGS) -c -o $@ $<

user/crt/stdio_lite.o: user/crt/stdio_lite.c user/crt/tsukasa_sdk.h
	$(SDK_CC) $(SDK_CFLAGS) -c -o $@ $<


$(INITRD_BIN)/%.elf: user/cli/%.c $(SDK_OBJS) user/crt/tsukasa_app.ld
	@mkdir -p $(INITRD_BIN)
	$(SDK_CC) $(SDK_CFLAGS) -c -o user/cli/$*.o $<
	$(LD) -m elf_x86_64 -T user/crt/tsukasa_app.ld -nostdlib \
	    -o $@ user/crt/crt0.o user/cli/$*.o user/crt/syscalls.o user/crt/stdio_lite.o

$(INITRD_BIN)/hellosdk.elf: user/examples/hello.c $(SDK_OBJS) user/crt/tsukasa_app.ld
	@mkdir -p $(INITRD_BIN)
	$(SDK_CC) $(SDK_CFLAGS) -c -o user/examples/hello.o user/examples/hello.c
	$(LD) -m elf_x86_64 -T user/crt/tsukasa_app.ld -nostdlib \
	    -o $@ user/crt/crt0.o user/examples/hello.o user/crt/syscalls.o

TK_OBJS = user/tk/tk_client.sdk.o user/tk/tk_painter.sdk.o user/tk/tk_app.sdk.o \
          user/lib/libwidget.sdk.o gfx/font_8x8.sdk.o

user/tk/%.sdk.o: user/tk/%.c user/crt/tsukasa_sdk.h
	$(SDK_CC) $(SDK_CFLAGS) -c -o $@ $<

user/lib/libwidget.sdk.o: user/lib/libwidget.c user/include/libwidget.h
	$(SDK_CC) $(SDK_CFLAGS) -c -o $@ $<

gfx/font_8x8.sdk.o: gfx/font_8x8.c gfx/font_8x8.h
	$(SDK_CC) $(SDK_CFLAGS) -c -o $@ $<

$(INITRD_BIN)/wdemo16.elf: user/cli/wdemo16.c $(SDK_OBJS) $(TK_OBJS) user/crt/tsukasa_app.ld
	@mkdir -p $(INITRD_BIN)
	$(SDK_CC) $(SDK_CFLAGS) -c -o user/cli/wdemo16.o $<
	$(LD) -m elf_x86_64 -T user/crt/tsukasa_app.ld -nostdlib \
	    -o $@ user/crt/crt0.o user/cli/wdemo16.o $(TK_OBJS) user/crt/syscalls.o user/crt/stdio_lite.o

libc:
	$(MAKE) TOOLCHAIN=$(TOOLCHAIN) -C tsukasa-libc all

vanilla: libc
	$(MAKE) TOOLCHAIN=$(TOOLCHAIN) -C vanilla all

coreutils: libc
	$(MAKE) TOOLCHAIN=$(TOOLCHAIN) -C tsukasa-coreutils all

disktools: libc
	$(MAKE) TOOLCHAIN=$(TOOLCHAIN) -C tsukasa-disktools all

ports: vanilla
	$(MAKE) TOOLCHAIN=$(TOOLCHAIN) -C tsukasa-ports all

libc-tests: libc
	$(MAKE) TOOLCHAIN=$(TOOLCHAIN) -C tsukasa-libc all check

$(INITRD_BIN)/TESTDRV.ELF: user/cli/testdrv.c $(SDK_OBJS) user/crt/tsukasa_app.ld
	@mkdir -p $(INITRD_BIN)
	$(SDK_CC) $(SDK_CFLAGS) -c -o user/cli/testdrv.o $<
	$(LD) -m elf_x86_64 -T user/crt/tsukasa_app.ld -nostdlib \
	    -o $@ user/crt/crt0.o user/cli/testdrv.o user/crt/syscalls.o user/crt/stdio_lite.o

USER_TEST_BINS = $(INITRD_BIN)/hello.elf $(INITRD_BIN)/hello_sc.elf $(INITRD_BIN)/fault.elf \
                 $(INITRD_BIN)/exit7.elf $(INITRD_BIN)/hellosdk.elf $(INITRD_BIN)/echo.elf \
                 $(INITRD_BIN)/cat.elf $(INITRD_BIN)/argvchk.elf $(INITRD_BIN)/futex10.elf \
                 $(INITRD_BIN)/usock20.elf $(INITRD_BIN)/wsrv15.elf $(INITRD_BIN)/wapp15.elf \
                 $(INITRD_BIN)/wdemo16.elf $(INITRD_BIN)/TESTDRV.ELF

COREUTILS_NAMES = ls cat mkdir rm cp mv grep ps free sysfetch kill date poweroff reboot \
                  head tail wc sort uniq cut uname touch net ping telnet fetch tsh

apps: $(USER_TEST_BINS) vanilla coreutils disktools ports libc-tests

sdk: $(SDK_OBJS) user/crt/tsukasa_app.ld user/crt/tsukasa_sdk.h
	@mkdir -p $(SDK_DIR)/include $(SDK_DIR)/lib $(SDK_DIR)/crt
	cp user/crt/tsukasa_sdk.h $(SDK_DIR)/include/
	cp user/crt/crt0.o user/crt/tsukasa_app.ld $(SDK_DIR)/crt/
	cp user/crt/syscalls.o $(SDK_DIR)/lib/
	@echo "[OK] SDK staged in $(SDK_DIR)/ (crt0.o, tsukasa_app.ld, syscalls.o, tsukasa_sdk.h)."

$(LZ4PACK): tools/lz4pack.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -O2 -Wall -Wextra -Werror -o $@ $<

$(INITRD_IMG): apps limine-artifacts bootblob.bin $(LZ4PACK)
	@mkdir -p $(INITRD_DIR)/bin $(INITRD_DIR)/sbin $(INITRD_DIR)/usr/bin \
	          $(INITRD_DIR)/usr/lib $(INITRD_DIR)/usr/include \
	          $(INITRD_DIR)/usr/share/vanilla/cursors \
	          $(INITRD_DIR)/usr/share/vanilla/icons \
	          $(INITRD_DIR)/usr/share/doom \
	          $(INITRD_DIR)/usr/share/limine \
	          $(INITRD_DIR)/etc/vanilla/themes \
	          $(INITRD_DIR)/home $(INITRD_DIR)/root \
	          $(INITRD_DIR)/var/log $(INITRD_DIR)/var/run $(INITRD_DIR)/var/tmp \
	          $(INITRD_DIR)/tmp $(INITRD_DIR)/dev $(INITRD_DIR)/proc \
	          $(INITRD_DIR)/sys $(INITRD_DIR)/boot $(INITRD_DIR)/mnt
	@for util in $(COREUTILS_NAMES); do \
	    cp tsukasa-coreutils/bin/$${util}.elf $(INITRD_BIN)/$${util}; \
	done
	ln -sf tsh $(INITRD_BIN)/tsh.elf
	ln -sf tsh $(INITRD_BIN)/TSH.ELF
	cp vanilla/bin/vanilla_srv.elf $(INITRD_BIN)/vsrv
	ln -sf vsrv $(INITRD_BIN)/vsrv.elf
	ln -sf vsrv $(INITRD_BIN)/VSRV.ELF
	cp vanilla/bin/shell.elf $(INITRD_BIN)/shell
	ln -sf shell $(INITRD_BIN)/shell.elf
	ln -sf shell $(INITRD_BIN)/SHELL.ELF
	cp vanilla/bin/terminal.elf $(INITRD_BIN)/terminal
	ln -sf terminal $(INITRD_BIN)/terminal.elf
	ln -sf terminal $(INITRD_BIN)/TERMINAL.ELF
	cp vanilla/bin/notepad.elf $(INITRD_BIN)/notepad
	ln -sf notepad $(INITRD_BIN)/notepad.elf
	ln -sf notepad $(INITRD_BIN)/NOTEPAD.ELF
	cp vanilla/bin/filemgr.elf $(INITRD_BIN)/filemgr
	ln -sf filemgr $(INITRD_BIN)/filemgr.elf
	ln -sf filemgr $(INITRD_BIN)/FILEMGR.ELF
	cp vanilla/bin/calc.elf $(INITRD_BIN)/calc
	ln -sf calc $(INITRD_BIN)/calc.elf
	ln -sf calc $(INITRD_BIN)/CALC.ELF
	cp vanilla/bin/taskmgr.elf $(INITRD_BIN)/taskmgr
	ln -sf taskmgr $(INITRD_BIN)/taskmgr.elf
	ln -sf taskmgr $(INITRD_BIN)/TASKMGR.ELF
	cp vanilla/bin/settings.elf $(INITRD_BIN)/settings
	ln -sf settings $(INITRD_BIN)/settings.elf
	ln -sf settings $(INITRD_BIN)/SETTINGS.ELF
	cp vanilla/supervisor/SUPERVSD.ELF $(INITRD_BIN)/supervisord
	ln -sf supervisord $(INITRD_BIN)/supervsd.elf
	ln -sf supervisord $(INITRD_BIN)/SUPERVSD.ELF
	cp vanilla/bin/registryd.elf $(INITRD_BIN)/registryd
	ln -sf registryd $(INITRD_BIN)/registryd.elf
	ln -sf registryd $(INITRD_BIN)/REGISTRD.ELF
	cp vanilla/bin/clipboardd.elf $(INITRD_BIN)/clipboardd
	ln -sf clipboardd $(INITRD_BIN)/clipboardd.elf
	ln -sf clipboardd $(INITRD_BIN)/CLIPBD.ELF
	cp vanilla/bin/notifyd.elf $(INITRD_BIN)/notifyd
	ln -sf notifyd $(INITRD_BIN)/notifyd.elf
	ln -sf notifyd $(INITRD_BIN)/NOTIFYD.ELF
	cp vanilla/devtools/DEVTOOLS.ELF $(INITRD_BIN)/devtools
	ln -sf devtools $(INITRD_BIN)/devtools.elf
	ln -sf devtools $(INITRD_BIN)/DEVTOOLS.ELF
	cp tsukasa-disktools/bin/fdisk.elf $(INITRD_BIN)/fdisk
	ln -sf fdisk $(INITRD_BIN)/fdisk.elf
	ln -sf fdisk $(INITRD_BIN)/FDISK.ELF
	cp tsukasa-disktools/bin/mkfs.fat32.elf $(INITRD_BIN)/mkfs.fat32
	ln -sf mkfs.fat32 $(INITRD_BIN)/MKFSFAT.ELF
	cp tsukasa-disktools/bin/mkfs.ext2.elf $(INITRD_BIN)/mkfs.ext2
	ln -sf mkfs.ext2 $(INITRD_BIN)/MKFSEXT2.ELF
	cp tsukasa-disktools/bin/installer.elf $(INITRD_BIN)/installer
	ln -sf installer $(INITRD_BIN)/installer.elf
	ln -sf installer $(INITRD_BIN)/INSTALL.ELF
	ln -sf installer $(INITRD_BIN)/install
	cp tsukasa-ports/staging/bin/doomgeneric.elf $(INITRD_BIN)/doom
	ln -sf doom $(INITRD_BIN)/doom.elf
	ln -sf doom $(INITRD_BIN)/DOOM.ELF
	cp tsukasa-ports/staging/usr/share/doom/doom1.wad $(INITRD_DIR)/usr/share/doom/doom1.wad
	ln -sf ../usr/share/doom/doom1.wad $(INITRD_BIN)/doom1.wad
	ln -sf ../usr/share/doom/doom1.wad $(INITRD_BIN)/DOOM1.WAD
	cp tsukasa-ports/staging/bin/tcc.elf $(INITRD_BIN)/tcc
	ln -sf tcc $(INITRD_BIN)/tcc.elf
	ln -sf tcc $(INITRD_BIN)/TCC.ELF
	cp tsukasa-ports/staging/bin/lua.elf $(INITRD_BIN)/lua
	ln -sf lua $(INITRD_BIN)/lua.elf
	ln -sf lua $(INITRD_BIN)/LUA.ELF
	cp tsukasa-ports/staging/bin/luac.elf $(INITRD_BIN)/luac
	ln -sf luac $(INITRD_BIN)/luac.elf
	ln -sf luac $(INITRD_BIN)/LUAC.ELF
	cp -r tsukasa-ports/staging/usr/include/. $(INITRD_DIR)/usr/include/
	cp -r tsukasa-ports/staging/usr/lib/. $(INITRD_DIR)/usr/lib/
	cp tsukasa-libc/tests/test_crt0.elf $(INITRD_BIN)/TCRT0.ELF
	cp tsukasa-libc/tests/test_syscalls.elf $(INITRD_BIN)/TSYSC.ELF
	cp tsukasa-libc/tests/test_libc.elf $(INITRD_BIN)/TLIBC.ELF
	cp tsukasa-libc/tests/test_math.elf $(INITRD_BIN)/TMATH.ELF
	cp tsukasa-libc/tests/test_stdio.elf $(INITRD_BIN)/TSTDIO.ELF
	cp vanilla/bin/test_ipc.elf $(INITRD_BIN)/TESTIPC.ELF
	cp vanilla/bin/test_compositor.elf $(INITRD_BIN)/TESTCOMP.ELF
	cp vanilla/bin/test_typography.elf $(INITRD_BIN)/TESTTYPO.ELF
	cp vanilla/bin/test_shell.elf $(INITRD_BIN)/TSHELL.ELF
	cp assets/themes/*.ini $(INITRD_DIR)/etc/vanilla/themes/
	cp assets/cursors/*.argb $(INITRD_DIR)/usr/share/vanilla/cursors/
	cp vanilla/icons/builtin/*.VCO $(INITRD_DIR)/usr/share/vanilla/icons/
	cp vanilla/assets/wallpaper.png vanilla/assets/wallpaper.bmp $(INITRD_DIR)/usr/share/vanilla/
	cp vanilla/supervisor/services.conf $(INITRD_DIR)/etc/services.conf
	cp $(LIMINE_DIR)/BOOTX64.EFI $(LIMINE_DIR)/limine-bios.sys bootblob.bin $(INITRD_DIR)/usr/share/limine/
	tar --numeric-owner --format=ustar -C $(INITRD_DIR) -cf $(INITRD_TAR) .
	@if command -v lz4 >/dev/null 2>&1; then \
	    lz4 -f -9 $(INITRD_TAR) $@; \
	else \
	    $(LZ4PACK) $(INITRD_TAR) $@; \
	fi
	@echo "[OK] $@ ready (USTAR + LZ4)."

initrd: $(INITRD_IMG)

check:
	@bash scripts/test/run-host-tests.sh
	@if command -v qemu-system-x86_64 >/dev/null 2>&1 && command -v qemu-img >/dev/null 2>&1; then \
		bash scripts/test/run-guest-tests.sh; \
	else \
		echo "SKIP: qemu not installed; skipping guest tests"; \
	fi
	@bash scripts/test/summarize.sh

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
	cp $(INITRD_IMG) $(BOOT_DIR)/initrd.tar.lz4
	@echo "[OK] initrd.tar.lz4 included in ISO."
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

.PHONY: visual-test vnc-run

visual-test: iso-x86_64
	python3 scripts/test/visual-test.py --iso $(ISO_IMAGE) --scenario full

vnc-run: iso-x86_64
	python3 scripts/test/visual-test.py --iso $(ISO_IMAGE) --vnc :1 --scenario interactive


clean:
	find . -name '*.o' -delete
	rm -f tsukasa_x64.elf $(ISO_IMAGE) $(INITRD_IMG) $(INITRD_TAR) lib/ksymbols_table.c
	rm -rf $(ISO_DIR) $(SDK_DIR) build
	$(MAKE) -C vanilla clean
	$(MAKE) -C tsukasa-disktools clean
	$(MAKE) -C tsukasa-coreutils clean
	$(MAKE) -C tsukasa-libc clean
