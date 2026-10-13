#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Variabili d'ambiente di Watari Boot (da "source", non da eseguire).
# Controlla che il disco esterno sia montato.

WATARI_DATA="${WATARI_DATA:-/Volumes/Arthur Fonzarelli/WatariBoot-data}"
if [ ! -d "$WATARI_DATA" ]; then
    echo "ERRORE: $WATARI_DATA non trovato. Collega il disco esterno 'Arthur Fonzarelli'." >&2
    return 1 2>/dev/null || exit 1
fi

export WATARI_DATA
# EDK2 (edksetup.sh/BuildEnv) non supporta spazi nei percorsi: il disco si chiama
# "Arthur Fonzarelli", quindi si usa un link simbolico senza spazi in /tmp
# (sparisce al riavvio; viene ricreato da qui; nessun dato sta nel link).
WATARI_LINK="/tmp/watari-data"
if [ "$(readlink "$WATARI_LINK" 2>/dev/null)" != "$WATARI_DATA" ]; then
    rm -f "$WATARI_LINK"; ln -s "$WATARI_DATA" "$WATARI_LINK"
fi
export WATARI_LINK
export WATARI_EDK2="$WATARI_LINK/src/edk2"
export WATARI_LOGS="$WATARI_DATA/logs"
export WATARI_BREW_PREFIX="$(brew --prefix)"

# Toolchain EDK2: CLANGDWARF con LLVM/lld di Homebrew (documentata in
# BaseTools/Conf/tools_def.template, sezione CLANGDWARF: Linux, Windows, Mac).
export WATARI_TOOLCHAIN="CLANGDWARF"
export CLANG_BIN="$WATARI_BREW_PREFIX/opt/llvm/bin/"
export CLANGDWARF_BIN="$CLANG_BIN"
export CLANGDWARF_DLL="$WATARI_BREW_PREFIX/opt/llvm/lib/"
export CLANG_HOST_BIN=""
# Compilatore ASL (acpica) e nasm sono nel PATH di Homebrew.
export PATH="$WATARI_BREW_PREFIX/opt/lld/bin:$WATARI_BREW_PREFIX/bin:$PATH"
