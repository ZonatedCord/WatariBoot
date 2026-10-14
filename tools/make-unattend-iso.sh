#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Crea $WATARI_DATA/vm/unattend.iso con autounattend.xml nella radice (volume UNATTEND).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "$HERE/env.sh"
SRC="$WATARI_LINK/tmp-unattend"
OUT="$WATARI_DATA/vm/unattend.iso"
rm -rf "$SRC"; mkdir -p "$SRC" "$WATARI_DATA/vm"
cp "$HERE/autounattend/autounattend.xml" "$SRC/autounattend.xml"
rm -f "$OUT"
hdiutil makehybrid -iso -joliet -default-volume-name UNATTEND -o "$OUT" "$SRC"
rm -rf "$SRC"
echo "creata: $OUT"
