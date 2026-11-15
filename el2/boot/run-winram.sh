#!/bin/bash
# SPDX-License-Identifier: MIT
# T22 (docs/design/watari-winram, PLAN T22): Windows PE / Setup from a RAM disk on the M1-SHAPED bench. QEMU TCG with no virtio and no
# PCI device (-nic none, no -device but loaders), the layer's M1-shaped guest FDT (wel2.abi=fdt wel2.gfdt=build), the layer's fake
# panel as the framebuffer (wel2.fakefb=WxH, 10-bit, or wel2.fakefb8=, 8-bit) and the PRODUCTION firmware of tools/build-edk2-m1.sh
# (ACPI model j293, hypercall console, WatariSimpleFbDxe GOP, WatariM1RamDiskDxe). The disk image of tools/m3a/build-winram-image.sh
# plus its 4 KiB trailer (tools/m3a/winram_trailer.py) is loaded into guest RAM at 0x100000000 with -device loader (what
# watari-load.py --ramdisk will do on hardware), the layer is told wel2.ramdisk=<base>,<size>.
# Not a pass/fail test: it runs until TIMEOUT seconds or until QEMU exits (a bugcheck / reset ends it: -no-reboot), saves the panel
# every SHOT_EVERY seconds (monitor pmemsave -> tools/m3a/fb-png.py -> PNG) and prints the layer log's progress lines.
# Env:
#   IMG=<file>       disk image (default $WATARI_DATA/winram/winram.img; the .sha256 sidecar must match)
#   FD=<file>        firmware (default $WATARI_DATA/build-m1/WATARI_M1_EFI-bgr8-$WATARI_M1_BUILD_TAG.fd, i.e. --bgr8 production)
#   PANEL=10|8       fake panel format: 10 = x2r10g10b10 (the M1's, wel2.fakefb), 8 = x8r8g8b8 (wel2.fakefb8)   default 10
#   SIZE=WxH         fake panel size (default 1280x800)
#   FB=band|guest    wel2.fb mode (default band: the layer keeps the top rows for its own log, like the hardware)
#   RAM_BASE=0x800000000  (T16k) guest RAM base, a multiple of 1 GiB: runs the patched QEMU (QEMU default
#                    /private/tmp/watari-qemu/build/qemu-system-aarch64, option ram-base=) so all RAM sits above 4 GiB like the M1's.
#                    Then GUEST (EDK2 load address) defaults to RAM_BASE+0x280000 and RDBASE to RAM_BASE+0x42000000 (the loader's
#                    0x842000000); the layer is still loaded by -kernel (RAM_BASE+0x80000; the loader puts it at 0x870000000).
#                    Override with GUEST=0x874000000 RDBASE=0x842000000 for the hardware plan. Unset = unchanged (RAM at 0x40000000).
#   RDPAD=1        pad the RAM-disk range to a 1 MiB multiple, the trailer in its last 4 KiB, like watari-load.py (the hardware
#                    size is 0x2ca00000 for this image). Default on with RAM_BASE, else off (T16k: EDK2 claims the range in 64 KiB
#                    pages; an unpadded size makes the claim cross into a neighbouring memory-map entry, F-38).
#   SMP=8 MEM=4G     cores / guest RAM (the host has 8 GB: do not raise MEM; the image is 0.7 GB above 4 GB)
#   CPUIF=hw|trap|trap-notc   virtual CPU interface (default trap, as the other TCG runs)
#   TIMEOUT=3600 SHOT_EVERY=120   seconds;  STALL=N stop when the panel is unchanged for N shots (default 0 = never)
#   QEMU_EXTRA="-no-shutdown"  keep QEMU alive (paused) after the guest's SYSTEM_RESET: the panel and the registers can still be read
#   WEL2_ARGS="..."  extra layer arguments        TAG=name  run directory name
# Output (data disk, never the repository): $WATARI_DATA/logs/winram/<TAG>/ serial.log, shot-NNNN.png, pc.log (PC of every vCPU per shot), qemu.out
# Nothing is downloaded or installed; the Windows VM disk is not touched; QEMU is waited on by its own PID only.
set -u
cd "$(dirname "$0")"
# shellcheck source=/dev/null
source ../../tools/env.sh || { echo "FAIL: data disk not mounted"; exit 2; }
TAGSUF=""; [ -n "${WATARI_M1_BUILD_TAG:-}" ] && TAGSUF="-$WATARI_M1_BUILD_TAG"
FD="${FD:-$WATARI_DATA/build-m1/WATARI_M1_EFI-bgr8${TAGSUF}.fd}"
IMG="${IMG:-$WATARI_DATA/winram/winram.img}"
PANEL="${PANEL:-10}"; SIZE="${SIZE:-1280x800}"; FB="${FB:-band}"
SMP="${SMP:-8}"; MEM="${MEM:-4G}"; CPUIF="${CPUIF:-trap}"
TIMEOUT="${TIMEOUT:-3600}"; SHOT_EVERY="${SHOT_EVERY:-120}"
W=${SIZE%x*}; H=${SIZE#*x}
RAM_BASE="${RAM_BASE:-}"; MACHINE_EXTRA=""
if [ -n "$RAM_BASE" ]; then
    GUEST="${GUEST:-$(printf '0x%x' $((RAM_BASE + 0x280000)))}"
    RDBASE="${RDBASE:-$(printf '0x%x' $((RAM_BASE + 0x42000000)))}"
    MACHINE_EXTRA=",ram-base=$RAM_BASE"
    QEMU="${QEMU:-/private/tmp/watari-qemu/build/qemu-system-aarch64}"
    [ -x "$QEMU" ] || { echo "FAIL: no patched QEMU at $QEMU"; exit 2; }
else
    GUEST="${GUEST:-0x40280000}"
fi
RDBASE="${RDBASE:-0x100000000}"   # RDBASE=0x80000000 with MEM=3G keeps guest RAM below the layer's MSI frame (0xFFFFF000), F-35
[ -f "$FD" ] || { echo "FAIL: $FD missing: run tools/build-edk2-m1.sh --bgr8 (WATARI_M1_BUILD_TAG=${WATARI_M1_BUILD_TAG:-})"; exit 2; }
[ -f "$IMG" ] || { echo "FAIL: $IMG missing: tools/m3a/build-winram-image.sh"; exit 2; }
case "$PANEL" in
    10) FAKE="wel2.fakefb=${W}x${H}"; PFMT=x2r10g10b10 ;;
    8) FAKE="wel2.fakefb8=${W}x${H}"; PFMT=x8r8g8b8 ;;
    *) echo "FAIL: PANEL must be 10 or 8"; exit 2 ;;
