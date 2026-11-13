#!/bin/bash
# SPDX-License-Identifier: MIT
# M1-9 (T12c, PLAN E11 + E12): the M1 platform firmware (production build, hypercall console) on the layer's guest FDT with a
# framebuffer: WatariSimpleFbDxe makes a GOP from the simple-framebuffer node, the boot manager finds nothing to boot and starts
# the built-in self-test (four colour bars, ten timer ticks, SELFTEST PASS, ResetSystem -> PSCI SYSTEM_RESET through the layer).
# QEMU TCG, -smp 4, -m 2G, layer arguments  wel2.guest=<entry> wel2.abi=fdt wel2.gfdt=build wel2.split=3  +  the case's:
#   band-x2r10   wel2.fakefb=WxH  (x2r10g10b10) wel2.fb=band    GOP = the guest part (rows >= the band), PixelBitMask
#   band-x8r8    wel2.fakefb8=WxH (x8r8g8b8)    wel2.fb=band    PixelBlueGreenRedReserved8BitPerColor
#   guest-x2r10  wel2.fakefb=WxH  wel2.fb=guest                  GOP = the whole panel
#   guest-x8r8   wel2.fakefb8=WxH wel2.fb=guest
#   badfmt       wel2.fakefb=WxH wel2.fb=band wel2.test=badfbfmt  the node's format is "r5g6b5": no GOP, the firmware still boots
#   nofb         no fake panel, wel2.fb=layer                     no node: no GOP, the firmware still boots
# Two phases per case:
#   A (wel2.hold=1: the layer holds at the final SYSTEM_RESET instead of resetting): the log is checked (GOP line, ten SELFTEST
#     ticks, SELFTEST PASS, the layer's PSCI SYSTEM_RESET line, no ASSERT/exception) and the fake panel is dumped with the QEMU
#     monitor (pmemsave) and checked from outside, pixel by pixel, in Python: every pixel of the bar rows (the GOP's rows without
#     the bottom 64-row status strip) is the bar colour of its quarter of the width, in the panel's format; in band mode no pixel
#     of the band rows has a bar value (white is not tested for x8r8g8b8: it is the layer's own text colour) and the ticker line
#     shows text; with no GOP the guest part of the panel is still all zero.
#   B (no hold, -no-reboot): the run ends by itself: QEMU exits after the layer's PSCI SYSTEM_RESET line. (EXITCHK=0 skips B.)
# 10-bit bar values: FrameBufferBltLib shifts the WHOLE 32-bit pixel (B | G<<8 | R<<16) once per channel and then masks, so an 8-bit
# value lands in the TOP of its 10-bit field (0xFF -> 0x3FC) and the two low bits of a field are the top two bits of the channel
# below it: pure red 0x3FC00000, pure green 0x003FF000 (red field 3), pure blue 0x00000FFC (green field 3), white 0x3FFFFFFC (blue
# field 0x3FC). Two LSBs of 1023, invisible [verified: FrameBufferBltLib.c formula and run output; DESIGN 8.1 expected the clean
# 0x3FC00000/0x000FF000/0x000003FC].
# Env:
#   CASES="..."           which cases (default: all six)
#   CPUIF=hw|trap|trap-notc   virtual CPU interface mode of the layer (default hw)
#   SIZE=WxH              fake panel (default 1280x800: 4 096 000 bytes = 250 pages of 16 KB)
#   SPLIT=<n>             wel2.split (default 3)
#   EXITCHK=1|0           run phase B too (default 1)
#   WEL2_ARGS="..."       extra layer boot arguments (e.g. "wel2.e2h=0 wel2.gran=4k")
#   FD=<file>             firmware image (default: the production build on the data disk)
#   BGR8=1                FD is the --bgr8 build: the 10-bit GOP reports 8-bit BGR, bars expected as 0xFF0000 etc.
# Prerequisite: the data disk and tools/build-edk2-m1.sh (production build, once). Nothing is downloaded or installed.
# Exit 0 = every case passes.
set -u
cd "$(dirname "$0")"
CPUIF="${CPUIF:-hw}"
SIZE="${SIZE:-1280x800}"
SPLIT="${SPLIT:-3}"
EXITCHK="${EXITCHK:-1}"
CASES="${CASES:-band-x2r10 band-x8r8 guest-x2r10 guest-x8r8 badfmt nofb}"
ENTRY=0x40280000
W=${SIZE%x*}; H=${SIZE#*x}
case "$CPUIF" in hw|trap|trap-notc) ;; *) echo "FAIL: CPUIF must be hw, trap or trap-notc"; exit 2 ;; esac
# shellcheck source=/dev/null
source ../../tools/env.sh || { echo "FAIL: data disk not mounted"; exit 2; }
FD="${FD:-$WATARI_DATA/build-m1/WATARI_M1_EFI.fd}"
[ -f "$FD" ] || { echo "FAIL: $FD missing: run tools/build-edk2-m1.sh first"; exit 2; }
make -s chain || { echo "FAIL: layer build"; exit 2; }
OUT=build-chain
HOLDMSG="Watari EL2: HOLD: take the photo, then hold the power button"
total_rc=0

