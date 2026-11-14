#!/bin/bash
# SPDX-License-Identifier: MIT
# T22 reference run: the SAME winram image (tools/m3a/build-winram-image.sh) booted as an ordinary NVMe disk (snapshot=on, never
# modified) on the regular QEMU-virt path that already boots the installed Windows (EDK2 ArmVirtQemuKernel, QEMU's own ACPI, PCI,
# no layer), QEMU TCG. If WinPE reaches Setup here, the image is fine and a hang on run-winram.sh is the M1-shaped model's.
# Env: IMG=<file> (default $WATARI_DATA/winram/winram.img)  SMP=4 MEM=4G  TIMEOUT=3600  SHOT_EVERY=120  TAG=name  FWDIR=<dir>
# Output: $WATARI_DATA/logs/winram/<TAG>/ (serial.log, shot-NNNN.png). The Windows VM disk (vm/win11.qcow2) is NOT used.
set -u
cd "$(dirname "$0")"
# shellcheck source=/dev/null
source ../../tools/env.sh || { echo "FAIL: data disk not mounted"; exit 2; }
FD="${FWDIR:-$WATARI_DATA/build-kernel}/QEMU_EFI-kernel.fd"
IMG="${IMG:-$WATARI_DATA/winram/winram.img}"
[ -f "$FD" ] && [ -f "$IMG" ] || { echo "FAIL: $FD or $IMG missing"; exit 2; }
TAG="${TAG:-ref-$(date +%m%d-%H%M)}"
OUT="$WATARI_DATA/logs/winram/$TAG"; mkdir -p "$OUT"
TIMEOUT="${TIMEOUT:-3600}"; SHOT_EVERY="${SHOT_EVERY:-120}"
SOCKDIR="$(mktemp -d /tmp/wwf.XXXXXX)"
LOG="$OUT/serial.log"; : > "$LOG"
"${QEMU:-qemu-system-aarch64}" -M virt,gic-version=3,its=off,msi=gicv2m -cpu max -accel tcg \
    -m "${MEM:-4G}" -smp "${SMP:-4}" -display none -nic none -kernel "$FD" \
    -chardev "socket,id=ser0,path=$SOCKDIR/ser.sock,server=on,wait=off,logfile=$LOG" -serial chardev:ser0 \
    -monitor "unix:$SOCKDIR/mon.sock,server,nowait" \
    -device ramfb -device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 \
    -drive if=none,id=hd0,format=raw,file="$IMG",snapshot=on \
    -device nvme,drive=hd0,serial=watari0,bootindex=0 \
    >"$OUT/qemu.out" 2>&1 < /dev/null &
QPID=$!
echo "QEMU pid $QPID, out $OUT"
shot() {
    printf 'sendkey ctrl\n' | nc -U "$SOCKDIR/mon.sock" -w 1 >/dev/null 2>&1; sleep 1
    printf 'screendump %s/s.ppm\n' "$SOCKDIR" | nc -U "$SOCKDIR/mon.sock" -w 1 >/dev/null 2>&1; sleep 1
    [ -s "$SOCKDIR/s.ppm" ] && sips -s format png "$SOCKDIR/s.ppm" --out "$OUT/shot-$(printf %04d "$1").png" >/dev/null 2>&1
}
T0=$(date +%s); N=0; NEXT=0
while kill -0 "$QPID" 2>/dev/null; do
    NOW=$(( $(date +%s) - T0 ))
    if [ "$NOW" -ge "$TIMEOUT" ]; then echo "TIMEOUT after ${NOW}s"; break; fi
    if [ "$NOW" -ge "$NEXT" ]; then N=$((N + 1)); NEXT=$((NOW + SHOT_EVERY)); shot "$N"; echo "[$NOW s] shot $N"; fi
    sleep 5
done
kill -0 "$QPID" 2>/dev/null && kill "$QPID" 2>/dev/null
wait "$QPID" 2>/dev/null
echo "run directory: $OUT"
