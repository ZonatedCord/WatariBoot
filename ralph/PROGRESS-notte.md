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

## 2026-10-14 ~00:10: N1 RISOLTO (sessione principale)
- Causa: `ID_AA64PFR0_EL1.GIC=0` sotto HVF, per cui EDK2 sceglieva il driver GICv2 (dettagli in F-08). Patch: `tools/edk2-hvf-gicv3.patch`, applicata da `build-edk2.sh`.
- **Verificato da Opus:** screenshot `win-hvf-180s.png` = Setup di Windows 11, "Seleziona le impostazioni della lingua", con HVF.
- Corretto anche `run-qemu-windows.sh` (array vuoto con `set -u` su bash 3.2).

## N2: installazione automatica Windows 11 (HVF + unattend) - avviata 00:14
- Stato alle 00:23: **in corso**, senza domande. Il testo di Setup è "Installazione di Windows 11 in corso", 13% (00:18) e poi 22% (00:23). L'unattend è stato letto: partizionamento e chiave accettati (**verificato** dagli screenshot).
- QEMU PID **1173** (file `run/n2-qemu.pid`), headless, HVF, monitor `run/qemu-mon.sock`, seriale `logs/n2-serial.log`, comando in `logs/n2-cmd.txt`. Il disco `vm/win11.qcow2` era già quasi vuoto (384 KB), non è stato ricreato.
- Screenshot: `logs/screens/n2-boot.png` (00:18, 13%), `logs/screens/n2-0023.png` (00:23, 22%) in `/tmp/watari-data/` (= WatariBoot-data).
- Come controllare: `echo "screendump /tmp/watari-data/logs/screens/n2-HHMM.ppm" | nc -U /tmp/watari-data/run/qemu-mon.sock -w1`, poi `sips -s format png ... --out ...png`.
- Finita quando: schermata di login/desktop dell'utente `watari` (password `watari`, solo VM) e `C:\watari-ok.txt` creato dai FirstLogonCommands (dalla VM; non c'è rete). Riavvii multipli attesi. Se resta su "Press any key" dopo un riavvio, non premere nulla: parte dal disco.
- Se si rompe: ferma con `echo quit | nc -U /tmp/watari-data/run/qemu-mon.sock -w1` e documenta lo screenshot in `docs/FALLIMENTI.md`.

## 2026-10-14 00:25: N2 in corso (verificato da Opus)
- Lo screenshot `logs/screens/n2-0023.png` mostra "Installazione di Windows 11 in corso... Completamento operazione: 22%" con HVF, senza nessuna domanda: l'unattend funziona. QEMU ha PID 1173 (in `run/n2-qemu.pid`), è vivo e staccato dalla sessione.
