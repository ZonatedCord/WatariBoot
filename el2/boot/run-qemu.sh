#!/bin/bash
# SPDX-License-Identifier: MIT
# Build the M0-C skeleton, boot it in QEMU TCG headless (20 s timeout) and
# check the UART output. Exit 0 on success.
set -u
cd "$(dirname "$0")"
make -s || { echo "FAIL: build"; exit 2; }

LOG=build/uart.log
: > "$LOG"
qemu-system-aarch64 -M virt,virtualization=on,gic-version=3 -cpu max -accel tcg \
    -nographic -m 512M -smp 1 -kernel build/wel2.bin -serial "file:$LOG" -monitor none \
    >build/qemu.out 2>&1 &
QPID=$!

# portable 20 s timeout (macOS has no timeout(1))
for _ in $(seq 1 200); do
    kill -0 "$QPID" 2>/dev/null || break
    sleep 0.1
done
if kill -0 "$QPID" 2>/dev/null; then
    kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null
    echo "FAIL: QEMU did not exit within 20 s (SYSTEM_OFF not honoured?)"
    TIMEOUT=1
else
    wait "$QPID"; TIMEOUT=0
fi

tr -d '\r' < "$LOG" > build/uart.txt
cat build/uart.txt

rc=0
expect() {
    grep -qF -- "$1" build/uart.txt || { echo "MISSING: $1"; rc=1; }
}
expect "Watari EL2: hello from EL2"
expect "Watari EL2: CurrentEL = 0x0000000000000002"
expect "Watari EL1: hello from EL1"
expect "Watari EL2: HVC from EL1, imm=0x0000000000000000 fid=0x0000000084000000"
expect "Watari EL1: PSCI_VERSION returned 1.1"
expect "Watari EL2: PSCI SYSTEM_OFF"
grep -q "UNEXPECTED" build/uart.txt && { echo "FAIL: unexpected exception"; rc=1; }
[ "$TIMEOUT" = 1 ] && rc=1
[ $rc = 0 ] && echo "PASS: boot-el2 (M0-C)"
exit $rc
