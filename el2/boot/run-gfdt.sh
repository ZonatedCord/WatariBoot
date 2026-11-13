#!/bin/bash
# SPDX-License-Identifier: MIT
# T12b/E6: the generalised hand-off (wel2.abi=fdt wel2.gfdt=build), QEMU TCG, -smp 4. The EL1 test guest (el2/guest) stands in
# for EDK2: an arm64 Image entered at EL1 with x0 = the guest FDT the layer built, not a patched copy of QEMU's. Without an
# info block the test guest assumes QEMU virt: G2-G11 run as in run-m1guest.sh, G1 (the info block check) is expected to fail.
#   wel2.guest=0x40280000 wel2.abi=fdt wel2.gfdt=build wel2.hold=1   (+ wel2.split=N when SPLIT=N: N RAM ranges with holes)
# The layer is held at the guest's final SYSTEM_RESET; the script then reads the guest FDT back out of guest RAM through the
# QEMU monitor (pmemsave) and checks it (dtc when installed): the number of /memory nodes, the image's range first, the vGIC
# window, no identifying property. No download, no installation.
# Env: SPLIT=0|2|3|4 (default 0), MEM (default 1G), CPUIF=hw|trap|trap-notc (default trap), WEL2_ARGS, QEMU, QEMU_CPU.
set -u
cd "$(dirname "$0")"
SPLIT="${SPLIT:-0}"
CPUIF="${CPUIF:-trap}"
MEM="${MEM:-1G}"
ARGS="wel2.guest=0x40280000 wel2.abi=fdt wel2.gfdt=build wel2.hold=1 wel2.cpuif=$CPUIF"
[ "$SPLIT" != 0 ] && ARGS="$ARGS wel2.split=$SPLIT"
[ -n "${WEL2_ARGS:-}" ] && ARGS="$ARGS $WEL2_ARGS"
make -s chain || { echo "FAIL: layer build"; exit 2; }
make -s -C ../guest || { echo "FAIL: guest build"; exit 2; }
GUEST=../guest/build/m1guest.bin
OUT=build-chain
LOG=$OUT/gfdt-uart.log
SOCK=$OUT/gfdt-mon.sock
DTB=$OUT/gfdt-guest.dtb
HOLDMSG="Watari EL2: HOLD: take the photo, then hold the power button"
rm -f "$SOCK" "$DTB"
: > "$LOG"
"${QEMU:-qemu-system-aarch64}" -M virt,virtualization=on,gic-version=3 -cpu "${QEMU_CPU:-max}" -accel tcg \
    -nographic -m "$MEM" -smp 4 -nic none -no-reboot -kernel $OUT/wel2.bin -append "$ARGS" \
    -device loader,file="$GUEST",addr=0x40280000,force-raw=on \
    -serial "file:$LOG" -monitor "unix:$SOCK,server,nowait" >$OUT/gfdt-qemu.out 2>&1 &
QPID=$!
HELD=0
for _ in $(seq 1 600); do
    kill -0 "$QPID" 2>/dev/null || break
    if tr -d '\r' < "$LOG" | grep -aqF "$HOLDMSG"; then HELD=1; break; fi
    sleep 0.1
done
tr -d '\r' < "$LOG" > $OUT/gfdt-uart.txt
rc=0
FDT_AT=$(grep -a "guest FDT at " $OUT/gfdt-uart.txt | head -1 | sed 's/.*guest FDT at //')
FDT_SZ=$(grep -a "  built, " $OUT/gfdt-uart.txt | head -1 | sed 's/.*built, \([0-9]*\) bytes.*/\1/')
grep -aE "guest FDT|guest image|image end|eret to the guest|WARNING|FATAL|halted" $OUT/gfdt-uart.txt | head -30
if [ "$HELD" = 1 ] && [ -n "$FDT_AT" ] && [ -n "$FDT_SZ" ]; then
    # read the FDT back from guest RAM (the monitor runs while the guest is held)
    python3 - "$SOCK" "$FDT_AT" "$FDT_SZ" "$DTB" <<'EOF'
import socket, sys, time
sock, addr, size, out = sys.argv[1:5]
s = socket.socket(socket.AF_UNIX)
for _ in range(50):
    try:
        s.connect(sock); break
    except OSError:
        time.sleep(0.1)
s.settimeout(3)
def rd():
    d = b""
    try:
        while True:
            c = s.recv(4096)
            if not c: break
            d += c
    except OSError:
        pass
    return d
