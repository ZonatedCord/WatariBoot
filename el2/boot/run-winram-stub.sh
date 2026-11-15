#!/bin/bash
# SPDX-License-Identifier: MIT
# T21: the RAM-disk plumbing of docs/design/watari-winram, end to end, with a STAND-IN instead of Windows (no Microsoft file).
# Firmware of tools/build-edk2-m1.sh --ramdisk (production configuration + the stand-in application WatariM1BenchBootTest.efi) on
# the layer, QEMU TCG, -m 4G -smp 4, no split: a 64 MiB FAT32 (ESP) image with the stand-in at \EFI\BOOT\BOOTAA64.EFI and a dummy
# \EFI\Microsoft\Boot\BCD, plus the 4 KiB trailer, is loaded by QEMU (-device loader) at 0x100000000 (ABOVE 4 GB, the HighMem path of
# the driver: GCD AddMemorySpace + Cpu->SetMemoryAttributes) the way watari-load.py will put the real image. The layer is started with
# wel2.ramdisk=0x100000000,0x4001000: it writes /reserved-memory/ramdisk@100000000 and cuts the range out of /memory; WatariM1RamDiskDxe
# claims it as EfiBootServicesData, checks the CRC-32, registers the RAM disk; EDK2 finds the file system and boots the stand-in from it.
#   PASS = "T16A BOOTED from \EFI\BOOT\BOOTAA64.EFI" after the driver's "registered: Success" line.
# Cases (CASES="..."): ramdisk (the above) and nonode (same firmware, no wel2.ramdisk: the driver says nothing and the self-test runs,
# i.e. the firmware boots as before). Env: CPUIF=hw|trap|trap-notc (default trap), FD, APP, WATARI_M1_BUILD_TAG, WEL2_ARGS, QEMU.
# Standalone (like run-t16a.sh): it needs tools/build-edk2-m1.sh --ramdisk and the data disk. Nothing is downloaded or installed.
set -u
cd "$(dirname "$0")"
CPUIF="${CPUIF:-trap}"
CASES="${CASES:-ramdisk nonode}"
ENTRY=0x40280000
RDBASE=0x100000000
RDSIZE=0x4001000       # 64 MiB image + 4 KiB trailer
TAGSUF=""; [ -n "${WATARI_M1_BUILD_TAG:-}" ] && TAGSUF="-$WATARI_M1_BUILD_TAG"
# shellcheck source=/dev/null
source ../../tools/env.sh || { echo "FAIL: data disk not mounted"; exit 2; }
FD="${FD:-$WATARI_DATA/build-m1/WATARI_M1_EFI-ramdisk${TAGSUF}.fd}"
APP="${APP:-$WATARI_DATA/build-m1/WatariM1BenchBootTest${TAGSUF}.efi}"
[ -f "$FD" ] && [ -f "$APP" ] || { echo "FAIL: $FD or $APP missing: run tools/build-edk2-m1.sh --ramdisk first"; exit 2; }
make -s chain || { echo "FAIL: layer build"; exit 2; }
OUT=build-chain
IMG=$OUT/winram-stub
mkdir -p "$IMG"
SRC="$IMG/src"
rm -rf "$SRC" "$IMG/disk.cdr" "$IMG/disk.raw" "$IMG/winram.raw"
mkdir -p "$SRC/EFI/BOOT" "$SRC/EFI/Microsoft/Boot"
cp "$APP" "$SRC/EFI/BOOT/BOOTAA64.EFI"
echo "stand-in: not a BCD" > "$SRC/EFI/Microsoft/Boot/BCD"
hdiutil create -quiet -size 64m -fs "MS-DOS FAT32" -volname WINRAM -layout GPTSPUD -srcfolder "$SRC" -format UDTO -ov "$IMG/disk" >/dev/null || { echo "FAIL: hdiutil"; exit 2; }
mv "$IMG/disk.cdr" "$IMG/disk.raw"
[ "$(stat -f %z "$IMG/disk.raw")" = 67108864 ] || { echo "FAIL: image is not 64 MiB"; exit 2; }
python3 -I ../../tools/m3a/winram_trailer.py append "$IMG/disk.raw" "$IMG/winram.raw" || { echo "FAIL: trailer"; exit 2; }
total_rc=0

