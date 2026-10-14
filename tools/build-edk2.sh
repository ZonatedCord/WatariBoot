#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build riproducibile di ArmVirtPkg/ArmVirtQemu.dsc (AARCH64) per QEMU.
# Uso: tools/build-edk2.sh [DEBUG|RELEASE]
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "$HERE/env.sh"

TARGET="${1:-DEBUG}"
mkdir -p "$WATARI_LOGS"
cd "$WATARI_EDK2"

# Patch Watari (tools/edk2-hvf-gicv3.patch): con HVF (Apple Silicon) ID_AA64PFR0_EL1.GIC vale 0, quindi
# ArmGicDxe sceglieva il driver GICv2 e nessuna interruzione (timer incluso) arrivava: blocco su BdsWait(5).
# Idempotente: si applica solo se non e' gia' applicata.
if git -C "$WATARI_EDK2" apply --check "$HERE/edk2-hvf-gicv3.patch" 2>/dev/null; then
    git -C "$WATARI_EDK2" apply "$HERE/edk2-hvf-gicv3.patch"
fi

export WORKSPACE="$WATARI_EDK2"
export PACKAGES_PATH="$WATARI_EDK2"
export PYTHON_COMMAND="${PYTHON_COMMAND:-python3}"
# shellcheck disable=SC1091
set +u
set --
source edksetup.sh
set -u
make -C BaseTools -j"$(sysctl -n hw.ncpu)"

build -a AARCH64 -t "$WATARI_TOOLCHAIN" -b "$TARGET" \
      -p ArmVirtPkg/ArmVirtQemu.dsc -n "$(sysctl -n hw.ncpu)" \
      2>&1 | tee "$WATARI_LOGS/b2-build.log"

FD="$WATARI_EDK2/Build/ArmVirtQemu-AArch64/${TARGET}_${WATARI_TOOLCHAIN}/FV"
echo "Output: $FD"
ls -l "$FD"/QEMU_EFI.fd "$FD"/QEMU_VARS.fd