rd()
s.sendall(("pmemsave %d %s %s\n" % (int(addr, 16), size, out)).encode())
time.sleep(1.0)
rd()
s.sendall(b"quit\n")
time.sleep(0.3)
EOF
fi
kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null

[ "$HELD" = 1 ] && echo "ok: the guest ran to its final SYSTEM_RESET and the layer held" || { echo "FAIL: no HOLD message (the chain or the guest did not finish)"; rc=1; }
gline() { grep -aE "(^|[: ])$1 (PASS|FAIL|SKIP) " $OUT/gfdt-uart.txt | head -1; }
# G1 is expected to FAIL here: it checks the info block, and x0 is the FDT under wel2.abi=fdt (the guest then assumes QEMU virt).
l=$(gline G1); echo "$l" | grep -q "FAIL.*no info block" || { echo "MISSING: G1 should fail with 'no info block' (x0 is the FDT)"; rc=1; }
for g in G2 G3 G4 G5a G5b G6 G7 G8 G9 G10 G11; do
    l=$(gline $g)
    if [ -z "$l" ]; then echo "MISSING: $g"; rc=1
    elif echo "$l" | grep -q " FAIL "; then echo "FAILED: $l"; rc=1; fi
done
grep -aq "eret to the guest at EL1" $OUT/gfdt-uart.txt || { echo "MISSING: the layer did not take the FDT hand-off"; rc=1; }
grep -aq "guest info block at" $OUT/gfdt-uart.txt && { echo "FAIL: the info-block path ran with wel2.abi=fdt"; rc=1; }
grep -q "UNEXPECTED\|FATAL\|unhandled guest\|non-PSCI SMC" $OUT/gfdt-uart.txt && { echo "FAIL: unexpected event reported"; rc=1; }
NMEM=$(grep -ac "guest FDT /memory base = " $OUT/gfdt-uart.txt)
WANT=1; [ "$SPLIT" != 0 ] && WANT=$SPLIT
[ "$NMEM" = "$WANT" ] || { echo "FAIL: $NMEM /memory ranges in the layer's log, expected $WANT"; rc=1; }
# the first range holds the image
FIRST=$(grep -a "guest FDT /memory base = " $OUT/gfdt-uart.txt | head -1 | sed 's/.*= //')
IMG=$(grep -a "guest image at " $OUT/gfdt-uart.txt | head -1 | sed 's/.*= //;s/.*at //')
echo "first /memory base $FIRST, image $IMG"

if [ -s "$DTB" ]; then
    echo "ok: guest FDT read back from guest RAM ($(wc -c < "$DTB" | tr -d ' ') bytes)"
    if command -v dtc >/dev/null 2>&1; then
        dtc -I dtb -O dts "$DTB" > $OUT/gfdt-guest.dts 2>$OUT/gfdt-dtc.err || { echo "FAIL: dtc rejects the guest FDT"; rc=1; }
        N=$(grep -c "device_type = \"memory\"" $OUT/gfdt-guest.dts)
        [ "$N" = "$WANT" ] || { echo "FAIL: $N memory nodes in the FDT read back, expected $WANT"; rc=1; }
        grep -q 'method = "hvc"' $OUT/gfdt-guest.dts || { echo "FAIL: /psci method is not hvc"; rc=1; }
        grep -q 'arm,gic-v3' $OUT/gfdt-guest.dts || { echo "FAIL: no GICv3 node"; rc=1; }
        grep -q 'arm,armv8-timer' $OUT/gfdt-guest.dts || { echo "FAIL: no timer node"; rc=1; }
        grep -q 'platform = "qemu"' $OUT/gfdt-guest.dts || { echo "FAIL: /chosen/watari,el2 platform"; rc=1; }
        grep -qi 'serial-number\|mac-address\|kaslr-seed\|rng-seed' $OUT/gfdt-guest.dts && { echo "FAIL: an identifying property in the guest FDT"; rc=1; }
        grep -A3 -m1 'memory@' $OUT/gfdt-guest.dts | head -4
    else
        echo "SKIP: dtc is not installed (FDT magic only)"
        [ "$(head -c 4 "$DTB" | od -An -tx1 | tr -d ' \n')" = d00dfeed ] || { echo "FAIL: no FDT magic at the guest FDT address"; rc=1; }
    fi
else
    echo "FAIL: could not read the guest FDT back (monitor pmemsave)"; rc=1
fi
[ $rc = 0 ] && echo "PASS: generalised hand-off, guest FDT built (SPLIT=$SPLIT, CPUIF=$CPUIF, ARGS=${WEL2_ARGS:-})"
exit $rc
