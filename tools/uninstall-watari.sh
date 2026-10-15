#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Disinstallazione completa di Watari Boot dal Mac.
#  - disinstalla SOLO i pacchetti brew che non erano in brew-prima.txt;
#  - chiede conferma prima di cancellare la cartella dati sul disco esterno;
#  - cancella ~/WatariBoot-specs (specifiche Arm, task M0-A) con --yes.
# Default: DRY-RUN (stampa soltanto). Esegue davvero solo con --yes.
set -euo pipefail

WATARI_DATA="${WATARI_DATA:-/Volumes/Arthur Fonzarelli/WatariBoot-data}"
BEFORE="$WATARI_DATA/brew-prima.txt"
# Specifiche Arm scaricate per il task M0-A (PDF + testo estratto), sul disco interno.
SPECS="${WATARI_SPECS:-$HOME/WatariBoot-specs}"
DO=0
[ "${1:-}" = "--yes" ] && DO=1

# Le specifiche stanno nella home: si gestiscono prima del controllo del disco esterno.
if [ -d "$SPECS" ]; then
    echo "Specifiche Arm (M0-A): $SPECS ($(du -sh "$SPECS" 2>/dev/null | cut -f1))"
    if [ "$DO" = 1 ]; then
        rm -rf "$SPECS"
        echo "Cartella specifiche cancellata: $SPECS"
    else
        echo "  (dry-run) rm -rf \"$SPECS\""
    fi
else
    echo "Specifiche Arm (M0-A): $SPECS non presente."
fi

if [ ! -f "$BEFORE" ]; then
    echo "ERRORE: $BEFORE non trovato (disco collegato?). Non posso calcolare la differenza." >&2
    exit 1
fi

# Pacchetti attuali meno quelli di prima = quelli aggiunti da Watari.
ADDED="$(comm -13 <(sort "$BEFORE") <(brew list --formula | sort))"
CASKS_ADDED=""   # i cask non sono usati dal progetto

[ "$DO" = 1 ] && echo "=== ESECUZIONE REALE ===" || echo "=== DRY-RUN (usa --yes per eseguire) ==="
echo "Pacchetti brew aggiunti da Watari:"; echo "$ADDED" | sed 's/^/  /'
echo "Cartella dati: $WATARI_DATA ($(du -sh "$WATARI_DATA" 2>/dev/null | cut -f1))"
echo "Link simbolico temporaneo: /tmp/watari-data"

if [ "$DO" = 0 ]; then
    echo "Comandi che verrebbero eseguiti:"
    # shellcheck disable=SC2086
    [ -n "$ADDED" ] && echo "  brew uninstall $(echo $ADDED | tr '\n' ' ')"
    echo "  brew autoremove   (solo dopo aver controllato cosa propone)"
    echo "  rm -f /tmp/watari-data"
    echo "  rm -rf \"$WATARI_DATA\"   (previa conferma)"
    exit 0
fi

if [ -n "$ADDED" ]; then
    # shellcheck disable=SC2086
    brew uninstall $(echo $ADDED | tr '\n' ' ') || true
fi
rm -f /tmp/watari-data
read -r -p "Cancellare DEFINITIVAMENTE '$WATARI_DATA'? Scrivi 'si' per confermare: " ANS
if [ "$ANS" = "si" ]; then
    rm -rf "$WATARI_DATA"
    echo "Cartella dati cancellata."
else
    echo "Cartella dati NON cancellata."
fi
