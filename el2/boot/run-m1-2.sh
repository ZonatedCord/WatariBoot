#!/bin/bash
# SPDX-License-Identifier: MIT
# M1-2: virtual interrupts (CNTV timer PPI 27 + SGI) through the GICv3 list registers. Build the image, boot it in QEMU TCG headless (20 s timeout) and
# check the UART output. Exit 0 on success.
set -u
# CPUIF=trap runs the same test with the virtual CPU interface in TRAP_EMULATE mode
# (boot argument wel2.cpuif=trap, see cpuif.c); the default is HW list registers.
APPEND=()
[ "${CPUIF:-hw}" = trap ] && APPEND=(-append "wel2.cpuif=trap")
cd "$(dirname "$0")"
make -s || { echo "FAIL: build"; exit 2; }

LOG=build/uart.log
: > "$LOG"
qemu-system-aarch64 -M virt,virtualization=on,gic-version=3 -cpu max -accel tcg \
    -nographic -m 512M -smp 1 -kernel build/wel2.bin ${APPEND[@]+"${APPEND[@]}"} -serial "file:$LOG" -monitor none \
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
expect "Watari EL2: physical GIC owned by EL2, virtual CPU interface on"
expect "Watari EL2: list registers (LRs) = 0x0000000000000004"
expect "Watari EL1: ICC_SRE_EL1 = 0x0000000000000007"
expect "Watari EL1: GICR_ISENABLER0 = 0x0000000008000002"
expect "Watari EL1: ICC_PMR_EL1 = 0x00000000000000f0"
expect "Watari EL1: ICC_IGRPEN1_EL1 = 0x0000000000000001"
expect "Watari EL1: timer irq 27 x5"
expect "Watari EL2: ICC_SGI1R_EL1 write trapped"
expect "Watari EL1: sgi 1 received"
expect "Watari EL2: PSCI SYSTEM_OFF"
n=$(grep -c "Watari EL1: irq INTID = 0x000000000000001b" build/uart.txt)
[ "$n" = 5 ] || { echo "MISSING: five INTID 27 lines (got $n)"; rc=1; }
grep -q "FAILED\|STILL ASLEEP\|unexpected irq" build/uart.txt && { echo "FAIL: a test reported failure"; rc=1; }
grep -q "UNEXPECTED" build/uart.txt && { echo "FAIL: unexpected exception"; rc=1; }
[ "$TIMEOUT" = 1 ] && rc=1
[ $rc = 0 ] && echo "PASS: boot-el2 (M1-2 virtual timer + SGI via GICv3 list registers)"
exit $rc
