#!/bin/sh
# SPDX-License-Identifier: MIT
# T6f build check: no `smc` is reachable on the M1 path. The M1 has no EL3, so an SMC issued by the layer would be
# UNDEFINED [hypothesis: Arm ARM, SMC without EL3]. Two checks on the linked image(s) given as arguments (default
# build/wel2.elf build-chain/wel2.elf):
#  1. every function that contains an `smc` instruction is one of plat_qemu.c's (psci_smc and the q_* methods it is
#     inlined into);
#  2. none of the M1-side objects (plat_m1, cpu_start, apple_fiq, apple_sysreg, hvc_vendor, psci) contains one.
# Exit 0 = ok.
LLVM=${LLVM:-/opt/homebrew/opt/llvm/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
rc=0
[ $# -gt 0 ] || set -- "$HERE/build/wel2.elf" "$HERE/build-chain/wel2.elf"
for elf in "$@"; do
    [ -f "$elf" ] || { echo "check-nosmc: FAIL: $elf missing" >&2; rc=1; continue; }
    funcs=$("$LLVM/llvm-objdump" -d "$elf" | awk '/^[0-9a-f]+ <.*>:$/ {f=$2} $3=="smc" || $2=="smc" {print f}' | sort -u)
    for f in $funcs; do
        case $f in
            "<psci_smc>:"|"<q_"*) ;;
            *) echo "check-nosmc: FAIL: smc in $f of $elf" >&2; rc=1 ;;
        esac
    done
    echo "check-nosmc: $elf: smc only in:" $funcs
    dir=$(dirname "$elf")
    for o in plat_m1 cpu_start apple_fiq apple_sysreg hvc_vendor psci; do
        if "$LLVM/llvm-objdump" -d "$dir/$o.o" | awk '$3=="smc" || $2=="smc" {found=1} END {exit !found}'; then
            echo "check-nosmc: FAIL: smc in $dir/$o.o" >&2; rc=1
        fi
    done
done
[ $rc = 0 ] && echo "check-nosmc: OK"
exit $rc
