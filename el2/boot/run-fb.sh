#!/bin/bash
# SPDX-License-Identifier: MIT
# E9: the framebuffer hand-over (wel2.fb=band|guest) on the QEMU bench, with the fake panel (wel2.fakefb=<W>x<H>, x2r10g10b10, and
# wel2.fakefb8=<W>x<H>, x8r8g8b8: a block at the top of the RAM that the layer draws on and advertises). Five runs, QEMU TCG, -smp 4:
#   1 band,  x2r10g10b10, EL1 test guest with the info block (version 2): G12 draws four colour bars in the guest part
#   2 band,  x8r8g8b8                                                      "
#   3 guest, x2r10g10b10                                                    "
#   4 guest, x8r8g8b8                                                       "
#   5 band,  x2r10g10b10, wel2.abi=fdt wel2.gfdt=build: no info block (G12 skips); the guest FDT read back from guest RAM has the
#     simple-framebuffer node of the guest part, fb-mode "band", and /memory does not cover the fake panel.
# Each run is held at the guest's final SYSTEM_RESET (wel2.hold=1: QEMU stays up); the script then dumps the fake panel with the QEMU
# monitor (pmemsave) and checks it from outside, pixel by pixel, in Python:
#   guest part (rows >= the band, or every row in guest mode): every pixel is the bar colour of its quarter of the width, in the
#     panel's format (red, green, blue, yellow at full intensity: the layer's palette has none of these values), so the colour
#     channel order is proved and, in guest mode, that the layer wrote NOTHING after the hand-over (the guest's log lines, the
#     summary, the statistics and the HOLD message would otherwise have drawn over the bars);
#   band mode: no pixel of the band rows has one of the four bar values (the guest wrote nothing there; its stage 2 has no
#     mapping for them) and the ticker line shows text (the layer kept drawing inside the band).
# Env: CPUIF=hw|trap|trap-notc (default hw), WEL2_ARGS (extra boot arguments, e.g. "wel2.e2h=0 wel2.gran=4k"), SIZE=WxH
# (default 640x480: 1 228 800 bytes = 75 pages of 16 KB; the size must be a whole number of pages), QEMU, QEMU_CPU.
# No download, no installation. Exit 0 = all five runs pass.
set -u
cd "$(dirname "$0")"
CPUIF="${CPUIF:-hw}"
SIZE="${SIZE:-640x480}"
W=${SIZE%x*}; H=${SIZE#*x}
case "$CPUIF" in hw|trap|trap-notc) ;; *) echo "FAIL: CPUIF must be hw, trap or trap-notc"; exit 2 ;; esac
make -s chain || { echo "FAIL: layer build"; exit 2; }
make -s -C ../guest || { echo "FAIL: guest build"; exit 2; }
GUEST=../guest/build/m1guest.bin
OUT=build-chain
HOLDMSG="Watari EL2: HOLD: take the photo, then hold the power button"
total_rc=0

