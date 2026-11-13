#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Builds the M3a hardware-day bundle (PLAN S16, DESIGN 2.5 and 4.2). No network, no downloads, no installs: it uses
# the Homebrew LLVM and the dtc that are already on this Mac. Nothing is written into the repository: sources are
# compiled out of tree (OUT=...) into <dir>/work, the result goes to <dir>/bundle.
#
#   tools/m3a/build-m1-payload.sh [DIR]      DIR defaults to $WATARI_DATA/m3a-payload
#
# The bundle (everything the host Mac needs, nothing else):
#   wel2-m1.bin          layer, M1 build with the built-in EL1 payload   (rungs L1, L2)
#   wel2-chain.bin       layer, chain build, enters the guest at wel2.guest (rungs L3, L4, L5)
#   m1guest.bin          EL1 test guest G1-G11                           (rungs L3, L4, L5)
#   j293-minimal.dtb/.dts minimal device tree for m1n1's kboot_prepare_dt
#   watari-load.py       host loader (needs the m1n1 proxyclient of the host, never m1n1.setup)
#   ehw2.py              optional read-only inherited-state inventory (docs/procedures/m3a-ehw2-rung.md)
#   WATARI_M1_EFI.fd     EDK2 for the M1 (tools/build-edk2-m1.sh), copied from $WATARI_DATA/build-m1 (day 3: rungs D0, D2)
#   fdt_m1_dump          prebuilt arm64 host tool (no cc needed on the host)
#   gfdt_dump            prebuilt arm64 host tool: the guest FDT the layer would build from m1n1's prepared FDT (rung D0)
#   fdt-dump-src/        the sources of both + build.sh (used only if the host has the Command Line Tools)
# Not in the bundle yet: a WATARI_FB_REPORT_BGR8 EDK2 variant. The package has no such define (grep of edk2-apple and
# tools finds it only in the design documents), so it is E11 work; this script does not invent it.
# Environment: WATARI_M1_EFI=<file> picks another EDK2 image; WATARI_NO_EDK2=1 builds a day-2 bundle without one.
#   README-m1-payload.md the rung guide
#   SHA256SUMS           the loader refuses to run if these do not match
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
LLVM="${LLVM:-/opt/homebrew/opt/llvm/bin}"
BLOCK=$((32 * 1024 * 1024))

die() { echo "build-m1-payload: $*" >&2; exit 1; }

DIR="${1:-}"
if [ -z "$DIR" ]; then
    [ -n "${WATARI_DATA:-}" ] || die "no output directory: pass one, or 'source tools/env.sh' to set WATARI_DATA"
    DIR="$WATARI_DATA/m3a-payload"
