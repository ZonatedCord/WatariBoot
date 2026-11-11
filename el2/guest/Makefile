# SPDX-License-Identifier: MIT
# Watari EL2 EL1 test guest (T7a). Same toolchain as el2/boot (Homebrew LLVM, no libc).
#   make            -> build/m1guest.bin  (flat, position independent, arm64 Image header)
LLVM    ?= /opt/homebrew/opt/llvm/bin
CC      := $(LLVM)/clang
LD      := ld.lld
OBJCOPY := $(LLVM)/llvm-objcopy
OUT     ?= build
CFLAGS  := -I../include --target=aarch64-none-elf -march=armv8.3-a -std=c11 -Wall -Wextra -Werror -O2 -g \
           -ffreestanding -nostdlib -mgeneral-regs-only -mstrict-align -fno-builtin -fno-stack-protector \
           -fpie -fvisibility=hidden -fdirect-access-external-data -mno-outline-atomics
ASFLAGS := --target=aarch64-none-elf -march=armv8.3-a -g -DSTACK_SIZE=0x2000
OBJS    := $(OUT)/start.o $(OUT)/main.o $(OUT)/gic.o

all: $(OUT)/m1guest.bin

$(OUT)/%.o: %.c guest.h ../include/wel2_hvc.h ../include/wel2_guest_info.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/start.o: start.S | $(OUT)
	$(CC) $(ASFLAGS) -c $< -o $@

# Static link at 0: nothing is relocated at run time, so the image must contain no relocations and no GOT.
$(OUT)/m1guest.elf: $(OBJS) linker.ld
	$(LD) -T linker.ld -static -nostdlib -z norelro --no-dynamic-linker -z max-page-size=4096 --no-rosegment -o $@ $(OBJS)
	@if $(LLVM)/llvm-readelf -r $@ | grep -q 'R_AARCH64'; then echo "m1guest: relocations in a position-independent flat image"; exit 1; fi
	@if $(LLVM)/llvm-readelf -S $@ | grep -qE '\.got|\.rela|\.dynamic'; then echo "m1guest: GOT/dynamic sections present"; exit 1; fi
	@if $(LLVM)/llvm-nm $@ | grep -E ' U (memcpy|memset|memcmp|strlen)$$'; then echo "m1guest: needs libc"; exit 1; fi

$(OUT)/m1guest.bin: $(OUT)/m1guest.elf
	$(OBJCOPY) -O binary $< $@
	@echo "m1guest: $$(wc -c < $@) bytes (image_size in the header covers .bss and the stacks)"

$(OUT):
	mkdir -p $@

clean:
	rm -rf build

.PHONY: all clean
