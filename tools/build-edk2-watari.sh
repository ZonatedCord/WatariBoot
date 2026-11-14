#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build di edk2-apple/WatariQemuPkg/WatariQemuPkg.dsc (= ArmVirtQemu + WatariAcpiOverride).
# Non modifica il sorgente EDK2 (oltre alla patch HVF gia' applicata da build-edk2.sh) e scrive in
# Build/WatariQemu-AArch64; il firmware finale e' copiato in $WATARI_DATA/vm/QEMU_EFI-watari$SUFFIX.fd
# (QEMU_EFI.fd esistente non si tocca). Uso: tools/build-edk2-watari.sh [--fiq] [DEBUG|RELEASE]
# --fiq: aggiunge WatariTimerFiqDxe (V6, timer in Group 0 = FIQ) con -D WATARI_TIMER_FIQ=TRUE; usa un
# WORKSPACE separato (build-watari-fiq) e produce $WATARI_DATA/vm/QEMU_EFI-watari-fiq.fd.
# --parking: aggiunge WatariParkingDxe (G4/V3, Multiprocessor Parking Protocol; MADT parking v1 e FADT
# senza PSCI) con -D WATARI_PARKING=TRUE; WORKSPACE build-watari-park, output QEMU_EFI-watari-park.fd.
# --nogicd: V2 (G2), MADT senza GICD (modello acpigen qemu-virt-nogicd), -D WATARI_NOGICD=TRUE;
# WORKSPACE build-watari-nogicd, output QEMU_EFI-watari-nogicd.fd.
# --variant e13a|e13b|e13c: E13 (DESIGN 9.2 / 7), reduced ACPI for the Windows bench. Builds the ArmVirtQemuKernel
# flavour (the one el2/boot/run-m2.sh loads under Watari EL2) + WatariAcpiOverride in E13 mode: e13a = QEMU
# tables without DBG2/SPCR, e13b = QEMU DSDT replaced by tools/acpigen/dsdt/qemu-virt-min.asl, e13c = both. Own WORKSPACE
# and output: $WATARI_DATA/build-watari-e13a|b|c/QEMU_EFI-kernel.fd (nothing else is touched; run-m2.sh: FWDIR=).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
# shellcheck source=env.sh
source "$HERE/env.sh"

FIQ=0
PARK=0
NOGICD=0
VARIANT=""
if [ "${1:-}" = "--variant" ]; then
    VARIANT="${2:-}"; shift 2 || true
    case "$VARIANT" in e13a|e13b|e13c) ;; *) echo "ERROR: --variant e13a|e13b|e13c" >&2; exit 2 ;; esac
fi
if [ "${1:-}" = "--nogicd" ]; then NOGICD=1; shift; fi
if [ "${1:-}" = "--fiq" ]; then FIQ=1; shift; fi
if [ "${1:-}" = "--parking" ]; then PARK=1; shift; fi
TARGET="${1:-DEBUG}"
if [ -n "$VARIANT" ]; then SUFFIX="-$VARIANT"; elif [ "$FIQ" = 1 ]; then SUFFIX="-fiq"; elif [ "$PARK" = 1 ]; then SUFFIX="-park"; elif [ "$NOGICD" = 1 ]; then SUFFIX="-nogicd"; else SUFFIX=""; fi
mkdir -p "$WATARI_LOGS"

# Patch HVF idempotente (come build-edk2.sh).
if git -C "$WATARI_EDK2" apply --check "$HERE/edk2-hvf-gicv3.patch" 2>/dev/null; then
    git -C "$WATARI_EDK2" apply "$HERE/edk2-hvf-gicv3.patch"
fi

# Il repo ha spazi? PACKAGES_PATH non li regge: si usa un link senza spazi in /tmp.
# Per-checkout link (a shared /tmp/watari-repo would be re-pointed by concurrent worktrees).
LINK="/tmp/watari-repo-$(printf %s "$REPO" | shasum | cut -c1-8)"
if [ "$(readlink "$LINK" 2>/dev/null)" != "$REPO" ]; then rm -f "$LINK"; ln -s "$REPO" "$LINK"; fi

