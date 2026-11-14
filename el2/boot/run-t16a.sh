#!/bin/bash
# SPDX-License-Identifier: MIT
# T16a (migrated to the production RAM-disk path in T21): the boot manager finds a boot application on a disk although
# variables are RAM-backed. Firmware of tools/build-edk2-m1.sh --ramdisk (production configuration: hypercall console, plus the
# stand-in application WatariM1BenchBootTest.efi) on the layer, QEMU TCG, -smp 4.
# The M1 platform has no storage driver by design, so the bench loads a 16 MiB GPT disk image (FAT16 partition) + the 4 KiB RAM-disk
# trailer (tools/m3a/winram_trailer.py) into RAM at 0xB0000000 (-device loader) and starts the layer with
# wel2.ramdisk=0xB0000000,0x1001000: the layer puts /reserved-memory/ramdisk@b0000000 into the guest FDT and takes the range out of
# /memory; WatariM1RamDiskDxe (in every build, FDT driven) claims it, checks the CRC-32 and registers the RAM disk. The image holds the
# EDK2-built stand-in (prints "T16A BOOTED from <path>" and resets); no Windows or Microsoft binary is involved.
# Cases:
#   removable   the stand-in at \EFI\BOOT\BOOTAA64.EFI only            -> found by EDK2's own discovery
#   bootmgfw    the stand-in at \EFI\Microsoft\Boot\bootmgfw.efi only  -> found only by WatariM1BootManagerLib (T16a)
#   both        both files                                              -> the removable one boots first (nothing more is claimed)
#   empty       a disk with no boot file                                -> nothing found, the self-test runs (negative control)
#   crcbad      as removable, one byte of the image flipped after the trailer was made -> NOT registered, the self-test runs
#   notrailer   as removable, the 4 KiB behind the image are zero (no trailer)         -> NOT registered, the self-test runs
# Env: CASES="..."  CPUIF=hw|trap|trap-notc  FD=<firmware>  APP=<stand-in>  WATARI_M1_BUILD_TAG=<tag of tools/build-edk2-m1.sh>
# Prerequisite: the data disk and tools/build-edk2-m1.sh --ramdisk. Nothing is downloaded or installed (hdiutil only).
set -u
cd "$(dirname "$0")"
CPUIF="${CPUIF:-hw}"
CASES="${CASES:-removable bootmgfw both empty crcbad notrailer}"
ENTRY=0x40280000
RAMBASE=0xB0000000
RDSIZE=0x1001000       # 16 MiB image + 4 KiB trailer
TAGSUF=""; [ -n "${WATARI_M1_BUILD_TAG:-}" ] && TAGSUF="-$WATARI_M1_BUILD_TAG"
# shellcheck source=/dev/null
source ../../tools/env.sh || { echo "FAIL: data disk not mounted"; exit 2; }
FD="${FD:-$WATARI_DATA/build-m1/WATARI_M1_EFI-ramdisk${TAGSUF}.fd}"
APP="${APP:-$WATARI_DATA/build-m1/WatariM1BenchBootTest${TAGSUF}.efi}"
TRL=../../tools/m3a/winram_trailer.py
[ -f "$FD" ] && [ -f "$APP" ] || { echo "FAIL: $FD or $APP missing: run tools/build-edk2-m1.sh --ramdisk first"; exit 2; }
make -s chain || { echo "FAIL: layer build"; exit 2; }
OUT=build-chain
IMG=$OUT/t16a-img
mkdir -p "$IMG"
total_rc=0

# mkimg <case>  -> $IMG/<case>.raw: the 16 MiB disk + the 4 KiB trailer (exactly 0x1001000 bytes)
mkimg() {
    local c="$1" src="$IMG/$1-src" base="$1"
    case "$c" in crcbad|notrailer) base=removable ;; esac
    src="$IMG/$base-src"
    rm -f "$IMG/$base.cdr" "$IMG/$c.raw" "$IMG/$c.disk"
    rm -rf "$src"; mkdir -p "$src"
    case "$base" in
        removable) mkdir -p "$src/EFI/BOOT"; cp "$APP" "$src/EFI/BOOT/BOOTAA64.EFI" ;;
        bootmgfw)  mkdir -p "$src/EFI/Microsoft/Boot"; cp "$APP" "$src/EFI/Microsoft/Boot/bootmgfw.efi" ;;
        both)      mkdir -p "$src/EFI/BOOT" "$src/EFI/Microsoft/Boot"; cp "$APP" "$src/EFI/BOOT/BOOTAA64.EFI"; cp "$APP" "$src/EFI/Microsoft/Boot/bootmgfw.efi" ;;
        empty)     echo "no boot file here" > "$src/README.TXT" ;;
    esac
    hdiutil create -quiet -size 16m -fs "MS-DOS FAT16" -volname T16A -layout GPTSPUD -srcfolder "$src" -format UDTO -ov "$IMG/$base" >/dev/null || return 1
    mv "$IMG/$base.cdr" "$IMG/$c.disk"
    [ "$(stat -f %z "$IMG/$c.disk")" = 16777216 ] || return 1
    python3 -I "$TRL" append "$IMG/$c.disk" "$IMG/$c.raw" || return 1
    case "$c" in
        crcbad)    # flip one byte of the image (inside the FAT data area, far from the headers) after the trailer was made
                   python3 -I -c "
