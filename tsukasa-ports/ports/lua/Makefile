#
# Project Tsukasa - Lua 5.4 Port Makefile
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

include ../../ports.mk

OBJDIR = build
SRCDIR = src/lua-5.4.7/src

CORE_SRCS = lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c llex.c \
            lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c ltable.c \
            ltm.c lundump.c lvm.c lzio.c
LIB_SRCS  = lauxlib.c lbaselib.c lcorolib.c ldblib.c liolib.c lmathlib.c loadlib.c \
            loslib.c lstrlib.c ltablib.c lutf8lib.c linit.c

CORE_OBJS = $(addprefix $(OBJDIR)/, $(CORE_SRCS:.c=.o))
LIB_OBJS  = $(addprefix $(OBJDIR)/, $(LIB_SRCS:.c=.o))
BASE_OBJS = $(CORE_OBJS) $(LIB_OBJS)

CFLAGS = $(PORTS_CFLAGS) -I$(SRCDIR) -DLUA_USE_POSIX \
         -Wno-unused-parameter -Wno-sign-compare -Wno-unused-variable \
         -Wno-missing-field-initializers -Wno-invalid-noreturn

.PHONY: all clean

all: lua.elf luac.elf

lua.elf: $(CRT0) $(BASE_OBJS) $(OBJDIR)/lua.o $(LIBC_A) $(LIBM_A)
	$(LD) $(PORTS_LDFLAGS) -o $@ $(CRT0) $(BASE_OBJS) $(OBJDIR)/lua.o $(LIBC_A) $(LIBM_A)

luac.elf: $(CRT0) $(BASE_OBJS) $(OBJDIR)/luac.o $(LIBC_A) $(LIBM_A)
	$(LD) $(PORTS_LDFLAGS) -o $@ $(CRT0) $(BASE_OBJS) $(OBJDIR)/luac.o $(LIBC_A) $(LIBM_A)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(OBJDIR) lua.elf luac.elf
