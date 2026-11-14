#!/bin/bash
# SPDX-License-Identifier: MIT
# M1-7 (T12a, PLAN E3/E4): the M1 platform firmware (edk2-apple/WatariM1Pkg, bench build) at EL1 on top of
# Watari EL2, QEMU TCG, -smp 4, -m 2G. Same as run-m1-4.sh but with our FD; the layer is unchanged and still
# takes the QEMU-bench chain path (QEMU's FDT patched in place, PL011 console). Documented limits: QEMU FDT,
# PL011 console, no GOP, ACPI tables from the acpigen model qemu-virt (so that the GIC addresses are QEMU's).
#   QEMU -kernel build-chain/wel2.bin           : the layer (arm64 Image, x0 = FDT)
#   QEMU -device loader,...,addr=0x40280000     : the firmware, entered by the layer at EL1
# Prerequisite: the external data disk and tools/build-edk2-m1.sh --bench (once).
# Success = the firmware reaches BDS (the bench build then boots the UEFI Shell) with the layer having
# handled the vGIC traps and injected timer interrupts. 60 s timeout. Exit 0 on success.
#   CPUIF=hw|trap|trap-notc   virtual CPU interface mode of the layer (boot argument wel2.cpuif), default hw.
#   WEL2_ARGS="..."           extra boot arguments for the layer.
#   SHELL_CMDS="cmd1;cmd2"    once the Shell prompt appears, type these commands on the serial console
#                             (QEMU -serial pipe:), e.g. SHELL_CMDS="acpiview -l" (E4: the ACPI tables as
#                             listed by the firmware's own Shell). SHELL_CMD_WAIT = seconds per command (8).
#   FD=<file>                 firmware image (default: the bench build on the data disk).
set -u
APPEND=()
case "${CPUIF:-hw}" in trap|trap-notc) APPEND=(-append "wel2.cpuif=${CPUIF}");; esac   # trap-notc: T6g
[ -n "${WEL2_ARGS:-}" ] && APPEND=(-append "${APPEND[1]:-} $WEL2_ARGS")
cd "$(dirname "$0")"
# shellcheck source=/dev/null
source ../../tools/env.sh || { echo "FAIL: data disk not mounted"; exit 2; }
FD="${FD:-$WATARI_DATA/build-m1/WATARI_M1_EFI-bench.fd}"
[ -f "$FD" ] || { echo "FAIL: $FD missing: run tools/build-edk2-m1.sh --bench first"; exit 2; }
make -s chain || { echo "FAIL: build"; exit 2; }

TICKS=600                       # 60 s
LOG=build-chain/uart.log
: > "$LOG"
SERIAL=(-serial "file:$LOG")
CATPID=""
if [ -n "${SHELL_CMDS:-}" ]; then
    PIPE=build-chain/ser
    rm -f "$PIPE.in" "$PIPE.out"; mkfifo "$PIPE.in" "$PIPE.out"
    SERIAL=(-serial "pipe:$PIPE")
    cat "$PIPE.out" > "$LOG" & CATPID=$!
    TICKS=$((TICKS + 100 * 4))
fi
"${QEMU:-qemu-system-aarch64}" -M virt,virtualization=on,gic-version=3 -cpu max -accel tcg \
    -nographic -m 2G -smp 4 -nic none -kernel build-chain/wel2.bin ${APPEND[@]+"${APPEND[@]}"} \
    -device loader,file="$FD",addr=0x40280000,force-raw=on \
    "${SERIAL[@]}" -monitor none >build-chain/qemu.out 2>&1 &
QPID=$!
if [ -n "${SHELL_CMDS:-}" ]; then exec 3> "build-chain/ser.in"; fi

# Stop as soon as the firmware reached its end state or the layer died; portable timeout.
DONE=0
for _ in $(seq 1 "$TICKS"); do
    kill -0 "$QPID" 2>/dev/null || { DONE=1; break; }
    if grep -aqE "BdsDxe: Press any key to enter the Boot Manager|Shell> |Watari EL2: halted" "$LOG"; then
        sleep 2; DONE=2
        if [ -n "${SHELL_CMDS:-}" ]; then
            IFS=';' read -ra CMDS <<< "$SHELL_CMDS"
            for c in "${CMDS[@]}"; do printf '%s\r' "$c" >&3; sleep "${SHELL_CMD_WAIT:-8}"; done
        fi
        break
    fi
    sleep 0.1
done
kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null
if [ -n "$CATPID" ]; then exec 3>&-; kill "$CATPID" 2>/dev/null; wait "$CATPID" 2>/dev/null; fi
tr -d '\r' < "$LOG" > build-chain/uart.txt

# Layer lines and the tail of the firmware log.
grep "^Watari EL2" build-chain/uart.txt | grep -v "vgic effect=" | head -50
echo "... [EDK2 output] ..."
tail -"${TAIL:-12}" build-chain/uart.txt

rc=0
expect() {
    grep -qF -- "$1" build-chain/uart.txt || { echo "MISSING: $1"; rc=1; }
}
expect "Watari EL2: cores (redistributors) = 0x0000000000000004"
expect 'Watari EL2: FDT /psci method "smc" patched to "hvc"'
expect "Watari EL2: FDT /memory base now 0x0000000040200000"
expect "Watari EL2: eret to EDK2 at EL1, entry = 0x0000000040280000"
# Proof that it is OUR firmware: the M1 MemInfoLib's map line and the ACPI driver's table listing.
expect "ArmVirtGetMemoryMap: System RAM: PhysicalBase 0x40200000"
expect "WatariM1AcpiDxe: FADT X_DSDT = 0x"
expect "WatariM1AcpiDxe: table DSDT (via X_DSDT)"
expect "Found GIC v3 Distributor @ 0x8000000"
expect "Watari EL2: vGIC GICD traps handled = 0x0000000000000064"
expect "Watari EL2: vGIC GICR traps handled = 0x0000000000000064"
expect "Watari EL2: cpu 0 virtual timer PPI 27 taken, injecting into the guest"
expect "Watari EL2:   timer irqs on this cpu so far = 0x0000000000000064"
expect "[Bds]BdsWait ...Zzzzzzzzzzzz..."
expect "[Bds]Exit the waiting!"
if [ "${CPUIF:-hw}" != hw ]; then
    expect "Watari EL2: cpuif mode = TRAP_EMULATE"
    expect "Watari EL2: trapped ICC_PMR_EL1 writes so far = "
    echo "ICC traps during this boot (last report):"
    grep -a "Watari EL2: trapped ICC_PMR_EL1 writes so far\|Watari EL2:   trapped\|Watari EL2:   other trapped" build-chain/uart.txt | tail -5
fi
if ! grep -qE "BdsDxe: Press any key to enter the Boot Manager|Shell> " build-chain/uart.txt; then
    echo "MISSING: firmware end state (Boot Manager prompt or Shell>)"; rc=1
fi
grep -q "UNEXPECTED\|FATAL\|unhandled guest\|non-PSCI SMC" build-chain/uart.txt && { echo "FAIL: the layer reported an unexpected event"; rc=1; }
grep -q "ASSERT\|Synchronous Exception" build-chain/uart.txt && { echo "FAIL: the firmware reported an ASSERT/exception"; rc=1; }
[ "$DONE" = 0 ] && { echo "FAIL: timeout before the end state"; rc=1; }
[ $rc = 0 ] && echo "PASS: the M1 platform firmware (WatariM1Pkg, bench build) at EL1 under Watari EL2 (M1-7): BDS reached, vGIC traps handled, timer injected"
exit $rc