import sys
f = open(sys.argv[1], 'r+b'); f.seek(9 * 1024 * 1024); b = f.read(1); f.seek(9 * 1024 * 1024); f.write(bytes([b[0] ^ 0xFF])); f.close()" "$IMG/$c.raw" ;;
        notrailer) python3 -I -c "
import sys
f = open(sys.argv[1], 'r+b'); f.seek(16 * 1024 * 1024); f.write(b'\0' * 4096); f.close()" "$IMG/$c.raw" ;;
    esac
    [ "$(stat -f %z "$IMG/$c.raw")" = $((RDSIZE)) ]
}

run_case() {
    local c="$1" rc=0
    mkimg "$c" || { echo "FAIL: cannot build the $c image"; total_rc=1; return; }
    local ARGS="wel2.guest=$ENTRY wel2.abi=fdt wel2.gfdt=build wel2.split=3 wel2.cpuif=$CPUIF wel2.ramdisk=$RAMBASE,$RDSIZE ${WEL2_ARGS:-}"
    local LOG=$OUT/t16a-$c-uart.log T=$OUT/t16a-$c-uart.txt
    : > "$LOG"
    echo "---- $c: $ARGS"
    "${QEMU:-qemu-system-aarch64}" -M virt,virtualization=on,gic-version=3 -cpu max -accel tcg \
        -nographic -m 2G -smp 4 -nic none -no-reboot -kernel $OUT/wel2.bin -append "$ARGS" \
        -device loader,file="$FD",addr="$ENTRY",force-raw=on \
        -device loader,file="$IMG/$c.raw",addr="$RAMBASE",force-raw=on \
        -serial "file:$LOG" -monitor none >$OUT/t16a-$c-qemu.out 2>&1 &
    local QPID=$! EXITED=0
    for _ in $(seq 1 3000); do
        kill -0 "$QPID" 2>/dev/null || { EXITED=1; break; }
        sleep 0.1
    done
    kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null
    tr -d '\r' < "$LOG" > "$T"
    grep -aE "WatariM1RamDisk|Watari EL2: ramdisk|trying boot option|trying Windows|T16A BOOTED|SELFTEST PASS|Booting Watari|\[Bds\]Booting|PSCI SYSTEM_RESET" "$T" | head -20
    chk() { grep -aqF -- "$1" "$T" || { echo "MISSING: $1"; rc=1; }; }
    chk "Watari EL2: ramdisk at "
    chk "WatariM1RamDisk: node: image at B0000000"
    grep -aqE "ASSERT [/A-Za-z(]|ASSERT_EFI_ERROR|Synchronous Exception" "$T" && { echo "FAIL: ASSERT/exception in the log"; rc=1; }
    case "$c" in
        removable|both|bootmgfw|empty) chk "WatariM1RamDisk: CRC-32 "; chk "WatariM1RamDisk: image at B0000000, 1000000 bytes registered: Success" ;;
    esac
    case "$c" in
        removable) chk 'T16A BOOTED from \EFI\BOOT\BOOTAA64.EFI' ;;
        bootmgfw)  chk 'T16A BOOTED from \EFI\Microsoft\Boot\bootmgfw.efi'
                   grep -aqF "trying Windows Boot Manager" "$T" || { echo "MISSING: the bootmgfw path was not the one that booted"; rc=1; } ;;
        both)      [ "$(grep -ac 'T16A BOOTED from' "$T")" -ge 1 ] || { echo "MISSING: no boot"; rc=1; }
                   grep -aqF 'T16A BOOTED from \EFI\BOOT\BOOTAA64.EFI' "$T" || { echo "MISSING: the removable path did not boot first"; rc=1; } ;;
        empty)     chk "SELFTEST PASS"
                   grep -aqF "T16A BOOTED" "$T" && { echo "FAIL: something booted from an empty disk"; rc=1; } ;;
        crcbad)    chk "WatariM1RamDisk: CRC-32 mismatch"; chk "SELFTEST PASS"
                   grep -aqF "registered" "$T" && { echo "FAIL: a disk with a bad CRC was registered"; rc=1; }
                   grep -aqF "T16A BOOTED" "$T" && { echo "FAIL: something booted from a corrupt image"; rc=1; } ;;
        notrailer) chk "WatariM1RamDisk: no trailer"; chk "SELFTEST PASS"
                   grep -aqF "registered" "$T" && { echo "FAIL: a disk without a trailer was registered"; rc=1; }
                   grep -aqF "T16A BOOTED" "$T" && { echo "FAIL: something booted without a trailer"; rc=1; } ;;
    esac
    [ "$EXITED" = 1 ] || { echo "FAIL: QEMU did not exit by itself"; rc=1; }
    [ $rc = 0 ] && echo "PASS: $c" || { echo "FAILED: $c"; total_rc=1; }
}

for c in $CASES; do run_case "$c"; done
[ $total_rc = 0 ] && echo "PASS: T16a boot discovery on a RAM-backed-variables firmware, RAM disk from the guest FDT node: $CASES"
exit $total_rc
