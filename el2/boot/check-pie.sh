#!/bin/sh
# SPDX-License-Identifier: MIT
# T6a build checks for the position-independent layer image (usage: check-pie.sh build/wel2.elf):
#   1. every dynamic relocation is R_AARCH64_RELATIVE (the only kind start.S resolves);
#   2. no GOT / RELR / dynamic-symbol sections in the image (nothing may be used before the relocation);
#   3. the linker-script symbols are section-relative (an absolute one would not move with the image);
#   4. the image is linked at 0;
#   5. start.S has no literal-pool pseudo-load (a "ldr" of a register from "=" something).
# Exit 0 = all pass. Prints the relocation count.
LLVM=${LLVM:-/opt/homebrew/opt/llvm/bin}
ELF=${1:?usage: check-pie.sh file.elf}
HERE=$(cd "$(dirname "$0")" && pwd)
rc=0
fail() { echo "check-pie: FAIL: $*" >&2; rc=1; }

# 1. relocation types (the "Type" column of llvm-readelf -r, e.g. R_AARCH64_RELATIVE)
RELS=$("$LLVM/llvm-readelf" -r "$ELF" | grep -E '^[0-9a-f]{16} ' | awk '{print $3}')
NREL=$(echo "$RELS" | grep -c . || true)
OTHER=$(echo "$RELS" | grep -v '^R_AARCH64_RELATIVE$' | sort -u | tr '\n' ' ')
[ -z "$OTHER" ] || fail "relocation types other than R_AARCH64_RELATIVE: $OTHER"
[ "$NREL" -gt 0 ] || fail "no relocations at all (is the image really a PIE?)"

# 2. forbidden sections
SECS=$("$LLVM/llvm-readelf" -S "$ELF")
for s in got got.plt igot relr.dyn dynsym dynstr dynamic interp hash gnu.hash rela.plt; do
    echo "$SECS" | grep -qE "\] \.$(echo "$s" | sed 's/\./\\./g') " && fail "section .$s present in the image"
done
echo "$SECS" | grep -qE "\] \.rela\.dyn " || fail "no .rela.dyn section"

# 3. + 4. symbols: section-relative (not ABS), _start at 0
SYMS=$("$LLVM/llvm-readelf" -s "$ELF")
for sym in _start __image_start __text_end __rodata_end __rela_start __rela_end __bss_start __bss_end \
           __image_end __el2_stacks __el1_stack_top __layer_end el2_vectors; do
    line=$(echo "$SYMS" | awk -v s="$sym" '$8 == s')
    [ -n "$line" ] || { fail "symbol $sym missing"; continue; }
    echo "$line" | awk '{exit !($7 == "ABS")}' && fail "symbol $sym is absolute (must be section-relative)"
done
TEXTEND=$(echo "$SYMS" | awk '$8 == "__text_end" {print $2}')
BSS=$(echo "$SYMS" | awk '$8 == "__bss_start" {print $2}')
for o in $("$LLVM/llvm-readelf" -r "$ELF" | grep -E '^[0-9a-f]{16} ' | awk '{print $1}'); do
    # a relocation must hit 8-aligned data after the code (never the header or the code) and before .bss
    [ $((0x$o % 8)) -eq 0 ] && [ $((0x$o)) -ge $((0x$TEXTEND)) ] && [ $((0x$o)) -lt $((0x$BSS)) ] || fail "relocation target 0x$o is misaligned or outside the data sections"
done
START=$(echo "$SYMS" | awk '$8 == "_start" {print $2}')
case "$START" in 0000000000000000) ;; *) fail "_start is at $START, not 0";; esac

# 5. no literal pool loads in start.S (comments excluded)
if grep -nE '^[^/]*\bldr[[:space:]]+[xw][0-9]+,[[:space:]]*=' "$HERE/start.S"; then
    fail "start.S has a literal-pool load (ldr reg, =...)"
fi

[ $rc = 0 ] && echo "check-pie: OK $ELF: $NREL R_AARCH64_RELATIVE relocations, no GOT/RELR/dynsym, symbols section-relative, linked at 0"
exit $rc