esac
# the sidecar must match (the loader will refuse otherwise)
if [ -f "$IMG.sha256" ]; then
    want=$(cut -d' ' -f1 "$IMG.sha256"); got=$(shasum -a 256 "$IMG" | cut -d' ' -f1)
    [ "$want" = "$got" ] || { echo "FAIL: $IMG does not match its sidecar"; exit 2; }
    echo "image sha256 ok: $got"
fi
make -s chain || { echo "FAIL: layer build"; exit 2; }
TAG="${TAG:-$(date +%m%d-%H%M)}"
OUT="$WATARI_DATA/logs/winram/$TAG"; mkdir -p "$OUT"
RD="$WATARI_DATA/winram/$(basename "$IMG").rd"       # image + trailer, cached; rebuilt when the image is newer
if [ ! -f "$RD" ] || [ "$IMG" -nt "$RD" ]; then
    python3 -I ../../tools/m3a/winram_trailer.py append "$IMG" "$RD" || { echo "FAIL: trailer"; exit 2; }
fi
if [ "${RDPAD:-${RAM_BASE:+1}}" = 1 ]; then
    RDP="$RD.pad1m"
    if [ ! -f "$RDP" ] || [ "$RD" -nt "$RDP" ]; then
        python3 -I - "$IMG" "$RD" "$RDP" <<'PYEOF' || { echo "FAIL: padding"; exit 2; }
import sys
img = sys.argv[1]; rd = open(sys.argv[2], "rb").read()
n = (len(rd) + (1 << 20) - 1) & ~((1 << 20) - 1)
open(sys.argv[3], "wb").write(rd[:-4096] + b"\0" * (n - len(rd)) + rd[-4096:])
PYEOF
    fi
    RD="$RDP"
fi
RDSIZE=$(printf '0x%x' "$(stat -f %z "$RD")")
SOCK="$(mktemp -d /tmp/wwr.XXXXXX)/mon.sock"
ARGS="wel2.guest=$GUEST wel2.abi=fdt wel2.gfdt=build wel2.cpuif=$CPUIF wel2.fb=$FB $FAKE wel2.ramdisk=$RDBASE,$RDSIZE ${WEL2_ARGS:-}"
echo "layer args: $ARGS"; echo "smp=$SMP mem=$MEM fd=$FD out=$OUT"
LOG="$OUT/serial.log"; : > "$LOG"
"${QEMU:-qemu-system-aarch64}" -M virt,virtualization=on,gic-version=3$MACHINE_EXTRA -cpu "${QEMU_CPU:-max}" -accel tcg,thread=multi \
    -nographic -m "$MEM" -smp "$SMP" -nic none -no-reboot ${QEMU_EXTRA:-} -kernel build-chain/wel2.bin -append "$ARGS" \
    -device loader,file="$FD",addr=$GUEST,force-raw=on \
    -device loader,file="$RD",addr=$RDBASE,force-raw=on \
    -serial "file:$LOG" -monitor "unix:$SOCK,server,nowait" >"$OUT/qemu.out" 2>&1 &
