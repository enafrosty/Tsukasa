#
# Project Tsukasa - Master Ports Makefile
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

SHELL = bash

.PHONY: all clean check doom tcc lua fetch patch build install

all:
	@bash package.sh all fetch
	@bash package.sh all patch
	@bash package.sh all build
	@bash package.sh all install
	@echo "[OK] All tsukasa-ports built and staged successfully."

doom:
	@bash package.sh doom fetch
	@bash package.sh doom patch
	@bash package.sh doom build
	@bash package.sh doom install

tcc:
	@bash package.sh tcc fetch
	@bash package.sh tcc patch
	@bash package.sh tcc build
	@bash package.sh tcc install

lua:
	@bash package.sh lua fetch
	@bash package.sh lua patch
	@bash package.sh lua build
	@bash package.sh lua install

clean:
	@bash package.sh all clean || true
	@rm -rf staging

check:
	@echo "Checking staged port artifacts..."
	@test -f staging/bin/doomgeneric.elf || (echo "[FAIL] staging/bin/doomgeneric.elf missing" && exit 1)
	@test -f staging/usr/share/doom/doom1.wad || (echo "[FAIL] staging/usr/share/doom/doom1.wad missing" && exit 1)
	@test -f staging/bin/tcc.elf || (echo "[FAIL] staging/bin/tcc.elf missing" && exit 1)
	@test -f staging/bin/lua.elf || (echo "[FAIL] staging/bin/lua.elf missing" && exit 1)
	@test -f staging/bin/luac.elf || (echo "[FAIL] staging/bin/luac.elf missing" && exit 1)
	@echo "[OK] All required port artifacts present in staging."
