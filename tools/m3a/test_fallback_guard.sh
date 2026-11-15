#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# T16g: host test of the firmware guard of tools/m3a/build-m1-payload.sh for the declared fallbacks (--fallback-efi e4= / e1=).
# It fabricates tiny stand-in firmware files (an arm64 Image header, no EDK2 code) with build manifests in the format of
# tools/build-edk2-m1.sh and checks which combinations the bundle script accepts. The guard runs before any build, so the refusals
# are instant; the one accepting run builds a whole bundle (layer, guest, host tools; about a minute). Output goes under $WATARI_DATA,
# removed on exit. No download, no install, no device.
#   tools/m3a/test_fallback_guard.sh        (needs: source tools/env.sh, dtc, Homebrew llvm)
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
[ -n "${WATARI_DATA:-}" ] || { echo "FAIL: source tools/env.sh first (WATARI_DATA)"; exit 2; }
T="$(mktemp -d "$WATARI_DATA/guardtest.XXXXXX")"
trap 'rm -rf "$T"' EXIT
PASS=0; FAIL=0
ok()  { PASS=$((PASS + 1)); echo "ok   $1"; }
bad() { FAIL=$((FAIL + 1)); echo "FAIL $1"; }

# a stand-in firmware: 64 KiB, arm64 Image magic at 0x38, image_size (offset 16) = file size; $1 = file, $2 = a byte to make files differ
mkfd() {
    python3 -I - "$1" "$2" <<'EOF'
import struct, sys
d = bytearray(0x10000)
struct.pack_into("<QQ", d, 8, 0x80000, len(d))
d[0x38:0x3c] = b"ARM\x64"
d[0x100] = int(sys.argv[2])
open(sys.argv[1], "wb").write(bytes(d))
EOF
}
# a manifest: file, model, bench, ramdisk, bgr8, tag, pcd, dirty, commit
mkman() {
    local sha; sha="$(shasum -a 256 "$1" | cut -d' ' -f1)"
    { echo "model=$2"; echo "bench=$3"; echo "ramdisk_standin=$4"; echo "bgr8=$5"; echo "target=DEBUG"; echo "build_tag=$6"
      echo "build_pcd=$7"; echo "commit=$9"; echo "dirty=$8"; echo "fd_size=65536"; echo "fd_sha256=$sha"; } > "$1.build.txt"
}
C=1111111111111111111111111111111111111111
mkfd "$T/main.fd" 1;   mkman "$T/main.fd" j293 0 0 1 "" "" 0 $C
mkfd "$T/e4.fd" 2;     mkman "$T/e4.fd" j293-e4 0 0 1 e4 "" 0 $C
mkfd "$T/e1.fd" 3;     mkman "$T/e1.fd" j293-e1 0 0 1 e1 "" 0 $C

# run the script into a fresh dir; $1 = label, then the arguments; WATARI_M1_EFI is main.fd unless FIRM is set
run() {
    local out="$T/out-$RANDOM"; mkdir -p "$out"
    OUTTXT="$(WATARI_M1_EFI="${FIRM:-$T/main.fd}" "$HERE/build-m1-payload.sh" "$@" "$out" 2>&1)"; RC=$?
    OUT="$out"
}
refuse() {   # $1 label, $2 expected text; arguments after: run's
    local label="$1" want="$2"; shift 2
    run "$@"
    if [ "$RC" != 0 ] && echo "$OUTTXT" | grep -q "$want" && [ ! -e "$OUT/bundle" ]; then ok "refused: $label"; else bad "refused: $label (rc=$RC, wanted '$want')"; echo "$OUTTXT" | tail -3; fi
}

# 1. argument errors
refuse "unknown fallback name e8" "expected e4=<file> or e1=<file>" --fallback-efi "e8=$T/e4.fd"
refuse "plain file without NAME=" "expected e4=<file> or e1=<file>" --fallback-efi "$T/e4.fd"
refuse "e4 given twice" "given twice" --fallback-efi "e4=$T/e4.fd" --fallback-efi "e4=$T/e4.fd"
refuse "fallback file missing" "is not a file" --fallback-efi "e4=$T/nope.fd"

