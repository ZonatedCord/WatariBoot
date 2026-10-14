# TEST_STRATEGY: Watari EL2

- Progetto: Watari Boot, sottosistema Watari EL2 · Data: 2026-10-14 · Versione: 0.1 · Stato: draft
- Documenti collegati: [PRD](PRD.md) · [ARCHITECTURE](ARCHITECTURE.md) · [INTERFACES](INTERFACES.md) · [RISKS](RISKS.md)
- Marcatura: **[V]** verificato, **[I]** ipotesi. Gli strumenti di base (QEMU 11.1.2 con TCG, EDK2 `edk2-stable202608`, Windows 11 installato in N2, script `tools/run-qemu-windows.sh`, trace del GIC con `kernel-irqchip=off`) esistono già nel repository [V: STATUS, FALLIMENTI F-08, F-11, F-13, F-15]. Nessun test di questo piano richiede download o installazioni nuove.

## 1. Piramide
```
              /\        L4  Hardware M3 (autotest + Windows su M1)      pochi, lenti, manuali con backup
             /  \       L3  Sistema QEMU TCG (EDK2, Windows)            decine, minuti-ore
            /----\      L2  Integrazione QEMU TCG con guest di prova    ~30, secondi
           /      \     L1  Oracolo di fedeltà (replay di trace)        centinaia di letture, secondi
          /--------\    L0  Unit test su host (macOS, clang nativo)     centinaia, < 10 s totali
```
Regola: ogni bug trovato a un livello alto genera un test a un livello più basso (come F-11 → confronto MADT).

## 2. L0 — test su host (il cuore del TDD)
Il nucleo (`vgic`, `cpuif` logica, `vtimer`, `psci`, `fdt`, `s2` costruzione tabelle) è C portabile **senza accesso hardware**: gli accessi passano da `*_ops` e da un piccolo strato `sysreg` che su host è una struttura in memoria. Si compila con il clang di sistema, senza QEMU.

| Modulo | Cosa si testa | Casi limite noti |
|---|---|---|
| `vgic` (distributor) | ogni registro di INTERFACES §5: reset, lettura/scrittura, set/clear (ISENABLER/ICENABLER, ISPENDR/ICPENDR, ISACTIVER/ICACTIVER), priorità a byte e a parola, ICFGR, IROUTER, ID register | scrittura a registri riservati; accesso a byte su registro a 32 bit; INTID 1020-1023; SPI oltre il massimo; `IROUTER` verso vCPU inesistente; `IRM=1` |
| `vgic` (redistributor) | `GICR_TYPER` con `Last` solo sull'ultima, `Affinity_Value`; `WAKER` (ChildrenAsleep immediato); frame SGI_base | 1 vCPU e 8 vCPU; scrittura `WAKER` ripetuta |
| `vgic` (consegna) | scelta dell'INTID a priorità più alta, preemption per priorità, pending→active→inactive, level vs edge, SGI/PPI/SPI, mascheratura per `PMR`/`BPR` | stesse priorità (vince l'INTID minore), >8 interrupt pendenti (overflow LR), interrupt abilitato mentre pendente, disabilitato mentre active |
| `cpuif` (logica) | flush/fold tra stato software e 8 LR finti; underflow; emulazione di `IAR1/EOIR1/HPPIR1/DIR` e `ICC_CTLR` (SEIS letto 0) | EOImode 0 e 1; IAR con nulla di pendente → 1023; EOIR di INTID non active (ignorato, log) |
| `vtimer` | timer firing → PPI 30/27 pendente; mask alla fonte; riarmo all'EOI se non più firing; re-iniezione se ancora firing | timer disabilitato mentre pendente; due timer insieme; EOI senza firing |
| `psci` | tutte le funzioni di INTERFACES §9, macchina a stati OFF→ON_PENDING→ON, errori | `CPU_ON` doppio, su sé stesso, MPIDR inesistente, entry fuori RAM; `AFFINITY_INFO` in transizione |
| `fdt` | lettura nodi/proprietà, `fdt_trim_memory`, patch in loco, ricostruzione `/cpus`, FDT corrotti (lunghezze incoerenti) | FDT senza `/memory`; stringa `psci/method` già `hvc`; buffer troppo piccolo |
| `s2` | costruzione tabelle (blocchi vs pagine), `unmap` di una regione dentro un blocco (split), attributi Device-nGnRnE vs Normal | regione non allineata; sovrapposizione; ultima pagina dello spazio PA |
| `trap` | decodifica di ESR (EC, ISS, ISV, SAS, SRT, WnR), tabelle di registrazione (sovrapposizioni → `E_BUSY`), default-deny | `ISV=0` → panic; sysreg non in tabella; HVC non noto |