# WORKSPACE separato: ArmVirtQemu.dsc impone OUTPUT_DIRECTORY=Build/ArmVirtQemu-AArch64, quindi con lo
# stesso WORKSPACE si sovrascriverebbe l'output di build-edk2.sh. Sorgenti e Conf restano quelli di EDK2.
export WORKSPACE="$WATARI_LINK/build-watari${SUFFIX}"
mkdir -p "$WORKSPACE"
# Le !include dell'FDF si risolvono solo da WORKSPACE (non da PACKAGES_PATH): il WORKSPACE separato
# contiene link ai pacchetti di EDK2 (tranne Build e Conf).
for e in "$WATARI_EDK2"/*; do
    n="$(basename "$e")"
    case "$n" in Build|Conf) continue ;; esac
    [ -e "$WORKSPACE/$n" ] || ln -s "$e" "$WORKSPACE/$n"
done
FIQ_INF=""
[ "$FIQ" = 1 ] && FIQ_INF='\
INF WatariQemuPkg/WatariTimerFiqDxe/WatariTimerFiqDxe.inf'
[ "$PARK" = 1 ] && FIQ_INF='\
INF WatariQemuPkg/WatariParkingDxe/WatariParkingDxe.inf'
E13VAL=0; DSC="WatariQemuPkg/WatariQemuPkg.dsc"; FDFSRC="$WATARI_EDK2/ArmVirtPkg/ArmVirtQemu.fdf"
FDFOUT="$WORKSPACE/WatariQemu.fdf"; OUTSUB="ArmVirtQemu-AArch64"
if [ -n "$VARIANT" ]; then
    DSC="WatariQemuPkg/WatariQemuKernel.dsc"; FDFSRC="$WATARI_EDK2/ArmVirtPkg/ArmVirtQemuKernel.fdf"
    FDFOUT="$WORKSPACE/WatariQemuKernel.fdf"; OUTSUB="ArmVirtQemuKernel-AArch64"
    case "$VARIANT" in e13a) E13VAL=1 ;; e13b) E13VAL=2 ;; e13c) E13VAL=3 ;; esac
    # qemu_virt_min.hex (qemu_virt_min_aml_code[]) from the ASL, compiled in the WORKSPACE (not in the repo).
    mkdir -p "$WORKSPACE/e13"; cp "$REPO/tools/acpigen/dsdt/qemu-virt-min.asl" "$WORKSPACE/e13/qemu_virt_min.asl"
    (cd "$WORKSPACE/e13" && iasl -tc qemu_virt_min.asl >/dev/null) || { echo "ERROR: iasl" >&2; exit 1; }
    export WATARI_E13INC="$WORKSPACE/e13"
fi
# FDF di wrapper generato da ArmVirtQemu.fdf: include con percorso completo e INF del driver in FvMain.
sed -e 's/\r$//' -e 's|^!include \([A-Za-z0-9_.]*\)$|!include ArmVirtPkg/\1|' \
    -e '/^!include ArmVirtPkg\/ArmVirtQemuFvMain.fdf.inc$/a\
INF WatariQemuPkg/WatariAcpiOverride/WatariAcpiOverride.inf'"${FIQ_INF}" \
    "$FDFSRC" > "$FDFOUT"
grep -q WatariAcpiOverride "$FDFOUT" || { echo "ERRORE: INF non inserito nell'FDF" >&2; exit 1; }
export CONF_PATH="$WATARI_EDK2/Conf"
export PACKAGES_PATH="$WATARI_EDK2:$LINK/edk2-apple"
export WATARI_ACPIGEN="$LINK/tools/acpigen"
export PYTHON_COMMAND="${PYTHON_COMMAND:-python3}"
cd "$WATARI_EDK2"
set +u; set --
# shellcheck disable=SC1091
source edksetup.sh
set -u
make -C BaseTools -j"$(sysctl -n hw.ncpu)" >/dev/null

FIQ_DEFS=()
[ "$FIQ" = 1 ] && FIQ_DEFS=(-D WATARI_TIMER_FIQ=TRUE)
[ "$PARK" = 1 ] && FIQ_DEFS=(-D WATARI_PARKING=TRUE -D WATARI_PMODE="${WATARI_PMODE:-0}")
[ "$NOGICD" = 1 ] && FIQ_DEFS=(-D WATARI_NOGICD=TRUE)
[ -n "$VARIANT" ] && FIQ_DEFS=(-D WATARI_E13="$E13VAL")
# F-12: EDK2 non rileva le modifiche agli header di acpigen (percorso fuori da PACKAGES_PATH):
# senza questo il driver puo' restare compilato con un layout vecchio di acpigen_model. Si rifa' il modulo.
rm -rf "$WORKSPACE/Build/$OUTSUB/${TARGET}_${WATARI_TOOLCHAIN}/AARCH64/WatariQemuPkg"

build -a AARCH64 -t "$WATARI_TOOLCHAIN" -b "$TARGET" \
      -p "$DSC" -n "$(sysctl -n hw.ncpu)" ${FIQ_DEFS[@]+"${FIQ_DEFS[@]}"} \
      2>&1 | tee "${BUILD_LOG:-$WATARI_LOGS/n4-build$SUFFIX.log}"

FD="$WORKSPACE/Build/$OUTSUB/${TARGET}_${WATARI_TOOLCHAIN}/FV"
if [ -n "$VARIANT" ]; then
    cp "$FD/QEMU_EFI.fd" "$WORKSPACE/QEMU_EFI-kernel.fd"
    echo "Output: $FD/QEMU_EFI.fd -> $WORKSPACE/QEMU_EFI-kernel.fd"; ls -l "$WORKSPACE/QEMU_EFI-kernel.fd"; exit 0
fi
mkdir -p "$WATARI_DATA/vm"
cp "$FD/QEMU_EFI.fd" "$WATARI_DATA/vm/QEMU_EFI-watari$SUFFIX.fd"
echo "Output: $FD/QEMU_EFI.fd -> $WATARI_DATA/vm/QEMU_EFI-watari$SUFFIX.fd"
ls -l "$WATARI_DATA/vm/QEMU_EFI-watari$SUFFIX.fd"
