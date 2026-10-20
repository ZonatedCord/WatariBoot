#!/bin/bash
# SPDX-License-Identifier: MIT
# M1-5 (#54): list-register overflow. The EL1 payload pends more SGIs than QEMU has list
# registers (4) and checks that all arrive exactly once in priority order, including a
# higher priority SGI that arrives while every LR is taken (eviction).
#   ./run-m1-5.sh                  default: LRs refill on every guest EOI (EOI maintenance irq)
#   NOEOI=1 ./run-m1-5.sh          test knob wel2.test=noeoi: only the underflow interrupt
#                                  (ICH_HCR_EL2.UIE) refills the LRs; the run must count UIE irqs
#   CPUIF=trap ./run-m1-5.sh       same in TRAP_EMULATE mode (EOIR emulated in EL2)
# QEMU TCG, -smp 1, 20 s timeout. Exit 0 on success.
set -u
APPEND_STR=""
[ "${CPUIF:-hw}" = trap ] && APPEND_STR="wel2.cpuif=trap"
[ "${NOEOI:-0}" = 1 ] && APPEND_STR="$APPEND_STR wel2.test=noeoi"
APPEND=()
[ -n "$APPEND_STR" ] && APPEND=(-append "$APPEND_STR")
cd "$(dirname "$0")"
make -s || { echo "FAIL: build"; exit 2; }

LOG=build/uart.log
: > "$LOG"
qemu-system-aarch64 -M virt,virtualization=on,gic-version=3 -cpu max -accel tcg \
    -nographic -m 512M -smp 1 -kernel build/wel2.bin ${APPEND[@]+"${APPEND[@]}"} \
    -serial "file:$LOG" -monitor none >build/qemu.out 2>&1 &
QPID=$!

for _ in $(seq 1 200); do
    kill -0 "$QPID" 2>/dev/null || break
    sleep 0.1
done
if kill -0 "$QPID" 2>/dev/null; then
    kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null
    echo "FAIL: QEMU did not exit within 20 s"
    TIMEOUT=1
else
    wait "$QPID"; TIMEOUT=0
fi

tr -d '\r' < "$LOG" > build/uart.txt
grep -a "LR overflow\|stats\|cpuif mode\|TEST knob\|  [0-9]*$" build/uart.txt

rc=0
expect() {
    grep -qF -- "$1" build/uart.txt || { echo "MISSING: $1"; rc=1; }
}
expect "Watari EL1: LR overflow burst (8 SGIs, 4 LRs): ok, every SGI once, in priority order"
expect "Watari EL1: LR overflow eviction (high priority SGI, LRs full): ok, every SGI once, in priority order"
expect "Watari EL2: list registers (LRs) = 0x0000000000000004"
expect "Watari EL2: PSCI SYSTEM_OFF"
nonzero() {   # a stats line must exist and must not be zero
    local l
    l=$(grep -a -F -- "$1" build/uart.txt | head -1)
    if [ -z "$l" ] || [ "${l##*= }" = "0x0000000000000000" ]; then
        echo "MISSING (zero or absent): $1"; rc=1
    fi
}
nonzero "LR evictions (higher priority arrived)"
if [ "${NOEOI:-0}" = 1 ]; then
    expect "TEST knob wel2.test=noeoi"
    # in TRAP_EMULATE mode the guest's EOIR traps, EL2 refills at once and UIE is moot
    [ "${CPUIF:-hw}" = trap ] || nonzero "underflow maintenance irqs (UIE)"
fi
if [ "${CPUIF:-hw}" = trap ]; then
    expect "cpuif mode = TRAP_EMULATE"
else
    expect "cpuif mode = HW"
fi
grep -q "FAILED\|STILL ASLEEP\|unexpected irq" build/uart.txt && { echo "FAIL: a test reported failure"; rc=1; }
grep -q "UNEXPECTED\|FATAL" build/uart.txt && { echo "FAIL: unexpected exception"; rc=1; }
[ "$TIMEOUT" = 1 ] && rc=1
[ $rc = 0 ] && echo "PASS: boot-el2 (M1-5 LR overflow: ${CPUIF:-hw} mode, noeoi=${NOEOI:-0})"
exit $rc
