# SPDX-License-Identifier: MIT
# Watari EL2 - host build and tests (no cross toolchain needed).

CC      ?= clang
WARN    := -std=c11 -Wall -Wextra -Werror
INC     := -Iinclude
BUILD   := build
LIBSRC  := vgic/vgic.c vgic/vcif.c vgic/intr_map.c vgic/pirq.c vgic/pirq_guard.c vgic/v2m.c vgic/ram_map.c
TESTS   := vgic_dist vgic_redist vgic_delivery vgic_sgi vgic_smp vgic_lpi vcif intr_map ram_map pirq_guard v2m pirq_eoi

.PHONY: test clean
test: $(addprefix $(BUILD)/test_,$(TESTS))
	@set -e; for t in $^; do ./$$t; done; echo "ALL TESTS PASSED"

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

$(BUILD)/v2m.o: vgic/v2m.c include/wel2_v2m.h include/wel2_vgic.h | $(BUILD)
	$(CC) $(WARN) -ffreestanding -O2 $(INC) -c $< -o $@

$(BUILD)/ram_map.o: vgic/ram_map.c include/wel2_ram.h | $(BUILD)
	$(CC) $(WARN) -ffreestanding -O2 $(INC) -c $< -o $@

LIBOBJ := $(BUILD)/vgic.o $(BUILD)/vcif.o $(BUILD)/intr_map.o $(BUILD)/pirq.o $(BUILD)/pirq_guard.o $(BUILD)/v2m.o $(BUILD)/ram_map.o

$(BUILD)/test_%: tests/test_%.c tests/test.h tests/fake_pirq.h $(LIBOBJ) | $(BUILD)
	$(CC) $(WARN) -O1 -g $(INC) -Itests $< $(LIBOBJ) -o $@

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
