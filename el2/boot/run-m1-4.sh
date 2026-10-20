#!/bin/bash
# SPDX-License-Identifier: MIT
# M1-4: EDK2 (ArmVirtQemuKernel) at EL1 on top of Watari EL2, QEMU TCG, -smp 4, -m 2G.
#   QEMU -kernel build-chain/wel2.bin           : the layer (arm64 Image, x0 = FDT)
#   QEMU -device loader,...,addr=0x40280000     : EDK2, entered by the layer at EL1
# Prerequisite: the external data disk and tools/build-edk2-kernel.sh (once).
# Success = EDK2 reaches the Boot Manager / BDS end state (or the UEFI Shell) with the layer
# having handled the vGIC traps and injected timer interrupts. 60 s timeout. Exit 0 on success.
set -u
# CPUIF=trap runs the same test with the virtual CPU interface in TRAP_EMULATE mode
# (boot argument wel2.cpuif=trap, see cpuif.c); the default is HW list registers.
APPEND=()
[ "${CPUIF:-hw}" = trap ] && APPEND=(-append "wel2.cpuif=trap")
cd "$(dirname "$0")"
# shellcheck source=/dev/null
source ../../tools/env.sh || { echo "FAIL: data disk not mounted"; exit 2; }
FD="$WATARI_DATA/build-kernel/QEMU_EFI-kernel.fd"
[ -f "$FD" ] || { echo "FAIL: $FD missing: run tools/build-edk2-kernel.sh first"; exit 2; }
make -s chain || { echo "FAIL: build"; exit 2; }

# Default: no NIC (BDS ends in ~5 s). WITH_NIC=1 keeps the default virtio-net: EDK2 then also
# tries PXE ("UEFI PXEv4"), which takes about 60 s in TCG.
NIC="-nic none"
TICKS=600                       # 60 s
[ "${WITH_NIC:-0}" = 1 ] && { NIC=""; TICKS=1500; }   # 150 s
LOG=build-chain/uart.log
: > "$LOG"
qemu-system-aarch64 -M virt,virtualization=on,gic-version=3 -cpu max -accel tcg \
    -nographic -m 2G -smp 4 $NIC -kernel build-chain/wel2.bin ${APPEND[@]+"${APPEND[@]}"} \
    -device loader,file="$FD",addr=0x40280000,force-raw=on \
    -serial "file:$LOG" -monitor none >build-chain/qemu.out 2>&1 &
QPID=$!

# Stop as soon as EDK2 reached its end state or the layer died; portable 60 s timeout.
DONE=0
for _ in $(seq 1 "$TICKS"); do
    kill -0 "$QPID" 2>/dev/null || { DONE=1; break; }
    if grep -aqE "BdsDxe: Press any key to enter the Boot Manager|Shell> |Watari EL2: halted" "$LOG"; then
        sleep 2; DONE=2; break
    fi
    sleep 0.1
done
kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null
tr -d '\r' < "$LOG" > build-chain/uart.txt

# Layer lines and the tail of the firmware log.
grep "^Watari EL2" build-chain/uart.txt | grep -v "vgic effect=" | head -50
echo "... [EDK2 output] ..."
tail -12 build-chain/uart.txt

rc=0
expect() {
    grep -qF -- "$1" build-chain/uart.txt || { echo "MISSING: $1"; rc=1; }
}
expect "Watari EL2: cores (redistributors) = 0x0000000000000004"
expect 'Watari EL2: FDT /psci method "smc" patched to "hvc"'
expect "Watari EL2: FDT /memory base now 0x0000000040200000"
expect "Watari EL2: eret to EDK2 at EL1, entry = 0x0000000040280000"
expect "Found GIC v3 Distributor @ 0x8000000"
expect "Watari EL2: vGIC GICD traps handled = 0x0000000000000064"
expect "Watari EL2: vGIC GICR traps handled = 0x0000000000000064"
expect "Watari EL2: cpu 0 virtual timer PPI 27 taken, injecting into the guest"
expect "Watari EL2:   timer irqs on this cpu so far = 0x0000000000000064"
expect "[Bds]BdsWait ...Zzzzzzzzzzzz..."
expect "[Bds]Exit the waiting!"
[ "${WITH_NIC:-0}" = 1 ] && expect "BdsDxe: failed to load Boot0002"
if [ "${CPUIF:-hw}" = trap ]; then
    expect "Watari EL2: cpuif mode = TRAP_EMULATE"
    expect "Watari EL2: trapped ICC_PMR_EL1 writes so far = "
    # input for #43 / RISKS R-05: the last periodic report of the layer (every 100th timer tick)
    echo "ICC traps during this EDK2 boot (last report):"
    grep -a "Watari EL2: trapped ICC_PMR_EL1 writes so far\|Watari EL2:   trapped\|Watari EL2:   other trapped" build-chain/uart.txt | tail -5
fi
if ! grep -qE "BdsDxe: Press any key to enter the Boot Manager|Shell> " build-chain/uart.txt; then
    echo "MISSING: EDK2 end state (Boot Manager prompt or Shell>)"; rc=1
fi
grep -q "UNEXPECTED\|FATAL\|unhandled guest\|stray SMC" build-chain/uart.txt && { echo "FAIL: the layer reported an unexpected event"; rc=1; }
grep -q "ASSERT\|Synchronous Exception" build-chain/uart.txt && { echo "FAIL: EDK2 reported an ASSERT/exception"; rc=1; }
[ "$DONE" = 0 ] && { echo "FAIL: timeout before the end state"; rc=1; }
[ $rc = 0 ] && echo "PASS: EDK2 at EL1 under Watari EL2 (M1-4): BDS reached, vGIC traps handled, timer injected"
exit $rc
