# SPDX-License-Identifier: MIT
# Watari EL2 - host build and tests (no cross toolchain needed).

CC      ?= clang
WARN    := -std=c11 -Wall -Wextra -Werror
INC     := -Iinclude
BUILD   := build
LIBSRC  := vgic/vgic.c vgic/vcif.c vgic/intr_map.c vgic/pirq.c vgic/pirq_guard.c vgic/pirq_aic.c vgic/v2m.c vgic/ram_map.c vgic/pt.c vgic/maps.c vgic/uart.c vgic/fb.c vgic/apple_fiq.c
TESTS   := vgic_dist vgic_redist vgic_delivery vgic_sgi vgic_smp vgic_lpi vcif intr_map ram_map pirq_guard pirq_aic v2m pirq_eoi pt core_index uart_s5l fb fdt_m1 apple_fiq

.PHONY: test clean
test: $(addprefix $(BUILD)/test_,$(TESTS)) $(BUILD)/fdt_m1_dump
	@set -e; for t in $(addprefix $(BUILD)/test_,$(TESTS)); do ./$$t; done
	@# T6b: smoke run of the host dump tool on the fixture the FDT test wrote (never prints identifying properties)
	@./$(BUILD)/fdt_m1_dump $(BUILD)/fx_j293.dtb > $(BUILD)/fx_j293.out
	@grep -q 'cpu 7 reg=0x10103' $(BUILD)/fx_j293.out && grep -q 'framebuffer base=' $(BUILD)/fx_j293.out
	@! grep -qi 'secret\|serial-number' $(BUILD)/fx_j293.out
	@echo "ok   fdt_m1_dump smoke"
	@# T6a: position-independence checks of both layer images (needs the Homebrew LLVM; skipped without it)
	@if [ -x /opt/homebrew/opt/llvm/bin/clang ]; then $(MAKE) -s -C boot check; else echo "SKIP: pie check (no cross toolchain)"; fi
	@# T6e: the Apple system-register accessors hold exactly the expected mrs/msr encodings (needs the Homebrew LLVM)
	@if [ -x /opt/homebrew/opt/llvm/bin/clang ]; then sh boot/check-apple-enc.sh; else echo "SKIP: Apple encoding check (no cross toolchain)"; fi
	@echo "ALL TESTS PASSED"

# The model itself is built freestanding; tests use the host libc.
$(BUILD)/vgic.o: vgic/vgic.c include/wel2_vgic.h | $(BUILD)
	$(CC) $(WARN) -ffreestanding -O2 $(INC) -c $< -o $@

$(BUILD)/vcif.o: vgic/vcif.c include/wel2_vcif.h include/wel2_vgic.h | $(BUILD)
	$(CC) $(WARN) -ffreestanding -O2 $(INC) -c $< -o $@

$(BUILD)/intr_map.o: vgic/intr_map.c include/wel2_intr_map.h | $(BUILD)
	$(CC) $(WARN) -ffreestanding -O2 $(INC) -c $< -o $@

$(BUILD)/pirq.o: vgic/pirq.c include/wel2_pirq.h include/wel2_intr_map.h include/wel2_vgic.h | $(BUILD)
	$(CC) $(WARN) -ffreestanding -O2 $(INC) -c $< -o $@

$(BUILD)/pirq_guard.o: vgic/pirq_guard.c include/wel2_pirq_guard.h include/wel2_pirq.h | $(BUILD)
	$(CC) $(WARN) -ffreestanding -O2 $(INC) -c $< -o $@

$(BUILD)/pirq_aic.o: vgic/pirq_aic.c include/wel2_aic.h include/wel2_pirq.h include/wel2_intr_map.h | $(BUILD)
	$(CC) $(WARN) -ffreestanding -O2 $(INC) -c $< -o $@

$(BUILD)/v2m.o: vgic/v2m.c include/wel2_v2m.h include/wel2_vgic.h | $(BUILD)
	$(CC) $(WARN) -ffreestanding -O2 $(INC) -c $< -o $@

$(BUILD)/ram_map.o: vgic/ram_map.c include/wel2_ram.h | $(BUILD)
	$(CC) $(WARN) -ffreestanding -O2 $(INC) -c $< -o $@

$(BUILD)/pt.o: vgic/pt.c include/wel2_pt.h | $(BUILD)
	$(CC) $(WARN) -ffreestanding -O2 $(INC) -c $< -o $@

$(BUILD)/maps.o: vgic/maps.c include/wel2_maps.h include/wel2_pt.h include/wel2_ram.h | $(BUILD)
	$(CC) $(WARN) -ffreestanding -O2 $(INC) -c $< -o $@

$(BUILD)/uart.o: vgic/uart.c include/wel2_uart.h | $(BUILD)
	$(CC) $(WARN) -ffreestanding -O2 $(INC) -c $< -o $@

$(BUILD)/fb.o: vgic/fb.c vgic/font5x7.h include/wel2_fb.h | $(BUILD)
	$(CC) $(WARN) -ffreestanding -O2 $(INC) -c $< -o $@

$(BUILD)/apple_fiq.o: vgic/apple_fiq.c include/wel2_apple_fiq.h include/wel2_apple_sysreg.h include/wel2_vgic.h | $(BUILD)
	$(CC) $(WARN) -ffreestanding -O2 $(INC) -c $< -o $@

# The FDT reader of the boot code is plain C: the FDT tests build it for the host as it is.
$(BUILD)/fdt.o: boot/fdt.c boot/fdt.h boot/boot.h include/wel2_fb.h |$(BUILD)
	$(CC) $(WARN) -ffreestanding -O2 $(INC) -Iboot -c $< -o $@

$(BUILD)/fdt_m1.o: boot/fdt_m1.c boot/fdt_m1.h boot/fdt.h include/wel2_fb.h | $(BUILD)
	$(CC) $(WARN) -ffreestanding -O2 $(INC) -Iboot -c $< -o $@

$(BUILD)/fdt_m1_dump: ../tools/m3a/fdt_m1_dump.c $(BUILD)/fdt_m1.o $(BUILD)/fdt.o $(BUILD)/fb.o $(BUILD)/ram_map.o | $(BUILD)
	$(CC) $(WARN) -O1 $(INC) -Iboot ../tools/m3a/fdt_m1_dump.c $(BUILD)/fdt_m1.o $(BUILD)/fdt.o $(BUILD)/fb.o $(BUILD)/ram_map.o -o $@

LIBOBJ := $(BUILD)/fdt_m1.o $(BUILD)/uart.o $(BUILD)/fb.o $(BUILD)/fdt.o $(BUILD)/vgic.o $(BUILD)/vcif.o $(BUILD)/intr_map.o $(BUILD)/pirq.o $(BUILD)/pirq_guard.o $(BUILD)/pirq_aic.o $(BUILD)/v2m.o $(BUILD)/ram_map.o $(BUILD)/pt.o $(BUILD)/maps.o $(BUILD)/apple_fiq.o

$(BUILD)/test_%: tests/test_%.c tests/test.h tests/fake_pirq.h tests/fake_aic.h tests/fdt_build.h tests/fake_apple_sysreg.h $(LIBOBJ) | $(BUILD)
	$(CC) $(WARN) -O1 -g $(INC) -Itests -Iboot $< $(LIBOBJ) -o $@

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
