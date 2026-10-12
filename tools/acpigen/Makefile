# SPDX-License-Identifier: MIT
CC      = clang
CFLAGS  := -std=c11 -Wall -Wextra -Werror -Iinclude -Isrc -O1
LIBSRC  := src/common.c src/madt.c src/gtdt.c src/pptt.c src/csrt.c src/t8103_model.c
TESTS   := madt gtdt pptt csrt
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
	@for t in madt gtdt pptt csrt; do ./$(BUILD)/acpigen $$t -o $(BUILD)/$$t.bin; done
	@echo "TUTTI I TEST OK"

clean:
	rm -rf $(BUILD)

.PHONY: all test clean