fi
mkdir -p "$DIR"
DIR="$(cd "$DIR" && pwd)"
case "$DIR/" in "$REPO"/*) die "refusing to build inside the repository ($DIR): the bundle holds binaries" ;; esac

command -v dtc >/dev/null 2>&1 || die "dtc not found (it is installed with QEMU's dependencies: brew list dtc). Nothing is downloaded by this script."
[ -x "$LLVM/clang" ] || die "Homebrew LLVM not found at $LLVM (set LLVM=...)"
command -v ld.lld >/dev/null 2>&1 || export PATH="$LLVM:$PATH"
command -v ld.lld >/dev/null 2>&1 || die "ld.lld not found"
if command -v sha256sum >/dev/null 2>&1; then SHA() { sha256sum "$@"; }; else SHA() { shasum -a 256 "$@"; }; fi

OUT="$DIR/bundle"
# make cannot handle spaces in paths (the data disk is "Arthur Fonzarelli"): when DIR has one, the work tree goes
# to a space-free temp dir (removed on exit); only the finished bundle is written under DIR (the shell quotes it).
case "$DIR" in
    *[[:space:]]*)
        WORK="$(mktemp -d /tmp/watari-m3a-work.XXXXXX)"
        trap 'rm -rf "$WORK"' EXIT
        ;;
    *)
        WORK="$DIR/work"
        ;;
esac
rm -rf "$OUT"
mkdir -p "$WORK" "$OUT"

echo "== layer, M1 build (non-chain) =="
make -s -C "$REPO/el2/boot" OUT="$WORK/m1" "$WORK/m1/wel2.bin"
echo "== layer, chain build =="
make -s -C "$REPO/el2/boot" OUT="$WORK/chain" XFLAGS=-DWEL2_CHAIN=1 PAYLOAD_OBJS= "$WORK/chain/wel2.bin"
echo "== EL1 test guest =="
make -s -C "$REPO/el2/guest" OUT="$WORK/guest" "$WORK/guest/m1guest.bin"

cp "$WORK/m1/wel2.bin" "$OUT/wel2-m1.bin"
cp "$WORK/chain/wel2.bin" "$OUT/wel2-chain.bin"
cp "$WORK/guest/m1guest.bin" "$OUT/m1guest.bin"

EFI_NAME="WATARI_M1_EFI.fd"
if [ "${WATARI_NO_EDK2:-0}" != 1 ]; then
    EFI_SRC="${WATARI_M1_EFI:-${WATARI_DATA:-}/build-m1/$EFI_NAME}"
    [ -f "$EFI_SRC" ] || die "no EDK2 image at $EFI_SRC: run tools/build-edk2-m1.sh first, or set WATARI_M1_EFI=<file>, or WATARI_NO_EDK2=1 for a bundle without it"
    echo "== EDK2 image: $EFI_SRC =="
    cp "$EFI_SRC" "$OUT/$EFI_NAME"
fi

echo "== device tree =="
dtc -q -I dts -O dtb -o "$OUT/j293-minimal.dtb" "$HERE/j293-minimal.dts"
cp "$HERE/j293-minimal.dts" "$OUT/"
# round trip: the DTB must decompile
dtc -q -I dtb -O dts "$OUT/j293-minimal.dtb" >/dev/null || die "DTB round trip failed"

echo "== image header checks (arm64 Image: magic at 0x38, image_size at 16) =="
check_image() {
    python3 -I - "$1" "$BLOCK" <<'PYEOF'
import struct, sys
path, block = sys.argv[1], int(sys.argv[2])
d = open(path, "rb").read()
if len(d) < 64 or d[0x38:0x3c] != b"ARM\x64":
    sys.exit(f"{path}: no arm64 Image magic at 0x38")
text_off, image_size = struct.unpack_from("<QQ", d, 8)
if len(d) > block:
    sys.exit(f"{path}: file is {len(d)} bytes, larger than the 32 MB block")
if image_size > block:
    sys.exit(f"{path}: header image_size {image_size:#x} larger than the 32 MB block")
if image_size < len(d):
    sys.exit(f"{path}: header image_size {image_size:#x} smaller than the file ({len(d)})")
print(f"  {path.rsplit('/',1)[-1]}: {len(d)} bytes, text_offset {text_off:#x}, image_size {image_size:#x}")
PYEOF
}
check_image "$OUT/wel2-m1.bin"
check_image "$OUT/wel2-chain.bin"
check_image "$OUT/m1guest.bin"
[ -f "$OUT/$EFI_NAME" ] && check_image "$OUT/$EFI_NAME"

echo "== fdt_m1_dump: parse the DTB with the layer's own parser =="
SRC="$OUT/fdt-dump-src"
mkdir -p "$SRC"
cp "$REPO/tools/m3a/fdt_m1_dump.c" "$REPO/el2/boot/fdt.c" "$REPO/el2/boot/fdt.h" "$REPO/el2/boot/fdt_m1.c" \
   "$REPO/el2/boot/fdt_m1.h" "$REPO/el2/boot/boot.h" "$REPO/el2/vgic/fb.c" "$REPO/el2/vgic/font5x7.h" \
   "$REPO/el2/vgic/ram_map.c" "$SRC/"
# gfdt_dump (E14): the guest-FDT builder (E5) and the range arithmetic of the layer, compiled for the host
cp "$REPO/tools/m3a/gfdt_dump.c" "$REPO/el2/vgic/fdt_build.c" "$REPO/el2/vgic/maps.c" "$REPO/el2/vgic/pt.c" "$SRC/"
# every layer header (boot.h pulls several in; the list changes with the layer)
cp "$REPO"/el2/include/*.h "$SRC/"

cat > "$SRC/build.sh" <<'EOF'
#!/bin/sh
# SPDX-License-Identifier: MIT
# Builds fdt_m1_dump and gfdt_dump with the host's own cc (Command Line Tools are enough). Output: ./fdt_m1_dump ./gfdt_dump
set -e
cd "$(dirname "$0")"
cc -std=c11 -O1 -I. fdt_m1_dump.c fdt_m1.c fdt.c fb.c ram_map.c -o fdt_m1_dump
cc -std=c11 -O1 -I. gfdt_dump.c fdt_m1.c fdt.c fb.c ram_map.c fdt_build.c maps.c pt.c -o gfdt_dump
echo "built $(pwd)/fdt_m1_dump and $(pwd)/gfdt_dump"
EOF
chmod +x "$SRC/build.sh"
DUMP_SRCS="fdt_m1_dump.c fdt_m1.c fdt.c fb.c ram_map.c"
GFDT_SRCS="gfdt_dump.c fdt_m1.c fdt.c fb.c ram_map.c fdt_build.c maps.c pt.c"
(cd "$SRC" && cc -std=c11 -Wall -Wextra -O1 -I. $DUMP_SRCS -o "$WORK/fdt_m1_dump") \
    || die "fdt_m1_dump does not build from the bundle sources"
(cd "$SRC" && cc -std=c11 -Wall -Wextra -O1 -I. $GFDT_SRCS -o "$WORK/gfdt_dump") \
    || die "gfdt_dump does not build from the bundle sources"
(cd "$SRC" && cc -arch arm64 -std=c11 -O1 -I. $GFDT_SRCS -o "$OUT/gfdt_dump") \
    || die "prebuilt gfdt_dump (arm64) does not build"
chmod 755 "$OUT/gfdt_dump"
"$WORK/fdt_m1_dump" "$OUT/j293-minimal.dtb" > "$WORK/dtb-dump.txt"
# prebuilt for the host (arm64 macOS): the host never has to run cc (the cc shim can open the CLT installer)
(cd "$SRC" && cc -arch arm64 -std=c11 -O1 -I. $DUMP_SRCS -o "$OUT/fdt_m1_dump") \
    || die "prebuilt fdt_m1_dump (arm64) does not build"
chmod 755 "$OUT/fdt_m1_dump"
grep -q '^platform m1' "$WORK/dtb-dump.txt" || die "the DTB is not recognised as an M1 tree"
[ "$(grep -c '^cpu [0-9]' "$WORK/dtb-dump.txt")" = 8 ] || die "the DTB does not give 8 cpus"
grep -q '^aic 0 base=0x23b100000' "$WORK/dtb-dump.txt" || die "AIC missing"
grep -q '^uart s5l base=0x235200000' "$WORK/dtb-dump.txt" || die "UART missing"
grep -q '^wdt base=0x23d2b0000' "$WORK/dtb-dump.txt" || die "WDT missing"
sed 's/^/  /' "$WORK/dtb-dump.txt"

echo "== gfdt_dump: the guest FDT of the minimal DTB, with the layer's placement rule =="
"$WORK/gfdt_dump" "$OUT/j293-minimal.dtb" --layer 0x800000000 --entry 0x840000000 --image-size 0x300000 > "$WORK/gfdt-dump.txt" \
    || die "gfdt_dump refuses the minimal DTB"
grep -q '^  intc@f00000000:' "$WORK/gfdt-dump.txt" || die "gfdt_dump: the GIC node is missing"
if grep -qi 'apple\|serial-number\|mac-address\|seed' "$WORK/gfdt-dump.txt"; then die "gfdt_dump output carries an Apple or identifying string"; fi
sed -n '1,3p' "$WORK/gfdt-dump.txt" | sed 's/^/  /'

cp "$HERE/watari-load.py" "$OUT/"
cp "$HERE/ehw2.py" "$OUT/"
cp "$HERE/README-m1-payload.md" "$OUT/"

echo "== SHA256SUMS =="
( cd "$OUT" && find . -type f ! -name SHA256SUMS | sed 's|^\./||' | LC_ALL=C sort | while read -r f; do SHA "$f"; done > SHA256SUMS )
cat "$OUT/SHA256SUMS"

cat <<EOF

Bundle ready: $OUT
Manual steps for the hardware day (docs/procedures/m3a-day2-hardware.md, README-m1-payload.md):
  1. Copy the bundle folder to the host Mac (Ciani's) into a temp folder inside ~/WatariBoot-host/ (zero footprint).
  2. The m1n1 checkout of day 1 must be there; point the loader at it: --m1n1 <m1n1>/proxyclient or env M1N1_DIR.
  3. fdt_m1_dump is prebuilt (arm64) in the bundle; never run cc on the host without the Command Line Tools.
  4. python3 watari-load.py --rung L0 --dry-run first; then L1 ... L5, one rung at a time.
     Day 3 (EDK2): --rung D0 --edk2 WATARI_M1_EFI.fd (prepares, previews the guest FDT with gfdt_dump, no jump), then
     --rung D2 --edk2 WATARI_M1_EFI.fd --fb band --go (docs/design/watari-edk2-m1/PLAN.md T13 H2-H8).
  5. Afterwards remove the bundle folder.
EOF
