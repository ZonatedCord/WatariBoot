#!/bin/bash
# SPDX-License-Identifier: MIT
# T21: wel2.ramdisk=0x<base>,0x<size> (DESIGN watari-winram 3): the layer's side, QEMU TCG, -smp 4, the EL1 test guest standing in
# for EDK2 (as run-gfdt.sh). Positive cases: the guest FDT read back from guest RAM (pmemsave + dtc) has /reserved-memory/ramdisk@<base>
# (compatible watari,ramdisk, reg, no-map), no /memory node overlaps the range, the number of /memory pieces is the carve's, and the guest
# still runs to its final SYSTEM_RESET (stage 2 keeps the range as RAM). Negative cases: a FATAL line with the reason and no jump into the
# guest. No download, no installation.
# Env: CPUIF=hw|trap|trap-notc (default trap), WEL2_ARGS (appended to every case), QEMU, CASES (default: all).
set -u
cd "$(dirname "$0")"
CPUIF="${CPUIF:-trap}"
CASES="${CASES:-split3 nosplit gran4k noarg badbase badsize overlap-image outside tiny-entry-range malformed zero nonfdt}"
make -s chain || { echo "FAIL: layer build"; exit 2; }
make -s -C ../guest || { echo "FAIL: guest build"; exit 2; }
GUEST=../guest/build/m1guest.bin
OUT=build-chain
HOLDMSG="Watari EL2: HOLD: take the photo, then hold the power button"
total=0

# readback <socket> <addr> <size> <out>: pmemsave of the guest FDT through the QEMU monitor
readback() {
    python3 -I - "$@" <<'PYEOF'
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
PYEOF
}

# checkdts <dts file> <base:size:pieces>: the node, the properties, the /memory pieces
checkdts() {
    python3 -I - "$1" "$2" <<'PYEOF'
import re, sys
dts = open(sys.argv[1]).read()
base, size, npieces = [int(x, 16) if i < 2 else int(x) for i, x in enumerate(sys.argv[2].split(":"))]
def cells(m):
    v = [int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]+", m)]
    return (v[0] << 32 | v[1], v[2] << 32 | v[3])
ok = True
node = re.search(r"reserved-memory \{(.*?)\n\t\};", dts, re.S)
if not node:
    print("FAIL: no /reserved-memory node"); sys.exit(1)
body = node.group(1)
if "ramdisk@%x {" % base not in body: print("FAIL: no ramdisk@%x node" % base); ok = False
if 'compatible = "watari,ramdisk"' not in body: print("FAIL: compatible"); ok = False
if "no-map;" not in body: print("FAIL: no-map missing"); ok = False
m = re.search(r"ramdisk@%x \{.*?reg = <([^>]*)>" % base, body, re.S)
if not m or cells(m.group(1)) != (base, size): print("FAIL: reg of the node"); ok = False
mem = [cells(m.group(1)) for m in re.finditer(r"memory@[0-9a-f]+ \{[^}]*?reg = <([^>]*)>", dts)]
if len(mem) != npieces: print("FAIL: %d /memory nodes, expected %d" % (len(mem), npieces)); ok = False
for b, s in mem:
    if b < base + size and b + s > base: print("FAIL: /memory %x+%x overlaps the RAM disk" % (b, s)); ok = False
if ok: print("ok: ramdisk@%x (%x bytes) in the guest FDT, %d /memory pieces, none overlapping it" % (base, size, len(mem)))
sys.exit(0 if ok else 1)
PYEOF
}

