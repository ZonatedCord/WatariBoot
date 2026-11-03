#!/bin/bash
# SPDX-License-Identifier: MIT
# #42: the M1-3 SMP test on the hybrid QEMU machine (GICv3 + Watari AIC, `aic=on`). The PL031 RTC
# alarm is wired to AIC input 2 instead of GIC SPI 2: the physical GIC never sees it. The layer finds
# the AIC in the FDT, takes the event from the AIC EVENT register, injects it as vINTID 34 and the
# unchanged EL1 payload receives "spi 34" on 4 cores. The re-target on CPU_OFF now moves the
# AIC TARGET_CPU bit. 60 s timeout. Exit 0 on success.
#
# Needs our QEMU build (tools/qemu-watari/build.sh); the Homebrew QEMU has no `aic=on`.
#   QEMU=/private/tmp/watari-qemu/build/qemu-system-aarch64 ./run-m1-6.sh
# Without a binary (QEMU unset and the default path missing) the script exits 77 = skipped.
# CPUIF=trap runs the virtual CPU interface in TRAP_EMULATE mode; WEL2_ARGS adds boot arguments.
set -u
QEMU="${QEMU:-/private/tmp/watari-qemu/build/qemu-system-aarch64}"
[ -x "$QEMU" ] || { echo "SKIP: no Watari QEMU build at $QEMU (tools/qemu-watari/build.sh)"; exit 77; }
"$QEMU" -M help 2>/dev/null | grep -q '^virt ' || { echo "SKIP: $QEMU is not a QEMU with the virt machine"; exit 77; }
"$QEMU" -M virt,help 2>&1 | grep -q 'aic=' || { echo "SKIP: $QEMU has no aic option (stock QEMU?)"; exit 77; }
APPEND=()
[ "${CPUIF:-hw}" = trap ] && APPEND=(-append "wel2.cpuif=trap")
[ -n "${WEL2_ARGS:-}" ] && APPEND=(-append "${APPEND[1]:-} $WEL2_ARGS")
cd "$(dirname "$0")"
make -s || { echo "FAIL: build"; exit 2; }

LOG=build/uart-aic.log
: > "$LOG"
"$QEMU" -M virt,virtualization=on,gic-version=3,aic=on${MACHINE_OPTS:+,$MACHINE_OPTS} -cpu max -accel tcg \
    -nographic -m 512M -smp 4 -kernel build/wel2.bin ${APPEND[@]+"${APPEND[@]}"} -serial "file:$LOG" -monitor none \
    >build/qemu-aic.out 2>&1 &
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

tr -d '\r' < "$LOG" > build/uart-aic.txt
U=build/uart-aic.txt
cat "$U"

rc=0
expect() {
    grep -qF -- "$1" "$U" || { echo "MISSING: $1"; rc=1; }
}
# The layer found and initialised the AIC: window, 128 inputs, RTC + UART0 routed (2 devices)
expect "Watari EL2: AIC at 0x000000000f004000"
expect "Watari EL2:   inputs = 0x0000000000000080"
expect "Watari EL2:   devices routed to the AIC = 0x0000000000000002"
expect "Watari EL2: stage-2 on (identity, GICD/GICR unmapped), vGIC ready"
expect "Watari EL2: cores (redistributors) = 0x0000000000000004"
expect "Watari EL1: cores visible in the vGIC = 0x0000000000000004"
expect "Watari EL1: psci contract ok"
for n in 1 2 3; do
    expect "Watari EL2: PSCI CPU_ON cpu $n accepted"
    expect "Watari EL1: cpu $n online"
    expect "Watari EL1: cpu $n timer irq 27 x3"
    expect "Watari EL1: cpu $n sgi 2"
    expect "Watari EL2: PSCI CPU_OFF cpu $n"
    expect "Watari EL1: cpu $n is OFF (AFFINITY_INFO = 1)"
done
# The interrupt reached the guest as vINTID 34 on the routed core, through the AIC
expect "Watari EL1: cpu 3 spi 34 received"
expect "Watari EL2: AIC event taken on cpu 3"
expect "Watari EL2:   AIC hwirq = 0x0000000000000002"
expect "Watari EL2:   injected as vINTID = 0x0000000000000022"
# ... and NOT through the physical GIC (the RTC is not wired to it on this machine)
grep -qF "physical SPI 34 taken" "$U" && { echo "FAIL: the RTC reached the physical GIC"; rc=1; }
# Re-target on CPU_OFF: the layer moved the AIC TARGET_CPU bit, the event came on cpu 0 BEFORE the guest migrated it
expect "Watari EL2: SPIs re-routed away from the core going off = 0x0000000000000001"
expect "Watari EL1: retarget: no delivery yet, migrating SPI 34 to cpu 0"
l_taken=$(grep -n "AIC event taken on cpu 0" "$U" | head -1 | cut -d: -f1)
l_mig=$(grep -n "retarget: no delivery yet" "$U" | head -1 | cut -d: -f1)
{ [ -n "$l_taken" ] && [ -n "$l_mig" ] && [ "$l_taken" -lt "$l_mig" ]; } || { echo "MISSING: AIC event on cpu 0 before the guest's migration"; rc=1; }
expect "Watari EL1: retarget ok, SPI 34 stayed pending while cpu 3 was off and arrived on cpu 0 after migration"
expect "Watari EL1: cpu 1 cross-core kick eviction: ok, the better SGI came first, every SGI once"
expect "Watari EL1: cpu 1 online again after CPU_OFF"
expect "Watari EL2: PSCI SYSTEM_OFF"
# AIC statistics: events seen, none unexpected, nothing denied
l=$(grep -a -F -- "stats: AIC events = " "$U" | head -1)
{ [ -n "$l" ] && [ "${l##*= }" != "0x0000000000000000" ]; } || { echo "MISSING (zero or absent): stats: AIC events"; rc=1; }
expect "Watari EL2: stats: AIC unexpected (IPI / bad) = 0x0000000000000000"
grep -q "FAILED\|STILL ASLEEP\|unexpected irq\|DID NOT\|NEVER\|WRONG\|did not\|timeout" "$U" && { echo "FAIL: a test reported failure"; rc=1; }
grep -q "UNEXPECTED\|FATAL\|unexpected AIC event" "$U" && { echo "FAIL: unexpected exception or AIC event"; rc=1; }
[ "$TIMEOUT" = 1 ] && rc=1
[ $rc = 0 ] && echo "PASS: boot-el2 on the hybrid machine (#42: PL031 alarm via the Watari AIC as vINTID 34, 4 cores, re-target on CPU_OFF)"
exit $rc
