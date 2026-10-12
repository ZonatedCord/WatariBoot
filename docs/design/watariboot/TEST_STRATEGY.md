# TEST_STRATEGY

- Progetto: Watari Boot · Data: 2026-10-12 · Versione: 0.1 · Stato: draft
- Documenti collegati: [PRD](PRD.md) · [ARCHITECTURE](ARCHITECTURE.md) · [INTERFACES](INTERFACES.md) · [RISKS](RISKS.md)
- Marcatura: `[verificato: fonte]` / `[ipotesi]`.

## 1. Livelli (piramide)
1. **Unit su host macOS**: logica pura (parser ADT, generatore ACPI, checksum tabelle, layout mailbox, logica AIC con registri simulati, DART/SART con modello di memoria). Compilata in C/C++ su macOS, senza firmware ne' Windows.
2. **Validazione tabelle**: le tabelle ACPI generate vengono ricompilate/decompilate e controllate con strumenti ACPI standard (es. `iasl`) [ipotesi: strumento adatto, da pinnare], checksum, lunghezze, riferimenti incrociati.
3. **QEMU** (Fase 1): firmware EDK2 con MADT senza GIC, CSRT e parking protocol; Windows ARM64 con debug kernel. [ipotesi] QEMU non emula AIC: servira' un dispositivo di prova (controller finto) o stub; limiti di QEMU da documentare. QEMU e' solo banco di prova (CLAUDE.md).
4. **Hardware** (Fasi 2-5): test manuali guidati con evidenza (foto, video, log del debug host).
5. **Benchmark** (Fase 5): Geekbench/Cinebench, confronto con macOS.

I test hardware sono i piu' costosi e rischiosi: ogni passo sul disco richiede backup e conferma esplicita (CLAUDE.md, NFR-5).

## 2. Riproducibilita'
- Toolchain pinnate (versioni EDK2, compilatore, WDK, QEMU, Ghidra) registrate in `tools/` con hash; build in script/container; nessuna dipendenza da "macchina dello sviluppatore".
- Input Microsoft (ntoskrnl, PDB, ISO) fuori dal repo in `/Volumes/Arthur Fonzarelli/WatariBoot-data/` (disco esterno; va collegato); nel repo solo nome file, versione, SHA-256 [proposta].
- Seed fissi per qualsiasi test con casualita'; fixture ADT: blob di ADT t8103/j293 catturato da dispositivo e conservato [ipotesi: licenza/privacy del blob da valutare; se non committabile, generatore sintetico].
- Esito ripetuto: ogni criterio di uscita richiede N ripetizioni consecutive (N nel PRD).

## 3. Test per fase

### Fase 0 (solo Mac)
| Cosa | Come | Uscita |
|---|---|---|
| Verdetto si'/no | Analisi Ghidra di ntoskrnl/HAL con PDB pubblici | Report in `re/` con indirizzi/funzioni/pseudocodice per ogni conclusione |
| Revisione | Opus controlla che ogni affermazione abbia evidenza | Nessuna conclusione senza riferimento |
| Domande obbligatorie | Registrazione controller non-GIC; caricamento via CSRT; gestione FIQ; caso BCM2836; MADT senza GIC ammessa?; come Windows invia IPI di avvio parking | Ognuna con risposta si'/no/non determinabile |
Criterio di uscita: documento completo; decisione di prosecuzione o piano B presa dall'utente.

### Fase 1 (QEMU)
- Unit: M-ACPI-GEN con ADT sintetica; tabelle validate.
- Integrazione: avvio Windows ARM64 in QEMU con debug kernel (KDNET o seriale) [ipotesi: trasporto funzionante in QEMU].
- Criterio: log WinDbg che mostrano la nostra estensione caricata e invocata (breakpoint). Ripetuto 3 volte. Se Windows non raggiunge quel punto: raccogliere bugcheck, analisi, tornare a Fase 0.

### Fase 2 (hardware)
- Unit: parser ADT con ADT reale di j293; layout mailbox.
- Hardware: m1n1 -> EDK2 -> shell UEFI; comando che elenca i core; verifica che macOS riavvii.
- Criterio: shell UEFI con 8 core visibili; 3 avvii; macOS avviabile.
- Pre-requisito: debug host presente (Pi 5).

### Fase 3
- Unit: logica AIC (mask/unmask/IPI) con registri simulati; invarianti: nessun accesso a registri FIQ.
- QEMU: regressione della Fase 1.
- Hardware: boot da USB; seriale/framebuffer; misurare dove si ferma il boot (bugcheck code registrati).
- Criterio: kernel nativo fino al caricamento dei driver, 3 avvii su 3.
- Edge case: FIQ spuria (contatore + log), timer che non arriva (watchdog), core che non si sveglia dal parcheggio, interrupt storm.

### Fase 4
- Unit/host: miniport ANS con modello di coda; SART/DART con memoria simulata; casi limite (dimensioni non multiple di 16K, allineamento).
- Hardware: installazione su SSD, riavvio 5 volte, integrita' file (hash di file di test), stress I/O.
- Criterio: desktop con USB tastiera/mouse; installazione da SSD.
- Rischio dati: usare solo la partizione Asahi dedicata; backup e conferma.

### Fase 5
- PEP/`_CPC`: verificare che frequenza e carico siano coerenti con i contatori; test termici; sleep/resume ripetuto (N cicli).
- SPI HID: registrazione di eventi di input; rilevamento perdite.
- Benchmark: Geekbench/Cinebench, almeno 3 ripetizioni, temperatura e alimentazione registrate, confronto con macOS.

### Fase 6
- Verifica a dati: stesso codice, ADT di altro SoC; diff che mostra solo dati nuovi.

## 4. TDD: primo test
Primo test unit (Fase 1): dato un `ADT` sintetico con 8 CPU (4 P, 4 E), `AcpiGenBuildAll()` produce una MADT con 8 GICC valide (checksum corretto, UID unici, parked address allineati) e nessuna struttura GICD. Il test deve fallire prima dell'implementazione.

## 5. Criteri di uscita generali
Una fase e' chiusa solo se: criterio del PRD soddisfatto con evidenza archiviata in `docs/sessions/`; nessun rischio rosso nuovo non mitigato; STATUS aggiornato; macOS ancora avviabile (da Fase 2).