# run_case <name> <MEM> <extra layer args> <positive|negative|none> <expectation>
run_case() {
    local name="$1" mem="$2" extra="$3" kind="$4" want="$5" rc=0
    local ARGS="wel2.guest=0x40280000 wel2.abi=fdt wel2.gfdt=build wel2.hold=1 wel2.cpuif=$CPUIF $extra ${WEL2_ARGS:-}"
    local LOG=$OUT/rdfdt-$name-uart.log T=$OUT/rdfdt-$name-uart.txt SOCK=$OUT/rdfdt-$name.sock DTB=$OUT/rdfdt-$name.dtb
    rm -f "$SOCK" "$DTB"; : > "$LOG"
    echo "---- $name: $extra (MEM $mem)"
    "${QEMU:-qemu-system-aarch64}" -M virt,virtualization=on,gic-version=3 -cpu "${QEMU_CPU:-max}" -accel tcg \
        -nographic -m "$mem" -smp 4 -nic none -no-reboot -kernel $OUT/wel2.bin -append "$ARGS" \
        -device loader,file="$GUEST",addr=0x40280000,force-raw=on \
        -serial "file:$LOG" -monitor "unix:$SOCK,server,nowait" >$OUT/rdfdt-$name-qemu.out 2>&1 &
    local QPID=$! HELD=0
    for _ in $(seq 1 900); do
        kill -0 "$QPID" 2>/dev/null || break
        if tr -d '\r' < "$LOG" | grep -aqF "$HOLDMSG"; then HELD=1; break; fi
        if [ "$kind" = negative ] && tr -d '\r' < "$LOG" | grep -aqE "FATAL|halted"; then break; fi
        sleep 0.1
    done
    tr -d '\r' < "$LOG" > "$T"
    if [ "$kind" = positive ] && [ "$HELD" = 1 ]; then
        readback "$SOCK" "$(grep -a 'guest FDT at ' "$T" | head -1 | sed 's/.*guest FDT at //')" \
                 "$(grep -a '  built, ' "$T" | head -1 | sed 's/.*built, \([0-9]*\) bytes.*/\1/')" "$DTB"
    fi
    kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null
    if [ "$kind" = positive ]; then
        [ "$HELD" = 1 ] || { echo "FAIL: no HOLD message (the guest did not finish)"; rc=1; }
        grep -aq "eret to the guest at EL1" "$T" || { echo "FAIL: no hand-off"; rc=1; }
        grep -aq "FATAL\|UNEXPECTED\|unhandled guest" "$T" && { echo "FAIL: unexpected event"; rc=1; }
        grep -a "Watari EL2: ramdisk at\|Watari EL2:   size" "$T" | head -2
        if [ -s "$DTB" ] && dtc -I dtb -O dts "$DTB" > "$OUT/rdfdt-$name.dts" 2>/dev/null; then
            checkdts "$OUT/rdfdt-$name.dts" "$want" || rc=1
        else
            echo "FAIL: could not read or decode the guest FDT"; rc=1
        fi
    elif [ "$kind" = negative ]; then
        grep -aqF -- "$want" "$T" || { echo "MISSING: $want"; rc=1; }
        grep -aq "FATAL" "$T" || { echo "FAIL: no FATAL line"; rc=1; }
        grep -aq "eret to the guest" "$T" && { echo "FAIL: the layer jumped into the guest despite the bad argument"; rc=1; }
    else   # none: the argument is not given
        [ "$HELD" = 1 ] || { echo "FAIL: no HOLD message"; rc=1; }
        grep -aq "ramdisk" "$T" && { echo "FAIL: the layer mentions a ramdisk that was not asked for"; rc=1; }
        grep -aq "FATAL" "$T" && { echo "FAIL: FATAL in the log"; rc=1; }
    fi
    [ $rc = 0 ] && echo "PASS: $name" || { echo "FAILED: $name"; total=1; }
}

for c in $CASES; do
    case "$c" in
        split3)     run_case "$c" 2G "wel2.split=3 wel2.ramdisk=0xB0000000,0x1001000" positive "b0000000:1001000:4" ;;
        nosplit)    run_case "$c" 2G "wel2.ramdisk=0x80000000,0x2000000" positive "80000000:2000000:2" ;;
        gran4k)     run_case "$c" 2G "wel2.gran=4k wel2.ramdisk=0xB0001000,0x1001000" positive "b0001000:1001000:2" ;;
        noarg)      run_case "$c" 1G "" none "" ;;
        badbase)    run_case "$c" 2G "wel2.ramdisk=0xB0000800,0x1001000" negative "not aligned to the granule" ;;
        badsize)    run_case "$c" 2G "wel2.ramdisk=0xB0000000,0x1000800" negative "not aligned to the granule, or size is not a multiple of 4 KB" ;;
        overlap-image) run_case "$c" 2G "wel2.ramdisk=0x40200000,0x1000000" negative "overlaps the guest image or the guest FDT" ;;
        outside)    run_case "$c" 2G "wel2.ramdisk=0xC0000000,0x1000000" negative "not wholly inside one guest RAM range" ;;
        tiny-entry-range) run_case "$c" 1G "wel2.ramdisk=0x44000000,0x8000000" negative "below 128 MB" ;;
        malformed)  run_case "$c" 1G "wel2.ramdisk=0xB0000000" negative "expected 0x<base>,0x<size>" ;;
        zero)       run_case "$c" 1G "wel2.ramdisk=0x60000000,0x0" negative "zero base, size not above the 4 KB trailer" ;;
        nonfdt)     run_case "$c" 1G "wel2.abi=info wel2.ramdisk=0x60000000,0x1001000" negative "wel2.ramdisk needs wel2.abi=fdt" ;;
        *) echo "unknown case $c"; total=1 ;;
    esac
done
[ $total = 0 ] && echo "PASS: wel2.ramdisk: guest FDT node and /memory cut-out, refusals ($CASES)"
exit $total
