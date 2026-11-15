#!/bin/bash
# SPDX-License-Identifier: MIT
# T16j: wel2.pmlog=0x<base>,0x<size>, the post-mortem log ring, QEMU TCG, -smp 4, the EL1 test guest standing in for EDK2 (as
# run-ramdisk-fdt.sh). The layer's ring is read back out of guest RAM through the QEMU monitor (pmemsave, relative path) and parsed
# with the SAME parser the loader uses (tools/m3a/pmlog_read.py -> watari-load.py pmlog_parse_region). Cases:
#   nosplit  ring in the one guest span (stage-2 hole + FDT cut): the ring text equals the UART text from its first line on, holds
#            the boot, PSCI CPU_ON and HOLD lines; the guest FDT has /reserved-memory/pmlog@<base>, no /memory overlaps it
#   split3   ring inside a piece of wel2.split=3 (g_guest_ram[] cut, the M1 path)
#   gran4k   the same with wel2.gran=4k
#   reset    after the hold, the QEMU monitor does system_reset (RAM is kept, the images are reloaded): the second boot finds the valid
#            header, counts boot 2, and the ring holds only the second boot's lines (simulated reset: write, reset, read back)
#   fatal    a FATAL after the ring is up (bad wel2.ramdisk): the ring ends with the FATAL and halted lines
#   badarg   an unaligned ring: a WARNING line, no ring, the boot goes on to the HOLD (never stops the boot)
#   noarg    without the argument: no mention of a ring anywhere
# Env: CPUIF=hw|trap|trap-notc (default trap), WEL2_ARGS (appended to every case), QEMU, CASES. No download, no installation.
set -u
cd "$(dirname "$0")"
CPUIF="${CPUIF:-trap}"
CASES="${CASES:-nosplit split3 gran4k reset fatal badarg noarg}"
make -s chain || { echo "FAIL: layer build"; exit 2; }
make -s -C ../guest || { echo "FAIL: guest build"; exit 2; }
GUEST=../guest/build/m1guest.bin
OUT=build-chain
READ=../../tools/m3a/pmlog_read.py
HOLDMSG="Watari EL2: HOLD: take the photo, then hold the power button"
total=0

# mon <socket> <sleep> <command...>: run monitor commands one after the other (the socket accepts one connection after another)
mon() {
    python3 -I - "$@" <<'PYEOF'
import socket, sys, time
sock, wait = sys.argv[1], float(sys.argv[2])
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
for cmd in sys.argv[3:]:
    s.sendall((cmd + "\n").encode())
    time.sleep(wait)
    rd()
PYEOF
}

# run_case <name> <MEM> <extra layer args> <ring base> <ring size hex> <kind>
run_case() {
    local name="$1" mem="$2" extra="$3" base="$4" size="$5" kind="$6" rc=0
    local ARGS="wel2.guest=0x40280000 wel2.abi=fdt wel2.gfdt=build wel2.hold=1 wel2.cpuif=$CPUIF $extra ${WEL2_ARGS:-}"
    local LOG=$OUT/pm-$name-uart.log T=$OUT/pm-$name-uart.txt SOCK=$OUT/pm-$name.sock DTB=$OUT/pm-$name.dtb
    local BIN=$OUT/pm-$name.bin TXT=$OUT/pm-$name-ring.txt
    local NOREBOOT="-no-reboot"
    [ "$kind" = reset ] && NOREBOOT=""
    rm -f "$SOCK" "$DTB" "$BIN" "$TXT" "$BIN.2"; : > "$LOG"
    echo "---- $name: $extra (MEM $mem)"
    "${QEMU:-qemu-system-aarch64}" -M virt,virtualization=on,gic-version=3 -cpu "${QEMU_CPU:-max}" -accel tcg \
        -nographic -m "$mem" -smp 4 -nic none $NOREBOOT -kernel $OUT/wel2.bin -append "$ARGS" \
        -device loader,file="$GUEST",addr=0x40280000,force-raw=on \
        -serial "file:$LOG" -monitor "unix:$SOCK,server,nowait" >$OUT/pm-$name-qemu.out 2>&1 &
    local QPID=$! HELD=0
    for _ in $(seq 1 900); do
        kill -0 "$QPID" 2>/dev/null || break
        if tr -d '\r' < "$LOG" | grep -aqF "$HOLDMSG"; then HELD=1; break; fi
        if [ "$kind" = fatal ] && tr -d '\r' < "$LOG" | grep -aq "Watari EL2: halted"; then break; fi
        sleep 0.1
    done
    sleep 0.5
    tr -d '\r' < "$LOG" > "$T"
    case "$kind" in
    ring|reset|fatal)
        mon "$SOCK" 1.0 "pmemsave $((base)) $((size)) $BIN"
        python3 -I $READ "$BIN" -o "$TXT" || { echo "FAIL: the ring does not parse"; rc=1; }
        ;;
    esac
    if [ "$kind" = ring ] && [ "$HELD" = 1 ]; then
        mon "$SOCK" 1.0 "pmemsave $(grep -a 'guest FDT at ' "$T" | head -1 | sed 's/.*guest FDT at //') $(grep -a '  built, ' "$T" | head -1 | sed 's/.*built, \([0-9]*\) bytes.*/\1/') $DTB"
    fi
    case "$kind" in
    ring)
        [ "$HELD" = 1 ] || { echo "FAIL: no HOLD message"; rc=1; }
        grep -aq "FATAL\|UNEXPECTED\|unhandled guest" "$T" && { echo "FAIL: unexpected event"; rc=1; }
        grep -aq "Watari EL2: post-mortem log ring at $(printf '0x%016x' $((base)))" "$T" || { echo "FAIL: no ring line"; rc=1; }
        # the ring holds every line the UART shows from its first line on (guest console text: printable bytes and LF only)
        python3 -I - "$T" "$TXT" <<'PYEOF' || rc=1