**Primo test (TDD, passo per passo)**
1. Scrivere `tests/host/test_gicd_typer.c`: crea un `vgic` con 4 vCPU, legge `GICD_TYPER` e verifica `0x0178001D` (valore [I] da confermare con l'oracolo, vedi §4).
2. Vederlo fallire (il modulo non esiste: errore di link).
3. Implementare il minimo (`vgic_new`, `vgic_mmio_read` per quel solo offset), farlo passare.
4. Secondo test: `test_spi_enable_pend_ack_eoi`: abilita SPI 40 (INTID), lo mette pendente, imposta `PMR`, chiama l'emulazione di `IAR1` e si aspetta 40, poi `EOIR1` e si aspetta stato inattivo.
5. Da qui in poi ogni funzione nuova nasce da un test rosso.

**Test di proprietà (deterministici)**: generatore pseudo-casuale `xorshift64*` con **seed fisso scritto nel test e stampato a ogni esecuzione**; sequenze casuali di operazioni MMIO/IRQ/ack/EOI contro un **modello di riferimento** semplice (non l'implementazione ottimizzata). Invarianti controllati dopo ogni passo:
- un INTID compare in al più un LR;
- nessun INTID è contemporaneamente pending e active se non per le regole GICv3 (pending+active ammesso per level);
- l'INTID consegnato ha priorità ≥ di qualsiasi altro pendente abilitato e non mascherato da `PMR`;
- dopo `EOIR` seguita da `ICENABLER`, nessuna re-iniezione.
Numero di sequenze per esecuzione: 10.000, tempo massimo 5 s.

**Copertura**: `llvm-cov`/`gcov` sul nucleo, soglia **≥ 90% delle righe** e 100% dei rami di decodifica dei registri (il comando e la soglia fanno fallire la build locale).

## 3. L2 — integrazione su QEMU TCG con payload di prova
`virtualization=on` rende disponibili a EL2 il GICv3 con `ICH_*` e il maintenance interrupt in TCG [V: PB §7: `arm_gicv3_cpuif.c`, `virt.c`]. Comando di riferimento [I]:
```
qemu-system-aarch64 -M virt,virtualization=on,gic-version=3 -cpu max -smp 4 -m 2G \
  -nographic -serial mon:stdio -kernel build/wel2.elf -d guest_errors -D logs/qemu.log \
  -icount shift=auto,sleep=off   # solo per i test a tempo deterministico
```
Il payload di prova (`tests/qemu/guest-*.S`, scritto da noi, ≤ 200 righe) stampa su UART un token per ogni passo, e il test controlla il flusso di token (grep con timeout). Casi:

| Test | Verifica | Milestone |
|---|---|---|
| `boot-el2` | banner, `CurrentEL=2`, `E2H=1` impostato dallo strato, stage-2 attivo | M0 |
| `inject-ppi` | iniezione PPI 30 → il guest esegue `IAR1` (trap o HW), legge 30, `EOIR1` | M0 |
| `gic-probe` | il guest legge tutti i registri GICD/GICR di INTERFACES §5-6 e confronta con i valori attesi stampati dallo strato | M0-M1 |
| `psci-smp` | il guest accende i core 1-3 con `HVC CPU_ON`; ogni core stampa il proprio MPIDR; poi `CPU_OFF` e `AFFINITY_INFO` | M1 |
| `sgi-ipi` | ogni core invia SGI a ogni altro con `ICC_SGI1R_EL1`; il destinatario conta le ricezioni (N×(N-1) attesi) | M1 |
| `spi-device` | un dispositivo virtio con interrupt reali (backend `gic-qemu`) → vSPI → guest | M1-M2 |
| `timer-tick` | il guest programma `CNTP` a 10 ms e conta 100 interrupt in ~1 s virtuale (tolleranza dichiarata) | M1 |
| `modes` | le stesse prove con `cpuif_mode=HW` e `TRAP_EMULATE`: risultati identici | M1 |
| `fault-injection` | accesso a regione dello strato; `ISV=0` forzato; HVC ignoto; MMIO non mappato → panic con dump e codice atteso | M1 |
| `linux-control` (opzionale) | un kernel Linux arm64 di prova (se già presente in locale; **non si scarica**) usa il vGIC e arriva alla console | M1 |

**Test host di sezione E2H**: il boot dello strato in QEMU imposta `HCR_EL2.E2H=1` per riprodurre il M1 [I: ARCHITECTURE §1.3]; un test verifica che l'accesso ai registri del guest passi da `*_EL12` e non da `*_EL1`.

## 4. L1 — test di fedeltà del vGIC (anti-F-14)
Obiettivo: nessun registro del vGIC che Windows legge deve differire da quanto ha visto su un GICv3 funzionante.

**Oracolo.** Il boot V1 di Windows 11 (MADT acpigen, GICv3 di QEMU, schermata di blocco raggiunta [V: F-11]) viene rieseguito con `-accel tcg` o HVF e `kernel-irqchip=off` con il trace del GIC di QEMU (`-trace 'gicv3_*'`, come in F-13/F-15 [V]) e registrato in un file: ogni riga = (tempo, CPU, tipo accesso, offset, dimensione, valore letto/scritto). Il file di trace e il suo hash vanno in `WatariBoot-data/oracle/` (fuori dal repository: contiene solo accessi a registri GIC, nessun codice Microsoft, ma è grande).

**Replay su host.** `tests/oracle/replay.c` legge il trace e ripete le scritture di Windows sul nostro `vgic` (stesse vCPU), confrontando ogni lettura con il valore dell'oracolo. Classificazione:
- **statici** (devono coincidere sempre): `TYPER`, `IIDR`, `TYPER2`, ID register, `GICR_TYPER`, `ICFGR` di SGI/PPI, `IPRIORITYR` rilette dopo scrittura, `ISENABLER` rilette dopo scrittura, `WAKER`, `CTLR` (incluso RWP);
- **dinamici** (pending/active, `HPPIR`, `IAR`): non confrontati nel replay; coperti da L0 e dal modello di riferimento;
- le differenze **attese** (es. numero di SPI, indirizzi) sono in una lista `oracle-allow.txt` con motivazione.
Criterio: 100% dei registri statici letti da Windows coincide; report con la tabella "registro letto da Windows / frequenza" (serve anche a ordinare le priorità implementative).

**Misure aggiuntive dal trace** (rispondono a decisioni aperte):
- frequenza di scrittura di `ICC_PMR_EL1` e di `IAR/EOIR` per secondo durante il boot e a riposo → stima del costo del modo `TRAP_EMULATE` (RISKS R-05). Il trace via `kernel-irqchip=off` registra gli accessi MMIO al GIC; i registri `ICC_*` sono di sistema e vanno contati con un trace dei sysreg (QEMU TCG: `-d` con `trace:` sui registri `ICC_*` oppure contatori dello strato in `TRAP_EMULATE` — l'alternativa pratica) [I].
- elenco delle sequenze di avvio delle CPU (come F-15 T6) per verificare che il vGIC le accetti.

**Test negativi di fedeltà** (da F-14): per ogni registro "Critico" di INTERFACES si fa una variante con il valore sbagliato (mutation testing manuale: `RWP=1` permanente, `Last` errato, `ChildrenAsleep` fisso a 1) e si verifica in L2/L3 che il sintomo sia **rilevato dal watchdog dello strato** (timeout con dump) invece di essere un hang silenzioso (NFR-9).

## 5. L3 — sistema su QEMU TCG
| Test | Passi | Criterio |
|---|---|---|
| `edk2-boot` (M1) | `-kernel wel2.elf`, pflash con `QEMU_EFI-watari.fd`, `-smp 4` | seriale: GICv3 rilevato, `BdsWait` termina, Boot Manager; 3 esecuzioni con gli stessi eventi chiave |
| `edk2-reset` (M1) | dal Boot Manager, `Reset` | lo strato logga `SYSTEM_RESET` e QEMU riparte; nessun doppio avvio |
| `win-boot` (M2) | disco Windows installato (N2) con `snapshot=on`, `-smp 4`, `ramfb` | schermata di blocco entro il limite (§7); screenshot con `screendump` su percorso senza spazi (F-07); prima di dichiarare un hang si manda un tasto (`sendkey shift`, F-10) |
| `win-smp` (M2) | stesso | log strato: 3 `CPU_ON` riusciti + BSP; nessun bugcheck 0x5C |
| `win-soak` (M2) | 30 minuti di inattività | 0 panic, contatori di uscite stabili (rate ≤ soglia NFR-1) |
| `win-baseline` (M2) | Windows su TCG **senza** strato (firmware originale) | tempo di riferimento per il criterio 1,5× |

Diagnosi di un blocco: il watchdog dello strato (timeout senza uscite da una vCPU per T secondi, T configurabile) stampa lo stato; il gdbstub di QEMU (`-gdb`) è utilizzabile **ma altera i timeout del guest** (F-15) e quindi solo per post-mortem.

## 6. L4 — hardware (M3)
Si esegue solo dopo conferma di backup e del via di Marco (regola assoluta). Prerequisito: host di debug (RISKS R-07).
- **Autotest di M3a** (payload di prova + strato, senza EDK2/Windows): 
  1. `aic-sw-irq`: `SW_SET` dell'AIC su un IRQ non collegato a nulla [V: F0 §3.4] → vSPI → il payload lo riceve; verifica mask/unmask e target.
  2. `ipi-fast`: IPI veloce core→core per tutte le 56 coppie.
  3. `timer-fiq`: timer guest fisico e virtuale → vPPI 30 e 27; verifica che `CNTP_CTL` letto dal guest non mostri `IMASK` modificato; assenza di tempeste di FIQ (contatore di FIQ per secondo).
  4. `psci-8`: `CPU_ON` degli 8 core con spin-table.
  5. `serror-probe`: letture/scritture mirate dei registri `ICC/ICH` per verificare la variante B del trap (R-01) senza SError; qualunque SError → classificazione e log.
  6. `s2-attr`: scrittura MMIO nGnRE dal guest verso un registro innocuo → nessun SError (verifica l'ipotesi nGnRnE).
  7. `mem-protect`: accesso del guest alla regione dello strato → fault gestito.
- **Misure di prestazione (NFR-1):** guest di controllo (Linux Asahi se già installato, o il payload di prova) in due configurazioni: stage-2 identità e traps minime vs. native; ciclo CPU-bound, ciclo di accessi MMIO reali, IRQ ad alto ritmo (`SW_SET` in loop). Scrivere i numeri in `docs/costs/` o in un file di misure del sottosistema. m1n1 hv può servire per osservazioni in sola lettura [I].
- **Windows M3b:** USB-C con disco/ tastiera USB; criterio di PRD §8.

## 7. Riproducibilità
| Elemento | Regola |
|---|---|
| Toolchain | versione di clang scritta in `el2/TOOLCHAIN.md` e controllata all'avvio del `make` (fallisce se diversa); flag `-ffreestanding -fno-pic -mgeneral-regs-only -Wall -Wextra -Werror`; `-ffile-prefix-map` e nessun timestamp |
| Build | `make -C el2 repro-check`: due build da cartelle diverse → stesso SHA-256 |
| QEMU | 11.1.2, comando completo in `tools/run-qemu-wel2.sh` (da scrivere); `snapshot=on` per i dischi Windows; durata e seed registrati nel log |
| Tempo | per L2: `-icount shift=auto,sleep=off` dove possibile (tempo guest deterministico); per L3: tolleranze dichiarate e controllo su eventi (non su durata) |
| Dati | immagini identificate da SHA-256 in `WatariBoot-data/…/SHA256.txt` (già in uso per l'ISO) |
| Seed | tutti i test casuali stampano il seed; `SEED=<n> make test` lo riproduce |
| Ambiente | percorsi senza spazi (F-07: `/tmp/watari-data`); nessun uso di rete nei test |
| Esito | ogni test scrive un file di esito con comando, versioni, hash degli input e risultato |

## 8. Cosa NON si testa in QEMU (e come si compensa)
| Limite | Compensazione |
|---|---|
| Timer come FIQ e `VM_TMR_FIQ_ENA_EL2` | L0 con sysreg finti; L4 `timer-fiq` |
| AIC (non esiste in QEMU, F-06) | L0 con `pirq-fake` e con un modello a registri dell'AIC scritto nei test (mappa 0x2000-0x4200); L4 `aic-sw-irq` |
| Errata SEIS e SError reali | L4 `serror-probe`; L2 simula SError con iniezione software |
| Prestazioni | solo contatori di uscite in QEMU (utili per confronti relativi, non per assolute); misure vere in L4 |
| Fast IPI, PMU FIQ | L4 |
| Comportamento di Windows sulle periferiche Apple | fuori scopo (driver) |
Un modello AIC dentro QEMU sarebbe l'ideale ma richiede di compilare QEMU: **non si fa senza il via di Marco** (F-13) e non è nel percorso critico.

## 9. Criteri di uscita per milestone
Riportati in PRD §8; ogni criterio ha un test numerato qui (M0: §2 + §3 `boot-el2`, `inject-ppi`; M1: §3 e §5 `edk2-*`; M2: §4 e §5 `win-*`; M3a: §6).
