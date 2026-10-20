#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build of ArmVirtPkg/ArmVirtQemuKernel.dsc (AARCH64): the firmware variant that is
# loaded into RAM like an arm64 Linux kernel (Image header + FDT pointer in x0,
# relocatable PrePi). Used by M1-4 to boot EDK2 at EL1 on top of Watari EL2.
# Output (data disk, nothing in the repo):
#   $WATARI_EDK2/Build/ArmVirtQemuKernel-AArch64/<TARGET>_CLANGDWARF/FV/QEMU_EFI.fd
# and a copy in $WATARI_DATA/build-kernel/QEMU_EFI-kernel.fd.
# Usage: tools/build-edk2-kernel.sh [DEBUG|RELEASE]
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "$HERE/env.sh"

TARGET="${1:-DEBUG}"
mkdir -p "$WATARI_LOGS"
cd "$WATARI_EDK2"

# Same HVF/GICv3 patch as build-edk2.sh (idempotent: applied only if not yet applied).
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
      -p ArmVirtPkg/ArmVirtQemuKernel.dsc -n "$(sysctl -n hw.ncpu)" \
      2>&1 | tee "$WATARI_LOGS/m1-4-build-kernel.log"

FD="$WATARI_EDK2/Build/ArmVirtQemuKernel-AArch64/${TARGET}_${WATARI_TOOLCHAIN}/FV"
mkdir -p "$WATARI_DATA/build-kernel"
cp "$FD/QEMU_EFI.fd" "$WATARI_DATA/build-kernel/QEMU_EFI-kernel.fd"
echo "Output: $FD/QEMU_EFI.fd"
ls -l "$FD"/QEMU_EFI.fd "$WATARI_DATA/build-kernel/QEMU_EFI-kernel.fd"