import sys
uart = open(sys.argv[1], "rb").read()
ring = open(sys.argv[2], "rb").read()
i = uart.find(b"Watari EL2: post-mortem log ring at ")
want = bytes(b for b in uart[i:] if b == 10 or 32 <= b <= 126)
if i < 0 or ring != want:
    n = next((k for k in range(min(len(ring), len(want))) if ring[k] != want[k]), min(len(ring), len(want)))
    print("FAIL: ring text differs from the UART text at byte %d (ring %d bytes, uart %d): %r vs %r" % (n, len(ring), len(want), ring[n:n+60], want[n:n+60]))
    sys.exit(1)
print("ok: the ring (%d bytes) equals the UART text from its first line on" % len(ring))
PYEOF
        for k in "hello from EL2" "PSCI CPU_ON cpu" "HOLD: take the photo" "eret to the guest at EL1"; do
            grep -aqF "$k" "$TXT" || { echo "FAIL: ring lacks: $k"; rc=1; }
        done
        if [ -s "$DTB" ] && dtc -I dtb -O dts "$DTB" > "$OUT/pm-$name.dts" 2>/dev/null; then
            python3 -I - "$OUT/pm-$name.dts" "$base" "$size" <<'PYEOF' || rc=1
import re, sys
dts = open(sys.argv[1]).read()
base, size = int(sys.argv[2], 0), int(sys.argv[3], 0)
def cells(m):
    v = [int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]+", m)]
    return (v[0] << 32 | v[1], v[2] << 32 | v[3])
ok = True
node = re.search(r"reserved-memory \{(.*?)\n\t\};", dts, re.S)
body = node.group(1) if node else ""
m = re.search(r"pmlog@%x \{.*?reg = <([^>]*)>" % base, body, re.S)
if not m or cells(m.group(1)) != (base, size): print("FAIL: pmlog node or its reg"); ok = False
if 'compatible = "watari,pmlog"' not in body or "no-map;" not in body: print("FAIL: pmlog compatible / no-map"); ok = False
for mm in re.finditer(r"memory@[0-9a-f]+ \{[^}]*?reg = <([^>]*)>", dts):
    b, s = cells(mm.group(1))
    if b < base + size and b + s > base: print("FAIL: /memory %x+%x overlaps the ring" % (b, s)); ok = False
