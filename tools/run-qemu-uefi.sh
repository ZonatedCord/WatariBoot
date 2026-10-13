#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Avvia QEMU (headless, seriale su stdout) fino alla shell UEFI / menu di boot.
# Uso: tools/run-qemu-uefi.sh [tcg|hvf] [secondi]   (default: tcg, 40 s)
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "$HERE/env.sh"

ACCEL="${1:-tcg}"
SECS="${2:-40}"
TARGET="${TARGET:-DEBUG}"
FD="$WATARI_EDK2/Build/ArmVirtQemu-AArch64/${TARGET}_${WATARI_TOOLCHAIN}/FV"
RUN="$WATARI_DATA/run"
mkdir -p "$RUN" "$WATARI_LOGS"

# pflash richiede file da 64 MiB: si lavora su copie nella cartella dati.
truncate -s 64M "$RUN/efi.img" "$RUN/vars.img" 2>/dev/null || true
dd if="$FD/QEMU_EFI.fd" of="$RUN/efi.img" conv=notrunc 2>/dev/null
dd if="$FD/QEMU_VARS.fd" of="$RUN/vars.img" conv=notrunc 2>/dev/null

if [ "$ACCEL" = hvf ]; then CPU_ARGS=(-accel hvf -cpu host); else CPU_ARGS=(-accel tcg -cpu max); fi
LOG="$WATARI_LOGS/b2-uefi.log"

# timeout non c'e' di default su macOS: kill dopo SECS secondi.
qemu-system-aarch64 -M virt "${CPU_ARGS[@]}" -m 2G -nographic \
    -drive if=pflash,format=raw,file="$RUN/efi.img",readonly=on \
    -drive if=pflash,format=raw,file="$RUN/vars.img" \
    -net none > "$LOG" 2>&1 < /dev/null &
QPID=$!
sleep "$SECS"
kill "$QPID" 2>/dev/null || true
wait "$QPID" 2>/dev/null || true
echo "Log: $LOG"
tail -n 15 "$LOG"
grep -aqE "Shell>|UEFI Interactive Shell|Boot Manager|Device Manager|Unable to boot|No bootable option" "$LOG" \
    && echo "OK: raggiunta shell/menu UEFI" || { echo "ATTENZIONE: shell UEFI non rilevata"; exit 1; }
