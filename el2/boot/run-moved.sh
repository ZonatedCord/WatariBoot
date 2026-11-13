#!/bin/bash
# SPDX-License-Identifier: MIT
# T6a, optional proof of position independence: the same wel2.bin, but with the arm64 Image header's text_offset
# patched from 0x80000 to 0x84000, so QEMU -kernel loads it at 0x40084000 instead of 0x40080000 (both inside the
# 2 MB carve-out). The relocation loop of start.S must then fix the image up for the new address, and the layer must
# report it (g_layer_base). Same checks as run-qemu.sh (M0: EL1 test payload, PSCI SYSTEM_OFF). 20 s timeout.
# Not part of run-all.sh. Exit 0 on success.
set -u
cd "$(dirname "$0")"
make -s || { echo "FAIL: build"; exit 2; }
BIN=build/wel2-moved.bin
cp build/wel2.bin "$BIN"
# text_offset is the little-endian u64 at byte 8 of the header: 0x80000 -> 0x84000 (T6c: the bigger M1 table pools make the layer 0x164000 bytes, so the move must stay small)
printf '\x00\x40\x08\x00\x00\x00\x00\x00' | dd of="$BIN" bs=1 seek=8 conv=notrunc 2>/dev/null

LOG=build/uart-moved.log
: > "$LOG"
"${QEMU:-qemu-system-aarch64}" -M virt,virtualization=on,gic-version=3 -cpu max -accel tcg \
    -nographic -m 512M -smp 1 -kernel "$BIN" -serial "file:$LOG" -monitor none >build/qemu-moved.out 2>&1 &
QPID=$!
for _ in $(seq 1 200); do
    kill -0 "$QPID" 2>/dev/null || break
    sleep 0.1
done
TIMEOUT=0
if kill -0 "$QPID" 2>/dev/null; then kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null; TIMEOUT=1; else wait "$QPID"; fi
tr -d '\r' < "$LOG" > build/uart-moved.txt
cat build/uart-moved.txt

rc=0
expect() { grep -qF -- "$1" build/uart-moved.txt || { echo "MISSING: $1"; rc=1; }; }
expect "Watari EL2: hello from EL2"
expect "Watari EL2: layer image (PIE, relocated) at 0x0000000040084000"
expect "Watari EL2:   carve-out base = 0x0000000040000000"
expect "Watari EL1: hello from EL1"
expect "Watari EL1: PSCI_VERSION returned 1.1"
expect "Watari EL2: PSCI SYSTEM_OFF"
grep -q "UNEXPECTED\|FATAL" build/uart-moved.txt && { echo "FAIL: unexpected event"; rc=1; }
[ "$TIMEOUT" = 1 ] && { echo "FAIL: timeout"; rc=1; }
[ $rc = 0 ] && echo "PASS: the same image runs at 0x40084000 (position independent)"
exit $rc
