# Cosa è stato installato sul Mac (Gate B)

Backup Time Machine confermato dall'utente prima dei download. Stato verificato il 2026-10-13.

## Pacchetti Homebrew aggiunti
Richiesti: `qemu acpica llvm lld nasm`. Gli altri sono dipendenze automatiche.

| Pacchetto | Versione | Ruolo |
|---|---|---|
| qemu | 11.1.2 | emulatore (banco di prova) |
| acpica | 20260408 | `iasl`, compilatore ASL/ACPI |
| llvm | 23.1.3_1 | clang + llvm-objcopy per la toolchain CLANGDWARF (keg-only, non entra nel PATH) |
| lld | 23.1.3 | linker |
| nasm | 3.02 | assembler (non serve ad AARCH64, tenuto per i target x86) |
| capstone, dtc, libslirp, openssl@4, vde, z3 | 5.0.9, 1.8.1, 4.9.5, 4.0.3, 2.3.3, 5.1.0 | dipendenze di qemu/llvm |

Elenco di riferimento "prima": `/Volumes/Arthur Fonzarelli/WatariBoot-data/brew-prima.txt`.
Spazio (Cellar): qemu 696M, llvm 1.8G, lld 5.8M, nasm 2.9M, acpica 3.3M.

## Toolchain
EDK2 `CLANGDWARF` (documentata in `BaseTools/Conf/tools_def.template`: "Linux, Windows, Mac", richiede Clang >= 9) con LLVM/lld di Homebrew. Target `-target aarch64-linux-gnu`.

## Dati scaricati (solo sul disco esterno)
Radice: `/Volumes/Arthur Fonzarelli/WatariBoot-data/` (totale ~1.0G)
- `src/edk2/`: clone di `tianocore/edk2` tag `edk2-stable202608` (commit 2970e56, `--depth 1`, submodule inclusi), 695M prima della build.
- `src/edk2/Build/`: output della build ArmVirtQemu, 355M.
- `logs/`: log di build e QEMU (`b2-uefi.log`, `b2-uefi-tcg.log`, `b2-uefi-hvf.log`, `b2-build.log`).
- `run/`: copie da 64M dei firmware usate da QEMU (`efi.img`, `vars.img`).
- `brew-prima.txt`: pacchetti brew presenti prima.

Fuori dal disco esterno: solo il link simbolico `/tmp/watari-data` -> cartella dati (creato da `tools/env.sh`; serve perché EDK2 non accetta spazi nei percorsi; sparisce al riavvio).

## Script
`tools/env.sh`, `tools/build-edk2.sh`, `tools/run-qemu-uefi.sh`, `tools/uninstall-watari.sh`.

## Come rimuovere tutto
Con lo script (default dry-run, non cancella nulla):
```
tools/uninstall-watari.sh          # mostra cosa farebbe
tools/uninstall-watari.sh --yes    # esegue (chiede conferma per la cartella dati)
```
A mano:
```
brew uninstall qemu acpica llvm lld nasm
brew autoremove        # controlla l'elenco proposto prima di confermare
rm -f /tmp/watari-data
rm -rf "/Volumes/Arthur Fonzarelli/WatariBoot-data"
```
Per confrontare: `comm -13 <(sort brew-prima.txt) <(brew list --formula | sort)` deve tornare vuoto.
