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
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
# shellcheck source=env.sh
source "$HERE/env.sh"

FIQ=0
PARK=0
NOGICD=0
if [ "${1:-}" = "--nogicd" ]; then NOGICD=1; shift; fi
if [ "${1:-}" = "--fiq" ]; then FIQ=1; shift; fi
if [ "${1:-}" = "--parking" ]; then PARK=1; shift; fi
TARGET="${1:-DEBUG}"
if [ "$FIQ" = 1 ]; then SUFFIX="-fiq"; elif [ "$PARK" = 1 ]; then SUFFIX="-park"; elif [ "$NOGICD" = 1 ]; then SUFFIX="-nogicd"; else SUFFIX=""; fi
mkdir -p "$WATARI_LOGS"

# Patch HVF idempotente (come build-edk2.sh).
if git -C "$WATARI_EDK2" apply --check "$HERE/edk2-hvf-gicv3.patch" 2>/dev/null; then
    git -C "$WATARI_EDK2" apply "$HERE/edk2-hvf-gicv3.patch"
fi

# Il repo ha spazi? PACKAGES_PATH non li regge: si usa un link senza spazi in /tmp.
LINK="/tmp/watari-repo"
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
# FDF di wrapper generato da ArmVirtQemu.fdf: include con percorso completo e INF del driver in FvMain.
sed -e 's/\r$//' -e 's|^!include \([A-Za-z0-9_.]*\)$|!include ArmVirtPkg/\1|' \
    -e '/^!include ArmVirtPkg\/ArmVirtQemuFvMain.fdf.inc$/a\
INF WatariQemuPkg/WatariAcpiOverride/WatariAcpiOverride.inf'"${FIQ_INF}" \
    "$WATARI_EDK2/ArmVirtPkg/ArmVirtQemu.fdf" > "$WORKSPACE/WatariQemu.fdf"
grep -q WatariAcpiOverride "$WORKSPACE/WatariQemu.fdf" || { echo "ERRORE: INF non inserito nell'FDF" >&2; exit 1; }
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
[ "$PARK" = 1 ] && FIQ_DEFS=(-D WATARI_PARKING=TRUE)
[ "$NOGICD" = 1 ] && FIQ_DEFS=(-D WATARI_NOGICD=TRUE)
# F-12: EDK2 non rileva le modifiche agli header di acpigen (percorso fuori da PACKAGES_PATH):
# senza questo il driver puo' restare compilato con un layout vecchio di acpigen_model. Si rifa' il modulo.
rm -rf "$WORKSPACE/Build/ArmVirtQemu-AArch64/${TARGET}_${WATARI_TOOLCHAIN}/AARCH64/WatariQemuPkg"

build -a AARCH64 -t "$WATARI_TOOLCHAIN" -b "$TARGET" \
      -p WatariQemuPkg/WatariQemuPkg.dsc -n "$(sysctl -n hw.ncpu)" ${FIQ_DEFS[@]+"${FIQ_DEFS[@]}"} \
      2>&1 | tee "${BUILD_LOG:-$WATARI_LOGS/n4-build$SUFFIX.log}"

FD="$WORKSPACE/Build/ArmVirtQemu-AArch64/${TARGET}_${WATARI_TOOLCHAIN}/FV"
mkdir -p "$WATARI_DATA/vm"
cp "$FD/QEMU_EFI.fd" "$WATARI_DATA/vm/QEMU_EFI-watari$SUFFIX.fd"
echo "Output: $FD/QEMU_EFI.fd -> $WATARI_DATA/vm/QEMU_EFI-watari$SUFFIX.fd"
ls -l "$WATARI_DATA/vm/QEMU_EFI-watari$SUFFIX.fd"
