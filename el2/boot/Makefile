# SPDX-License-Identifier: MIT
# Watari EL2 boot skeleton. Uses the Homebrew LLVM toolchain already installed.
#   make          -> build/wel2.bin        (M0..M1-3 images, with the built-in EL1 test payload)
#   make chain    -> build-chain/wel2.bin  (M1-4: no payload, hands over to EDK2 at EL1)
LLVM  ?= /opt/homebrew/opt/llvm/bin
CC    := $(LLVM)/clang
LD    := ld.lld
OBJCOPY := $(LLVM)/llvm-objcopy
OUT   ?= build
XFLAGS ?=
PAYLOAD_OBJS ?= $(OUT)/payload_s.o $(OUT)/payload.o

CFLAGS := -I../include --target=aarch64-none-elf -std=c11 -Wall -Wextra -Werror -O2 -g \
          -ffreestanding -nostdlib -mgeneral-regs-only -fno-pic -fno-stack-protector \
          -mno-outline-atomics -fno-builtin -mstrict-align $(XFLAGS)
ASFLAGS := --target=aarch64-none-elf -g

OBJS := $(OUT)/start.o $(OUT)/main.o $(OUT)/gic_phys.o $(OUT)/cpuif.o $(OUT)/psci.o \
        $(OUT)/fdt.o $(OUT)/vgic.o $(OUT)/vcif.o $(PAYLOAD_OBJS)

all: build/wel2.bin

chain:
	$(MAKE) OUT=build-chain XFLAGS=-DWEL2_CHAIN=1 PAYLOAD_OBJS= build-chain/wel2.bin

$(OUT)/%.o: %.c boot.h fdt.h ../include/wel2_vgic.h ../include/wel2_vcif.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/vgic.o: ../vgic/vgic.c ../include/wel2_vgic.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/vcif.o: ../vgic/vcif.c ../include/wel2_vcif.h ../include/wel2_vgic.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/%.o: %.S | $(OUT)
	$(CC) $(ASFLAGS) -c $< -o $@

$(OUT)/payload_s.o: payload.S | $(OUT)
	$(CC) $(ASFLAGS) -c $< -o $@

$(OUT)/wel2.elf: $(OBJS) linker.ld
	$(LD) -T linker.ld -nostdlib -z norelro --no-dynamic-linker -o $@ $(OBJS)

$(OUT)/wel2.bin: $(OUT)/wel2.elf
	$(OBJCOPY) -O binary $< $@

$(OUT):
	mkdir -p $@

clean:
	rm -rf build build-chain

.PHONY: all chain clean
