# RISKS: Watari EL2

- Progetto: Watari Boot, sottosistema Watari EL2 · Data: 2026-10-14 · Versione: 0.1 · Stato: draft
- Documenti collegati: [PRD](PRD.md) · [ARCHITECTURE](ARCHITECTURE.md) · [INTERFACES](INTERFACES.md) · [TEST_STRATEGY](TEST_STRATEGY.md)
- Scala: probabilità (P) e impatto (I) da 1 (basso) a 5 (alto); punteggio = P×I. Le valutazioni sono **[ipotesi]** salvo dove indicato.

## 1. Registro dei rischi
| ID | Rischio | P | I | P×I | Mitigazione | Segnale di allarme |
|---|---|---|---|---|---|---|
| R-01 | **Errata SEIS del M1**: il vGIC può generare SError fisici in risposta ad accessi del guest ai registri GIC; un trap non gestito è un panic dell'hypervisor | 4 | 5 | 20 | Modo `TRAP_EMULATE` di default con variante A (trap massimo); `HCR_EL2.AMO=1` per intercettare i SError; classificazione e log; `SEIS` nascosto in `ICC_CTLR_EL1`; riduzione a variante B solo dopo `serror-probe` | qualunque SError nel log di M3a |
| R-02 | **Fedeltà del vGIC** verso Windows: un registro sbagliato porta a hang silenzioso (F-14) | 3 | 5 | 15 | Oracolo (TEST_STRATEGY §4), colonna "fedeltà" in INTERFACES, watchdog dello strato con dump, registri "Critici" rivisti da Opus | schermo identico per > 60 s senza voci di log |
| R-03 | **`HCR_EL2.E2H` bloccato a 1** sul M1: formato diverso di registri (CNTHCTL, TTBR), accesso al contesto guest con `*_EL12`/`*_EL02`; errori sottili che QEMU non mostra se non si riproduce E2H=1 | 4 | 3 | 12 | Lo strato imposta E2H=1 anche in QEMU; test L0 sugli accessori `_EL12`; revisione dei bit di `CNTHCTL_EL2` con il manuale in M0 | comportamento diverso tra QEMU e M1 su timer |
| R-04 | **Percorso FIQ del timer non testabile in QEMU**: semantica reale di `VM_TMR_FIQ_ENA_EL2`, di ISTATUS/IMASK e dei livelli solo su hardware | 4 | 4 | 16 | Test L0 con sysreg finti; autotest `timer-fiq` in M3a; osservazione con m1n1 hv come strumento (sola lettura); percorso alternativo previsto: mascherare con `IMASK` (visibile al guest) se il bit IMP-DEF non si comporta come atteso | tempesta di FIQ, tick persi |
| R-05 | **Prestazioni del `TRAP_EMULATE`**: se Windows usa `ICC_PMR_EL1` per l'IRQL e `TC` lo intercetta, ogni cambio di IRQL è un trap (migliaia al secondo) | 3 | 4 | 12 | Misurare in M0/M1 la frequenza di `PMR`, `IAR`, `EOIR` dal trace (TEST_STRATEGY §4); variante B (niente `TC`) se la prova di sicurezza lo permette; soglie in NFR-1 | > 300 uscite/s/vCPU a riposo |
| R-06 | **Abort di stage-2 con `ISV=0`** (istruzioni con writeback, load/store pair, accessi del compilatore a regioni GIC): nessuna informazione sul registro | 2 | 4 | 8 | Panic con dump alla prima occorrenza; se osservato, decoder minimo delle istruzioni (via `AT S1E1R` + lettura istruzione) come modulo aggiunto; F-12 mostra il sintomo in QEMU/HVF | `PANIC_ISV0` |
| R-07 | **Debug senza host**: sul MacBook lo strato non ha altro canale che framebuffer e disco; un hang in EL2 non lascia tracce | 4 | 4 | 16 | Scala: (1) ring log in RAM riservata + console sul framebuffer di iBoot (`simple-framebuffer`) con dump al panic; (2) watchdog software per vCPU e reset controllato; (3) `WEL2_LOG` da EDK2 su disco/ESP (file di log) [I]; (4) **host di debug consigliato per M3** (secondo computer, già nei blocchi di STATUS: Pi 5 o portatile). Il ring in RAM potrebbe non sopravvivere a un reset [I]: da provare | nessuna traccia dopo un blocco |
| R-08 | **Licenze**: m1n1 ≥ 2.0.0 è GPL-2.0-or-later (MIT fino a 1.9.9); KVM è GPL-2.0; NTASP dichiara MIT ma deriva da entrambi | 3 | 4 | 12 | Strato nuovo scritto da specifica; nessun codice copiato; processo "leggi, chiudi, scrivi" e scansione di similarità; `bozza-el2-licenza.md`; ricorso eventuale a licenza GPL per l'immagine se si copiasse codice m1n1 | un file con struttura identica a una fonte GPL |
| R-09 | **Attributi MMIO**: Windows mappa i dispositivi come nGnRE; su Apple servono scritture non-posted | 3 | 4 | 12 | Stage-2 Device-nGnRnE su tutto l'MMIO; autotest `s2-attr` | SError su scrittura MMIO |
| R-10 | **Assunzioni sull'handoff di m1n1** (EL2, `x0`=FDT, spin-table, stato delle FIQ) non verificate | 3 | 4 | 12 | Verifica in M3a leggendo `kboot.c`/`smp.c` e con prova su hardware; contratto in INTERFACES §2 con campi isolati nel backend `plat-t8103` | handoff diverso da quanto atteso |
| R-11 | **DMA verso la regione dello strato**: il M1 ha DART e non SMMU; un driver Windows sbagliato potrebbe scrivere nella memoria dello strato | 2 | 5 | 10 | Regione tolta dalla mappa di memoria data a EDK2/Windows; v2: programmare i DART per negare la regione; documentare il limite in v1 | corruzione di dati dello strato |
| R-12 | **Windows richiede più del previsto**: ITS/LPI, Hyper-V, `GICD_TYPER` particolari, SPI > 960 | 2 | 4 | 8 | V1 mostra GICv3 senza ITS sufficiente su QEMU [V: F-11]; fallback documentato: aggiungere moduli (punto di estensione) | hang con MADT diversa dalla V1 |
| R-13 | **TCG lento**: boot di Windows in decine di minuti, ciclo di sviluppo lento | 4 | 2 | 8 | Test L0/L2 prima di L3; payload di prova; soglia 1,5× rispetto alla baseline TCG | iterazioni > 1 h |
| R-14 | **SMP e deadlock** (lock, IPI di notifica perse) | 3 | 4 | 12 | Ordine dei lock fissato, watchdog dei lock, test `sgi-ipi` N×(N-1), test di stress con IPI incrociate | `PANIC_LOCK_TIMEOUT` |
| R-15 | **Scarsa maturità delle fonti**: NTASP `hv_vgic.c` incompleto e di qualità non provata; nessun Windows documentato sopra un hypervisor su M1 con periferiche reali | 3 | 3 | 9 | Le fonti sono solo riferimento; fedeltà verificata col nostro oracolo | — |
| R-16 | **Durata**: le stime (8-12 settimane QEMU, 14-24 hardware) sono ipotesi di un solo sviluppatore con agenti | 4 | 3 | 12 | Milestone con criteri misurabili; M0 come spike di 1-2 settimane che riduce l'incertezza | M0 oltre le 3 settimane |
| R-17 | **Sicurezza hardware**: uno strato EL2 con bug può scrivere dove non deve | 2 | 5 | 10 | Lo strato non tocca disco né boot policy; regola assoluta di backup e conferma; reset sempre possibile da DFU/revive | — |
| R-18 | **Incoerenza con ADR-0001 e con CLAUDE.md** (vincolo "niente hypervisor") | 2 | 3 | 6 | La bozza del piano B è accettata da Marco; aggiornare CLAUDE.md al merge: "dual boot, niente macOS sotto, strato EL2 minimo consentito" | — |

