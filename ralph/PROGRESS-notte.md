# Notte 2026-10-14: diario

Ogni esecuzione notturna aggiunge qui data e ora, il task, cosa ha fatto, i risultati verificati e i fallimenti (questi anche in docs/FALLIMENTI.md).

## 2026-10-14 00:15, sessione principale (prima dei task notturni)
- **N2, preparazione completata** (non ancora eseguita):
  - `tools/autounattend/autounattend.xml` + README, `tools/make-unattend-iso.sh`, opzione `--unattend` in `tools/run-qemu-windows.sh`;
  - ISO `WATARI_DATA/vm/unattend.iso` (900 KB).
  - **Verificato da Opus:** `xmllint` OK; 6 componenti con `processorArchitecture="arm64"`; la ISO esiste.
  - Ipotesi: chiavi LabConfig/BypassNRO (non documentate da Microsoft), GVLK di Windows 11 Pro solo per scegliere l'edizione.
  - Credenziali della VM di test: `watari` / `watari`.
- **N1, in corso:** un agente della sessione principale stava ancora diagnosticando HVF (log `WatariBoot-data/logs/dbg-*.log`, `tools/edk2-timer-imask.patch`). Se non trovi un risultato in questo diario, rifai N1 da capo.
- **Prossimo:** lanciare N2 con `tools/run-qemu-windows.sh --unattend` (HVF se N1 è riuscito, altrimenti `--tcg`).
