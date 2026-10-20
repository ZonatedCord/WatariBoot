#!/bin/bash
# SPDX-License-Identifier: MIT
# Every QEMU test of the layer, in both modes of the virtual CPU interface (HW list registers
# and TRAP_EMULATE), plus the host tests. The runs share build/, so they run one after another.
#   ./run-all.sh            all (run-m1-4.sh needs the external data disk and the EDK2 kernel build)
#   SKIP_M14=1 ./run-all.sh without the EDK2 chain test
cd "$(dirname "$0")"
fail=0
run() {   # name, env assignments..., script
    local name="$1"; shift
    if env "$@" >/tmp/wel2-run-all.$$ 2>&1; then
        echo "PASS  $name"
    else
        echo "FAIL  $name"; tail -15 /tmp/wel2-run-all.$$; fail=1
    fi
}
make -s -C .. test >/tmp/wel2-run-all.$$ 2>&1 && echo "PASS  host tests (make -C el2 test)" || { echo "FAIL  host tests"; tail -20 /tmp/wel2-run-all.$$; fail=1; }
for mode in hw trap; do
    for t in run-qemu run-m1 run-m1-2 run-m1-3 run-m1-5; do
        run "$t [$mode]" CPUIF=$mode ./$t.sh
    done
    run "run-m1-5 [$mode, noeoi]" CPUIF=$mode NOEOI=1 ./run-m1-5.sh
    [ "${SKIP_M14:-0}" = 1 ] || run "run-m1-4 [$mode]" CPUIF=$mode ./run-m1-4.sh
done
rm -f /tmp/wel2-run-all.$$
exit $fail