## 2. Approfondimenti

### 2.1 Errata SEIS (R-01)
- **Fatti [V]:** KVM aggira il problema su M1 (Icestorm, Firestorm), M1 Pro/Max e M2: il commit originale `df652bcf1136` intercetta tutti gli accessi GIC del guest, li sanifica e li emula usando l'hardware "solo per la segnalazione"; il commit `d11a327ed9` restringe il workaround alle CPU Apple note, azzera `ICH_VTR_SEIS_MASK` nello stato globale e imposta `group0_trap`/`group1_trap`; `__vgic_v3_read_ctlr` mostra `SEIS` se il registro lo dichiara [V: lettura dei commit e dei riassunti, 2026-10-14]. NTASP cita l'errata nel suo `hv_vgic.c` ("an errata here exists on pre-M3 SoCs that can result in a host SError") [V: PB §1].
- **Ignoto [I]:** *quali* accessi precisi scatenano il SError fisico e se riguardino solo i registri di gruppo o anche `PMR`/`CTLR`. Da qui la scelta di partire dal trap massimo.
- **Conseguenza di design:** il modo `HW` pubblicizzato nella bozza del piano B non è il default. L'overhead è 2 trap per interrupt.
- **Prova prevista:** `serror-probe` in M3a, con i SError routati a EL2 (`AMO=1`) in modo che un errore sia un evento registrato e non un reset della macchina.

