# Watari Boot: istruzioni per Claude

Obiettivo: dual boot **nativo** di Windows ARM64 su Apple Silicon. Si parte da un MacBook Pro 13" M1 (t8103, j293) e poi si generalizza a tutti gli Apple Silicon. Il piano completo è in `docs/PLAN.md`.

## ⚠️ BACKUP OBBLIGATORIO (regola assoluta dell'utente)
**Prima di scaricare o installare QUALSIASI cosa sul Mac** (toolchain, test, ISO, pacchetti brew, repo grandi, qualunque cosa), fermarsi e **ricordare all'utente di fare il backup completo** (Time Machine). Si procede solo dopo che l'utente ha **confermato esplicitamente** che il backup è fatto. Nessuna eccezione, anche per download piccoli.

## Workflow (obbligatorio)
- **Opus = capo.**
  - Ragiona, progetta, scompone il lavoro, verifica i risultati e parla con l'utente.
  - Non riferisce mai il risultato di un agente senza averlo controllato.
- **Sonnet = esecutori.**
  - Agent tool con `model: "sonnet"`.
  - Eseguono task ben definiti e rispondono a Opus.
  - Task indipendenti vanno lanciati in parallelo.
- L'utente parla solo con Opus.
- Per un nuovo sottosistema serve prima una sessione di progettazione con `morasca-agent` (`model: "sonnet"`), con output in `docs/design/`.

## Documentazione (a ogni fine sessione, senza eccezioni)
1. Scrivere `docs/sessions/AAAA-MM-GG-<autore>-NN.md` (un file per autore, per evitare conflitti): obiettivo, cosa è stato fatto, decisioni (con link agli ADR), risultati verificati, problemi aperti, prossimi passi.
2. Aggiornare `docs/STATUS.md`: fase corrente, traguardi raggiunti, blocchi, da dove si riparte.
3. Ogni decisione architetturale va in una bozza `docs/adr/bozza-<titolo>.md`; il numero si assegna al merge.
4. Aggiornare il contatore dei token e dei costi: `python3 -I tools/token-usage.py --autore <marco|ciani>`. Aggiorna `docs/costs/<autore>.json` e la tabella nel README.
5. Commit e push **su un branch**, poi PR verso `main`. Mai push diretto su `main` (vedi `docs/CONTRIBUTING.md`).

## Collaborazione
- Lavorano in due (Marco + Ciani) più Ralph. Le regole su branch, PR, issue e file "caldi" sono in `docs/CONTRIBUTING.md` e vanno rispettate sempre.
- Ralph loop: solo sul proprio branch `ralph/...`, una sola istanza per volta, istruzioni in `ralph/README.md`.

## Vincoli
- **Nativo**: niente hypervisor, emulatore o VM come soluzione finale, salvo decisione esplicita dell'utente (vedi il piano B in `docs/PLAN.md`). QEMU si usa solo come banco di prova.
- **Nessun binario o codice Microsoft nel repository**: ntoskrnl, PDB, ISO e dump vanno in `/Volumes/Arthur Fonzarelli/WatariBoot-data/` (disco esterno; va collegato), fuori dal repository. Gli appunti di reverse engineering vanno in `re/`, scritti con parole nostre e citando gli indirizzi, senza incollare grandi blocchi di codice decompilato.
- Il codice Asahi (MIT / GPL-2.0 / dual) si riusa rispettando la licenza, con un header di provenienza.
- Ogni affermazione tecnica va marcata come **verificato** (con evidenza) o **ipotesi**.
- Sicurezza dell'hardware: macOS e recovery non si toccano. Le modifiche al disco passano solo dall'installer di Asahi, e servono un backup e la conferma dell'utente prima di ogni operazione sul disco o sul boot.

## Struttura
- `re/`: reverse engineering di Windows ARM64 (HAL, interrupt, estensioni HAL)
- `edk2-apple/`: piattaforma EDK2 AppleSiliconPkg
- `halext-aic/`: estensione HAL per AIC e timer
- `drivers/`: driver Windows (ANS StorPort, DART, PEP, SPI HID)
- `tools/`: build, immagini, QEMU
- `docs/`: piano, stato, sessioni, ADR, design
