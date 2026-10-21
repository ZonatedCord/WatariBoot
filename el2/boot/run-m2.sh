#!/bin/bash
# SPDX-License-Identifier: MIT
# M2: boot the installed Windows 11 ARM64 disk through EDK2 (ArmVirtQemuKernel) at EL1 on top of
# Watari EL2, QEMU TCG. Not a pass/fail test: it runs until TIMEOUT seconds (default 3600) or until
# QEMU exits, takes a screenshot every SHOT_EVERY seconds and prints the layer statistics.
# Knobs (environment):
#   CPUIF=trap     virtual CPU interface in TRAP_EMULATE mode (boot argument wel2.cpuif=trap)
#   SMP=4 MEM=4G   cores / RAM          TIMEOUT=3600  seconds        SHOT_EVERY=120  seconds
#   TRACE=SKIP     log trapped GICD/GICR accesses after the first SKIP (wel2.trace=, up to 3000 lines)
#   MSI=its|gicv2m|off|auto   MSI controller of the QEMU machine (default its = current working config;
#                  gicv2m = no ITS, GICv2m frame in the MADT; off = no MSI controller; F-23, #62, #65)
#   ITS=on|off     legacy knob: ITS=off is the same as MSI=off (default on)
#   NOLAYER=1      control run: no Watari EL2, plain QEMU (no virtualization=on) loads the same EDK2 as -kernel
#   STOP_ON_RESET=1  stop at the first PSCI SYSTEM_RESET/OFF (a bugcheck restarts Windows: use SHOT_EVERY=6 to catch the blue screen)
#   TAG=name       run directory name (default: cpuif mode + timestamp)
#   LOGDIR=m2x     log directory under $WATARI_DATA/logs (default m2)
# Output (external disk, never in the repo): $WATARI_DATA/logs/m2/<TAG>/
#   serial.log (layer + EDK2 + Windows KD on COM1), shot-NNNN.png, qemu.out, sockdir.txt
# Sockets live in a short temp dir (path in sockdir.txt): mon.sock (QEMU monitor), ser.sock (COM1).
# The Windows disk is ALWAYS attached with snapshot=on (never modified); never run two QEMUs on it.
# The display wake key is Ctrl (Shift x5 would open the Sticky Keys dialog, F-23). Log in / drive Windows: m2-login.sh.
# Interact with the EDK2 shell/boot manager:  printf 'FS0:\r' | nc -U <sockdir>/ser.sock
set -u
cd "$(dirname "$0")"
# shellcheck source=/dev/null
source ../../tools/env.sh || { echo "FAIL: data disk not mounted"; exit 2; }
FD="$WATARI_DATA/build-kernel/QEMU_EFI-kernel.fd"
DISK="$WATARI_DATA/vm/win11.qcow2"
[ -f "$FD" ] || { echo "FAIL: $FD missing"; exit 2; }
[ -f "$DISK" ] || { echo "FAIL: $DISK missing"; exit 2; }
pgrep -f "qemu-system.*win11.qcow2" >/dev/null && { echo "FAIL: another QEMU uses the Windows disk"; exit 2; }
make -s chain || { echo "FAIL: build"; exit 2; }

MODE="${CPUIF:-hw}"
ARGS=""
[ "$MODE" = trap ] && ARGS="wel2.cpuif=trap"
[ -n "${TRACE:-}" ] && ARGS="$ARGS wel2.trace=$TRACE"
APPEND=()
[ -n "$ARGS" ] && APPEND=(-append "$ARGS")
TAG="${TAG:-${MODE}-$(date +%m%d-%H%M)}"
OUT="$WATARI_DATA/logs/${LOGDIR:-m2}/$TAG"; mkdir -p "$OUT"
SOCKDIR="$(mktemp -d /tmp/wm2.XXXXXX)"   # unix socket paths must be short
LOG="$OUT/serial.log"; : > "$LOG"
echo "$SOCKDIR" > "$OUT/sockdir.txt"
TIMEOUT="${TIMEOUT:-3600}"; SHOT_EVERY="${SHOT_EVERY:-120}"

MSI="${MSI:-}"
[ -z "$MSI" ] && { [ "${ITS:-on}" = off ] && MSI=off || MSI=its; }
case "$MSI" in
    its) MSIOPT="its=on" ;;
    gicv2m) MSIOPT="its=off,msi=gicv2m" ;;
    off) MSIOPT="its=off,msi=off" ;;
    auto) MSIOPT="its=off" ;;   # as F-23: its=off, msi left at QEMU auto
    *) echo "FAIL: MSI=$MSI (its|gicv2m|off|auto)"; exit 2 ;;
esac
if [ -n "${NOLAYER:-}" ]; then
    MACH="virt,gic-version=3,$MSIOPT"; LOADER=(-kernel "$FD")
else
    MACH="virt,virtualization=on,gic-version=3,$MSIOPT"
    LOADER=(-kernel build-chain/wel2.bin ${APPEND[@]+"${APPEND[@]}"} -device loader,file="$FD",addr=0x40280000,force-raw=on)
fi
echo "machine: $MACH"
qemu-system-aarch64 -M "$MACH" -cpu max -accel tcg \
    -m "${MEM:-4G}" -smp "${SMP:-4}" -display none -nic none \
    "${LOADER[@]}" \
    -chardev "socket,id=ser0,path=$SOCKDIR/ser.sock,server=on,wait=off,logfile=$LOG" -serial chardev:ser0 \
    -monitor "unix:$SOCKDIR/mon.sock,server,nowait" \
    -device ramfb -device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 -device usb-tablet,bus=xhci.0 \
    -drive if=none,id=hd0,format=qcow2,file="$DISK",snapshot=on \
    -device nvme,drive=hd0,serial=watari0,bootindex=0 \
    >"$OUT/qemu.out" 2>&1 < /dev/null &
QPID=$!
echo "$QPID" > "$OUT/qemu.pid"
echo "QEMU pid $QPID, mode $MODE, output $OUT, sockets $SOCKDIR"

shot() {
    printf 'sendkey ctrl\n' | nc -U "$SOCKDIR/mon.sock" -w 1 >/dev/null 2>&1; sleep 1
    printf 'screendump %s/s.ppm\n' "$SOCKDIR" | nc -U "$SOCKDIR/mon.sock" -w 1 >/dev/null 2>&1; sleep 1
    [ -s "$SOCKDIR/s.ppm" ] && sips -s format png "$SOCKDIR/s.ppm" --out "$OUT/shot-$(printf %04d "$1").png" >/dev/null 2>&1
}
START=$(date +%s); N=0; NEXT=0
while kill -0 "$QPID" 2>/dev/null; do
    NOW=$(( $(date +%s) - START ))
    [ "$NOW" -ge "$TIMEOUT" ] && { echo "timeout after ${NOW}s"; break; }
    if [ -n "${STOP_ON_RESET:-}" ] && grep -aq "PSCI SYSTEM_RESET\|PSCI SYSTEM_OFF" "$LOG"; then echo "guest reset/off after ${NOW}s"; break; fi
    if [ "$NOW" -ge "$NEXT" ]; then N=$((N + 1)); shot "$N"; NEXT=$((NOW + SHOT_EVERY)); fi
    sleep 5
done
kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null
rm -rf "$SOCKDIR"
echo "---- layer statistics (last lines) ----"
tr -d '\r' < "$LOG" | grep -a "^Watari EL2" | grep -a "trapped\|timer irqs\|GICD traps\|GICR traps\|UNEXPECTED\|FATAL\|unhandled\|stray" | tail -12