if ok: print("ok: guest FDT has /reserved-memory/pmlog@%x and no /memory node overlaps it" % base)
sys.exit(0 if ok else 1)
PYEOF
        else
            echo "FAIL: could not read or decode the guest FDT"; rc=1
        fi
        ;;
    reset)
        [ "$HELD" = 1 ] || { echo "FAIL: no HOLD message"; rc=1; }
        python3 -I $READ "$BIN" --boots 1 >/dev/null || { echo "FAIL: first boot: boot counter is not 1"; rc=1; }
        : > "$LOG"
        mon "$SOCK" 0.2 "system_reset"
        HELD=0
        for _ in $(seq 1 900); do
            kill -0 "$QPID" 2>/dev/null || break
            if tr -d '\r' < "$LOG" | grep -aqF "$HOLDMSG"; then HELD=1; break; fi
            sleep 0.1
        done
        sleep 0.5
        tr -d '\r' < "$LOG" > "$T"
        [ "$HELD" = 1 ] || { echo "FAIL: no HOLD message after the reset"; rc=1; }
        mon "$SOCK" 1.0 "pmemsave $((base)) $((size)) $BIN.2"
        python3 -I $READ "$BIN.2" --boots 2 -o "$TXT" || { echo "FAIL: after the reset the ring is not boot 2"; rc=1; }
        grep -aqF "boot counter = 0x0000000000000002" "$TXT" || { echo "FAIL: ring lacks 'boot counter = 2'"; rc=1; }
        grep -aqF "hello from EL2" "$TXT" && grep -aqF "HOLD: take the photo" "$TXT" || { echo "FAIL: second boot's lines missing"; rc=1; }
        [ "$(grep -ac 'hello from EL2' "$TXT")" = 1 ] || { echo "FAIL: the ring holds more than the second boot"; rc=1; }
        ;;
    fatal)
        grep -aq "FATAL: wel2.ramdisk" "$T" || { echo "FAIL: no FATAL in the UART log"; rc=1; }
        grep -aq "FATAL: wel2.ramdisk" "$TXT" && grep -aq "Watari EL2: halted" "$TXT" || { echo "FAIL: ring lacks the FATAL / halted lines"; rc=1; }
        ;;
    badarg)
        [ "$HELD" = 1 ] || { echo "FAIL: a bad wel2.pmlog stopped the boot (no HOLD)"; rc=1; }
        grep -aq "FATAL" "$T" && { echo "FAIL: FATAL in the log"; rc=1; }
        grep -aq "WARNING: wel2.pmlog: base or size not 16 KB aligned" "$T" || { echo "FAIL: no WARNING with the reason"; rc=1; }
        grep -aq "post-mortem log ring at" "$T" && { echo "FAIL: a ring was started"; rc=1; }
        grep -aq "eret to the guest at EL1" "$T" || { echo "FAIL: no hand-off"; rc=1; }
        ;;
    none)
        [ "$HELD" = 1 ] || { echo "FAIL: no HOLD message"; rc=1; }
        grep -aqi "pmlog\|post-mortem" "$T" && { echo "FAIL: the layer mentions a ring that was not asked for"; rc=1; }
        grep -aq "FATAL" "$T" && { echo "FAIL: FATAL in the log"; rc=1; }
        ;;
    esac
    kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null
    [ $rc = 0 ] && echo "PASS: $name" || { echo "FAILED: $name"; total=1; }
}

for c in $CASES; do
    case "$c" in
        nosplit) run_case "$c" 1G "wel2.pmlog=0x7e000000,0x100000" 0x7e000000 0x100000 ring ;;
        split3)  run_case "$c" 2G "wel2.split=3 wel2.pmlog=0xB0000000,0x100000" 0xB0000000 0x100000 ring ;;
        gran4k)  run_case "$c" 2G "wel2.gran=4k wel2.pmlog=0xB0004000,0x40000" 0xB0004000 0x40000 ring ;;
        reset)   run_case "$c" 1G "wel2.pmlog=0x7e000000,0x100000" 0x7e000000 0x100000 reset ;;
        fatal)   run_case "$c" 2G "wel2.pmlog=0x7e000000,0x100000 wel2.ramdisk=0xB0000800,0x1001000" 0x7e000000 0x100000 fatal ;;
        badarg)  run_case "$c" 1G "wel2.pmlog=0x7e000100,0x100000" 0 0 badarg ;;
        noarg)   run_case "$c" 1G "" 0 0 none ;;
        *) echo "unknown case $c"; total=1 ;;
    esac
done
[ $total = 0 ] && echo "PASS: wel2.pmlog: ring contents, guest FDT node, reset survival in QEMU, FATAL kept, refusal does not stop the boot ($CASES)"
exit $total