### 2.2 Prestazioni (R-05, NFR-1)
- Overhead per interrupt di periferica in `TRAP_EMULATE`: IRQ fisico a EL2 (1 uscita) + `IAR1` (1) + `EOIR1` (1) + eventuale IPI di inoltro. Con l'ordine di grandezza di 1-3 µs per uscita (PB §8, [I]) → 3-9 µs per interrupt. Rilevante solo ad alti ritmi (NVMe, rete). Per i driver in sviluppo (USB mass storage al primo passo) è accettabile.
- Il timer costa ≥ 2 uscite per tick (FIQ + EOI) più, con `TC`, le uscite di `PMR`. Windows ha tick di 1-15 ms e usa il coalescing [I]: ordine di 100-1000 uscite/s per core.
- Letteratura: nell'ARM l'overhead è dominato dal software dell'hypervisor (iniezione e trap), non dallo stage-2 [V: Dall et al., ISCA 2016, citato in PB §8].
- Mitigazioni, in ordine di costo: variante B del trap; EOI più veloce con LR.EOI in modo `HW` se sicuro; blocchi grandi nello stage-2; evitare lock lunghi.
- Misura: contatori FR-15, benchmark con guest di controllo (TEST_STRATEGY §6). Le soglie di NFR-1 sono provvisorie.