# qemu_start <tag> <args> <hold:0|1>   -> sets QPID, LOG, SOCK
qemu_start() {
    local tag="$1" args="$2" hold="$3"
    LOG=$OUT/$tag-uart.log; SOCK=$OUT/$tag-mon.sock
    rm -f "$SOCK"; : > "$LOG"
    local mon=(-monitor none)
    [ "$hold" = 1 ] && mon=(-monitor "unix:$SOCK,server,nowait")
    "${QEMU:-qemu-system-aarch64}" -M virt,virtualization=on,gic-version=3 -cpu "${QEMU_CPU:-max}" -accel tcg \
        -nographic -m 2G -smp 4 -nic none -no-reboot -kernel $OUT/wel2.bin -append "$args" \
        -device loader,file="$FD",addr="$ENTRY",force-raw=on \
        -serial "file:$LOG" "${mon[@]}" >$OUT/$tag-qemu.out 2>&1 &
    QPID=$!
}

run_case() {
    local name="$1" rc=0
    local fake="" mode="band" extra="" wantgop=1 fmt=10
    case "$name" in
        band-x2r10)  fake="wel2.fakefb=${W}x${H}";  mode=band ;;
        band-x8r8)   fake="wel2.fakefb8=${W}x${H}"; mode=band;  fmt=8 ;;
        guest-x2r10) fake="wel2.fakefb=${W}x${H}";  mode=guest ;;
        guest-x8r8)  fake="wel2.fakefb8=${W}x${H}"; mode=guest; fmt=8 ;;
        badfmt)      fake="wel2.fakefb=${W}x${H}";  mode=band;  extra="wel2.test=badfbfmt"; wantgop=0 ;;
        nofb)        fake="";                       mode=layer; wantgop=0 ;;
        *) echo "FAIL: unknown case $name"; total_rc=1; return ;;
    esac
    local ARGS="wel2.guest=$ENTRY wel2.abi=fdt wel2.gfdt=build wel2.cpuif=$CPUIF wel2.fb=$mode"
    [ "$SPLIT" != 0 ] && ARGS="$ARGS wel2.split=$SPLIT"
    [ -n "$fake" ] && ARGS="$ARGS $fake"
    [ -n "$extra" ] && ARGS="$ARGS $extra"
    [ -n "${WEL2_ARGS:-}" ] && ARGS="$ARGS $WEL2_ARGS"
    echo "---- $name (phase A, hold): $ARGS"
    local tag="m1-9-$name"
    qemu_start "$tag" "$ARGS wel2.hold=1" 1
    local HELD=0
    for _ in $(seq 1 1500); do
        kill -0 "$QPID" 2>/dev/null || break
        if tr -d '\r' < "$LOG" | grep -aqF "$HOLDMSG"; then HELD=1; break; fi
        sleep 0.1
    done
    local T=$OUT/$tag-uart.txt DUMP=$OUT/$tag.bin
    tr -d '\r' < "$LOG" > "$T"
    local BASE STRIDE BAND GROWS
    BASE=$(grep -a "Watari EL2: framebuffer at " "$T" | head -1 | sed 's/.*at //')
    STRIDE=$(grep -a "  stride = " "$T" | head -1 | sed 's/.*= \([0-9]*\).*/\1/')
    BAND=$(grep -a "  band rows = " "$T" | head -1 | sed 's/.*= \([0-9]*\).*/\1/')
    GROWS=$(grep -a "  guest rows = " "$T" | head -1 | sed 's/.*= \([0-9]*\).*/\1/')
    rm -f "$DUMP"
    if [ "$HELD" = 1 ] && [ -n "$BASE" ]; then
        python3 -I - "$SOCK" "$BASE" "$((STRIDE * H))" "$DUMP" <<'EOF'
