#!/bin/bash
# SPDX-License-Identifier: MIT
# M1-8 (T12b, PLAN E10): the M1 platform firmware, PRODUCTION build (edk2-apple/WatariM1Pkg, no UART of any kind: its console
# is the hypercall WEL2_CONSOLE_WRITE) at EL1 on top of Watari EL2, on an FDT shaped like the M1's, on QEMU TCG, -smp 4.
#   layer boot arguments: wel2.guest=<entry> wel2.abi=fdt wel2.gfdt=build wel2.split=<n>   (+ wel2.cpuif=...)
# The guest FDT is the one the LAYER builds (DESIGN 4.2): several /memory nodes, /psci (hvc), a GICv3 node at QEMU's addresses
# (the physical GIC is QEMU's), the timer, the cpus; no UART, PCI, fw_cfg or virtio node. The firmware's whole log therefore
# reaches us through the hypercall, on the layer's UART.
#   QEMU -kernel build-chain/wel2.bin        : the layer (arm64 Image, x0 = QEMU's FDT)
#   QEMU -device loader,...,addr=<entry>     : the production firmware, entered by the layer at EL1 with x0 = the guest FDT
# Prerequisite: the data disk and tools/build-edk2-m1.sh (production build, once).
# Success = the firmware reaches BDS (since E12 its end state in the production build is the built-in self-test, started by WatariM1BootManagerLib because nothing is
#   bootable; before E12 it was the Boot Manager Menu. The self-test itself is checked by run-m1-9.sh; this run only sees it start) with
#   - every /memory range of the layer's FDT accounted for: the first (the one holding the image) is PrePi's system memory
#     (ArmVirtGetMemoryMap line), every other one is added by HighMemDxe;
#   - the layer having handled the vGIC traps and injected timer interrupts; no ASSERT, no exception.
# Env:
#   CPUIF=hw|trap|trap-notc   virtual CPU interface mode of the layer (default hw)
#   SPLIT=<n>                 wel2.split (default 3: three RAM ranges with 32 MB holes); 0 = one range
#   MEM=<size>                guest RAM given to QEMU (default 2G)
#   ENTRY=0x<addr>            where the firmware is loaded and entered (default 0x40280000)
#   HIGH=1                    the RISKS R-E4 run: -m 6G and the image at 0x1_0000_0000 (above 4 GB); checks that it really is
#   WEL2_ARGS="..."           extra boot arguments for the layer
#   FD=<file>                 firmware image (default: the production build on the data disk)
#   COMPARE_E3=<log>          the cleaned log of a run-m1-7.sh (build-chain/uart.txt): compare the [Bds] end state with it
# Limits (documented, not hidden): the GIC sits at QEMU's addresses and the IPA size is QEMU's, not the M1's; there is no AIC and
# no FIQ timer (the timer is the virtual timer PPI 27 through the vGIC); no framebuffer; the ACPI tables are the j293 ones (the
# production build), so they do not describe this machine's GIC (nothing here consumes them); caches are not modelled by TCG.
# 90 s timeout. Exit 0 on success.
set -u
cd "$(dirname "$0")"
CPUIF="${CPUIF:-hw}"
SPLIT="${SPLIT:-3}"
MEM="${MEM:-2G}"
ENTRY="${ENTRY:-0x40280000}"
if [ "${HIGH:-0}" = 1 ]; then MEM=6G; ENTRY=0x100000000; fi
ARGS="wel2.guest=$ENTRY wel2.abi=fdt wel2.gfdt=build"
[ "$SPLIT" != 0 ] && ARGS="$ARGS wel2.split=$SPLIT"
case "$CPUIF" in trap|trap-notc) ARGS="$ARGS wel2.cpuif=$CPUIF";; esac
[ -n "${WEL2_ARGS:-}" ] && ARGS="$ARGS $WEL2_ARGS"
# shellcheck source=/dev/null
source ../../tools/env.sh || { echo "FAIL: data disk not mounted"; exit 2; }
FD="${FD:-$WATARI_DATA/build-m1/WATARI_M1_EFI.fd}"
[ -f "$FD" ] || { echo "FAIL: $FD missing: run tools/build-edk2-m1.sh first"; exit 2; }
make -s chain || { echo "FAIL: build"; exit 2; }

