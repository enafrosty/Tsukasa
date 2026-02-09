# User-space app Makefile (run from project root: make -C user)
CC = gcc
LD = ld
CFLAGS = -m32 -ffreestanding -fno-pie -nostdlib -Wall -O2 -I.
LDFLAGS = -m elf_i386 -T user/user.ld -nostdlib

hello_gui: user/hello_gui.o user/syscall.o
	$(LD) $(LDFLAGS) -o $@ $^

user/hello_gui.o: user/apps/hello_gui.c
	$(CC) $(CFLAGS) -c -o $@ $<

user/syscall.o: user/lib/syscall.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f hello_gui user/hello_gui.o user/syscall.o
