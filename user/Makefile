# User-space SDK Makefile
# Usage:
#   make -C user hello_gui
#   make -C user sample_cli
#   make -C user clean

CC ?= gcc
LD ?= ld

ROOT := ..
BUILD := build

USER_ARCH ?= x86_64

ifeq ($(USER_ARCH),i386)
CFLAGS := -m32 -ffreestanding -fno-pie -nostdlib -Wall -Wextra -O2 -I$(ROOT)/user/include -I$(ROOT)
LDFLAGS := -m elf_i386 -T $(ROOT)/user/user.ld -nostdlib
else
CFLAGS := -m64 -mcmodel=small -ffreestanding -fno-pie -nostdlib -Wall -Wextra -O2 -I$(ROOT)/user/include -I$(ROOT)
LDFLAGS := -m elf_x86_64 -T $(ROOT)/user/user.ld -nostdlib
endif

LIB_OBJS := \
	$(BUILD)/syscall.o \
	$(BUILD)/libui.o \
	$(BUILD)/libwidget.o \
	$(BUILD)/string.o \
	$(BUILD)/unistd.o \
	$(BUILD)/fcntl.o \
	$(BUILD)/signal.o \
	$(BUILD)/stat.o \
	$(BUILD)/time.o \
	$(BUILD)/ioctl.o \
	$(BUILD)/mman.o \
	$(BUILD)/poll.o \
	$(BUILD)/stdio.o \
	$(BUILD)/stdlib.o \
	$(BUILD)/app_runtime.o \
	$(BUILD)/shell.o

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.o: $(ROOT)/user/lib/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/hello_gui.o: $(ROOT)/user/apps/hello_gui.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/sample_cli.o: $(ROOT)/user/examples/sample_cli.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

hello_gui: $(BUILD)/hello_gui.o $(LIB_OBJS)
	$(LD) $(LDFLAGS) -e hello_gui_main -o $@ $^

sample_cli: $(BUILD)/sample_cli.o $(LIB_OBJS)
	$(LD) $(LDFLAGS) -e sample_cli_main -o $@ $^

clean:
	@rm -rf $(BUILD) hello_gui sample_cli

.PHONY: clean
