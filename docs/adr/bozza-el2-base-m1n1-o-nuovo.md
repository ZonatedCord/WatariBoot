# ADR (bozza): Watari EL2 è uno strato nuovo; m1n1 resta solo il loader

- Data: 2026-10-14 · Stato: **proposto** (da approvare da Marco). Il numero si assegna al merge.
- Collegamenti: `docs/adr/bozza-piano-b-strato-el2.md` (decisione di fondo), `docs/design/watari-el2/ARCHITECTURE.md` §2, `docs/adr/bozza-el2-licenza.md`, ADR-0007 (strada indipendente da NTASP).

## Contesto
Il piano B richiede uno strato a EL2 con vGICv3, PSCI, timer come interrupt e SGI→IPI. Esistono tre basi possibili: l'hypervisor di m1n1, il fork NTASP `m1n1_windows`, o codice nuovo. Fatti verificati il 2026-10-14:
- m1n1 `hv` ha stage-2, trap MMIO, più vCPU e timer guest via FIQ virtuale, ma **non ha un vGIC**; ogni eccezione non gestita passa al proxy Python e il guest si ferma finché l'host risponde; il percorso eccezioni è lento e single-thread [V: `re/pianoB-fattibilita.md` §2].
- m1n1 dalla 2.0.0 è **GPL-2.0-or-later** (MIT fino a 1.9.9): README e file `src/hv/hv_vm.c`, `src/main.c` [V].
- NTASP `hv_vgic.c` è incompleto (pending/active mancanti, 1 core, SGI "TODO"), ha un solo manutentore attivo e dichiara di ispirarsi a KVM [V: `re/ntasp-analisi.md`, `pianoB-fattibilita.md` §3]; `re/ntasp-audit-qualita.md` giudica "da riscrivere" il lato UEFI.
- Un nucleo che gira solo su hardware Apple non si può testare nel banco QEMU TCG con `virtualization=on`, che è l'unico ambiente di sviluppo senza host di debug.

## Decisione
Si scrive **Watari EL2 come progetto nuovo** (C freestanding + assembly AArch64, directory `el2/`), con:
- un **nucleo portabile** (vGIC, PSCI, timer, stage-2, trap) senza dipendenze hardware, testabile su host;
- **backend** sostituibili per la piattaforma (`qemu-virt`, `t8103`) e per le sorgenti di interrupt/IPI/timer;
- **m1n1 come loader** (iBoot → m1n1 → Watari EL2 come payload in stile Linux `Image` con FDT in `x0`), non come hypervisor;
- **m1n1 hv come strumento di misura** in sola lettura (osservare come macOS programma AIC/timer, verificare la semantica di registri IMP-DEF), mai nel percorso di produzione.

## Conseguenze
- Positivo: testabilità (L0-L3 senza MacBook), autonomia (nessun proxy), confini chiari, licenza controllata, coerenza con ADR-0007.
- Negativo: si riscrivono stage-2 e trap (stima 600-900 righe [I]) che m1n1 ha già; il vGIC si scrive comunque (2.500-4.000 righe [I]).
- Il formato di consegna da m1n1 e lo stato dei core secondari sono **assunzioni da verificare** in M3a (R-10).
- Si crea un nuovo albero `el2/`: aggiornare "Struttura" di `CLAUDE.md` al merge.
- Il vincolo di `CLAUDE.md` "niente hypervisor" diventa "dual boot, niente macOS sotto, strato EL2 minimo consentito" (già deciso nella bozza del piano B).

## Alternative
1. **Estendere m1n1 hv aggiungendo il vGIC.** Scartata: GPL-2.0-or-later (vincola la licenza dell'immagine), proxy e percorso lento da ristrutturare (2-3 settimane [I, PB §2]) più un vGIC comunque da scrivere, nessun test automatico, impossibile da eseguire in QEMU.
2. **Fork di NTASP `m1n1_windows`.** Scartata: incompleto, stato di licenza incerto (MIT dichiarato su codice derivato da m1n1 GPL e "ispirato" a KVM), contraddice ADR-0007, dipendenza da un manutentore.
3. **Linux/KVM (Asahi) come host con Windows guest e periferiche in passthrough.** Scartata: un sistema operativo sotto Windows contraddice "dual boot, niente OS sotto"; resta come **controllo sperimentale** perché Windows 11 gira già come guest KVM su Asahi [V: `windows-11-vm.md`, via PB §0].
4. **Rust `no_std`.** Scartata: richiede toolchain da installare (regola del backup) e non c'è esperienza nel repository; il C con test host e `-Werror` basta per le dimensioni previste. Riconsiderabile per il solo vGIC.
5. **Strato come driver di EDK2 (UEFI a EL2).** Scartata: EDK2 deve girare a EL1 sotto lo strato; mescolare i due rende impossibile testare lo strato da solo.
