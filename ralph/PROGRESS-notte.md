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

## 2026-10-14 02:30-02:45: N2 RIUSCITO (esecuzione notturna 1)
- Ripresa: QEMU PID 1173 vivo dopo 2h15 di HVF. Lo screenshot `n2-0230.png` era **tutto nero**, il disco non scriveva più da minuti e la seriale era ferma dalle 01:29: sembrava un blocco.
- Era invece lo **schermo spento per inattività** di Windows (nessun display driver reale, ramfb). Un `sendkey shift` dal monitor lo ha risvegliato: `n2-0236.png` (copia `n2-SUCCESSO-desktop.png`) mostra il **desktop di Windows 11 con Start aperto e utente `watari`**, ora 00:34 della VM, 14/10/2026. **Verificato da Opus guardando lo screenshot.**
- Non verificato: l'esistenza di `C:\watari-ok.txt`, testsigning e debug seriale (non c'è accesso alla VM). Il file seriale `n2-serial.log` contiene solo il log di EDK2 (nessun output del kernel debugger, ipotesi: serve la verifica in V1/N4).
- Spenta con `system_powerdown`: chiusura pulita, QEMU terminato. `vm/win11.qcow2` ≈ 14 GB = Windows installato e pronto da riavviare.
- Durata totale installazione con HVF: circa 1h30-2h (da 00:14 a ~01:30 desktop pronto).
- **N3 delegato a un agente Sonnet** (acpigen), in corso.

## 2026-10-14 ~02:55: N3 FATTO (agente Sonnet, verificato da Opus)
- `make -C tools/acpigen clean && make test` rifatto da Opus: **TUTTI I TEST OK** (7 test). Nuovi: FADT (HW_REDUCED_ACPI, PSCI=0), DBG2 (PL011), modello `qemu-virt` (CLI `-m`), parking v1 con pagine distinte, boot CPU per prima, GICR=1, niente GICD segnaposto.
- Verificato dall'agente con `-machine dumpdtb`+`dtc`: MPIDR 0..3, GICD 0x08000000, GICR 0x080A0000, timer GSIV 29/30/27/26, PL011 0x09000000.
- **Ipotesi aperte:** layout FADT/DBG2/GICR Structure scritto a memoria dalla spec ACPI 6.5 (`Acpi65.h` non confrontato); flag GTDT qemu-virt solo ALWAYS_ON (non active-low come chiesto: il DT dice active-high); parking con PSCI=0 su qemu-virt anche se QEMU usa PSCI.
- Passi falsi dell'agente: `find /` troppo ampia (nessun danno), un `sed` fallito e rifatto.
- **Prossimo: N4** (driver DXE WatariAcpiOverride in `edk2-apple/WatariQemuPkg`).
