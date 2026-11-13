#!/bin/bash
# SPDX-License-Identifier: MIT
# T6g (PLAN S15 negative control): G5b must notice a layer whose ICH_HCR_EL2.TC bit disagrees with the mode it reports, and
# name TC. Two cases (test knobs wel2.test=tcoff / tcon in cpuif.c):
#   reports trap       but runs without TC  -> G5b FAIL "TC: expected traps, none counted"
#   reports trap-notc  but runs with TC     -> G5b FAIL "TC: counted traps although TC should be off"
# Exit 0 when both are detected.
set -u
cd "$(dirname "$0")"
OUT=build-chain
rc=0
check() {   # mode, knob, text
    CPUIF="$1" WEL2_ARGS="wel2.test=$2" ./run-m1guest.sh >"$OUT/g5b-neg.out" 2>&1
    local st=$?
    local l
    l=$(grep -aE "(^|[: ])G5b (PASS|FAIL) " $OUT/guest-uart.txt | head -1)
    if [ $st != 0 ] && echo "$l" | grep -q " FAIL " && echo "$l" | grep -qF "$3"; then
        echo "ok: $1 + $2: G5b FAIL as designed ($3)"
    else
        echo "FAIL: $1 + $2: exit $st, G5b line: ${l:-none}"; rc=1
    fi
}
check trap tcoff "TC: expected traps"
check trap-notc tcon "TC: counted traps"
[ $rc = 0 ] && echo "PASS: G5b distinguishes trap from trap-notc and names TC"
exit $rc
