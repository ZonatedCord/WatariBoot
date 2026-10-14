# Gate B: loop autonomo (Ralph) dal 2026-10-14

Marco è via: Opus lavora da solo con un Ralph loop. Branch unico: **`marco/f11-acpi`** (mai main, mai merge, mai force-push). **Una sola PR** alla fine, verso main, senza merge.

## Regole (oltre a CLAUDE.md)
- Esecuzione delegata a subagenti **Sonnet** (`model: "sonnet"`). Opus verifica di persona (screenshot con Read, test rieseguiti) prima di spuntare.
- Download e installazioni: **nessuno nuovo** (solo QEMU, iasl, llvm, EDK2, ISO e disco Windows già presenti). Niente analisi di binari Microsoft.
- Disco di Windows sempre con `snapshot=on` nei test. **Mai due QEMU sullo stesso disco.** Prima di lanciarne uno, `pgrep -fl qemu-system`: se c'è un QEMU di un agente ancora attivo, aspetta.
- Dati solo in `/tmp/watari-data` (= disco esterno). Ogni cosa nuova va in `docs/INSTALLATO.md`.
- **Ogni fallimento** in `docs/FALLIMENTI.md` (formato F-NN), ogni risultato in `ralph/PROGRESS-gateb.md`.
- Commit e push dopo ogni task. Niente commenti o assegnazioni su GitHub.
- **Budget:** i limiti di utilizzo sono vicini. Agenti con prompt mirati, al massimo 2 in parallelo; fermati se un task fallisce due volte.

## Task (in ordine; un task per iterazione, quelli indipendenti si possono fare in parallelo)
- [ ] **G1 · F-11 / V1.** Windows arriva al desktop con le nostre MADT+GTDT (`acpigen -m qemu-virt` via `WatariAcpiOverride`). Un agente ci sta già lavorando (iniziato alle 09:05); **non rilanciarlo** se `re/f11-confronto-acpi.md` è in scrittura o c'è un QEMU attivo. Criterio: screenshot `logs/screens/v1-ok.png` con il desktop.
- [ ] **G2 · V2.** Come V1 ma con la MADT **senza GICD** (solo GICC + GICR). Esito atteso: in un caso o nell'altro, documentare. Se Windows non parte, catturare il bugcheck (screenshot e seriale) → voce F-NN. Criterio: esito documentato con prove.
- [ ] **G3 · V6.** Il timer consegnato come FIQ. Metodo "A" da `ESPERIMENTO-F0.md` §4.4: un driver DXE (in `WatariQemuPkg`) che dopo `ArmGicDxe` mette i PPI 27 e 30 in Group 0, con `kernel-irqchip=off` sotto HVF. Prima verifica (seriale e trace di QEMU) che il timer arrivi davvero come FIQ, poi avvia Windows. Criterio: esito (Windows parte e l'orologio avanza / blocco / bugcheck) documentato con prove. Se Windows riporta il timer in Group 1, documentalo e prepara la patch "B" a QEMU **senza compilarla** (compilare QEMU richiede download: chiedi a Marco).
- [ ] **G4 · V3 (preparazione).** Parking protocol reale: codice di parcheggio in EDK2 (`WatariQemuPkg`) per i core secondari, MADT con parking v1, FADT senza PSCI. Criterio: build riuscita e, se possibile, un test su Windows con 4 CPU. Esito documentato.
- [ ] **G5 · Chiusura.**
  - aggiornare `docs/STATUS.md`, `docs/sessions/2026-10-14-marco-01.md` e il contatore dei token;
  - un **riepilogo per Marco** in `ralph/PROGRESS-gateb.md`;
  - aprire **una** PR `marco/f11-acpi → main` (senza merge);
  - poi emettere la promise.
