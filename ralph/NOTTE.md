# Lavoro autonomo notturno (task pianificati)

Ogni esecuzione notturna parte **da zero**: legge questo file e lavora sul primo task non fatto.

## Regole
- Leggi prima `CLAUDE.md`, `docs/STATUS.md`, `docs/FALLIMENTI.md`, `docs/INSTALLATO.md` e questo file.
- Branch: **`marco/notte-2026-10-14`** (`git switch marco/notte-2026-10-14 && git pull --rebase`; se non esiste in remoto, crealo da main). Mai `main`, mai merge, mai force-push.
- **Download e installazioni:** sono autorizzati solo QEMU, acpica, llvm/lld/nasm, EDK2 (già presenti) e la ISO già scaricata. **Niente** nuovi pacchetti, driver virtio, swtpm o binari Microsoft. Niente analisi di binari Microsoft.
- Tutti i dati vanno in `/Volumes/Arthur Fonzarelli/WatariBoot-data/` (usa `/tmp/watari-data` tramite `tools/env.sh`). Se il disco non è montato, scrivilo in PROGRESS e fermati.
- Ogni cosa nuova va registrata in `docs/INSTALLATO.md`, così si toglie facilmente.
- Esecuzione: delega a subagenti **Sonnet** (`model: "sonnet"`); tu (Opus) verifichi almeno 2 affermazioni chiave e **guardi gli screenshot** prima di dichiarare un risultato.
- **Documenta tutto, soprattutto i fallimenti**: ogni tentativo fallito va in `docs/FALLIMENTI.md` (formato F-NN), ogni risultato in `ralph/PROGRESS-notte.md`.
- Notifiche: **una sola PR** per tutta la notte. La prima esecuzione che ha qualcosa da committare la apre (titolo "Notte 2026-10-14: ..."); le successive fanno solo push. Nessun commento o assegnazione su GitHub.
- QEMU va sempre chiuso alla fine dell'esecuzione, tranne nel caso N2 (vedi sotto).
- Prima di finire: commit + push, aggiorna `ralph/PROGRESS-notte.md` e le righe "Notte" in `docs/STATUS.md`.

## Task (in ordine)
- [ ] **N1. HVF.** Diagnosi del blocco HVF (F-08):
  - controlla `tools/edk2-timer-imask.patch` e il log di eventuali tentativi in `WatariBoot-data/logs/`;
  - prova con il firmware EDK2 incluso in QEMU (`/opt/homebrew/share/qemu/edk2-aarch64-code.fd`), poi con una build RELEASE, poi con `gic-version=2`;
  - obiettivo: con HVF arrivare almeno all'avvio del CD di Windows. Se dopo un tentativo serio non si risolve, documenta in F-08 e passa avanti (si continua con TCG).
- [ ] **N2. Installazione automatica di Windows (senza interazione).**
  - Crea `tools/autounattend/autounattend.xml`:
    - lingua it-IT;
    - partizioni GPT sul disco NVMe;
    - bypass dei controlli TPM, Secure Boot e RAM tramite `HKLM\SYSTEM\Setup\LabConfig` in windowsPE;
    - account locale `watari` senza rete (BypassNRO);
    - **`bcdedit /set testsigning on` e il debug del kernel su seriale** nei `FirstLogonCommands` o in specialize.
  - Mettilo in una piccola ISO con `hdiutil makehybrid -iso -joliet` (incluso in macOS) e collegala come secondo CD USB.
  - Lancia l'installazione headless (HVF se N1 è riuscito, altrimenti TCG: con TCG può richiedere molte ore). Il disco `vm/win11.qcow2` sta sul disco esterno.
  - Ogni ~20 minuti fai uno `screendump` in `logs/screens/n2-*.png`.
  - Se l'esecuzione sta per finire e l'installazione è ancora in corso, **lascia QEMU acceso** e scrivi in PROGRESS il PID e lo stato; l'esecuzione successiva riprende da lì (controlla gli screenshot).
  - **Successo:** il desktop di Windows o la schermata di login nello screenshot.
- [ ] **N3. Correzioni di `tools/acpigen`** da `re/f0-acpi-requisiti-windows.md` (sezione discrepanze):
  - parking version 1 + indirizzo;
  - CPU di boot per prima;
  - GICR=1;
  - niente GICD segnaposto;
  - modello `qemu-virt` (letto dalle ACPI di QEMU: `-machine dumpdtb` oppure le tabelle dumpate da EDK2/UEFI shell `acpiview`);
  - generatori FADT e DBG2.
  Criterio: `make -C tools/acpigen test` verde.
- [ ] **N4. V1 (preparazione).** Driver DXE `WatariAcpiOverride` in un pacchetto `edk2-apple/WatariQemuPkg` (sorgenti nella repo, build con l'EDK2 sul disco esterno) che sostituisce MADT e GTDT con quelle di `acpigen`. Criterio: build riuscita e, se N2 è fatto, Windows avviato con le nostre tabelle (screenshot).
