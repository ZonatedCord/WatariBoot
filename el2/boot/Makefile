# SPDX-License-Identifier: MIT
# Watari EL2 M0-C skeleton. Uses the Homebrew LLVM toolchain already installed.
LLVM  ?= /opt/homebrew/opt/llvm/bin
CC    := $(LLVM)/clang
LD    := ld.lld
OBJCOPY := $(LLVM)/llvm-objcopy
OUT   := build

CFLAGS := -I../include --target=aarch64-none-elf -std=c11 -Wall -Wextra -Werror -O2 -g \
          -ffreestanding -nostdlib -mgeneral-regs-only -fno-pic -fno-stack-protector \
          -mno-outline-atomics -fno-builtin -mstrict-align
ASFLAGS := --target=aarch64-none-elf -g

all: $(OUT)/wel2.bin

$(OUT)/%.o: %.c boot.h ../include/wel2_vgic.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/vgic.o: ../vgic/vgic.c ../include/wel2_vgic.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/%.o: %.S | $(OUT)
	$(CC) $(ASFLAGS) -c $< -o $@

$(OUT)/payload_s.o: payload.S | $(OUT)
	$(CC) $(ASFLAGS) -c $< -o $@

$(OUT)/wel2.elf: $(OUT)/start.o $(OUT)/payload_s.o $(OUT)/main.o $(OUT)/payload.o $(OUT)/gic_phys.o $(OUT)/cpuif.o $(OUT)/psci.o $(OUT)/vgic.o linker.ld
	$(LD) -T linker.ld -nostdlib -z norelro --no-dynamic-linker -o $@ $(OUT)/start.o $(OUT)/payload_s.o $(OUT)/main.o $(OUT)/payload.o $(OUT)/gic_phys.o $(OUT)/cpuif.o $(OUT)/psci.o $(OUT)/vgic.o

$(OUT)/wel2.bin: $(OUT)/wel2.elf
	$(OBJCOPY) -O binary $< $@

$(OUT):
	mkdir -p $@

clean:
	rm -rf $(OUT)

.PHONY: all clean
