#!/bin/sh
# SPDX-License-Identifier: MIT
# T6e build check (review L-5, risk R-17): assemble the real Apple system-register accessors and compare the
# instruction word of every accessor function with an independent table of encodings. A mistyped S3_x_Cx_Cx_x name
# (which would silently touch another IMPLEMENTATION DEFINED register on the M1; none of them exists in QEMU) fails.
# Usage: check-apple-enc.sh [apple_sysreg.c]   Exit 0 = every function holds exactly the expected mrs/msr.
#
# The table below is deliberately NOT generated from include/wel2_apple_sysreg.h. Sources (facts only):
#   Linux v6.12 drivers/irqchip/irq-apple-aic.c:167-199, arch/arm64/include/asm/apple_m1_pmu.h:22,
#   arch/arm64/include/asm/sysreg.h:576,578,650,653; CNTHP/CNTHV_CTL_EL2 and ICH_*: encoded by llvm-mc.
# Columns: function-suffix  direction(rd|wr)  op0 op1 CRn CRm op2
LLVM=${LLVM:-/opt/homebrew/opt/llvm/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=${1:-$HERE/apple_sysreg.c}
TMP=${TMPDIR:-/tmp}/check-apple-enc.$$
OBJ=$TMP.o
trap 'rm -f "$OBJ"' EXIT
rc=0
fail() { echo "check-apple-enc: FAIL: $*" >&2; rc=1; }

"$LLVM/clang" --target=aarch64-none-elf -std=c11 -O2 -ffreestanding -fno-pic -I"$HERE/../include" -c "$SRC" -o "$OBJ" \
    || { echo "check-apple-enc: FAIL: cannot assemble $SRC" >&2; exit 1; }

# mrs/msr (register form): bits 31:22 = 1101010100 (0x354), bit 20 = 1  ->  (w & 0xFFD00000) == 0xD5100000
# word = 0xD5000000 | L<<21 | 1<<20 | (op0&1)<<19 | op1<<16 | CRn<<12 | CRm<<8 | op2<<5 | Rt   (op0 is 2 or 3)
enc() { # dir op0 op1 crn crm op2 -> word with Rt = 0
    l=0; [ "$1" = rd ] && l=1
    echo $(( 0xD5100000 | (l << 21) | (($2 & 1) << 19) | ($3 << 16) | ($4 << 12) | ($5 << 8) | ($6 << 5) ))
}
sysword() { # function -> every mrs/msr word (Rt cleared) in it, one per line
    "$LLVM/llvm-objdump" -d --disassemble-symbols="$1" "$OBJ" | awk '$1 ~ /^[0-9a-f]+:$/ && $2 ~ /^[0-9a-f]{8}$/ {print $2}' |
    while read -r w; do
        v=$(( 0x$w ))
        [ $(( v & 0xFFD00000 )) -eq $(( 0xD5100000 )) ] && echo $(( v & ~0x1f ))
    done
}

TABLE="vm_tmr_fiq_ena rd 3 5 15 1 3
vm_tmr_fiq_ena wr 3 5 15 1 3
cntv_ctl_el02 rd 3 5 14 3 1
cntv_ctl_el02 wr 3 5 14 3 1
cntp_ctl_el02 rd 3 5 14 2 1
cntp_ctl_el02 wr 3 5 14 2 1
cnthv_ctl rd 3 4 14 3 1
cnthv_ctl wr 3 4 14 3 1
cnthp_ctl rd 3 4 14 2 1
cnthp_ctl wr 3 4 14 2 1
ipi_sr rd 3 5 15 1 1
ipi_sr wr 3 5 15 1 1
ipi_rr_global wr 3 5 15 0 1
pmcr0 rd 3 1 15 0 0
pmcr0 wr 3 1 15 0 0
upmcr0 rd 3 7 15 0 4
upmcr0 wr 3 7 15 0 4
upmsr rd 3 7 15 6 4
ich_hcr rd 3 4 12 11 0
ich_hcr wr 3 4 12 11 0
ich_misr rd 3 4 12 11 2"

echo "$TABLE" | while read -r name dir a b c d e; do
    exp=$(enc "$dir" "$a" "$b" "$c" "$d" "$e")
    got=$(sysword "apple_sr_${dir}_${name}")
    if [ "$got" != "$exp" ]; then
        printf 'check-apple-enc: FAIL: apple_sr_%s_%s: expected word 0x%x, found "%s"\n' "$dir" "$name" "$exp" \
            "$(for g in $got; do printf '0x%x ' "$g"; done)" >&2
        exit 1
    fi
done || rc=1

# no other system-register access anywhere in the object (a stray accessor would escape the per-function check)
NEXP=$(echo "$TABLE" | grep -c .)
NALL=$("$LLVM/llvm-objdump" -d "$OBJ" | awk '$1 ~ /^[0-9a-f]+:$/ && $2 ~ /^[0-9a-f]{8}$/ {print $2}' |
       while read -r w; do v=$(( 0x$w )); [ $(( v & 0xFFD00000 )) -eq $(( 0xD5100000 )) ] && echo x; done | grep -c x || true)
[ "$NALL" -eq "$NEXP" ] || fail "object holds $NALL mrs/msr instructions, the table lists $NEXP"
[ $rc -eq 0 ] && echo "check-apple-enc: OK ($NEXP accessors match the encoding table)"
exit $rc
