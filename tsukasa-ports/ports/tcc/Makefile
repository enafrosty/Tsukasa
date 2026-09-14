#
# Project Tsukasa - TinyCC (tcc) Port Makefile
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
OUTPUT = tcc.elf
SRCDIR = src/tinycc

OBJS = $(OBJDIR)/tcc.o

CFLAGS = $(PORTS_CFLAGS) -I$(SRCDIR) \
         -DONE_SOURCE=1 -DCONFIG_TCC_STATIC=1 -DTCC_TARGET_X86_64=1 \
         -DCONFIG_TCCDIR=\"/usr/lib/tcc\" -DCONFIG_SYSROOT=\"\" \
         -DCONFIG_TCC_CRTPATH=\"/usr/lib\" -DCONFIG_TCC_LIBPATHS=\"/usr/lib\" \
         -DCONFIG_TCC_SYSINCLUDEPATHS=\"/usr/include\" -DTCC_VERSION=\"0.9.27\" \
         -DCONFIG_TCC_SEMLOCK=0 -DCONFIG_TCC_BACKTRACE=0 -Dldexpl=ldexp \
         -Wno-unused-parameter -Wno-sign-compare -Wno-unused-variable \
         -Wno-missing-field-initializers -Wno-incompatible-pointer-types \
         -Wno-unterminated-string-initialization -Wno-invalid-noreturn \
         -Wno-unused-function

.PHONY: all clean

all: $(OUTPUT)

$(OUTPUT): $(CRT0) $(OBJS) $(LIBC_A) $(LIBM_A)
	$(LD) $(PORTS_LDFLAGS) -o $@ $(CRT0) $(OBJS) $(LIBC_A) $(LIBM_A)

$(OBJDIR)/tcc.o: $(SRCDIR)/tcc.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(OBJDIR) $(OUTPUT)
