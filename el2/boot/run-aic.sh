#!/bin/bash
# SPDX-License-Identifier: MIT
# T9b: the EL1 test guest (G1-G12) on the patched QEMU's hybrid machine (GICv3 + Watari AIC, aic=on; PL031 and UART0
# wired to the AIC), so the layer's AIC interrupt path runs under the guest. Opt-in: needs tools/qemu-watari/build.sh
# (exit 77 = skipped without it). The layer runs its QEMU platform with the AIC backend, NOT plat_m1: the Apple FIQ
# timer and fast IPI sysregs are not emulated (re/qemu-aic-bench.md).
# RAM_BASE=0x800000000 (any multiple of 1 GiB) puts all guest RAM there instead of 0x40000000 (patch 0003, ram-base=).
# Usage: [RAM_BASE=0x800000000] [QEMU=...] ./run-aic.sh   (CPUIF / WEL2_ARGS as run-m1guest.sh)
set -u
cd "$(dirname "$0")"
export QEMU="${QEMU:-/private/tmp/watari-qemu/build/qemu-system-aarch64}"
[ -x "$QEMU" ] || { echo "SKIP: no Watari QEMU build at $QEMU (tools/qemu-watari/build.sh)"; exit 77; }
"$QEMU" -M virt,help 2>&1 | grep -q 'aic=' || { echo "SKIP: $QEMU has no aic option"; exit 77; }
export MACHINE_OPTS="aic=on"
if [ -n "${RAM_BASE:-}" ]; then
    MACHINE_OPTS="$MACHINE_OPTS,ram-base=$RAM_BASE"
    GUEST_ADDR=$(printf '0x%x' $((RAM_BASE + 0x280000)))
    export GUEST_ADDR
fi
exec ./run-m1guest.sh