QPID=$!
echo "QEMU pid $QPID"

shot() {   # shot <n>: pmemsave of the panel, then the PNG
    local n BASE STRIDE
    n=$(printf '%04d' "$1")
    BASE=$(tr -d '\r' < "$LOG" | grep -a "Watari EL2: framebuffer at " | head -1 | sed 's/.*at //')
    STRIDE=$(tr -d '\r' < "$LOG" | grep -a "  stride = " | head -1 | sed 's/.*= \([0-9]*\).*/\1/')
    [ -n "$BASE" ] && [ -n "$STRIDE" ] || { echo "shot $n: no framebuffer line in the log yet"; return; }
    rm -f build-chain/winram-last.raw
    python3 -I - "$SOCK" "$BASE" "$((STRIDE * H))" build-chain/winram-last.raw <<'PYEOF'
import socket, sys, time
sock, base, size, out = sys.argv[1:5]
s = socket.socket(socket.AF_UNIX)
try:
    s.connect(sock)
except OSError:
    sys.exit(0)
s.settimeout(2)
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
time.sleep(3.0)
rd()
s.sendall(b"info registers -a\n")
time.sleep(2.0)
import re
o = re.sub(rb"\x1b\[[0-9;]*[A-Za-z]", b"", rd()).decode(errors="replace")
with open(out + ".regs", "w") as f:
    for m in re.finditer(r"CPU#(\d+)\s+PC=(\w+).*?SP=(\w+)\s+PSTATE=(\w+) (\S+) (\S+)", o, re.S):
        f.write("cpu%s pc=%s sp=%s pstate=%s %s\n" % (m.group(1), m.group(2), m.group(3), m.group(4), m.group(6)))
PYEOF
    [ -f build-chain/winram-last.raw.regs ] && { printf '[%s s] ' "$(( $(date +%s) - T0 ))" >> "$OUT/pc.log"; tr '\n' ' ' < build-chain/winram-last.raw.regs >> "$OUT/pc.log"; echo >> "$OUT/pc.log"; }
    [ -s build-chain/winram-last.raw ] || return
    python3 -I ../../tools/m3a/fb-png.py build-chain/winram-last.raw "$OUT/shot-$n.png" "$W" "$H" "$STRIDE" "$PFMT"
    # a --bgr8 firmware on a 10-bit fake panel writes 8-bit pixels: a second PNG read that way
    [ "$PANEL" = 10 ] && python3 -I ../../tools/m3a/fb-png.py build-chain/winram-last.raw "$OUT/shot-$n-as8.png" "$W" "$H" "$STRIDE" x2r10-as8 >/dev/null
}

T0=$(date +%s); N=0; NEXT=0; SAME=0; HPREV=''
while kill -0 "$QPID" 2>/dev/null; do
    NOW=$(( $(date +%s) - T0 ))
    if [ "$NOW" -ge "$TIMEOUT" ]; then echo "TIMEOUT after ${NOW}s"; break; fi
    if [ "$NOW" -ge "$NEXT" ]; then
        N=$((N + 1)); NEXT=$((NOW + SHOT_EVERY))
        shot "$N"
        if [ -s build-chain/winram-last.raw ]; then
            H1=$(shasum build-chain/winram-last.raw | cut -c1-12)
            if [ "$H1" = "${HPREV:-}" ]; then SAME=$((SAME + 1)); else SAME=0; fi; HPREV=$H1
            if [ "${STALL:-0}" -gt 0 ] && [ "$SAME" -ge "${STALL:-0}" ]; then echo "STALL: panel unchanged for $SAME shots"; break; fi
        fi
        echo "[$NOW s] shot $N; log lines: $(wc -l < "$LOG"); last: $(tr -d '\r' < "$LOG" | tail -1 | cut -c1-100)"
    fi
    sleep 5
done
if kill -0 "$QPID" 2>/dev/null; then N=$((N + 1)); shot "$N"; kill "$QPID" 2>/dev/null; fi
wait "$QPID" 2>/dev/null
tr -d '\r' < "$LOG" > "$OUT/serial.txt"
echo "---- log highlights"
grep -aE "Watari EL2: (ramdisk|framebuffer|PSCI|guest)|WatariM1RamDisk|WatariSimpleFb|trying boot option|Boot|ASSERT|Exception|FATAL|UNEXPECTED" "$OUT/serial.txt" | head -60
echo "run directory: $OUT"
