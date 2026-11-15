#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Regenerates the loader dry-run samples used by tools/m3a/test_monday.py from the two rehearsal bundles
# (m-day3/bundle, m-day4/bundle) built by tools/m3a/build-m1-payload.sh. No device, no network: --dry-run only.
# Usage: gen-fixtures.sh <rehearsal dir containing m-day3/bundle and m-day4/bundle>
set -euo pipefail
R="${1:?rehearsal dir (e.g. \$WATARI_DATA/rehearsal-n5)}"
OUT="$(cd "$(dirname "$0")" && pwd)"
gen() {
    local n=$1 b=$2; shift 2
    ( cd "$R/$b/bundle" && python3 watari-load.py "$@" --m1n1 /nonexistent --dry-run 2>&1; echo "exit=$?" ) \
        | grep -v '^  \[proxy\]' | sed "s|$R|<R>|g" > "$OUT/$n.txt"
    echo "$n: $(tail -1 "$OUT/$n.txt")"
}
gen D0 m-day3 --rung D0 --edk2 WATARI_M1_EFI.fd --fb band --no-smp
gen D1 m-day3 --rung L5 --go --fb band --args wel2.hold=1
gen D2 m-day3 --rung D2 --edk2 WATARI_M1_EFI.fd --fb band --go --pmlog --args wel2.hold=1
gen D4-0 m-day3 --rung D0 --edk2 WATARI_M1_EFI.fd --fb band --smp
gen D4 m-day3 --rung D2 --edk2 WATARI_M1_EFI.fd --fb band --go --cpuif trap-notc --smp --pmlog --args wel2.hold=1
gen D5-0 m-day3 --rung D0 --edk2 WATARI_M1_EFI.fd --fb guest --no-smp
gen D5 m-day3 --rung D2 --edk2 WATARI_M1_EFI.fd --fb guest --go --pmlog --args wel2.hold=1
gen W1a m-day4 --ramdisk-test 64
gen W1a-nocsum m-day4 --ramdisk-test 64 --no-data-csums
gen W1b m-day4 --rung D0 --edk2 WATARI_M1_EFI.fd --ramdisk winram/winram.img --fb band --no-smp
gen W1c m-day4 --rung D2 --edk2 WATARI_M1_EFI.fd --ramdisk winram/winram.img --fb band --go --pmlog --args wel2.hold=1
gen W2 m-day4 --rung D2 --edk2 WATARI_M1_EFI.fd --ramdisk winram/winram.img --fb band --go --cpuif trap-notc --smp --pmlog --args wel2.hold=1
