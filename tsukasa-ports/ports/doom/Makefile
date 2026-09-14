#
# Project Tsukasa - DOOM (doomgeneric) Port Makefile
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
OUTPUT = doomgeneric.elf
SRCDIR = src/doomgeneric

DOOM_SRCS = dummy.c am_map.c doomdef.c doomstat.c dstrings.c d_event.c d_items.c \
            d_iwad.c d_loop.c d_main.c d_mode.c d_net.c f_finale.c f_wipe.c \
            g_game.c hu_lib.c hu_stuff.c info.c i_cdmus.c i_endoom.c i_joystick.c \
            i_scale.c i_sound.c i_system.c i_timer.c memio.c m_argv.c m_bbox.c \
            m_cheat.c m_config.c m_controls.c m_fixed.c m_menu.c m_misc.c \
            m_random.c p_ceilng.c p_doors.c p_enemy.c p_floor.c p_inter.c \
            p_lights.c p_map.c p_maputl.c p_mobj.c p_plats.c p_pspr.c \
            p_saveg.c p_setup.c p_sight.c p_spec.c p_switch.c p_telept.c \
            p_tick.c p_user.c r_bsp.c r_data.c r_draw.c r_main.c r_plane.c \
            r_segs.c r_sky.c r_things.c sha1.c sounds.c statdump.c st_lib.c \
            st_stuff.c s_sound.c tables.c v_video.c wi_stuff.c w_checksum.c \
            w_file.c w_main.c w_wad.c z_zone.c w_file_stdc.c i_input.c \
            i_video.c doomgeneric.c

OBJS = $(addprefix $(OBJDIR)/, $(DOOM_SRCS:.c=.o)) $(OBJDIR)/doomgeneric_tsukasa.o

CFLAGS = $(PORTS_CFLAGS) -I$(SRCDIR) -I$(TOP)/vanilla/include -I../../../vanilla/include \
         -DNORMALUNIX -DLINUX -D_DEFAULT_SOURCE \
         -Wno-unused-parameter -Wno-sign-compare -Wno-unused-variable \
         -Wno-missing-field-initializers -Wno-incompatible-pointer-types

.PHONY: all clean

all: $(OUTPUT)

$(OUTPUT): $(CRT0) $(OBJS) $(LIBC_A) $(LIBM_A)
	$(LD) $(PORTS_LDFLAGS) -L$(TOP)/vanilla/lib -L../../../vanilla/lib -o $@ $(CRT0) $(OBJS) -lvanilla $(LIBC_A) $(LIBM_A)

$(OBJDIR)/doomgeneric_tsukasa.o: doomgeneric_tsukasa.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(OBJDIR) $(OUTPUT)