OUT=build-chain
LOG=$OUT/m1-8-uart.log
TXT=$OUT/m1-8-uart.txt
: > "$LOG"
"${QEMU:-qemu-system-aarch64}" -M virt,virtualization=on,gic-version=3 -cpu max -accel tcg \
    -nographic -m "$MEM" -smp 4 -nic none -kernel $OUT/wel2.bin -append "$ARGS" \
    -device loader,file="$FD",addr="$ENTRY",force-raw=on \
    -serial "file:$LOG" -monitor none >$OUT/m1-8-qemu.out 2>&1 &
QPID=$!
trap 'kill "$QPID" 2>/dev/null' EXIT INT TERM

# Stop as soon as the firmware reached its end state or the layer died.
DONE=0
for _ in $(seq 1 900); do
    kill -0 "$QPID" 2>/dev/null || { DONE=1; break; }
    if grep -aqE "\[Bds\] ?Booting (Boot Manager Menu|Watari M1 self-test)|Shell> |Watari EL2: halted" "$LOG"; then sleep 3; DONE=2; break; fi
    sleep 0.1
done
kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null
tr -d '\r' < "$LOG" > "$TXT"

echo "[layer] boot arguments: $ARGS   (QEMU -m $MEM, cpuif $CPUIF)"
grep -a "^Watari EL2" "$TXT" | grep -aE "guest FDT|guest image|image end|eret to the guest|WARNING|FATAL|halted|cpuif mode" | head -24
echo "... [firmware log through the hypercall] ..."
grep -aE "ArmVirtGetMemoryMap|InitializeHighMemDxe|UEFI firmware|\[Bds\]|Found GIC|SELFTEST start|ASSERT [/A-Za-z(]" "$TXT" | head -40

rc=0
expect() { grep -aqF -- "$1" "$TXT" || { echo "MISSING: $1"; rc=1; }; }
expect "Watari EL2: eret to the guest at EL1, entry = "
expect "UEFI firmware (version "
expect "Found GIC v3 Distributor @ 0x8000000"
expect "[Bds]Exit the waiting!"
expect "[Bds]Booting Watari M1 self-test"
expect "Watari EL2: vGIC GICD traps handled = "
expect "Watari EL2: vGIC GICR traps handled = "
expect "Watari EL2: cpu 0 virtual timer PPI 27 taken, injecting into the guest"
if [ "$CPUIF" != hw ]; then
    expect "Watari EL2: cpuif mode = TRAP_EMULATE"
    expect "Watari EL2: trapped ICC_PMR_EL1 writes so far = "
fi

# --- the memory map: the layer's ranges against what the firmware did with them -------------------------------------
# Layer lines: "guest FDT /memory base = 0x<b>" followed by "  size = 0x<s>", first = the range that holds the image.
RANGES=$(grep -aE "guest FDT /memory base = |^Watari EL2:   size = " "$TXT" | sed -e 's/.*= 0x0*//' | paste -d' ' - - )
NR=$(echo "$RANGES" | grep -ac .)
WANT=1; [ "$SPLIT" != 0 ] && WANT=$SPLIT
[ "$NR" = "$WANT" ] || { echo "FAIL: the layer lists $NR /memory ranges, expected $WANT"; rc=1; }
hex() { printf '%x' "$((16#$1))"; }
FIRST_B=$(echo "$RANGES" | head -1 | cut -d' ' -f1); FIRST_S=$(echo "$RANGES" | head -1 | cut -d' ' -f2)
# PrePi / MemInfoLib: PcdSystemMemoryBase and size = the first range.
MAPL=$(grep -a "ArmVirtGetMemoryMap: System RAM: PhysicalBase " "$TXT" | head -1)
GOT_B=$(echo "$MAPL" | sed -n 's/.*PhysicalBase 0x\([0-9A-Fa-f]*\),.*/\1/p'); GOT_S=$(echo "$MAPL" | sed -n 's/.*Length 0x\([0-9A-Fa-f]*\).*/\1/p')
if [ -n "$GOT_B" ] && [ "$(hex "$GOT_B")" = "$(hex "$FIRST_B")" ] && [ "$(hex "$GOT_S")" = "$(hex "$FIRST_S")" ]; then
    echo "ok: PrePi system memory = the range of the image: base 0x$(hex "$FIRST_B"), size 0x$(hex "$FIRST_S")"
