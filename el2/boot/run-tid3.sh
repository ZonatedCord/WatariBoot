#!/bin/bash
# SPDX-License-Identifier: MIT
# T6g (PLAN S12): ID registers with and without HCR_EL2.TID3. The test guest (G3) prints every allocated register of the ID
# group ("IDREG c<CRm>.<op2 base> ..."); this script runs the guest twice, once without and once with wel2.tid3=1, and
# requires: identical IDREG lines (QEMU -cpu max already reports ID_AA64PFR0_EL1.GIC = 1, so nothing may change at all),
# the layer's TID3 counter absent without and >= the number of registers with the trap, and every guest check passing in
# both runs. (The GIC override itself, 0 -> 1 with every other bit unchanged, is the host test tests/test_idreg.c.)
# Env: CPUIF=hw|trap|trap-notc (default trap), QEMU. Exit 0 on success.
set -u
cd "$(dirname "$0")"
OUT=build-chain
CPUIF="${CPUIF:-trap}"
rc=0
for t in off on; do
    args=""
    [ "$t" = on ] && args="wel2.tid3=1"
    CPUIF="$CPUIF" WEL2_ARGS="$args" ./run-m1guest.sh >"$OUT/tid3-$t.out" 2>&1 || { echo "FAIL: run-m1guest.sh (tid3 $t)"; tail -20 "$OUT/tid3-$t.out"; rc=1; }
    grep -a "IDREG" $OUT/guest-uart.txt | sed 's/^.*IDREG/IDREG/' > "$OUT/tid3-$t.idreg"
    grep -a "TID3\|HCR_EL2" $OUT/guest-uart.txt > "$OUT/tid3-$t.layer"
    grep -a "G3 " $OUT/guest-uart.txt | head -1 > "$OUT/tid3-$t.g3"
done
n=$(wc -l < "$OUT/tid3-off.idreg" | tr -d ' ')
[ "$n" = 14 ] || { echo "FAIL: expected 14 IDREG lines, got $n"; rc=1; }
if diff "$OUT/tid3-off.idreg" "$OUT/tid3-on.idreg" >"$OUT/tid3.diff"; then
    echo "ok: the $n IDREG lines (36 registers) are identical with and without TID3"
else
    echo "FAIL: ID registers differ with TID3:"; cat "$OUT/tid3.diff"; rc=1
fi
grep -aq "HCR_EL2.TID3 set" "$OUT/tid3-on.layer" || { echo "FAIL: the layer did not report TID3 set"; rc=1; }
grep -aq "HCR_EL2.TID3 set" "$OUT/tid3-off.layer" && { echo "FAIL: TID3 set without wel2.tid3=1"; rc=1; }
cnt=$(grep -a "TID3 ID-register reads emulated" "$OUT/tid3-on.layer" | head -1 | sed 's/.*= //')
if [ -n "$cnt" ] && [ $((cnt)) -ge 36 ]; then echo "ok: $((cnt)) ID register reads were trapped and emulated"; else echo "FAIL: TID3 counter ${cnt:-missing} (expected >= 36)"; rc=1; fi
grep -aq "TID3 ID-register reads emulated" "$OUT/tid3-off.layer" && { echo "FAIL: a TID3 counter without TID3"; rc=1; }
sed 's/^/   /' "$OUT/tid3-on.g3"
[ $rc = 0 ] && echo "PASS: ID registers unchanged by TID3 (CPUIF=$CPUIF)"
exit $rc