# 2. the manifest of each fallback
mkfd "$T/x.fd" 4
mkman "$T/x.fd" j293 0 0 1 e4 "" 0 $C;        refuse "plain j293 model as the e4 fallback" "model is 'j293', not j293-e4" --fallback-efi "e4=$T/x.fd"
mkman "$T/x.fd" j293-e1 0 0 1 e4 "" 0 $C;     refuse "e1 model in the e4 slot" "not j293-e4" --fallback-efi "e4=$T/x.fd"
mkman "$T/x.fd" j293-e4 1 0 1 e4 "" 0 $C;     refuse "bench build" "is a --bench build" --fallback-efi "e4=$T/x.fd"
mkman "$T/x.fd" j293-e4 0 1 1 e4 "" 0 $C;     refuse "RAM-disk stand-in build" "RAM-disk bench build\|--ramdisk bench build" --fallback-efi "e4=$T/x.fd"
mkman "$T/x.fd" j293-e4 0 0 1 t16be4 "" 0 $C; refuse "other build tag" "build tag is 't16be4', expected 'e4'" --fallback-efi "e4=$T/x.fd"
mkman "$T/x.fd" j293-e4 0 0 1 "" "" 0 $C;     refuse "no build tag" "build tag is '', expected 'e4'" --fallback-efi "e4=$T/x.fd"
mkman "$T/x.fd" j293-e4 0 0 1 e4 "gX.PcdY=0" 0 $C; refuse "PCD override" "WATARI_M1_BUILD_PCD" --fallback-efi "e4=$T/x.fd"
mkman "$T/x.fd" j293-e4 0 0 0 e4 "" 0 $C;     refuse "bgr8 differs from the main firmware" "bgr8=0 differs" --fallback-efi "e4=$T/x.fd"
mkman "$T/x.fd" j293-e4 0 0 1 e4 "" 1 $C;     refuse "dirty tree" "dirty tree" --fallback-efi "e4=$T/x.fd"
mkman "$T/x.fd" j293-e4 0 0 1 e4 "" 0 unknown; refuse "no commit" "no commit in the manifest" --fallback-efi "e4=$T/x.fd"
mkman "$T/x.fd" j293-e4 0 0 1 e4 "" 0 $C; printf 'X' | dd of="$T/x.fd" bs=1 seek=300 conv=notrunc 2>/dev/null
refuse "edited after its manifest (SHA-256)" "SHA-256 differs" --fallback-efi "e4=$T/x.fd"
rm -f "$T/x.fd.build.txt";                   refuse "no manifest" "no build manifest" --fallback-efi "e4=$T/x.fd"

# 3. the main firmware keeps its guard, and the overrides do not mix with fallbacks
FIRM="$T/e4.fd" refuse "a fallback build as the MAIN firmware" "model is 'j293-e4', not j293\|looks like a bench/tagged" --fallback-efi "e1=$T/e1.fd"
mkfd "$T/bench-main.fd" 5; mkman "$T/bench-main.fd" j293 1 0 1 "" "" 0 $C
FIRM="$T/bench-main.fd" refuse "a bench build as the main firmware, fallbacks declared" "is a --bench build" --fallback-efi "e4=$T/e4.fd"
WATARI_M1_EFI_ALLOW_BENCH=1 refuse "bench override (env) with fallbacks" "not together with WATARI_M1_EFI_ALLOW_BENCH" --fallback-efi "e4=$T/e4.fd"
WATARI_NO_EDK2=1 refuse "WATARI_NO_EDK2 with fallbacks" "needs the main EDK2 image" --fallback-efi "e4=$T/e4.fd"

# 4. the accepting run: both fallbacks declared; a full bundle
run --fallback-efi "e4=$T/e4.fd" --fallback-efi "e1=$T/e1.fd"
B="$OUT/bundle"
if [ "$RC" = 0 ]; then ok "accepted: e4 and e1 declared (bundle built)"; else bad "accepted run failed (rc=$RC)"; echo "$OUTTXT" | tail -5; fi
for n in WATARI_M1_EFI.fd WATARI_M1_EFI-e4.fd WATARI_M1_EFI-e1.fd; do
    [ -f "$B/$n" ] && [ -f "$B/$n.build.txt" ] && grep -q "  $n\$" "$B/SHA256SUMS" && ok "bundle has $n, its manifest, and a SHA256SUMS line" || bad "bundle lacks $n / manifest / SHA256SUMS line"
done
cmp -s "$B/WATARI_M1_EFI.fd" "$T/main.fd" && ok "main WATARI_M1_EFI.fd is the plain j293 file" || bad "main firmware changed"
cmp -s "$B/WATARI_M1_EFI-e4.fd" "$T/e4.fd" && cmp -s "$B/WATARI_M1_EFI-e1.fd" "$T/e1.fd" && ok "fallback copies are byte-identical" || bad "fallback copies differ"
( cd "$B" && shasum -a 256 -c SHA256SUMS >/dev/null 2>&1 ) && ok "shasum -c SHA256SUMS passes" || bad "shasum -c SHA256SUMS fails"
echo "$OUTTXT" | grep -q "guard: fallback e4" && echo "$OUTTXT" | grep -q "guard: fallback e1" && ok "the build log names both fallback guards" || bad "no fallback guard lines in the log"

# 5. no fallback declared: the bundle has none (the day-3 bundle is unchanged)
run
[ "$RC" = 0 ] && [ ! -e "$OUT/bundle/WATARI_M1_EFI-e4.fd" ] && [ ! -e "$OUT/bundle/WATARI_M1_EFI-e1.fd" ] && ok "no --fallback-efi: no fallback in the bundle" || bad "bundle without declarations has fallbacks or failed"

echo "$PASS passed, $FAIL failed"
[ "$FAIL" = 0 ]
