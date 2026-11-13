#!/bin/bash
# SPDX-License-Identifier: MIT
# Every QEMU test of the layer, in the three modes of the virtual CPU interface (HW list registers,
# TRAP_EMULATE and, T6g, trap-notc = TRAP_EMULATE without ICH_HCR_EL2.TC), plus the host tests. The runs share build/, so they run one after another.
#   ./run-all.sh            all (run-m1-4.sh needs the external data disk and the EDK2 kernel build)
#   SKIP_M14=1 ./run-all.sh without the EDK2 chain test
#   run-m1-6.sh (hybrid QEMU machine with the Watari AIC, #42) runs only if our QEMU binary exists
#   (QEMU_WATARI=path, default /private/tmp/watari-qemu/build/qemu-system-aarch64)
cd "$(dirname "$0")"
fail=0
run() {   # name, env assignments..., script
    local name="$1"; shift
    env "$@" >/tmp/wel2-run-all.$$ 2>&1
    case $? in
        0) echo "PASS  $name" ;;
        77) echo "SKIP  $name" ;;
        *) echo "FAIL  $name"; tail -15 /tmp/wel2-run-all.$$; fail=1 ;;
    esac
}
make -s -C .. test >/tmp/wel2-run-all.$$ 2>&1 && echo "PASS  host tests (make -C el2 test)" || { echo "FAIL  host tests"; tail -20 /tmp/wel2-run-all.$$; fail=1; }
for mode in hw trap trap-notc; do
    for t in run-qemu run-m1 run-m1-2 run-m1-3 run-m1-5 run-m1guest; do
        run "$t [$mode]" CPUIF=$mode ./$t.sh
    done
    run "run-m1-5 [$mode, noeoi]" CPUIF=$mode NOEOI=1 ./run-m1-5.sh
    [ "${SKIP_M14:-0}" = 1 ] || run "run-m1-4 [$mode]" CPUIF=$mode ./run-m1-4.sh
    # Optional (#42): needs our QEMU build (tools/qemu-watari/build.sh); SKIP when the binary is not there.
    [ -x "${QEMU_WATARI:-/private/tmp/watari-qemu/build/qemu-system-aarch64}" ] &&
        run "run-m1-6 (hybrid AIC machine) [$mode]" CPUIF=$mode QEMU="${QEMU_WATARI:-/private/tmp/watari-qemu/build/qemu-system-aarch64}" ./run-m1-6.sh
done
# T6g: the ladder, the ID registers with and without TID3, and the negative control of G5b (one run each, not per mode)
run "run-step (wel2.step=1, 2, 3)" ./run-step.sh
run "run-hold (wel2.hold=1 keeps the final state, no reset)" ./run-hold.sh
run "run-tid3 (ID registers with and without TID3)" ./run-tid3.sh
run "run-gfdt (guest FDT hand-off, wel2.abi=fdt, wel2.split=3)" SPLIT=3 ./run-gfdt.sh
run "run-g5b-negative (TC bit disagrees with the mode)" ./run-g5b-negative.sh
rm -f /tmp/wel2-run-all.$$
exit $fail
