#!/bin/bash
# SPDX-License-Identifier: MIT
# F-43 on the bench: is INTERNAL_POWER_ERROR (0xA0) about the cores the tables describe, or about the GIC?
# Runs el2/boot/run-winram.sh several times, ONE QEMU at a time, on the M1-shaped bench (j293 GIC windows, 10-bit fake panel, so the
# F-44 colours show up in shot-NNNN.png; shot-NNNN-as8.png is the same memory read as the 8-bit pixels Windows wrote).
# Usage: run-0xa0-matrix.sh [--dry-run] [test ...]        (default: all, in the order below)
# Env: FD_J293, FD_E1 (firmwares; their .build.txt must say model=j293 / j293-e1 and bgr8=1), TIMEOUT=480, SHOT_EVERY=30
# A run is NOT judged here: read the last screenshot. Nothing is downloaded; the Windows VM disk is not touched.
set -u
cd "$(dirname "$0")" || exit 2
DRY=0; [ "${1:-}" = "--dry-run" ] && { DRY=1; shift; }
# shellcheck source=/dev/null
source ../../tools/env.sh || exit 2
FD_J293="${FD_J293:-$WATARI_DATA/build-m1/WATARI_M1_EFI-bgr8.fd}"
# e1 was built at 84f37c4, the current j293 firmware later (30d015a): c1-j293old uses the j293 firmware of the SAME commit as e1
FD_OLD="${FD_OLD:-$WATARI_DATA/build-m1/WATARI_M1_EFI-bgr8.fd.pre-catch}"
FD_E1="${FD_E1:-$WATARI_DATA/build-m1/WATARI_M1_EFI-bgr8-e1.fd}"
export TIMEOUT="${TIMEOUT:-480}" SHOT_EVERY="${SHOT_EVERY:-30}" PANEL=10 MEM=3G RDBASE=0x80000000 QEMU_EXTRA="-no-shutdown"
STAMP="${STAMP:-$(date +%m%d-%H%M)}"
M1=wel2.vgicipa=m1
need() {   # need <fd> <model>
    grep -qx "model=$2" "$1.build.txt" 2>/dev/null && grep -qx "bgr8=1" "$1.build.txt" && grep -qx "bench=0" "$1.build.txt" ||
        { echo "SKIP: $1 missing or its manifest does not say model=$2 bgr8=1 bench=0"; return 1; }
}
run() {    # run <name> <fd> <model> <smp> <cpuif> <layer args>
    need "$2" "$3" || return
    echo "##### $1: fd=$(basename "$2") smp=$4 cpuif=$5 args=$6"
    [ $DRY = 1 ] && return
    pgrep -x qemu-system-aarch64 >/dev/null && { echo "FAIL: another QEMU is running"; exit 2; }
    FD="$2" SMP="$4" CPUIF="$5" WEL2_ARGS="$6" TAG="a0-$STAMP-$1" ./run-winram.sh > "/tmp/a0-$STAMP-$1.out" 2>&1
    local d="$WATARI_DATA/logs/winram/a0-$STAMP-$1"
    mv "/tmp/a0-$STAMP-$1.out" "$d/run.out" 2>/dev/null
    echo "  CPU_ON accepted: $(grep -ac 'PSCI CPU_ON cpu [0-9]* accepted' "$d/serial.txt")  SYSTEM_RESET: $(grep -ac 'SYSTEM_RESET' "$d/serial.txt")  FATAL: $(grep -ac FATAL "$d/serial.txt")  last shot: $(ls "$d"/shot-????.png 2>/dev/null | tail -1)"
}
one() {
    case "$1" in
        c1-e1)      run "$1" "$FD_E1"   j293-e1 1 trap      "$M1" ;;                   # control: 1 core, tables with 1 core
        c1-j293)    run "$1" "$FD_J293" j293    1 trap      "$M1" ;;                   # W1c: 1 core, tables with 8
        c1-j293old) run "$1" "$FD_OLD"  j293    1 trap      "$M1" ;;                   # as c1-j293, firmware of e1's commit: only the tables differ from c1-e1
        c8-j293)    run "$1" "$FD_J293" j293    8 trap      "$M1 wel2.vmpidr=m1" ;;    # control: 8 cores, tables with 8
        c8-notc)    run "$1" "$FD_J293" j293    8 trap-notc "$M1 wel2.vmpidr=m1" ;;    # the next hardware rung's layer options (W2)
        c1-notc)    run "$1" "$FD_J293" j293    1 trap-notc "$M1" ;;                   # GIC: only the CPU-interface mode changes
        c1-hw)      run "$1" "$FD_J293" j293    1 hw        "$M1" ;;                   # GIC: QEMU's hardware CPU interface
        c1-trace)   run "$1" "$FD_J293" j293    1 trap      "$M1 wel2.trace=0" ;;    # GIC: the first 3000 GICD/GICR accesses made by the Windows kernel
        c1-catch)   run "$1" "$FD_J293" j293    1 trap      "$M1 wel2.catch=pmu" ;;    # W1c's trapped PMU
        *) echo "unknown test $1"; return 1 ;;
    esac
}
[ $# -gt 0 ] || set -- c1-e1 c1-j293old c1-j293 c8-j293 c8-notc c1-notc c1-hw c1-trace c1-catch
for t in "$@"; do one "$t"; done
