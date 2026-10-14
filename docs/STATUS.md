# Stato del progetto

**Aggiornato:** 2026-10-13 (sessione marco-01, notte)
**Fase corrente:** 0, lo spike sul HAL di Windows ARM64 e i controller di interrupt non-GIC

## Traguardi
- [x] Piano approvato (`docs/PLAN.md`)
- [x] Repository creato
- [x] Nome: **Watari Boot**, con sottotitolo fisso "Windows nativo su Apple Silicon" (ADR-0006)
- [x] Sessione di progettazione (`docs/design/watariboot/`, ADR da 0001 a 0005)
- [x] Analisi e audit di NTASP: strada indipendente (ADR-0007), bug segnalato con la issue NT-for-ASi/apple_silicon_platforms_mu#23
- [x] Gate A, primo giro: A3, A5, A6 (PR #15). Scoperta: il timer del M1 è solo FIQ, vedi la bozza di ADR "strategia timer FIQ"
- [x] Gate A, secondo e terzo giro: A1, A2, A4, A7–A11 (PR #22 e la PR di questo giro). **Gate A completo.**
- [x] Pista `Wack0/NtHalExts` letta (README): l'API ufficiale per i controller di interrupt è stata **rimossa** da Microsoft prima di Windows 8; l'estensione funziona cercando funzioni interne. Vedi `re/f0-nthalexts-pista.md`.
- [ ] Studiare il codice di `halextbcm2709.c`/`halextapi.c` (solo lettura, senza copiare gli header DDK).
- [x] **Gate B, B1+B2** (2026-10-13): QEMU 11.1.2, EDK2 `edk2-stable202608` compilato (ArmVirtQemu AARCH64, CLANGDWARF) e avviato in QEMU fino al Boot Manager UEFI. Elenco di cosa è installato e come rimuoverlo: `docs/INSTALLATO.md` + `tools/uninstall-watari.sh`.
- [x] **V0 (TCG):** il Setup di Windows 11 ARM64 (ISO ufficiale it-it build 26300, SHA-256 in `WatariBoot-data/iso/SHA256.txt`) gira in QEMU sul **nostro** EDK2. Screenshot: `WatariBoot-data/logs/screens/tcg-c1.png`. Script: `tools/run-qemu-windows.sh`.
- [ ] **HVF:** il nostro EDK2 si blocca su "Start boot option" (`[Bds]BdsWait(5)`). Diagnosi in corso (un agente stava provando il firmware incluso in QEMU, una build RELEASE, GICv2). **Da riprendere qui:** controllare `git status` sul branch locale `marco/gate-b-v0` per eventuali modifiche lasciate dall'agente.
- [ ] **Notte 2026-10-14:** lavoro autonomo pianificato secondo `ralph/NOTTE.md` (N1 HVF, N2 installazione automatica, N3 acpigen, N4 V1), sul branch `marco/notte-2026-10-14`, diario in `ralph/PROGRESS-notte.md`. **N1 e N2 fatti** (Windows 11 installato in modo automatico con HVF, desktop verificato); N3 fatto; N4 build ok ma Windows non parte con le nostre tabelle (F-11), da lì si riparte.
- [ ] Poi: installazione di Windows nella VM (valutare il disco interno per la velocità), quindi V1 e V6.
- [ ] Fase 0: verdetto sì/no sull'estensione HAL per l'AIC
- [ ] Fase 1: prototipo in QEMU
- [ ] Fase 2: EDK2 sul MacBook
- [ ] Fase 3: kernel di Windows nativo
- [ ] Fase 4: desktop
- [ ] Fase 5: potenza piena
- [ ] Fase 6: tutti gli Apple Silicon

## Blocchi e necessità
- ✅ **Backup Time Machine confermato** (2026-10-12, completato alle 19:06). Destinazione: volume cifrato "Time Machine" sul disco esterno, con quota di 1 TB; backup orari automatici. Escluso `~/Library/Containers/com.apple.Safari/Data/Library/WebKit/WebsiteData` (file con nome troppo lungo che bloccava il backup). **La regola resta:** prima di ogni download si chiede comunque conferma all'utente.
- ✅ **Spazio per il Gate B (75–115 GB):** si usa il disco esterno, `/Volumes/Arthur Fonzarelli/WatariBoot-data/` (circa 450 GB liberi oltre la quota di Time Machine). Il disco va collegato; la cartella si crea al primo uso.
- Fase 0: il task sui binari Windows è stato fermato da un filtro di sicurezza. Serve decidere come procedere.
- Restore DFU: ✅ risolto. Il socio dell'utente ha un Mac (servono Apple Configurator e un cavo USB-C). Prima va tentato il **revive**, che non cancella i dati; il **restore** cancella tutto.
- Debug host (dalla Fase 2): consigliato un portatile o mini PC Windows x86 usato (m1n1 + WinDbg). Alternativa: Raspberry Pi 5 da 8 GB (minimo Pi 4 Model B da 4 GB) con cavo USB-A→USB-C, ma per WinDbg serve comunque Windows.
- Serve un **debug host** (un Raspberry Pi 5 o un vecchio portatile) dalla Fase 2.

## Verdetto preliminare (A7, prima della pista halextbcm2709)
- **Aggiornato dopo NtHalExts:** nativo pulito ~5%, nativo con funzioni interne ~20%, piano B ~50%, altro ~25% (`re/f0-nthalexts-pista.md`). Le stime di A7 sono in `re/f0-verdetto-preliminare.md`.
- La soglia del piano B la decide l'utente (bozza ADR `bozza-esito-fase0-preliminare.md`).

## Per Ciani (se lavora oggi)
Niente issue nuove, per non mandare notifiche. Scegli uno di questi, su un branch `ciani/...` e con **una sola PR** alla fine:
1. **Studio di `halextbcm2709`** (solo lettura su GitHub): come registra il controller di interrupt nell'HAL di NT (callback, strutture, IPI). Output: una sezione in `re/f0-nthalexts-pista.md`.
2. **Correzioni di `tools/acpigen`** da A2 (`re/f0-acpi-requisiti-windows.md`, sezione discrepanze): parking version 1 + indirizzo, CPU di boot per prima, GICR=1, niente GICD segnaposto; poi i generatori FADT e DBG2. Criterio: `make -C tools/acpigen test` verde.

## Da decidere (non bloccanti)
- **Organizzazione GitHub:** facoltativa e rimandabile. Il nome è da scegliere (`watari` è occupato, `watariboot` è libero). Va creata dal browser, poi si trasferisce la repo (la storia si conserva e il vecchio indirizzo viene reindirizzato). Riservare il nome prima di qualsiasi annuncio pubblico.

## Da dove si riparte
Vedi `docs/sessions/2026-10-12-marco-01.md` (ultime voci) e `ralph/PROGRESS-fase0.md`. Prima azione: analizzare `halextbcm2709` (lettura su GitHub, senza copiare gli header DDK).- Fallimenti e lezioni: `docs/FALLIMENTI.md` (letto da tutte le sessioni).
