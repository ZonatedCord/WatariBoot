#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build of edk2-apple/WatariM1Pkg/WatariM1Pkg.dsc: the EDK2 firmware for the Apple M1 (j293) at EL1
# on top of Watari EL2 (docs/design/watari-edk2-m1/, PLAN E1-E4).
#
# Usage: tools/build-edk2-m1.sh [--bench] [--model <acpigen model>] [DEBUG|RELEASE]
#   (default)  production configuration: no UART of any kind, ACPI tables of acpigen model j293.
#              Output: $WATARI_DATA/build-m1/WATARI_M1_EFI.fd
#   --bench    QEMU bench configuration (-D WATARI_M1_BENCH_PL011=TRUE): serial and debug through
#              the PL011 of QEMU's device tree, ACPI tables of model qemu-virt (GIC at QEMU's
#              addresses), the Shell with acpiview, boots the Shell by default.
#              Output: $WATARI_DATA/build-m1/WATARI_M1_EFI-bench.fd  (separate WORKSPACE)
#   --model m  override the acpigen model.
#
# Nothing is downloaded or installed. The EDK2 tree is the one on the data disk (WATARI_EDK2); the
# only change to it is the idempotent HVF/GICv3 patch tools/edk2-hvf-gicv3.patch, as in the other
# build scripts. Build outputs go under $WATARI_DATA, never in the repository.
#
# Checks (the build fails if one fails):
#   1. DSDT processor _UIDs == `acpigen madt -m j293` GICC UIDs (tools/check-dsdt-uids.py), and the
#      same after an `iasl -d` round trip of the compiled AML;
#   2. the list of built modules and libraries has no VirtNorFlashDxe, QemuFwCfg*, virtio, PCI or
#      PL011 module (PL011/FdtPL011 are allowed in a --bench build only);
#   3. the image header: magic "ARM\x64" at +0x38 and image_size (+0x10) == the FD size.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
# shellcheck source=env.sh
source "$HERE/env.sh"

BENCH=0
MODEL=""
TARGET="DEBUG"
while [ $# -gt 0 ]; do
    case "$1" in
        --bench) BENCH=1 ;;
        --model) MODEL="$2"; shift ;;
        DEBUG|RELEASE|NOOPT) TARGET="$1" ;;
        *) echo "usage: $0 [--bench] [--model <name>] [DEBUG|RELEASE]" >&2; exit 2 ;;
    esac
    shift
done
if [ "$BENCH" = 1 ]; then
    SUFFIX="-bench"; MODEL="${MODEL:-qemu-virt}"
else
    SUFFIX=""; MODEL="${MODEL:-j293}"
fi
case "$MODEL" in j293|qemu-virt) ;; *) echo "ERROR: --model must be j293 or qemu-virt (got '$MODEL')" >&2; exit 2 ;; esac
mkdir -p "$WATARI_LOGS"

# HVF/GICv3 patch, idempotent (as build-edk2-watari.sh).
if git -C "$WATARI_EDK2" apply --check "$HERE/edk2-hvf-gicv3.patch" 2>/dev/null; then
    git -C "$WATARI_EDK2" apply "$HERE/edk2-hvf-gicv3.patch"
fi

# EDK2 does not accept spaces in PACKAGES_PATH: link the repository from a path without spaces.
LINK="/tmp/watari-repo"
if [ "$(readlink "$LINK" 2>/dev/null)" != "$REPO" ]; then rm -f "$LINK"; ln -s "$REPO" "$LINK"; fi

# --- check 1a: the ASL against the acpigen model (before spending minutes in the build) -----------
make -s -C "$REPO/tools/acpigen" build/acpigen >/dev/null
ACPIGEN_BIN="$REPO/tools/acpigen/build/acpigen"
DSDT_ASL="$REPO/edk2-apple/WatariM1Pkg/AcpiTables/Dsdt.asl"
python3 -I "$HERE/check-dsdt-uids.py" "$DSDT_ASL" "$ACPIGEN_BIN" --model j293