### 2.3 Debug senza host (R-07)
Lo scenario peggiore: lo strato si blocca a EL2 prima di EDK2, schermo nero. Strategie in ordine:
1. **Prima di tutto QEMU:** oltre il 90% della logica si valida fuori dal MacBook (L0-L3).
2. **Log persistente:** ring in RAM riservata; al panic, dump sul framebuffer (testo con font 8×16, scrittura diretta nel framebuffer di iBoot); contatore di eventi.
3. **Early boot a lampeggio:** colori sul framebuffer per le fasi di avvio (rosso = entrato in EL2, giallo = stage-2 attivo, verde = EDK2 avviato) [I: tecnica semplice, non richiede UART].
4. **m1n1 stage 2 resta disponibile:** se lo strato non parte, la macchina ricade nel menu/boot di Asahi o in macOS/recovery (non modificati): nessun rischio per l'hardware.
5. **Host di debug** (consigliato dall'inizio di M3a): UART via USB-C con il proxy di m1n1; non è il canale del prodotto, è lo strumento di sviluppo.
Domanda per Marco (non bloccante): esiste un secondo computer disponibile quando si arriva a M3a? (STATUS cita come blocco la necessità di un host.)

### 2.4 Licenze (R-08)
Fatti: m1n1 README: MIT fino a 1.9.9, **GPL-2.0-or-later dalla 2.0.0**; `src/hv/hv_vm.c` e `src/main.c` portano `GPL-2.0-or-later` [V]. Il `LICENSE` di m1n1 è il testo GPL-2 (GitHub: GPL-2.0) e c'è un `LICENSE.MIT` per le versioni precedenti [V]. NTASP `m1n1_windows` è dichiarato MIT, ma deriva da m1n1 e dichiara «Inspiration borrowed from the KVM vGIC driver» [V: NTASP §6, PB §3]: stato legale non chiaro. KVM (`arch/arm64/kvm`) è parte di Linux (GPL-2.0, solo v2) [I per l'SPDX dei singoli file]. `irq-apple-aic.c` ha una licenza da verificare (richiesta HTTP bloccata con 429 durante la sessione). Strategia completa in `docs/adr/bozza-el2-licenza.md`. Questo documento non è un parere legale.

## 3. Debito potenziale
- La variante A del trap (tutto intercettato) è semplice ma costosa: il debito è rimuoverla quando sarà provata sicura.
- Il parser FDT minimo è una dipendenza di cui siamo proprietari (rischio di bug su FDT insoliti); alternativa libfdt (BSD-2/GPL) per l'M3.
- La mappa a 8 CPU fissa (`WEL2_MAX_CPUS`): per M1 Pro/Max (10 core) serve parametrizzare.
- Stato condiviso del distributor con un lock unico: sufficiente a 8 core, da rivedere se si osserva contesa.

## 4. Valutazione dei quattro requisiti chiave
| Requisito | Valutazione | Rischi residui |
|---|---|---|
| Mantenibilità | buona: moduli piccoli, tabelle dichiarative, errori come valori | un solo sviluppatore; il codice sul bring-up è difficile da leggere |
| Riproducibilità | buona se si rispettano le regole di TEST_STRATEGY §7; lo stato di Windows non è deterministico | tempi di TCG variabili |
| Modularità | alta nel nucleo; i backend concentrano la dipendenza dall'hardware | accoppiamento forte tra `cpuif` e `vgic` (giustificato: stesso stato) |
| Estensibilità | alta per SoC e dispositivi; il `boot` resta l'unico punto di modifica | la variante A→B richiede dati hardware |

## 5. Domande aperte non bloccanti
- **Q-1** L'EDK2 per M1 leggerà l'FDT (scelta di questo design) o l'ADT? Impatta `M-ADT-PARSE` del progetto principale.
- **Q-2** Licenza del repository (oggi nessun file `LICENSE` nella radice): vedi `bozza-el2-licenza.md`.
- **Q-3** Secondo computer di debug disponibile per M3a? (R-07)
- **Q-4** Si vuole un modello AIC in QEMU (richiede compilare QEMU)? Non è nel percorso critico.
- **Q-5** M1 Pro/Max e M2 (AICv2, errata) rientrano nella Fase 6: confermare che il punto di estensione `pirq` basti.
- **Q-6** Soglie NFR-1: calibrare dopo le misure di M2.

## 6. Decisioni prese durante la progettazione
- Strato nuovo, m1n1 solo loader (ADR base).
- Modo cpuif `TRAP_EMULATE` di default su M1, selezionabile a runtime e testato in TCG.
- Indirizzi virtuali del vGIC in QEMU uguali a quelli reali, così il modello `acpigen qemu-virt` si riusa.
- MPIDR virtuale piatto.
- Handoff via FDT standard (`x0`) e nodo `/chosen/watari,el2`.
