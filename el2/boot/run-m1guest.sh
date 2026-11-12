#!/bin/bash
# SPDX-License-Identifier: MIT
# T7a: the EL1 test guest (el2/guest, checks G1-G11 of DESIGN section 8) on top of Watari EL2, QEMU TCG, -smp 4.
#   QEMU -kernel build-chain/wel2.bin         : the layer, chain build (x0 = FDT)
#   QEMU -device loader,addr=0x40280000       : the guest image, entered by the layer at EL1 instead of EDK2
# wel2.guest=1 makes the layer pass the guest info block in x0 and enter the guest at the EDK2 entry. QEMU runs with
# -cpu max,pauth=off until T6g sets HCR_EL2.API/APK (the guest FAILs G3 if PAuth exists and the layer did not enable it).
# Until the layer implements the vendor hypercalls (docs/design/watari-el2-m1boot/T7-wiring.md) the guest sees
# NOT_SUPPORTED from WEL2_LOG and falls back to the QEMU PL011 (same text), so the checks that do not need the wiring
# work today; G5a/G5b judge the trap counters only once WEL2_STATS exists (otherwise "counters n/a").
# Env: CPUIF=hw|trap|trap-notc (default hw), WEL2_ARGS=extra boot arguments (e.g. "wel2.e2h=1 wel2.gran=16k").
# Exit 0 = success. 60 s timeout.
set -u
cd "$(dirname "$0")"
ARGS="wel2.guest=1"
case "${CPUIF:-hw}" in
    trap) ARGS="$ARGS wel2.cpuif=trap" ;;
    trap-notc) ARGS="$ARGS wel2.cpuif=trap-notc" ;;
    hw) ;;
    *) echo "FAIL: CPUIF must be hw, trap or trap-notc"; exit 2 ;;
esac
[ -n "${WEL2_ARGS:-}" ] && ARGS="$ARGS $WEL2_ARGS"
APPEND=()
[ -n "${ARGS# }" ] && APPEND=(-append "${ARGS# }")
make -s chain || { echo "FAIL: layer build"; exit 2; }
make -s -C ../guest || { echo "FAIL: guest build"; exit 2; }
GUEST=../guest/build/m1guest.bin
OUT=build-chain
LOG=$OUT/guest-uart.log
: > "$LOG"
"${QEMU:-qemu-system-aarch64}" -M virt,virtualization=on,gic-version=3 -cpu "${QEMU_CPU:-max,pauth=off}" -accel tcg \
    -nographic -m 512M -smp 4 -nic none -no-reboot -kernel $OUT/wel2.bin ${APPEND[@]+"${APPEND[@]}"} \
    -device loader,file="$GUEST",addr=0x40280000,force-raw=on \
    -serial "file:$LOG" -monitor none >$OUT/guest-qemu.out 2>&1 &
QPID=$!
EXITED=0
for _ in $(seq 1 600); do
    kill -0 "$QPID" 2>/dev/null || { EXITED=1; break; }
    sleep 0.1
done
kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null
tr -d '\r' < "$LOG" > $OUT/guest-uart.txt
grep -aE "Watari EL1:|^Watari EL2: (PSCI|guest|HVC)|FATAL|UNEXPECTED|unhandled|halted" $OUT/guest-uart.txt | head -80

rc=0
# The guest's own lines, whichever sink carried them: "...G5a PASS ..." (HVC log line or PL011 fallback).
gline() { grep -aE "(^|[: ])$1 (PASS|FAIL|SKIP) " $OUT/guest-uart.txt | head -1; }
echo "---- results"
for g in G1 G2 G3 G4 G5a G5b G6 G7 G8 G9 G10 G11; do
    l=$(gline $g)
    if [ -z "$l" ]; then echo "MISSING: $g"; rc=1
    elif echo "$l" | grep -q " FAIL "; then echo "FAILED: $l"; rc=1
    else echo "ok: $g"; fi
done
# T7 wiring: the output arrives through WEL2_LOG (the layer's "Watari EL1:" prefix, no PL011 fallback), and in a trap
# mode G5a/G5b are judged on the layer's counters (not "counters n/a").
grep -aq "guest info block at" $OUT/guest-uart.txt || { echo "MISSING: the layer did not pass the guest info block"; rc=1; }
if [ "${CPUIF:-hw}" != hw ]; then
    for g in G5a G5b; do
        gline $g | grep -q "n/a" && { echo "FAILED: $g judged without the layer's counters"; rc=1; }
    done
fi
grep -q "UNEXPECTED\|FATAL\|unhandled guest\|non-PSCI SMC" $OUT/guest-uart.txt && { echo "FAIL: unexpected event reported"; rc=1; }
grep -aq "PSCI SYSTEM_\(RESET\|OFF\)" $OUT/guest-uart.txt || { echo "MISSING: the layer's SYSTEM_RESET/OFF line"; rc=1; }
[ "$EXITED" = 1 ] || { echo "FAIL: QEMU still running after 60 s (SYSTEM_RESET not honoured?)"; rc=1; }
[ $rc = 0 ] && echo "PASS: EL1 test guest G1-G11 under Watari EL2 (CPUIF=${CPUIF:-hw})"
exit $rc