# Separate WORKSPACE (OUTPUT_DIRECTORY is Build/WatariM1-AArch64 in both configurations). The FDF
# !include lines resolve from WORKSPACE only, so it holds links to every EDK2 package (but Build, Conf).
export WORKSPACE="$WATARI_LINK/build-m1${SUFFIX}"
mkdir -p "$WORKSPACE"
for e in "$WATARI_EDK2"/*; do
    n="$(basename "$e")"
    case "$n" in Build|Conf) continue ;; esac
    [ -e "$WORKSPACE/$n" ] || ln -s "$e" "$WORKSPACE/$n"
done
export CONF_PATH="$WATARI_EDK2/Conf"
export PACKAGES_PATH="$WATARI_EDK2:$LINK/edk2-apple"
export WATARI_ACPIGEN="$LINK/tools/acpigen"
export PYTHON_COMMAND="${PYTHON_COMMAND:-python3}"
# VirtualRealTimeClockLib starts its clock from the build time (BUILD_EPOCH = $(SOURCE_DATE_EPOCH)).
export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-$(git -C "$REPO" log -1 --format=%ct 2>/dev/null || date +%s)}"
cd "$WATARI_EDK2"
set +u; set --
# shellcheck disable=SC1091
source edksetup.sh
set -u
make -C BaseTools -j"$(sysctl -n hw.ncpu)" >/dev/null

DEFS=(-D WATARI_M1_ACPIGEN_MODEL="$MODEL")
if [ "$BENCH" = 1 ]; then
    DEFS+=(-D WATARI_M1_BENCH_PL011=TRUE -D WATARI_M1_ACPIVIEW=TRUE -D WATARI_M1_SHELL_BOOT=TRUE)
fi

# F-12: EDK2 does not notice changes to the acpigen headers (a path outside PACKAGES_PATH), so the
# platform's modules are rebuilt every time.
OUT="$WORKSPACE/Build/WatariM1-AArch64/${TARGET}_${WATARI_TOOLCHAIN}"
rm -rf "$OUT/AARCH64/WatariM1Pkg"

LOG="${BUILD_LOG:-$WATARI_LOGS/m1-build${SUFFIX}.log}"
build -a AARCH64 -t "$WATARI_TOOLCHAIN" -b "$TARGET" \
      -p WatariM1Pkg/WatariM1Pkg.dsc -n "$(sysctl -n hw.ncpu)" "${DEFS[@]}" \
      2>&1 | tee "$LOG"

FD="$OUT/FV/WATARI_M1_EFI.fd"
[ -f "$FD" ] || { echo "ERROR: $FD was not produced" >&2; exit 1; }

# --- check 2: module list -----------------------------------------------------------------------
# Every module and library that was built has a directory <package path>/<module>/ under AARCH64.
BUILT="$(cd "$OUT/AARCH64" && find . -name OUTPUT -type d | sed -e 's|^\./||' -e 's|/OUTPUT$||' | sort)"
echo "$BUILT" > "$WORKSPACE/built-modules.txt"
FORBID='VirtNorFlash|QemuFwCfg|FwCfg|Virtio|PciBus|PciHostBridge|PciLib|NvmExpress|QemuRamfb|PL031|Xhci|Usb'
if [ "$BENCH" = 0 ]; then FORBID="$FORBID|PL011"; fi
if echo "$BUILT" | grep -E "$FORBID"; then
    echo "ERROR: forbidden module(s) listed above are part of this build (bench=$BENCH)" >&2
    exit 1
fi
echo "OK: no fw_cfg, virtio, PCI, NOR, PL031$([ "$BENCH" = 0 ] && echo ', PL011') module among the $(echo "$BUILT" | wc -l | tr -d ' ') built modules/libraries"

# --- check 1b: the compiled DSDT round trips through iasl -d to the same eight devices -----------
AML="$(find "$OUT/AARCH64/WatariM1Pkg/AcpiTables" -name '*.aml' | head -1)"
[ -n "$AML" ] || { echo "ERROR: no Dsdt.aml built" >&2; exit 1; }
python3 -I "$HERE/check-dsdt-uids.py" "$DSDT_ASL" "$ACPIGEN_BIN" --model j293 --aml "$AML"

# --- check 3: the arm64 Image header --------------------------------------------------------------
FDSIZE="$(stat -f %z "$FD")"
MAGIC="$(xxd -s 0x38 -l 4 -p "$FD")"
ISIZE="$(xxd -s 0x10 -l 8 -e -g 8 "$FD" | awk '{print $2}')"
[ "$MAGIC" = "41524d64" ] || { echo "ERROR: header magic at +0x38 is $MAGIC, expected 41524d64 (ARM\\x64)" >&2; exit 1; }
[ $((16#$ISIZE)) -eq "$FDSIZE" ] || { echo "ERROR: header image_size $((16#$ISIZE)) != FD size $FDSIZE" >&2; exit 1; }
echo "OK: image header: magic ARM\\x64 at +0x38, image_size = FD size = $FDSIZE"

mkdir -p "$WATARI_DATA/build-m1"
DEST="$WATARI_DATA/build-m1/WATARI_M1_EFI${SUFFIX}.fd"
cp "$FD" "$DEST"
echo "Output: $FD -> $DEST"
ls -l "$DEST"