# run_case <name> <fakefb|fakefb8> <band|guest> <info|fdt>
run_case() {
    local name="$1" fake="$2" mode="$3" abi="$4"
    local tag="fb-$name" rc=0
    local LOG=$OUT/$tag-uart.log SOCK=$OUT/$tag-mon.sock DUMP=$OUT/$tag.bin DTB=$OUT/$tag-guest.dtb
    local ARGS="wel2.guest=0x40280000 wel2.hold=1 wel2.cpuif=$CPUIF wel2.$fake=${W}x${H} wel2.fb=$mode"
    [ "$abi" = fdt ] && ARGS="$ARGS wel2.abi=fdt wel2.gfdt=build"
    [ -n "${WEL2_ARGS:-}" ] && ARGS="$ARGS $WEL2_ARGS"
    rm -f "$SOCK" "$DUMP" "$DTB"
    : > "$LOG"
    echo "---- $name: $ARGS"
    "${QEMU:-qemu-system-aarch64}" -M virt,virtualization=on,gic-version=3 -cpu "${QEMU_CPU:-max}" -accel tcg \
        -nographic -m 512M -smp 4 -nic none -no-reboot -kernel $OUT/wel2.bin -append "$ARGS" \
        -device loader,file="$GUEST",addr=0x40280000,force-raw=on \
        -serial "file:$LOG" -monitor "unix:$SOCK,server,nowait" >$OUT/$tag-qemu.out 2>&1 &
    local QPID=$!
    local HELD=0
    for _ in $(seq 1 600); do
        kill -0 "$QPID" 2>/dev/null || break
        if tr -d '\r' < "$LOG" | grep -aqF "$HOLDMSG"; then HELD=1; break; fi
        sleep 0.1
    done
    tr -d '\r' < "$LOG" > $OUT/$tag-uart.txt
    local T=$OUT/$tag-uart.txt
    local BASE STRIDE BAND GBASE GROWS FDT_AT FDT_SZ
    BASE=$(grep -a "Watari EL2: framebuffer at " $T | head -1 | sed 's/.*at //')
    STRIDE=$(grep -a "  stride = " $T | head -1 | sed 's/.*= \([0-9]*\).*/\1/')
    BAND=$(grep -a "  band rows = " $T | head -1 | sed 's/.*= \([0-9]*\).*/\1/')
    GBASE=$(grep -a "  guest part at " $T | head -1 | sed 's/.*at //')
    GROWS=$(grep -a "  guest rows = " $T | head -1 | sed 's/.*= \([0-9]*\).*/\1/')
    FDT_AT=$(grep -a "guest FDT at " $T | head -1 | sed 's/.*guest FDT at //')
    FDT_SZ=$(grep -a "  built, " $T | head -1 | sed 's/.*built, \([0-9]*\) bytes.*/\1/')
    grep -aE "framebuffer at|bench panel|wel2.fb =|band rows|guest part|guest rows|stage-2 tables|WARNING|FATAL|UNEXPECTED|halted" $T | head -12
    if [ "$HELD" = 1 ] && [ -n "$BASE" ]; then
        python3 -I - "$SOCK" "$BASE" "$((STRIDE * H))" "$DUMP" "${FDT_AT:-}" "${FDT_SZ:-0}" "$DTB" <<'EOF'
import socket, sys, time
sock, base, size, out, fdt_at, fdt_sz, dtb = sys.argv[1:8]
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
s.sendall(("pmemsave %d %s %s\n" % (int(base, 16), size, out)).encode())
time.sleep(1.5)
rd()
if fdt_at and int(fdt_sz) > 0:
    s.sendall(("pmemsave %d %s %s\n" % (int(fdt_at, 16), fdt_sz, dtb)).encode())
    time.sleep(0.5)
    rd()
s.sendall(b"quit\n")
time.sleep(0.3)
EOF
    fi
    kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null
    [ "$HELD" = 1 ] && echo "ok: the guest ran to its final SYSTEM_RESET and the layer held" || { echo "FAIL: no HOLD message"; rc=1; }
    grep -v "not 2 MB aligned" $T | grep -q "UNEXPECTED\|FATAL\|unhandled guest\|non-PSCI SMC\|WARNING" && { echo "FAIL: unexpected event or warning reported"; rc=1; }
    grep -aq "wel2.fb = $mode" $T || { echo "FAIL: the layer did not report wel2.fb = $mode (plan refused?)"; rc=1; }
    local g
    if [ "$abi" = info ]; then
        g=$(grep -aE "(^|[: ])G12 (PASS|FAIL|SKIP) " $T | head -1)
        echo "$g" | grep -q " PASS " || { echo "FAIL: G12 is not PASS: ${g:-missing}"; rc=1; }
        [ -n "$g" ] && echo "ok: $g"
        for c in G2 G3 G4 G5a G5b G6 G7 G9 G10 G11; do
            grep -aE "(^|[: ])$c (PASS|SKIP) " $T >/dev/null || { echo "FAIL: $c did not pass"; rc=1; }
        done
        grep -aE "Watari EL1: SUMMARY G1-G12" $T >/dev/null || { echo "FAIL: no G1-G12 SUMMARY header"; rc=1; }
    else
        g=$(grep -aE "(^|[: ])G12 (PASS|FAIL|SKIP) " $T | head -1)
        echo "$g" | grep -q " SKIP " || { echo "FAIL: G12 should skip without an info block: ${g:-missing}"; rc=1; }
    fi
    if [ "$abi" = fdt ]; then
        :   # no info block, so no bars: the panel was handed over, the FDT below is what is checked
    elif [ -s "$DUMP" ]; then
        python3 -I - "$DUMP" "$W" "$H" "$STRIDE" "$fake" "$mode" "${BAND:-0}" "${GROWS:-0}" <<'EOF' || rc=1
import struct, sys
dump, W, H, stride, fake, mode, band, grows = sys.argv[1:9]
W, H, stride, band, grows = int(W), int(H), int(stride), int(band), int(grows)
fmt10 = fake == "fakefb"
bars = ([0x3ff00000, 0x000ffc00, 0x000003ff, 0x3ffffc00] if fmt10 else [0xff0000, 0x00ff00, 0x0000ff, 0xffff00])
names = ["red", "green", "blue", "yellow"]
d = open(dump, "rb").read()
if len(d) != stride * H:
    print("FAIL: dump is %d bytes, expected %d" % (len(d), stride * H)); sys.exit(1)
first = band if mode == "band" else 0          # first guest row
if mode == "band" and band + grows != H:
    print("FAIL: band %d + guest rows %d != height %d" % (band, grows, H)); sys.exit(1)
px = lambda x, y: struct.unpack_from("<I", d, y * stride + 4 * x)[0]
bad = 0
for y in range(first, H):
    row = struct.unpack_from("<%dI" % W, d, y * stride)
    for x in range(W):
        want = bars[x * 4 // W]
        if row[x] != want:
            bad += 1
            if bad <= 3:
                print("  pixel (%d,%d) = 0x%08x, expected 0x%08x (%s)" % (x, y, row[x], want, names[x * 4 // W]))
total = (H - first) * W
if bad:
    print("FAIL: %d of %d guest-part pixels are not the bar colour (%s)" % (bad, total, "format x2r10g10b10" if fmt10 else "format x8r8g8b8")); sys.exit(1)
print("ok: all %d guest-part pixels (rows %d..%d) are the four bars, %s: %s" % (
    total, first, H - 1, "x2r10g10b10" if fmt10 else "x8r8g8b8",
    " ".join("%s=0x%08x" % (n, v) for n, v in zip(names, bars))))
if mode == "band":
    inband = 0; nonblack = 0
    for y in range(band):
        row = struct.unpack_from("<%dI" % W, d, y * stride)
        for v in row:
            if v in bars: inband += 1
            if v: nonblack += 1
    if inband:
        print("FAIL: %d band pixels have a bar value (the guest wrote into the band)" % inband); sys.exit(1)
    scale = max(1, min(6, W // 400)); chh = 9 * scale
    text_y = 32 + 3 * chh + 2 * chh + 4
    tick = sum(1 for y in range(text_y, text_y + chh) for v in struct.unpack_from("<%dI" % W, d, y * stride) if v)
    if not nonblack or not tick:
        print("FAIL: the band is empty or the ticker line (row %d) shows nothing" % text_y); sys.exit(1)
    print("ok: band rows 0..%d hold no bar pixel, %d non-black pixels, ticker line row %d has %d lit pixels" % (band - 1, nonblack, text_y, tick))
EOF
    else
        echo "FAIL: no dump of the fake panel (monitor pmemsave)"; rc=1
    fi
    if [ "$abi" = fdt ]; then
        if [ -s "$DTB" ]; then
            python3 -I - "$DTB" "$BASE" "$GBASE" "$W" "$GROWS" "$STRIDE" "$mode" "$fake" <<'EOF' || rc=1
import struct, sys
dtb, base, gbase, W, grows, stride, mode, fake = sys.argv[1:9]
base, gbase, W, grows, stride = int(base, 16), int(gbase, 16), int(W), int(grows), int(stride)
b = open(dtb, "rb").read()
magic, total, off_struct, off_strs = struct.unpack_from(">IIII", b, 0)
assert magic == 0xd00dfeed, "no FDT magic"
off = off_struct; path = []; props = {}
nodes = {}
def cstr(o):
    e = b.index(b"\0", o); return b[o:e].decode(), e + 1
while True:
    tok = struct.unpack_from(">I", b, off)[0]; off += 4
    if tok == 1:
        n, e = cstr(off); off = (e + 3) & ~3; path.append(n); nodes["/".join(path)] = {}
    elif tok == 2:
        path.pop()
    elif tok == 3:
        ln, nameoff = struct.unpack_from(">II", b, off); off += 8
        nm, _ = cstr(off_strs + nameoff); nodes["/".join(path)][nm] = b[off:off + ln]; off = (off + ln + 3) & ~3
    elif tok == 9:
        break
fb = [(k, v) for k, v in nodes.items() if k.split("/")[-1].startswith("framebuffer@")]
bad = 0
if len(fb) != 1:
    print("FAIL: %d framebuffer nodes in the guest FDT" % len(fb)); sys.exit(1)
k, v = fb[0]
reg = struct.unpack(">4I", v["reg"]); rbase = (reg[0] << 32) | reg[1]; rsize = (reg[2] << 32) | reg[3]
want = {"compatible": b"simple-framebuffer\0", "format": (b"x2r10g10b10\0" if fake == "fakefb" else b"x8r8g8b8\0"),
        "width": struct.pack(">I", W), "height": struct.pack(">I", grows), "stride": struct.pack(">I", stride)}
for p, w in want.items():
    if v.get(p) != w:
        print("FAIL: framebuffer %s = %r, expected %r" % (p, v.get(p), w)); bad = 1
if rbase != gbase or rsize != grows * stride:
    print("FAIL: framebuffer reg = 0x%x/0x%x, expected 0x%x/0x%x" % (rbase, rsize, gbase, grows * stride)); bad = 1
w = [v for kk, v in nodes.items() if kk.endswith("watari,el2")][0]
if w.get("fb-mode") != (mode + "\0").encode():
    print("FAIL: fb-mode = %r" % w.get("fb-mode")); bad = 1
for kk, v in nodes.items():
    if v.get("device_type") == b"memory\0":
        r = v["reg"]; n = len(r) // 16
        for i in range(n):
            a, s = struct.unpack_from(">QQ", r, 16 * i)
            if a < base + (grows * stride) and a + s > base:
                print("FAIL: /memory range 0x%x-0x%x overlaps the fake panel at 0x%x" % (a, a + s, base)); bad = 1
if bad: sys.exit(1)
print("ok: guest FDT: %s reg=0x%x/0x%x %dx%d stride %d, fb-mode %s, /memory clear of the panel" % (k.split("/")[-1], rbase, rsize, W, grows, stride, mode))
EOF
        else
            echo "FAIL: could not read the guest FDT back"; rc=1
        fi
    fi
    [ $rc = 0 ] && echo "PASS: $name" || { echo "FAILED: $name"; total_rc=1; }
}

run_case band-x2r10 fakefb band info
run_case band-x8r8  fakefb8 band info
run_case guest-x2r10 fakefb guest info
run_case guest-x8r8 fakefb8 guest info
run_case fdt-band fakefb band fdt
[ $total_rc = 0 ] && echo "PASS: framebuffer hand-over (E9), $SIZE, CPUIF=$CPUIF, ARGS=${WEL2_ARGS:-}"
exit $total_rc