run_case() {
    local c="$1" rc=0 RD="" LD=()
    local LOG=$OUT/winram-stub-$c-uart.log T=$OUT/winram-stub-$c-uart.txt
    if [ "$c" = ramdisk ]; then
        RD="wel2.ramdisk=$RDBASE,$RDSIZE"
        LD=(-device loader,file="$IMG/winram.raw",addr="$RDBASE",force-raw=on)
    fi
    local ARGS="wel2.guest=$ENTRY wel2.abi=fdt wel2.gfdt=build wel2.cpuif=$CPUIF $RD ${WEL2_ARGS:-}"
    : > "$LOG"
    echo "---- $c: $ARGS"
    "${QEMU:-qemu-system-aarch64}" -M virt,virtualization=on,gic-version=3 -cpu max -accel tcg \
        -nographic -m 4G -smp 4 -nic none -no-reboot -kernel $OUT/wel2.bin -append "$ARGS" \
        -device loader,file="$FD",addr="$ENTRY",force-raw=on ${LD[@]+"${LD[@]}"} \
        -serial "file:$LOG" -monitor none >$OUT/winram-stub-$c-qemu.out 2>&1 &
    local QPID=$! EXITED=0
    for _ in $(seq 1 6000); do
        kill -0 "$QPID" 2>/dev/null || { EXITED=1; break; }
        sleep 0.1
    done
    kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null
    tr -d '\r' < "$LOG" > "$T"
    grep -aE "Watari EL2: ramdisk|WatariM1RamDisk|trying boot option|T16A BOOTED|SELFTEST PASS|PSCI SYSTEM_RESET" "$T" | head -12
    chk() { grep -aqF -- "$1" "$T" || { echo "MISSING: $1"; rc=1; }; }
    grep -aqE "ASSERT [/A-Za-z(]|ASSERT_EFI_ERROR|Synchronous Exception|FATAL" "$T" && { echo "FAIL: ASSERT/exception/FATAL in the log"; rc=1; }
    if [ "$c" = ramdisk ]; then
        chk "Watari EL2: ramdisk at 0x0000000100000000"
        chk "WatariM1RamDisk: node: image at 100000000, 4001000 bytes"
        chk "WatariM1RamDisk: CRC-32 "
        chk "WatariM1RamDisk: image at 100000000, 4000000 bytes registered: Success"
        chk 'T16A BOOTED from \EFI\BOOT\BOOTAA64.EFI'
        # the order: registered before the boot
        local a b
        a=$(grep -an "bytes registered: Success" "$T" | head -1 | cut -d: -f1); b=$(grep -an "T16A BOOTED" "$T" | head -1 | cut -d: -f1)
        [ -n "$a" ] && [ -n "$b" ] && [ "$a" -lt "$b" ] || { echo "FAIL: the disk was not registered before the stand-in ran"; rc=1; }
    else
        chk "SELFTEST PASS"
        grep -aq "WatariM1RamDisk: " "$T" && { echo "FAIL: the RAM-disk driver spoke without a node"; rc=1; }
        grep -aqF "T16A BOOTED" "$T" && { echo "FAIL: something booted without a disk"; rc=1; }
    fi
    [ "$EXITED" = 1 ] || { echo "FAIL: QEMU did not exit by itself"; rc=1; }
    [ $rc = 0 ] && echo "PASS: $c" || { echo "FAILED: $c"; total_rc=1; }
}

for c in $CASES; do run_case "$c"; done
[ $total_rc = 0 ] && echo "PASS: winram stand-in: FAT32 image above 4 GB from the guest FDT node to the stand-in boot ($CASES)"
exit $total_rc
