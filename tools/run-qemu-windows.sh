#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Avvia Windows 11 ARM64 (ISO ufficiale) in QEMU con il nostro EDK2 (Gate B, V0).
# Uso: tools/run-qemu-windows.sh [--headless] [--tcg] [--mem 6G] [--iso PERCORSO] [--unattend]
#   senza opzioni: finestra cocoa (ramfb + tastiera/tablet USB) con HVF.
#   --headless: -display none, monitor su socket UNIX $WATARI_DATA/run/qemu-mon.sock
#               (es. echo "screendump /tmp/x.ppm" | nc -U <socket>); QEMU va in background.
#   --unattend: collega $WATARI_DATA/vm/unattend.iso come secondo CD USB (autounattend.xml).
#   --tcg: -accel tcg -cpu max (ripiego, lento).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "$HERE/env.sh"

HEADLESS=0; ACCEL=hvf; MEM=6G; UNATTEND=0
ISO="$WATARI_DATA/iso/Windows11_Client_arm64_it-it_26300_9457.iso"
while [ $# -gt 0 ]; do
    case "$1" in
        --headless) HEADLESS=1 ;;
        --tcg) ACCEL=tcg ;;
        --mem) MEM="$2"; shift ;;
        --iso) ISO="$2"; shift ;;
        --unattend) UNATTEND=1 ;;
        *) echo "opzione sconosciuta: $1" >&2; exit 2 ;;
    esac
    shift
done

TARGET="${TARGET:-DEBUG}"
FD="$WATARI_EDK2/Build/ArmVirtQemu-AArch64/${TARGET}_${WATARI_TOOLCHAIN}/FV"
VM="$WATARI_DATA/vm"; RUN="$WATARI_DATA/run"
mkdir -p "$VM" "$RUN" "$WATARI_LOGS/screens"
DISK="$VM/win11.qcow2"
[ -f "$DISK" ] || qemu-img create -f qcow2 "$DISK" 64G >/dev/null
# pflash: file da 64 MiB; il codice si riscrive sempre, le variabili solo se mancano.
truncate -s 64M "$VM/win11-efi.img"
dd if="$FD/QEMU_EFI.fd" of="$VM/win11-efi.img" conv=notrunc 2>/dev/null
if [ ! -f "$VM/win11-vars.img" ]; then
    truncate -s 64M "$VM/win11-vars.img"
    dd if="$FD/QEMU_VARS.fd" of="$VM/win11-vars.img" conv=notrunc 2>/dev/null
fi

if [ "$ACCEL" = hvf ]; then CPU=(-accel "hvf${HVF_OPTS:-}" -cpu host); else CPU=(-accel tcg -cpu max); fi
MON="$RUN/qemu-mon.sock"; rm -f "$MON"
if [ "$HEADLESS" = 1 ]; then
    DISP=(-display none -monitor "unix:$MON,server,nowait")
else
    DISP=(-display cocoa)
fi

UNA=()
if [ "$UNATTEND" = 1 ]; then
    [ -f "$VM/unattend.iso" ] || { echo "manca $VM/unattend.iso (tools/make-unattend-iso.sh)" >&2; exit 1; }
    UNA=(-drive if=none,id=cd1,format=raw,media=cdrom,readonly=on,file="$VM/unattend.iso"
         -device usb-storage,bus=xhci.0,drive=cd1)
fi

CMD=(qemu-system-aarch64 -M virt,gic-version=3 "${CPU[@]}" -smp 4 -m "$MEM"
    -drive if=pflash,format=raw,file="$VM/win11-efi.img",readonly=on
    -drive if=pflash,format=raw,file="$VM/win11-vars.img"
    -device ramfb
    -device qemu-xhci,id=xhci
    -device usb-kbd,bus=xhci.0 -device usb-tablet,bus=xhci.0
    -drive if=none,id=cd0,format=raw,media=cdrom,readonly=on,file="$ISO"
    -device usb-storage,bus=xhci.0,drive=cd0,bootindex=0
    "${UNA[@]}"
    -drive if=none,id=hd0,format=qcow2,file="$DISK"
    -device nvme,drive=hd0,serial=watari0,bootindex=1
    -net none "${DISP[@]}" ${QEMU_EXTRA:-})
printf '%q ' "${CMD[@]}" > "$WATARI_LOGS/win-cmd.txt"; echo >> "$WATARI_LOGS/win-cmd.txt"
if [ "$HEADLESS" = 1 ]; then
    "${CMD[@]}" > "$WATARI_LOGS/win-qemu.log" 2>&1 < /dev/null &
    echo "$!" > "$RUN/qemu.pid"; echo "QEMU pid $! ; monitor: $MON"
else
    exec "${CMD[@]}"
fi
