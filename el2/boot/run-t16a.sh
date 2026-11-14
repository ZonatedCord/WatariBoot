#!/bin/bash
# SPDX-License-Identifier: MIT
# T16a: the boot manager finds a boot application on a disk although variables are RAM-backed.
# BENCH-DISK firmware (tools/build-edk2-m1.sh --ramdisk: hypercall console, WatariM1BenchRamDiskDxe) on the layer, QEMU TCG, -smp 4.
# The M1 platform has no storage driver by design, so the bench loads a 16 MiB GPT disk image (FAT16 partition) into RAM at
# 0xB0000000 (-device loader) and WatariM1BenchRamDiskDxe turns it into a RAM disk. The image holds the EDK2-built stand-in
# WatariM1BenchBootTest.efi (prints "T16A BOOTED from <path>" and resets); no Windows or Microsoft binary is involved.
# Cases:
#   removable   the stand-in at \EFI\BOOT\BOOTAA64.EFI only            -> found by EDK2's own discovery
#   bootmgfw    the stand-in at \EFI\Microsoft\Boot\bootmgfw.efi only  -> found only by WatariM1BootManagerLib (T16a)
#   both        both files                                              -> both boot, the removable one first
#   empty       a disk with no boot file                                -> nothing found, the self-test runs (negative control)
# Env: CASES="..."  CPUIF=hw|trap|trap-notc  FD=<bench firmware>
# Prerequisite: the data disk and tools/build-edk2-m1.sh --ramdisk. Nothing is downloaded or installed (hdiutil only).
set -u
cd "$(dirname "$0")"
CPUIF="${CPUIF:-hw}"
CASES="${CASES:-removable bootmgfw both empty}"
ENTRY=0x40280000
RAMBASE=0xB0000000
# shellcheck source=/dev/null
source ../../tools/env.sh || { echo "FAIL: data disk not mounted"; exit 2; }
FD="${FD:-$WATARI_DATA/build-m1/WATARI_M1_EFI-ramdisk.fd}"
APP="$WATARI_DATA/build-m1/WatariM1BenchBootTest.efi"
[ -f "$FD" ] && [ -f "$APP" ] || { echo "FAIL: $FD or $APP missing: run tools/build-edk2-m1.sh --ramdisk first"; exit 2; }
make -s chain || { echo "FAIL: layer build"; exit 2; }
OUT=build-chain
IMG=$OUT/t16a-img
mkdir -p "$IMG"
total_rc=0

# mkimg <case>  -> $IMG/<case>.raw (exactly 16 MiB)
mkimg() {
    local c="$1" src="$IMG/$1-src"
    rm -f "$IMG/$c.cdr" "$IMG/$c.raw"
    mkdir -p "$src"
    case "$c" in
        removable) mkdir -p "$src/EFI/BOOT"; cp "$APP" "$src/EFI/BOOT/BOOTAA64.EFI" ;;
        bootmgfw)  mkdir -p "$src/EFI/Microsoft/Boot"; cp "$APP" "$src/EFI/Microsoft/Boot/bootmgfw.efi" ;;
        both)      mkdir -p "$src/EFI/BOOT" "$src/EFI/Microsoft/Boot"; cp "$APP" "$src/EFI/BOOT/BOOTAA64.EFI"; cp "$APP" "$src/EFI/Microsoft/Boot/bootmgfw.efi" ;;
        empty)     echo "no boot file here" > "$src/README.TXT" ;;
    esac
    hdiutil create -quiet -size 16m -fs "MS-DOS FAT16" -volname T16A -layout GPTSPUD -srcfolder "$src" -format UDTO -ov "$IMG/$c" >/dev/null || return 1
    mv "$IMG/$c.cdr" "$IMG/$c.raw"
    [ "$(stat -f %z "$IMG/$c.raw")" = 16777216 ]
}

run_case() {
    local c="$1" rc=0
    mkimg "$c" || { echo "FAIL: cannot build the $c image"; total_rc=1; return; }
    local ARGS="wel2.guest=$ENTRY wel2.abi=fdt wel2.gfdt=build wel2.split=3 wel2.cpuif=$CPUIF"
    local LOG=$OUT/t16a-$c-uart.log T=$OUT/t16a-$c-uart.txt
    : > "$LOG"
    echo "---- $c: $ARGS"
    "${QEMU:-qemu-system-aarch64}" -M virt,virtualization=on,gic-version=3 -cpu max -accel tcg \
        -nographic -m 2G -smp 4 -nic none -no-reboot -kernel $OUT/wel2.bin -append "$ARGS" \
        -device loader,file="$FD",addr="$ENTRY",force-raw=on \
        -device loader,file="$IMG/$c.raw",addr="$RAMBASE",force-raw=on \
        -serial "file:$LOG" -monitor none >$OUT/t16a-$c-qemu.out 2>&1 &
    local QPID=$! EXITED=0
    for _ in $(seq 1 1800); do
        kill -0 "$QPID" 2>/dev/null || { EXITED=1; break; }
        sleep 0.1
    done
    kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null
    tr -d '\r' < "$LOG" > "$T"
    grep -aE "WatariM1BenchRamDisk|trying boot option|trying Windows|T16A BOOTED|SELFTEST PASS|Booting Watari|\[Bds\]Booting|PSCI SYSTEM_RESET" "$T" | head -20
    chk() { grep -aqF -- "$1" "$T" || { echo "MISSING: $1"; rc=1; }; }
    chk "WatariM1BenchRamDisk: image at"
    grep -aqE "ASSERT [/A-Za-z(]|ASSERT_EFI_ERROR|Synchronous Exception" "$T" && { echo "FAIL: ASSERT/exception in the log"; rc=1; }
    case "$c" in
        removable) chk 'T16A BOOTED from \EFI\BOOT\BOOTAA64.EFI' ;;
        bootmgfw)  chk 'T16A BOOTED from \EFI\Microsoft\Boot\bootmgfw.efi'
                   grep -aqF "trying Windows Boot Manager" "$T" || { echo "MISSING: the bootmgfw path was not the one that booted"; rc=1; } ;;
        both)      [ "$(grep -ac 'T16A BOOTED from' "$T")" -ge 1 ] || { echo "MISSING: no boot"; rc=1; } ;;
        empty)     chk "SELFTEST PASS"
                   grep -aqF "T16A BOOTED" "$T" && { echo "FAIL: something booted from an empty disk"; rc=1; } ;;
    esac
    [ "$EXITED" = 1 ] || { echo "FAIL: QEMU did not exit by itself"; rc=1; }
    [ $rc = 0 ] && echo "PASS: $c" || { echo "FAILED: $c"; total_rc=1; }
}

for c in $CASES; do run_case "$c"; done
[ $total_rc = 0 ] && echo "PASS: T16a boot discovery on a RAM-backed-variables firmware: $CASES"
exit $total_rc
