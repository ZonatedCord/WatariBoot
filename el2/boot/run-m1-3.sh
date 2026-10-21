#!/bin/bash
# SPDX-License-Identifier: MIT
# M1-3: SMP + peripheral interrupts. Boots the image on QEMU TCG with 4 cores
# (only the boot core runs; the others are started by the guest's PSCI CPU_ON
# and stopped by CPU_OFF), forwards an SPI (PL031 RTC alarm, INTID 34) and checks
# the UART output. 60 s timeout. Exit 0 on success.
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
    -nographic -m 512M -smp 4 -kernel build/wel2.bin ${APPEND[@]+"${APPEND[@]}"} -serial "file:$LOG" -monitor none \
    >build/qemu.out 2>&1 &
QPID=$!

# portable 60 s timeout (macOS has no timeout(1))
for _ in $(seq 1 600); do
    kill -0 "$QPID" 2>/dev/null || break
    sleep 0.1
done
if kill -0 "$QPID" 2>/dev/null; then
    kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null
    echo "FAIL: QEMU did not exit within 60 s (SYSTEM_OFF not honoured?)"
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
expect "Watari EL2: cores (redistributors) = 0x0000000000000004"
expect "Watari EL1: cores visible in the vGIC = 0x0000000000000004"
expect "Watari EL1: psci contract ok"
for n in 1 2 3; do
    expect "Watari EL2: PSCI CPU_ON cpu $n accepted"
    expect "Watari EL2: cpu $n up at EL2, eret to the guest entry point"
    expect "Watari EL1: cpu $n online"
    expect "Watari EL1: cpu $n timer irq 27 x3"
    expect "Watari EL1: cpu $n sgi 2"
    expect "Watari EL1: cpu $n going off (CPU_OFF)"
    expect "Watari EL2: PSCI CPU_OFF cpu $n"
    expect "Watari EL1: cpu $n is OFF (AFFINITY_INFO = 1)"
done
expect "Watari EL1: cpu 3 spi 34 received"
expect "Watari EL2: SPIs re-routed away from the core going off = 0x0000000000000001"
expect "Watari EL1: retarget: no delivery yet, migrating SPI 34 to cpu 0"
# The physical SPI must have reached core 0 BEFORE the guest migrated it (the layer moved the routing at CPU_OFF).
l_taken=$(grep -n "physical SPI 34 taken on cpu 0" build/uart.txt | head -1 | cut -d: -f1)
l_mig=$(grep -n "retarget: no delivery yet" build/uart.txt | head -1 | cut -d: -f1)
{ [ -n "$l_taken" ] && [ -n "$l_mig" ] && [ "$l_taken" -lt "$l_mig" ]; } || { echo "MISSING: physical SPI 34 on cpu 0 before the guest's migration"; rc=1; }
expect "Watari EL1: retarget ok, SPI 34 stayed pending while cpu 3 was off and arrived on cpu 0 after migration"
expect "Watari EL1: cpu 1 online again after CPU_OFF"
expect "Watari EL2: PSCI SYSTEM_OFF"
grep -q "FAILED\|STILL ASLEEP\|unexpected irq\|DID NOT\|NEVER\|WRONG\|did not\|timeout" build/uart.txt && { echo "FAIL: a test reported failure"; rc=1; }
grep -q "UNEXPECTED\|FATAL" build/uart.txt && { echo "FAIL: unexpected exception"; rc=1; }
[ "$TIMEOUT" = 1 ] && rc=1
[ $rc = 0 ] && echo "PASS: boot-el2 (M1-3 SMP: PSCI CPU_ON/OFF, SGI across cores, forwarded SPI)"
exit $rc
