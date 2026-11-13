#!/bin/bash
# SPDX-License-Identifier: MIT
# T6g PART 3: wel2.hold=1 keeps the final state on the screen instead of resetting (for photos on the M1).
#   wel2.step=2 wel2.hold=1       : after checkpoint 5 the layer prints the HOLD message and does NOT reset: QEMU must still be
#                                    running 5 s after the message (then this script kills it)
#   wel2.guest=1 wel2.hold=1      : the guest runs G1-G11 and issues SYSTEM_RESET: HOLD message, QEMU still running 5 s later
#   wel2.step=1 wel2.hold=1       : the immediate reset of rung L1 is NOT held: QEMU exits at once, no text
# Chain build, QEMU TCG, -smp 4, -no-reboot. Env: CPUIF=hw|trap|trap-notc (default trap), QEMU. Exit 0 on success. About 60 s.
set -u
cd "$(dirname "$0")"
make -s chain || { echo "FAIL: layer build"; exit 2; }
make -s -C ../guest || { echo "FAIL: guest build"; exit 2; }
GUEST=../guest/build/m1guest.bin
OUT=build-chain
CPUIF="${CPUIF:-trap}"
HOLDMSG="Watari EL2: HOLD: take the photo, then hold the power button"
rc=0

# run_case <name> <extra args> <what to wait for: "exit" | "hold">
run_case() {
    local name="$1" extra="$2" wait="$3" log="$OUT/hold-$1-uart.log" args="wel2.guest=1 wel2.cpuif=$CPUIF $2"
    : > "$log"
    "${QEMU:-qemu-system-aarch64}" -M virt,virtualization=on,gic-version=3 -cpu max -accel tcg \
        -nographic -m 512M -smp 4 -nic none -no-reboot -kernel $OUT/wel2.bin -append "$args" \
        -device loader,file="$GUEST",addr=0x40280000,force-raw=on \
        -serial "file:$log" -monitor none >"$OUT/hold-$1-qemu.out" 2>&1 &
    local qpid=$!
    EXITED=0; HELD=0
    for _ in $(seq 1 600); do
        kill -0 "$qpid" 2>/dev/null || { EXITED=1; break; }
        if [ "$wait" = hold ] && tr -d '\r' < "$log" | grep -aqF "$HOLDMSG"; then HELD=1; break; fi
        sleep 0.1
    done
    ALIVE_LATER=0
    if [ "$HELD" = 1 ]; then      # the message is out: the machine must stay up (no reset ends the QEMU process)
        sleep 5
        kill -0 "$qpid" 2>/dev/null && ALIVE_LATER=1
    fi
    kill "$qpid" 2>/dev/null; wait "$qpid" 2>/dev/null
    tr -d '\r' < "$log" > "$OUT/hold-$1-uart.txt"
}

run_case step2 "wel2.step=2 wel2.hold=1" hold
if [ "$HELD" = 1 ] && [ "$ALIVE_LATER" = 1 ]; then echo "ok: step=2 + hold: HOLD message, no reset after 5 s"; else echo "FAIL: step=2 + hold (held=$HELD alive=$ALIVE_LATER)"; rc=1; fi
grep -aq "checkpoint 5 CPU_INIT" "$OUT/hold-step2-uart.txt" || { echo "MISSING: checkpoint 5 before the hold"; rc=1; }
grep -aq "stage-2 on" "$OUT/hold-step2-uart.txt" && { echo "UNEXPECTED: ran past step 2"; rc=1; }

run_case guest "wel2.hold=1" hold
if [ "$HELD" = 1 ] && [ "$ALIVE_LATER" = 1 ]; then echo "ok: guest + hold: SYSTEM_RESET held on the screen, no reset after 5 s"; else echo "FAIL: guest + hold (held=$HELD alive=$ALIVE_LATER)"; rc=1; fi
grep -aq "PSCI SYSTEM_RESET" "$OUT/hold-guest-uart.txt" || { echo "MISSING: the guest's SYSTEM_RESET"; rc=1; }
grep -aqE "(^|[: ])G11 PASS " "$OUT/hold-guest-uart.txt" || { echo "MISSING: G11 PASS"; rc=1; }

run_case step1 "wel2.step=1 wel2.hold=1" exit
if [ "$EXITED" = 1 ] && [ ! -s "$OUT/hold-step1-uart.txt" ]; then echo "ok: step=1 + hold: still resets at once, no text"; else echo "FAIL: step=1 must reset immediately even with hold (exited=$EXITED)"; rc=1; fi

[ $rc = 0 ] && echo "PASS: wel2.hold=1 holds on the screen (CPUIF=$CPUIF); wel2.step=1 is not held"
exit $rc