import socket, sys, time
sock, base, size, out = sys.argv[1:5]
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
time.sleep(2.0)
rd()
s.sendall(b"quit\n")
time.sleep(0.3)
EOF
    fi
    kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null

    # --- the log ---------------------------------------------------------------------------------------------------
    grep -aE "WatariSimpleFb|WatariM1Smbios|SELFTEST|Booting Watari|PSCI SYSTEM_RESET|GOP handle" "$T" | head -30
    [ "$HELD" = 1 ] && echo "ok: the firmware ran to the layer's final SYSTEM_RESET (held)" || { echo "FAIL: no HOLD message (timeout or the layer died)"; rc=1; }
    chk() { grep -aqF -- "$1" "$T" || { echo "MISSING: $1"; rc=1; }; }
    chk "UEFI firmware (version "
    chk "[Bds]Booting Watari M1 self-test"
    chk "SELFTEST start"
    chk "SELFTEST tick 10/10"
    chk "SELFTEST PASS"
    chk "Watari EL2: PSCI SYSTEM_RESET"
    chk "WatariM1Smbios: types 0, 1, 2, 3 added"
    local nt; nt=$(grep -ac "SELFTEST tick " "$T")
    [ "$nt" = 10 ] && echo "ok: ten SELFTEST tick lines" || { echo "FAIL: $nt SELFTEST tick lines, expected 10"; rc=1; }
    if [ "$wantgop" = 1 ]; then
        chk "WatariSimpleFb: GOP "
        chk "SELFTEST bars drawn"
        if [ "$fmt" = 10 ] && [ "${BGR8:-0}" != 1 ]; then chk "node format x2r10g10b10, reported as PixelBitMask R3FF00000 G000FFC00 B000003FF"
        elif [ "$fmt" = 10 ]; then chk "node format x2r10g10b10, reported as PixelBlueGreenRedReserved8BitPerColor"
        else chk "node format x8r8g8b8, reported as PixelBlueGreenRedReserved8BitPerColor"; fi
    else
        chk "SELFTEST GOP none"
        grep -aq "WatariSimpleFb: GOP " "$T" && { echo "FAIL: a GOP was installed"; rc=1; }
        [ "$name" = badfmt ] && chk "WatariSimpleFb: unknown format 'r5g6b5', no GOP"
        [ "$name" = nofb ] && chk "WatariSimpleFb: no simple-framebuffer node, no GOP"
    fi
    grep -aq "UNEXPECTED\|FATAL\|unhandled guest\|non-PSCI SMC" "$T" && { echo "FAIL: the layer reported an unexpected event"; rc=1; }
    grep -aqE "ASSERT [/A-Za-z(]|ASSERT_EFI_ERROR|Synchronous Exception|SELFTEST FAIL" "$T" && { echo "FAIL: ASSERT/exception/self-test failure in the log"; rc=1; }

    # --- the panel -------------------------------------------------------------------------------------------------
    if [ "$wantgop" = 1 ]; then
        if [ -s "$DUMP" ]; then
            python3 -I - "$DUMP" "$W" "$H" "$STRIDE" "$fmt" "$mode" "${BAND:-0}" "${GROWS:-0}" "${BGR8:-0}" <<'EOF' || rc=1
import struct, sys
dump, W, H, stride, fmt, mode, band, grows, bgr8 = sys.argv[1:10]
W, H, stride, band, grows = int(W), int(H), int(stride), int(band), int(grows)
if fmt == "10" and bgr8 != "1":
    bars = [0x3fc00000, 0x003ff000, 0x00000ffc, 0x3ffffffc]
    label = "x2r10g10b10, GOP PixelBitMask"
else:
    bars = [0x00ff0000, 0x0000ff00, 0x000000ff, 0x00ffffff]
    label = ("x2r10g10b10 panel, GOP reports 8-bit BGR (untruthful)" if fmt == "10" else "x8r8g8b8")
names = ["red", "green", "blue", "white"]
d = open(dump, "rb").read()
if len(d) != stride * H:
    print("FAIL: dump is %d bytes, expected %d" % (len(d), stride * H)); sys.exit(1)
