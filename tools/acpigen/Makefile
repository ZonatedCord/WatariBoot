# SPDX-License-Identifier: MIT
CC      = clang
CFLAGS  := -std=c11 -Wall -Wextra -Werror -Iinclude -Isrc -O1
LIBSRC  := src/common.c src/madt.c src/gtdt.c src/pptt.c src/csrt.c src/fadt.c src/dbg2.c src/t8103_model.c src/qemu_virt_model.c
TESTS   := madt gtdt pptt csrt fadt dbg2 models fadt dbg2 models
BUILD   := build

all: $(BUILD)/acpigen

$(BUILD)/acpigen: $(LIBSRC) src/main.c include/acpigen.h src/internal.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(LIBSRC) src/main.c -o $@

$(BUILD)/test_%: tests/test_%.c $(LIBSRC) tests/test.h include/acpigen.h src/internal.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(LIBSRC) $< -o $@

test: $(addprefix $(BUILD)/test_,$(TESTS)) $(BUILD)/acpigen
	@set -e; for t in $(TESTS); do ./$(BUILD)/test_$$t; done
	@for t in madt gtdt pptt csrt fadt; do ./$(BUILD)/acpigen $$t -o $(BUILD)/$$t.bin; done
	@for t in madt gtdt pptt fadt dbg2; do ./$(BUILD)/acpigen $$t -m qemu-virt -o $(BUILD)/qemu-virt-$$t.bin; done
	./$(BUILD)/acpigen madt -m qemu-virt-nogicd -o $(BUILD)/qemu-virt-nogicd-madt.bin
	@test $$(wc -c < $(BUILD)/qemu-virt-nogicd-madt.bin) -eq $$(( $$(wc -c < $(BUILD)/qemu-virt-madt.bin) - 24 ))
	@if ./$(BUILD)/acpigen dbg2 -o $(BUILD)/x.bin 2>/dev/null; then echo "dbg2 t8103 doveva fallire"; exit 1; fi
	@echo "TUTTI I TEST OK"

clean:
	rm -rf $(BUILD)

.PHONY: all test clean
