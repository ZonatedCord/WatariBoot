# SPDX-License-Identifier: MIT
# Watari EL2 - host build and tests (no cross toolchain needed).

CC      ?= clang
WARN    := -std=c11 -Wall -Wextra -Werror
INC     := -Iinclude
BUILD   := build
LIBSRC  := vgic/vgic.c
TESTS   := vgic_dist vgic_redist vgic_delivery vgic_sgi

.PHONY: test clean
test: $(addprefix $(BUILD)/test_,$(TESTS))
	@set -e; for t in $^; do ./$$t; done; echo "ALL TESTS PASSED"

# The model itself is built freestanding; tests use the host libc.
$(BUILD)/vgic.o: $(LIBSRC) include/wel2_vgic.h | $(BUILD)
	$(CC) $(WARN) -ffreestanding -O2 $(INC) -c $< -o $@

$(BUILD)/test_%: tests/test_%.c tests/test.h $(BUILD)/vgic.o | $(BUILD)
	$(CC) $(WARN) -O1 -g $(INC) -Itests $< $(BUILD)/vgic.o -o $@

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