first = band if mode == "band" else 0
gop_rows = H - first
bar_rows = gop_rows - 64 if gop_rows > 128 else gop_rows
bad = 0
for y in range(first, first + bar_rows):
    row = struct.unpack_from("<%dI" % W, d, y * stride)
    for x in range(W):
        want = bars[x * 4 // W]
        if row[x] != want:
            bad += 1
            if bad <= 3:
                print("  pixel (%d,%d) = 0x%08x, expected 0x%08x (%s)" % (x, y, row[x], want, names[x * 4 // W]))
total = bar_rows * W
if bad:
    print("FAIL: %d of %d bar-row pixels are not the bar colour (%s)" % (bad, total, label)); sys.exit(1)
print("ok: all %d pixels of rows %d..%d are the four bars, %s: %s" % (total, first, first + bar_rows - 1, label,
      " ".join("%s=0x%08x" % (n, v) for n, v in zip(names, bars))))
# the status strip below the bars: ten boxes of 40 x 40 at x = 16 + 56 i, y = gop_rows - 64 + 12, white once the tick was seen
lit = 0
for i in range(10):
    y = first + gop_rows - 64 + 12 + 20
    x = 16 + 56 * i + 20
    v = struct.unpack_from("<I", d, y * stride + 4 * x)[0]
    if v == bars[3]: lit += 1
if gop_rows > 128 and lit != 10:
    print("FAIL: %d of the 10 status boxes are white (every tick must have lit its box)" % lit); sys.exit(1)
print("ok: status strip: %d of 10 boxes lit (ten timer ticks seen by the firmware)" % lit)
if mode == "band":
    test = [b for n, b in zip(names, bars) if not (n == "white" and fmt == "8")]
    inband = 0; nonblack = 0
    for y in range(band):
        row = struct.unpack_from("<%dI" % W, d, y * stride)
        for v in row:
            if v in test: inband += 1
            if v: nonblack += 1
    if inband:
        print("FAIL: %d band pixels have a bar value (the guest wrote into the band)" % inband); sys.exit(1)
    if not nonblack:
        print("FAIL: the band is empty"); sys.exit(1)
    print("ok: band rows 0..%d hold no bar pixel, %d non-black pixels (the layer's own drawing)" % (band - 1, nonblack))
EOF
        else
            echo "FAIL: no dump of the fake panel (monitor pmemsave)"; rc=1
        fi
    elif [ -s "$DUMP" ] && [ "$mode" = band ]; then
        # no GOP: nobody wrote the guest part
        python3 -I - "$DUMP" "$STRIDE" "${BAND:-0}" "$H" <<'EOF' || rc=1
import sys
dump, stride, band, H = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
d = open(dump, "rb").read()
g = d[band * stride:H * stride]
nz = sum(1 for b in g if b)
if nz:
    print("FAIL: no GOP, but %d non-zero bytes in the guest part of the panel" % nz); sys.exit(1)
print("ok: no GOP: the guest part of the panel (rows %d..%d) is still all zero" % (band, H - 1))
EOF
    fi

    # --- phase B: the run ends by itself ---------------------------------------------------------------------------
    if [ "$EXITCHK" = 1 ]; then
        echo "---- $name (phase B, no hold: QEMU must exit by itself)"
        qemu_start "$tag-b" "$ARGS" 0
        local EXITED=0
        for _ in $(seq 1 1500); do
            kill -0 "$QPID" 2>/dev/null || { EXITED=1; break; }
            sleep 0.1
        done
        kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null
        local TB=$OUT/$tag-b-uart.txt
        tr -d '\r' < "$OUT/$tag-b-uart.log" > "$TB"
        if [ "$EXITED" = 1 ] && grep -aqF "SELFTEST PASS" "$TB" && grep -aqF "Watari EL2: PSCI SYSTEM_RESET" "$TB"; then
            echo "ok: SELFTEST PASS, then the layer's PSCI SYSTEM_RESET line, then QEMU exited by itself (-no-reboot)"
        else
            echo "FAIL: the run did not end by itself with SELFTEST PASS and a PSCI SYSTEM_RESET (exited=$EXITED)"; rc=1
        fi
    fi
    [ $rc = 0 ] && echo "PASS: $name" || { echo "FAILED: $name"; total_rc=1; }
}

for c in $CASES; do run_case "$c"; done
[ $total_rc = 0 ] && echo "PASS: M1 platform firmware, GOP + boot manager + self-test (M1-9, E11/E12): $CASES; $SIZE, CPUIF=$CPUIF, ARGS=${WEL2_ARGS:-}"
exit $total_rc
