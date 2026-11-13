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
          -ffreestanding -nostdlib -mgeneral-regs-only -fpie -fvisibility=hidden -fdirect-access-external-data -include pie.h -fno-stack-protector \
          -mno-outline-atomics -fno-builtin $(XFLAGS)
ASFLAGS := --target=aarch64-none-elf -g -I../include

OBJS := $(OUT)/start.o $(OUT)/util.o $(OUT)/stage1.o $(OUT)/main.o $(OUT)/gic_phys.o $(OUT)/cpuif.o $(OUT)/psci.o \
        $(OUT)/pirq_qemu.o $(OUT)/plat_qemu.o $(OUT)/plat_m1.o $(OUT)/plat_select.o $(OUT)/fdt.o $(OUT)/fdt_m1.o $(OUT)/vgic.o $(OUT)/vcif.o $(OUT)/intr_map.o $(OUT)/pirq.o $(OUT)/pirq_aic.o $(OUT)/v2m.o $(OUT)/ram_map.o $(OUT)/pt.o $(OUT)/maps.o $(OUT)/uart.o $(OUT)/fb.o \
        $(OUT)/apple_fiq.o $(OUT)/apple_sysreg.o $(OUT)/cpu_start.o $(OUT)/hvc_vendor.o $(OUT)/idreg.o $(OUT)/idreg_tbl.o $(OUT)/guest_place.o $(PAYLOAD_OBJS)

# The image is a PIE linked at 0 (linker.ld) and relocated by start.S: -fpie with hidden visibility and direct access
# to external data keeps every reference PC-relative (adrp/add), so the code needs no GOT before the relocation.
# check-pie.sh (run after every link) verifies it.
#
# Code that runs with the MMU off (stage1.c: table build before the MMU is on; util.c and
# payload.c: also run by the EL1 test payload, MMU off = Device memory) keeps -mstrict-align
# (F-18). Everything else runs at EL2 on Normal memory and is built without it (#53).
STRICT := util stage1 payload fdt fdt_m1 plat_select ram_map pt maps uart fb
$(foreach o,$(STRICT),$(OUT)/$(o).o): CFLAGS += -mstrict-align

all: build/wel2.bin

chain:
	$(MAKE) OUT=build-chain XFLAGS=-DWEL2_CHAIN=1 PAYLOAD_OBJS= build-chain/wel2.bin

$(OUT)/%.o: %.c pie.h boot.h plat.h gic_phys.h fdt.h fdt_m1.h ../include/wel2_vgic.h ../include/wel2_vcif.h ../include/wel2_pirq.h ../include/wel2_intr_map.h ../include/wel2_ram.h ../include/wel2_aic.h ../include/wel2_coreidx.h ../include/wel2_uart.h ../include/wel2_fb.h ../include/wel2_maps.h ../include/wel2_apple_fiq.h ../include/wel2_apple_sysreg.h ../include/wel2_cpu_start.h ../include/wel2_hvc.h ../include/wel2_hvc_vendor.h ../include/wel2_guest_info.h ../include/wel2_chk.h ../include/wel2_idreg.h ../include/wel2_guest_place.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/guest_place.o: ../vgic/guest_place.c ../include/wel2_guest_place.h ../include/wel2_maps.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/idreg_tbl.o: ../vgic/idreg_tbl.c ../include/wel2_idreg.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/apple_fiq.o: ../vgic/apple_fiq.c ../include/wel2_apple_fiq.h ../include/wel2_apple_sysreg.h ../include/wel2_vgic.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/cpu_start.o: ../vgic/cpu_start.c ../include/wel2_cpu_start.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/hvc_vendor.o: ../vgic/hvc_vendor.c ../include/wel2_hvc_vendor.h ../include/wel2_hvc.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/fb.o: ../vgic/fb.c ../vgic/font5x7.h ../include/wel2_fb.h pie.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/vgic.o: ../vgic/vgic.c ../include/wel2_vgic.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/vcif.o: ../vgic/vcif.c ../include/wel2_vcif.h ../include/wel2_vgic.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/intr_map.o: ../vgic/intr_map.c ../include/wel2_intr_map.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/pirq.o: ../vgic/pirq.c ../include/wel2_pirq.h ../include/wel2_intr_map.h ../include/wel2_vgic.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/pirq_aic.o: ../vgic/pirq_aic.c ../include/wel2_aic.h ../include/wel2_pirq.h ../include/wel2_intr_map.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/v2m.o: ../vgic/v2m.c ../include/wel2_v2m.h ../include/wel2_vgic.h ../include/wel2_maps.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/ram_map.o: ../vgic/ram_map.c ../include/wel2_ram.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/pt.o: ../vgic/pt.c ../include/wel2_pt.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/uart.o: ../vgic/uart.c ../include/wel2_uart.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/maps.o: ../vgic/maps.c ../include/wel2_maps.h ../include/wel2_pt.h ../include/wel2_ram.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/%.o: %.S ../include/wel2_coreidx.h | $(OUT)
	$(CC) $(ASFLAGS) -c $< -o $@

$(OUT)/payload_s.o: payload.S | $(OUT)
	$(CC) $(ASFLAGS) -c $< -o $@

$(OUT)/wel2.elf: $(OBJS) linker.ld
	$(LD) -T linker.ld -pie -nostdlib -z norelro --no-dynamic-linker -z max-page-size=16384 --no-rosegment -o $@ $(OBJS)
	sh ./check-pie.sh $@

$(OUT)/wel2.bin: $(OUT)/wel2.elf
	$(OBJCOPY) -O binary $< $@

$(OUT):
	mkdir -p $@

# Position independence checks (T6a) on both images, run by `make -C el2 test` and run-all.sh.
check:
	$(MAKE) -s all
	$(MAKE) -s chain
	$(MAKE) -s check-fdt-m1
	@# T6e/T6f: the linked images hold exactly the expected Apple encodings, and `smc` only in plat_qemu.c's functions
	sh ./check-apple-enc.sh apple_sysreg.c build/wel2.elf
	sh ./check-apple-enc.sh apple_sysreg.c build-chain/wel2.elf
	sh ./check-nosmc.sh build/wel2.elf build-chain/wel2.elf

# T6b/T6c: fdt_m1.c is linked into the image (stage1_build calls it with the MMU off). It must not need libc.
check-fdt-m1: $(OUT)/fdt_m1.o
	@if $(LLVM)/llvm-nm $< | grep -E ' U (memcpy|memset|memcmp|strlen)$$'; then echo "fdt_m1.o needs libc"; exit 1; fi

clean:
	rm -rf build build-chain

.PHONY: all chain check check-fdt-m1 clean