else
    echo "FAIL: PrePi's range ($MAPL) is not the first /memory range of the layer's FDT (0x$FIRST_B, 0x$FIRST_S)"; rc=1
fi
# the image lies in it
IMG=$(grep -a "guest image at " "$TXT" | head -1 | sed 's/.*at 0x0*//')
if [ -n "$IMG" ] && [ $((16#$IMG)) -ge $((16#$FIRST_B)) ] && [ $((16#$IMG)) -lt $((16#$FIRST_B + 16#$FIRST_S)) ]; then
    echo "ok: the image (0x$IMG) lies in the first range"
else echo "FAIL: the image (0x$IMG) is not in the first range"; rc=1; fi
if [ "${HIGH:-0}" = 1 ]; then
    [ -n "$IMG" ] && [ $((16#$IMG)) -ge $((16#100000000)) ] && echo "ok: HIGH run: the image is above 4 GB (0x$IMG)" ||
        { echo "FAIL: HIGH run but the image (0x$IMG) is not above 4 GB"; rc=1; }
fi
# every other range is added by HighMemDxe, with exactly its bounds
echo "$RANGES" | tail -n +2 | while read -r B S; do
    [ -z "$B" ] && continue
    END=$(printf '%X' $((16#$B + 16#$S - 1))); BEG=$(printf '%X' $((16#$B)))
    if grep -aq "InitializeHighMemDxe: Add System RAM @ 0x$BEG - 0x$END" "$TXT"; then
        echo "ok: HighMemDxe added 0x$BEG - 0x$END"
    else echo "FAIL: HighMemDxe did not add the range 0x$BEG - 0x$END"; exit 1; fi
done || rc=1
NADD=$(grep -ac "InitializeHighMemDxe: Add System RAM @ " "$TXT")
[ "$NADD" = $((NR - 1)) ] || { echo "FAIL: HighMemDxe added $NADD ranges, expected $((NR - 1))"; rc=1; }

# --- optional: the BdsDxe end state against the E3 (run-m1-7.sh) log ------------------------------------------------------
if [ -n "${COMPARE_E3:-}" ] && [ -f "$COMPARE_E3" ]; then
    norm() { sed 's/^\.*//' "$1" | grep -aE "^\[Bds\]|^\[BdsDxe\]" | sed -n '1,/Stop Hotkey Service/p' | sed -e 's/[0-9A-Fa-f]\{8\}-[0-9A-Fa-f-]*//g' -e 's/Boot[0-9]*:.*//'; }
    norm "$COMPARE_E3" > $OUT/m1-8-bds-e3.txt; norm "$TXT" > $OUT/m1-8-bds-m1-8.txt
    if diff -q $OUT/m1-8-bds-e3.txt $OUT/m1-8-bds-m1-8.txt >/dev/null; then
        echo "ok: the BdsDxe log up to 'Stop Hotkey Service' is the same as in the E3 run ($(wc -l < $OUT/m1-8-bds-m1-8.txt | tr -d ' ') lines)"
    else
        echo "DIFF: the BdsDxe log differs from the E3 run:"; diff $OUT/m1-8-bds-e3.txt $OUT/m1-8-bds-m1-8.txt | head -10
        rc=1
    fi
fi

grep -aq "UNEXPECTED\|FATAL\|unhandled guest\|non-PSCI SMC" "$TXT" && { echo "FAIL: the layer reported an unexpected event"; rc=1; }
grep -aqE "ASSERT [/A-Za-z(]|ASSERT_EFI_ERROR|Synchronous Exception" "$TXT" && { echo "FAIL: the firmware reported an ASSERT/exception"; rc=1; }
[ "$DONE" = 0 ] && { echo "FAIL: timeout before the end state"; rc=1; }
[ $rc = 0 ] && echo "PASS: the M1 platform firmware (production build, hypercall console) at EL1 under Watari EL2 on the layer's guest FDT (M1-8; split=$SPLIT, -m $MEM, cpuif=$CPUIF$([ "${HIGH:-0}" = 1 ] && echo ', image above 4 GB')): BDS reached, every range accounted for"
exit $rc
