#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Test headless: avvia, preme "any key", fa screendump a 60/120/180 s, chiude. Uso: [--tcg]
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$HERE/env.sh"
TAG=hvf; [ "${1:-}" = "--tcg" ] && TAG=tcg
MON="$WATARI_DATA/run/qemu-mon.sock"; S="$WATARI_LOGS/screens"; SL="$WATARI_LINK/logs/screens"  # screendump: percorso senza spazi
mon() { printf '%s\n' "$1" | nc -U -w 2 "$MON" >/dev/null 2>&1 || true; }
"$HERE/run-qemu-windows.sh" --headless "$@"
for i in $(seq 1 30); do [ -S "$MON" ] && break; sleep 0.5; done
sleep 3
for i in $(seq 1 8); do mon "sendkey ret"; sleep 2; done   # ~16 s
T0=$SECONDS
for t in 60 120 180; do
    while [ $((SECONDS-T0+19)) -lt "$t" ]; do sleep 1; done
    mon "screendump $SL/win-$TAG-${t}s.ppm"; sleep 2
    sips -s format png "$S/win-$TAG-${t}s.ppm" --out "$S/win-$TAG-${t}s.png" >/dev/null 2>&1 && rm -f "$S/win-$TAG-${t}s.ppm"
done
mon "quit"; sleep 3
kill "$(cat "$WATARI_DATA/run/qemu.pid")" 2>/dev/null || true
ls -l "$S"
