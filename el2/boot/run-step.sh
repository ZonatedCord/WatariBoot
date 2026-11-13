#!/bin/bash
# SPDX-License-Identifier: MIT
# T6g (PLAN S13, DESIGN 9): the boot ladder in QEMU. The chain build, QEMU TCG, -smp 4, -no-reboot (a PSCI SYSTEM_RESET ends
# the QEMU process, which stands in for the M1's watchdog reset).
#   wel2.step=1  the layer resets right after the relocation: not one line of text from C (no banner, no stage 1 report)
#   wel2.step=2  it resets after checkpoint 5 (platform init done), i.e. after the banner and before stage 2 / the guest
#   wel2.step=3  the guest runs G1-G6 only (G7-G10 absent), G11 always
#   no step      (not run here: run-m1guest.sh covers it) the whole guest
# Env: CPUIF=hw|trap|trap-notc (default trap), QEMU. Exit 0 on success. About 30 s.
set -u
cd "$(dirname "$0")"
make -s chain || { echo "FAIL: layer build"; exit 2; }
make -s -C ../guest || { echo "FAIL: guest build"; exit 2; }
GUEST=../guest/build/m1guest.bin
OUT=build-chain
CPUIF="${CPUIF:-trap}"
rc=0

# run_qemu <step>: boots the layer with wel2.step=<step> (+ the guest image in memory), waits for QEMU to exit (reset).
run_qemu() {
    local step="$1" log="$OUT/step$1-uart.log" args="wel2.guest=1 wel2.step=$1"
    case "$CPUIF" in trap|trap-notc) args="$args wel2.cpuif=$CPUIF" ;; esac
    : > "$log"
    "${QEMU:-qemu-system-aarch64}" -M virt,virtualization=on,gic-version=3 -cpu max -accel tcg \
        -nographic -m 512M -smp 4 -nic none -no-reboot -kernel $OUT/wel2.bin -append "$args" \
        -device loader,file="$GUEST",addr=0x40280000,force-raw=on \
        -serial "file:$log" -monitor none >"$OUT/step$1-qemu.out" 2>&1 &
    local qpid=$!
    EXITED=0
    for _ in $(seq 1 600); do
        kill -0 "$qpid" 2>/dev/null || { EXITED=1; break; }
        sleep 0.1
    done
    kill "$qpid" 2>/dev/null; wait "$qpid" 2>/dev/null
    tr -d '\r' < "$log" > "$OUT/step$1-uart.txt"
}
has() { grep -aqF -- "$2" "$OUT/step$1-uart.txt"; }
need() { has "$1" "$2" || { echo "MISSING (step $1): $2"; rc=1; }; }
forbid() { has "$1" "$2" && { echo "UNEXPECTED (step $1): $2"; rc=1; }; }

# ---- step 1: reset before any text
run_qemu 1
[ "$EXITED" = 1 ] || { echo "FAIL (step 1): QEMU still running after 60 s, no reset"; rc=1; }
[ -s "$OUT/step1-uart.txt" ] && { echo "UNEXPECTED (step 1): text on the UART before the reset:"; head -3 "$OUT/step1-uart.txt"; rc=1; }
echo "step 1: reset with no UART text: $([ ! -s "$OUT/step1-uart.txt" ] && [ "$EXITED" = 1 ] && echo ok || echo FAILED)"

# ---- step 2: banner, platform init, then reset
run_qemu 2
[ "$EXITED" = 1 ] || { echo "FAIL (step 2): QEMU still running after 60 s, no reset"; rc=1; }
need 2 "Watari EL2: hello from EL2"
need 2 "Watari EL2: checkpoint 2 BANNER"
need 2 "Watari EL2: checkpoint 5 CPU_INIT"
need 2 "Watari EL2: wel2.step=2: platform and interrupt controller are up, resetting the machine"
forbid 2 "stage-2 on"
forbid 2 "checkpoint 6"
forbid 2 "Watari EL1:"
forbid 2 "FATAL"
echo "step 2: banner and checkpoints 0-5, then reset, no stage 2: $([ $rc = 0 ] && echo ok || echo FAILED)"

# ---- step 3: the guest runs G1-G6 only
run_qemu 3
[ "$EXITED" = 1 ] || { echo "FAIL (step 3): QEMU still running after 60 s"; rc=1; }
for g in G1 G2 G3 G4 G5a G5b G6; do
    grep -aqE "(^|[: ])$g (PASS|FAIL|SKIP) " "$OUT/step3-uart.txt" || { echo "MISSING (step 3): $g"; rc=1; }
done
for g in G7 G8 G9 G10; do
    grep -aqE "(^|[: ])$g (PASS|FAIL|SKIP) " "$OUT/step3-uart.txt" && { echo "UNEXPECTED (step 3): $g ran"; rc=1; }
done
need 3 "Watari EL2: checkpoint 8 ENTER_GUEST"
grep -aE "(^|[: ])G[0-9]+[ab]? (FAIL) " "$OUT/step3-uart.txt" && { echo "FAIL (step 3): a guest check failed"; rc=1; }
echo "step 3: G1-G6 only: $([ $rc = 0 ] && echo ok || echo FAILED)"

[ $rc = 0 ] && echo "PASS: wel2.step=1 resets before any text, 2 after checkpoint 5, 3 runs G1-G6 (CPUIF=$CPUIF)"
exit $rc
