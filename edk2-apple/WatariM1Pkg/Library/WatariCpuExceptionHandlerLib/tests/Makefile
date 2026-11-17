# SPDX-License-Identifier: BSD-2-Clause-Patent
# Host test of the EL1 system-register decode (no cross toolchain). Run: make -C <this directory> test
CC ?= clang

test_sysreg_emu: test_sysreg_emu.c ../WatariSysregEmu.h
	$(CC) -Wall -Wextra -Werror -O1 -g $< -o $@

.PHONY: test clean
test: test_sysreg_emu
	./test_sysreg_emu

clean:
	rm -f test_sysreg_emu
