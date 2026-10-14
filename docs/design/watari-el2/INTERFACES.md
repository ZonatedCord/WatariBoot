# INTERFACES: Watari EL2

- Progetto: Watari Boot, sottosistema Watari EL2 · Data: 2026-10-14 · Versione: 0.1 · Stato: draft
- Documenti collegati: [PRD](PRD.md) · [ARCHITECTURE](ARCHITECTURE.md) · [TEST_STRATEGY](TEST_STRATEGY.md) · [RISKS](RISKS.md)
- Marcatura: **[V: fonte]** verificato; **[I]** ipotesi. Nessuna codifica di registro o valore numerico di questo file è stata ricontrollata sul manuale ARM (non disponibile in locale): tutti i valori etichettati **[I]** vanno confermati in M0 con il manuale ARM (ARM ARM, GICv3/v4 architecture spec IHI0069, PSCI DEN0022, SMCCC DEN0028) **prima** di scrivere il codice. Il metodo di conferma per i registri statici del GIC è l'**oracolo** (TEST_STRATEGY §4): confronto con le letture reali di QEMU GICv3.
- Lezione di riferimento: **F-14**. Un registro del GIC sbagliato non produce un bugcheck ma un hang silenzioso (Windows resta sul logo con lo schermo identico). Perciò questo documento ha una colonna "fedeltà" per ogni registro.

## 1. Convenzioni
- Offset in esadecimale; larghezze in bit; `RW`, `RO`, `WO`, `RAZ/WI` (legge zero, scritture ignorate), `RAO/WI`.
- "Fedele" = comportamento architetturale completo, testato dall'oracolo. "Statico" = valore costante. "Stato" = valore mantenuto dal vGIC. "Log-once" = ignorato ma registrato una volta sola (anti-rumore).
- Ogni accesso MMIO a una regione virtuale arriva come abort di stage-2 (EC 0x24) con `ESR.ISV=1` (registro e dimensione noti). Se `ISV=0` lo strato **non indovina**: panic con dump (R-06 in RISKS).

## 2. Contratto di boot m1n1 → Watari EL2 [I, da verificare in M3a]
| Elemento | Atteso |
|---|---|
| Formato | immagine in stile Linux arm64 (`Image`, magic `ARM\x64` a offset 0x38, `text_offset` valido), eventualmente con EDK2 FD concatenato dopo l'header (il nostro header dichiara offset e dimensione) |
| Caricamento | m1n1 (stage 2) carica il "kernel" da ESP come nel boot di Linux; consegna a un "next stage" con 4 argomenti dopo aver spento NVMe, eccezioni, USB, display, framebuffer, MMU [V: m1n1 `src/main.c`] |
| Livello | EL2 con `HCR_EL2.E2H=1` (CPU bloccata in VHE [V: patch Linux "Cope with CPUs stuck in VHE mode", titolo]) |
| Registri | `x0` = indirizzo fisico dell'FDT generato da m1n1; `x1..x3` = 0 [I, come protocollo Linux arm64] |
| MMU | spenta; cache: lo strato non assume nulla e invalida/pulisce da sé |
| Core secondari | già avviati da m1n1 e fermi in attesa; l'FDT riporta `enable-method = "spin-table"` e `cpu-release-addr` per ogni core [I: va letto in m1n1 `kboot.c`/`smp.c`]; lo strato scrive l'indirizzo di ingresso nel release-addr e fa `SEV` |
| FDT atteso | `/memory`, `/reserved-memory`, `/cpus`, nodo AIC (`apple,t8103-aic`), seriale, `simple-framebuffer`, `/chosen` con i parametri m1n1 |
| Stato delle FIQ | non garantito: lo strato esegue la sequenza di `aic_init_cpu` su ogni core [V: F0 §5] |

QEMU: `-kernel wel2.elf` (o `Image`) con `-M virt,virtualization=on,gic-version=3 -cpu max`; QEMU imposta `x0` = FDT e avvia a EL2 [I: comportamento di `arm_load_kernel`]. L'EDK2 è fornito come pflash0 (`-drive if=pflash,unit=0`), come nelle prove V0/V1: lo strato lo avvia saltando a `0x0` a EL1 [I].

## 3. Handoff Watari EL2 → EDK2
### 3.1 Stato della CPU all'ingresso (ogni vCPU che parte)
| Elemento | Valore |
|---|---|
| Livello | EL1h (`SPSel=1`) |
| `PC` | QEMU: `0x0` (pflash0); t8103: entry dichiarata nell'header dell'immagine |
| `x0` | indirizzo fisico dell'FDT patchato; `x1..x30 = 0` |
| `DAIF` | tutto mascherato (D, A, I, F = 1) |
| `SCTLR_EL1` | solo bit RES1; MMU, cache D e I spente |
| Stato GIC visto da EDK2 | vGIC al reset: tutti gli interrupt disabilitati, `GICR_WAKER.ProcessorSleep=1`, `GICD_CTLR=0` |
| `CNTVOFF_EL2` | 0; `CNTFRQ_EL0` come fisico (24 MHz sul M1 [V: PB/F0]) |

### 3.2 FDT patchato (modifiche **in loco**, nessuna riallocazione)
| Nodo/proprietà | Modifica |
|---|---|
| `/memory/reg` | tolta la regione dello strato (`fdt_trim_memory`: la regione sta in cima alla RAM, quindi si riduce la dimensione) |
| `/psci/method` | `"smc"` → `"hvc"` (stessa lunghezza, patch in loco). In QEMU con `virtualization=on` il conduit è SMC [V: `hw/arm/virt.c`] |
| `/cpus/cpu@N/reg` | MPIDR virtuale (vedi §6.3); `enable-method` da `"spin-table"` a `"psci"` e rimozione di `cpu-release-addr`: le lunghezze cambiano, quindi su M1 il nodo `/cpus` si ricostruisce nel buffer di handoff (vedi nota sotto) |
| `/chosen/watari,el2` (nuovo, in un buffer aggiuntivo) | proprietà in §3.3 |
| nodo GIC | in QEMU: già `arm,gic-v3` agli indirizzi reali (che sono anche quelli virtuali); su M1: nodo nuovo `/watari-intc` (`compatible = "arm,gic-v3"`, `reg = <GICD GICR>`, `redistributor-stride = <0x20000>`) |
| `/timer/interrupts` | PPI 13, 14, 11, 10 (GSIV 29, 30, 27, 26) come QEMU [V: README acpigen] |
Nota: dove la patch in loco non basta (lunghezze diverse) lo strato **copia** l'FDT in un buffer proprio (`WEL2_FDT_BUF`, 256 KB) e lo ricostruisce con un writer minimo; questa scelta è in `wel2-fdt` ed è invisibile a EDK2. L'FDT del M1 contiene molti nodi: il writer copia verbatim tutto tranne i nodi elencati sopra.

### 3.3 `/chosen/watari,el2`
| Proprietà | Tipo | Significato |
|---|---|---|
| `compatible` | string | `"watari,el2"` |
| `version` | u32 | versione del contratto (parte da 1) |
| `platform` | string | `"qemu-virt"` o `"t8103"` |
| `gicd-base`, `gicr-base`, `msi-frame-base` | u64 | indirizzi virtuali (§4) |
| `gicr-stride` | u32 | 0x20000 |
| `reserved-region` | u64×2 | base e dimensione dello strato (non mappata nel guest) |
| `log-ring` | u64×2 | base e dimensione del ring log (leggibile da EDK2 in sola lettura per stamparlo) |
| `hvc-vendor-base` | u32 | `0xC6000000` |
| `fb-info` | u64×… | solo t8103: base, larghezza, altezza, stride, formato (copia da `simple-framebuffer`) |

## 4. Mappa MMIO emulata
| Regione | QEMU (M0-M2) | t8103 (M3) | Dimensione | Trattamento stage-2 |
|---|---|---|---|---|
| GICD | `0x0800_0000` [V: README acpigen] | `0xF_0000_0000` [I: come NTASP `0xf00000000`, V: audit Mu] | 64 KB | trap su ogni accesso (hook MMIO) |
| GICR (vCPU *i*) | `0x080A_0000 + 0x20000·i` [V: README acpigen] | `0xF_1000_0000 + 0x20000·i` | 128 KB per vCPU (RD_base 64 KB + SGI_base 64 KB) | trap |
| MSI Frame (GICv2m) | `0x0802_0000`, SPI base `0x50`, count `0x40` [V: F-11] | `0xF_2000_0000`, SPI base 928 (=0x3A0), count 32 [I] | 4 KB | QEMU: **passthrough** (i dispositivi QEMU scrivono davvero su `SETSPI_NSR`); t8103: trap, `SETSPI_NSR` ignorato (nessun dispositivo lo usa in v1) |
| Regione dello strato | RAM alta riservata | RAM alta riservata | 64 MB [I] | **non mappata** (fault → panic con log) |
| Altre periferiche | passthrough | passthrough (identità) con Device-nGnRnE | — | RAM: Normal WB; MMIO: Device-nGnRnE |

Perché in QEMU gli indirizzi virtuali coincidono con quelli reali [I, ragionamento]: i dispositivi QEMU (virtio, NVMe, xHCI) scrivono i messaggi MSI all'indirizzo che Windows ha letto dalla MADT, indirizzo che le scritture DMA non traducono tramite stage-2. Con indirizzi uguali la MADT `acpigen -m qemu-virt` del controllo V1 si riusa **senza modifiche**; la GICD/GICR vere restano raggiungibili solo da EL2 (mappa EL2 propria).

### 4.1 INTID e GSIV
| Intervallo | Uso |
|---|---|
| 0-15 | SGI (IPI di Windows) |
| 16-31 | PPI: 26 timer EL2 (GTDT, non usato), 27 timer virtuale EL1, 30 timer fisico EL1 non-secure, 25 maintenance (solo QEMU, non esposto), 23 PMU (richiesto nella MADT di V1) |
| 32-927 | SPI = 32 + indice AIC (M3). In QEMU: SPI reali 1:1 |
| 928-959 | riservati alla MSI Frame (t8103) |
`GICD_TYPER.ITLinesNumber = 29` (960 INTID) su M3; in QEMU quanto dichiara il GIC reale (oracolo). Un IRQ AIC ≥ 896 non è supportato in v1 (log + non abilitabile) [I: AIC t8103 ≤ 896 IRQ, F0/PB].

## 5. Distributor virtuale (GICD): registri e fedeltà
Modello: GICv3, **single security state** (`SecurityExtn=0`, `DS=1`), affinity routing sempre attivo, senza LPI/ITS/MBIS, `A3V=1`, `RSS=0`, `No1N=0`. Questo è il modello che KVM presenta ai guest [I: dalla memoria del codice KVM, non copiato]; l'**oracolo QEMU** decide in caso di differenza.

| Offset | Registro | Comportamento | Fedeltà / nota |
|---|---|---|---|
| 0x0000 | `GICD_CTLR` | bit0 EnableGrp0, bit1 EnableGrp1, bit4 ARE (RAO/WI), bit6 DS (RAO/WI), bit7 E1NWF (RAZ/WI), bit31 RWP **sempre 0** | **Critico.** Windows scrive ARE+EnableGrp1 e sonda RWP: RWP mai a 1 [I]. Layout con `DS=1` da confermare con l'oracolo |
| 0x0004 | `GICD_TYPER` | `ITLinesNumber=29`, `CPUNumber=0`, `SecurityExtn=0`, `MBIS=0`, `LPIS=0`, `IDbits=15`, `A3V=1`, `No1N=0`, `RSS=0` → valore atteso `0x0178_001D` [I] | Statico. Errore tipico: dichiarare LPIS o MBIS |
| 0x0008 | `GICD_IIDR` | valore fisso dell'oracolo (implementer/product/variant) | Statico. Windows può usare l'IIDR per workaround: usare lo stesso dell'oracolo |
| 0x000C | `GICD_TYPER2` | `RAZ` | Statico |
| 0x0010 | `GICD_STATUSR` | `RAZ/WI` | — |
| 0x0040/0x0048 | `GICD_SETSPI_NSR`/`CLRSPI_NSR` | `WI` (MBIS=0) | log-once |
| 0x0080-0x00FC | `GICD_IGROUPR<n>` | Stato; `IGROUPR0` (INTID 0-31) vive nel redistributor | Fedele. Windows imposta tutto a Group 1 |
| 0x0100-0x017C | `GICD_ISENABLER<n>` | set-enable; l'abilitazione di un SPI chiama `pirq_ops.unmask` e `set_target` | Fedele. Effetto fisico: AIC `MASK_CLR` + `TARGET_CPU` |
| 0x0180-0x01FC | `GICD_ICENABLER<n>` | clear-enable; `pirq_ops.mask` | Fedele |
| 0x0200-0x027C | `GICD_ISPENDR<n>` | set-pending (software); lettura = pending | Fedele (NTASP la lascia "not implemented": errore da non ripetere) |
| 0x0280-0x02FC | `GICD_ICPENDR<n>` | clear-pending | Fedele |
| 0x0300-0x037C | `GICD_ISACTIVER<n>`/0x0380 `ICACTIVER<n>` | stato active | Fedele |
| 0x0400-0x07F8 | `GICD_IPRIORITYR<n>` | 8 bit per INTID, accessibile a byte e a 32 bit; priorità implementate: 5 bit alti significativi (32 livelli [V: PB §1]); i bit bassi RAZ/WI | Fedele: Windows scrive e rilegge priorità; il numero di bit implementati deve coincidere con `ICH_VTR_EL2.PRIbits` |
| 0x0800-0x0BF8 | `GICD_ITARGETSR<n>` | `RAZ/WI` (ARE=1) | — |
| 0x0C00-0x0CFC | `GICD_ICFGR<n>` | 2 bit per INTID: bit1 = edge; il bit0 RAZ; SPI AIC: level di default | Fedele (influenza smaschera AIC all'iniezione vs EOI) |
| 0x0D00-0x0D7C | `GICD_IGRPMODR<n>` | `RAZ/WI` (DS=1) | — |
| 0x0E00 | `GICD_NSACR<n>` | `RAZ/WI` | — |
| 0x0F00 | `GICD_SGIR` | `WI` (solo GICv2 legacy) | log-once |
| 0x0F10/0x0F20 | `GICD_CPENDSGIR`/`SPENDSGIR` | `RAZ/WI` (ARE=1) | — |
| 0x6000-0x7FD8 | `GICD_IROUTER<n>` (INTID 32+) | 64 bit; `Aff3.Aff2.Aff1.Aff0` e bit31 `IRM`; scegliere la vCPU target; `IRM=1` → una qualsiasi: usare la vCPU 0 se abilitata, altrimenti la prima abilitata | Fedele: la scrittura riprogramma `pirq_ops.set_target`; lettura = valore scritto |
| 0xFFD0-0xFFFC | `GICD_PIDR4..PIDR3`, `CIDR0..3` | valori statici, **`PIDR2.ArchRev = 3`** (bit 7:4) | Statico: usato da OS per riconoscere la versione [I] |

Scritture a registri riservati: `RAZ/WI` con log-once. Accessi non allineati o di dimensione non ammessa: panic con dump (non si nascondono errori del guest in v1).

## 6. Redistributor virtuale (GICR) e sistema per vCPU
### 6.1 Frame RD_base (offset dal frame della vCPU)
| Offset | Registro | Comportamento | Fedeltà / nota |
|---|---|---|---|
| 0x0000 | `GICR_CTLR` | `EnableLPIs` RAZ/WI, `RWP` sempre 0, `UWP` 0 | **Critico** (RWP) |
| 0x0004 | `GICR_IIDR` | = `GICD_IIDR` oracolo | Statico |
| 0x0008 | `GICR_TYPER` (64 bit) | `PLPIS=0, VLPIS=0, DirectLPI=0`, `Last=1` solo sull'ultima vCPU, `Processor_Number`=indice vCPU, `Affinity_Value`=MPIDR virtuale (Aff3.Aff2.Aff1.Aff0 in bit 63:32) | **Critico.** Windows lo usa per associare il redistributor alla CPU. `Last` errato = ricerca che non termina [I] |
| 0x0010 | `GICR_STATUSR` | `RAZ/WI` | — |
| 0x0014 | `GICR_WAKER` | `ProcessorSleep` (bit1) RW; `ChildrenAsleep` (bit2) = `ProcessorSleep` **immediatamente**; reset: entrambi 1 | **Critico.** Windows azzera Sleep e attende `ChildrenAsleep=0`: se resta 1 si ha un hang |
| 0xFFD0-0xFFFC | ID | come GICD (PIDR2.ArchRev=3) | Statico |

### 6.2 Frame SGI_base (RD_base + 0x10000)
| Offset | Registro | Comportamento | Note |
|---|---|---|---|
| 0x0080 | `GICR_IGROUPR0` | Stato (INTID 0-31). Le scritture di Windows `0xFFFFFFFF` (Group 1) devono persistere. In F-13 si vide `IGROUPR0=0xFFFFFFFF` riscritto dopo ExitBootServices: nel nostro vGIC nessun reset implicito, quindi il valore è quello scritto da ultimo | Fedele |
| 0x0100/0x0180 | `ISENABLER0`/`ICENABLER0` | Stato; Windows scrive `ICENABLER0 = 0xFFFF00FF` all'avvio di ogni CPU [V: F-15, trace T6] | Fedele (SGI e PPI) |
| 0x0200/0x0280 | `ISPENDR0`/`ICPENDR0` | Stato (le SGI pendenti stanno qui) | Fedele |
| 0x0300/0x0380 | `ISACTIVER0`/`ICACTIVER0` | Stato | Fedele |
| 0x0400-0x041C | `IPRIORITYR0..7` | priorità di INTID 0-31 | come GICD |
| 0x0C00/0x0C04 | `ICFGR0`/`ICFGR1` | SGI: sempre edge (RAO); PPI: configurabili (timer = level) | Fedele |
| 0x0D00 | `IGRPMODR0` | `RAZ/WI` | — |
| 0x0E00 | `NSACR` | `RAZ/WI` | — |

### 6.3 MPIDR virtuale e registri di identificazione
- `VMPIDR_EL2` della vCPU *i*: **piatto**, `Aff0 = i`, `Aff1=Aff2=Aff3=0`, `U/MT=0`, bit31 RES1 [I]. Motivo: la target list delle SGI usa Aff0 (16 bit) e il resto si decodifica con una regola semplice; la MADT del modello `t8103-el2` deve usare gli stessi valori (task di `acpigen`).
- `VPIDR_EL2` = `MIDR_EL1` reale del core (E/P restano distinguibili: la MADT/PPTT dichiara l'efficiency class).
- `ID_AA64PFR0_EL1` (trap `TID3`): il campo `GIC` (bit 27:24) letto come **1**, il resto invariato. Motivo: lezione F-08 (su HVF il campo era 0 anche con GIC usabile); un firmware che decide dal campo sceglierebbe GICv2. Gli altri `ID_AA64*`: passthrough dal valore reale, con mascheramento di caratteristiche non supportate (es. SPE, nesting `ID_AA64MMFR2.NV`) elencato nella tabella `sysreg_overrides` [I].

## 7. Interfaccia CPU virtuale e registri di sistema
### 7.1 Valori iniziali
| Registro | Valore | Nota |
|---|---|---|
| `HCR_EL2` | `RW=1, VM=1, E2H=1, TGE=0, IMO=1, FMO=1, AMO=1, TSC=1, TID3=1`, `TWI=TWE=0`, `FB=0` | `AMO=1` serve a intercettare i SError dell'errata SEIS [I]; `TSC=1` per rispondere a `SMC` con `NOT_SUPPORTED` |
| `ICC_SRE_EL2` | `SRE=1, Enable=1` (lower EL può usare i registri di sistema) | [I] |
| `ICH_HCR_EL2` | `En=1`; modo `TRAP_EMULATE`: `TALL0=1, TALL1=1, TDIR=1` e `TC=1` nella variante A; solo `TALL1, TDIR` nella variante B | vedi §7.2 |
| `ICH_VMCR_EL2` | gestito dallo strato (VENG1, VPMR, VBPR1, VEOIM, VCBPR) | [I] |
| `VTCR_EL2` | granulo 16 KB, PS = PA range del core, SH0/ORGN0/IRGN0 inner-shareable write-back, T0SZ/SL0 come m1n1 [V: `hv_vm.c` `hv_pt_init`] | — |
| `VTTBR_EL2` | base tabelle + `VMID` (8 o 16 bit, 1) | — |
| `CNTHCTL_EL2` | `EL1PCTEN=EL1PCEN=1` (con `E2H=1` il formato cambia: confermare i bit [I]) | timer guest non intercettati |
| `VM_TMR_FIQ_ENA_EL2` (`S3_5_C15_C1_3`) | bit0 (V) e bit1 (P) = 1 | [V: F0 §1 righe 4-5] |

### 7.2 Registri `ICC_*` del guest da intercettare (modo `TRAP_EMULATE`)
Tutte le codifiche sono **[I]**, da confermare sul manuale ARM in M0.

| Registro | Trap | Emulazione | Costo atteso |
|---|---|---|---|
| `ICC_IAR1_EL1` (`S3_0_C12_C12_0`) | `TALL1` | sceglie l'INTID pendente a priorità più alta dallo stato vGIC, lo rende active, aggiorna LR e restituisce l'INTID (1023 se spurio) | 1 trap per interrupt |
| `ICC_EOIR1_EL1` (`..._C12_1`) | `TALL1` | priority drop (+ deactivate se `EOImode=0`), poi fine del ciclo: unmask AIC / rivalutazione timer | 1 trap per interrupt |
| `ICC_DIR_EL1` (`..._C11_1`) | `TDIR` | deactivate se `EOImode=1` | raro |
| `ICC_HPPIR1_EL1`, `ICC_BPR1_EL1`, `ICC_IGRPEN1_EL1` (`..._C12_7`), `ICC_AP1Rn_EL1` | `TALL1` | emulati sullo stato; `IGRPEN1` scrive anche `ICH_VMCR` | rari (avvio CPU) |
| `ICC_IAR0/EOIR0/HPPIR0/BPR0/IGRPEN0/AP0Rn` | `TALL0` | idem per Group 0 | rari |
| `ICC_CTLR_EL1` (`..._C12_4`) | `TC` (variante A) | lettura: `SEIS=0`, `IDbits=…`, `PRIbits=4`, `A3V=1`, `EOImode`; scrittura: `EOImode`, `CBPR` | rari |
| `ICC_SRE_EL1` | `TC` | `SRE=1` RAO/WI | rari |
| `ICC_PMR_EL1` (`S3_0_C4_C6_0`) | **`TC` la intercetta nella variante A** [I] | emulato su `ICH_VMCR.VPMR` | **Potenzialmente molto frequente.** Windows usa `PMR=0xF0` all'avvio di ogni CPU [V: F-15 trace T6]; se lo usasse per l'IRQL, il trap per ogni cambio di IRQL sarebbe proibitivo. Misura obbligatoria da trace prima di M2 (RISKS R-05). Variante B lascia `PMR` in hardware |
| `ICC_SGI1R_EL1` (`S3_0_C12_C11_5`), `ICC_ASGI1R_EL1` (`..._C11_6`), `ICC_SGI0R_EL1` (`..._C11_7`) | già intercettati con `IMO=1` [I, da confermare nell'ARM ARM; la nota di PB §5 citava `TALL1/TC`] | §7.3 | per ogni IPI di Windows |
| `ICC_RPR_EL1` | `TALL1`/`TC` | emulato | raro |

Principio di scelta della variante: si parte dalla **A** (trap massimo, come KVM sulle CPU con SEIS rotto [V: commit `d11a327ed9`]) per avere correttezza; si passa alla **B** solo con una prova di sicurezza su hardware (autotest M3a: scritture/letture mirate dei registri non intercettati senza SError) e con misure di prestazione. Il cambio è un parametro (`cpuif_mode`, `cpuif_trap_set`), non un cambio di codice.

### 7.3 Dispatch SGI
Precondizioni: `ICC_SGI1R_EL1` scritto dalla vCPU *s*: `INTID[27:24]` ∈ 0-15, `TargetList[15:0]` (Aff0), `Aff1[23:16]`, `Aff2[39:32]`, `Aff3[55:48]`, `IRM[40]` (1 = tutte tranne sé), `RS[47:44]` (0, RSS=0). Postcondizioni: per ogni vCPU destinataria con SGI abilitata (`GICR_ISENABLER0`) il bit pending è impostato; se destinataria ≠ pCPU corrente si chiama `ipi_ops.send`. Errori: INTID fuori range o destinataria inesistente → ignorata + log (come l'architettura: destinatari non esistenti sono ignorati). Esempio: Windows invia SGI 0 alla CPU 1 → `sgi_pending[1] |= 1` → `ipi_send(core 1)`.

## 8. Interfacce dei backend (C)
Tutte le tabelle sono `const` e sono selezionate nella radice di composizione.

```c
typedef enum { WEL2_OK = 0, WEL2_E_INVAL, WEL2_E_NOENT, WEL2_E_NOSUP,
               WEL2_E_BUSY, WEL2_E_RANGE, WEL2_E_FAULT, WEL2_E_STATE,
               WEL2_E_TIMEOUT } wel2_err_t;

typedef enum { PEV_NONE, PEV_DEVICE, PEV_IPI, PEV_TIMER_P, PEV_TIMER_V,
               PEV_MAINT, PEV_PMU, PEV_UNKNOWN } pev_kind_t;
typedef struct { pev_kind_t kind; uint32_t id; } pev_t;   /* id = indice sorgente fisica */
```

### 8.1 `pirq_ops` (sorgenti di interrupt fisico; normalizza IRQ **e** FIQ)
```c
typedef struct pirq_ops {
    const char *name;
    wel2_err_t (*init_bsp)(void);                     /* una volta, sul core di boot */
    wel2_err_t (*init_cpu)(uint32_t pcpu);            /* su ogni core: sequenza di aic_init_cpu o equivalente */
    uint32_t   (*device_irq_count)(void);             /* es. 896 */
    uint32_t   (*poll)(uint32_t pcpu, pev_t *out, uint32_t max); /* svuota gli eventi pendenti; ritorna n */
    void       (*mask)(uint32_t irq);
    void       (*unmask)(uint32_t irq);
    wel2_err_t (*set_target)(uint32_t irq, uint32_t pcpu_mask);
    wel2_err_t (*set_trigger)(uint32_t irq, bool edge);
    uint32_t   (*intid_of)(uint32_t irq);             /* irq fisico → INTID virtuale */
    void       (*sw_trigger)(uint32_t irq, bool on);  /* solo test: AIC SW_SET/CLR, fake */
} pirq_ops_t;
```
- `poll` precondizione: chiamata a EL2 con IRQ/FIQ pendenti per `pcpu`. Postcondizione: ogni evento restituito è già riconosciuto (acknowledged) e, per `PEV_DEVICE`, **mascherato alla fonte**; un evento mai restituito due volte. Errori: ritorna 0 eventi se spurio; log se `PEV_UNKNOWN` (il chiamante non lo ignora in silenzio).
- Backend `pirq-aic-t8103`: `poll` legge `AIC_EVENT` (`base+0x2004`) finché 0, decodifica tipo (bit 23:16) e numero (15:0); controlla `IPI_SR_EL1`, i timer guest (`CNTP_CTL_EL02`, `CNTV_CTL_EL02` con `VM_TMR_FIQ_ENA_EL2`), `ICH_MISR_EL2` (→ `PEV_MAINT`), PMCR0/UPMCR0 (→ `PEV_PMU`, spegnimento) [V: F0 §1]. Non legge l'evento AIC nel ramo FIQ (bug D7 di Mu).
- Backend `pirq-gic-qemu`: `poll` esegue `ICC_IAR1_EL1` fisico a EL2; INTID 30/27 → `PEV_TIMER_P/V`; 25 → `PEV_MAINT`; SGI 0 riservata allo strato → `PEV_IPI`; altro → `PEV_DEVICE`; poi `ICC_EOIR1_EL1` fisico (priority drop + deactivate) subito, dato che la semantica level è gestita sopra con mask.
- Backend `pirq-fake` (host): coda di eventi programmabile dal test.

### 8.2 `ipi_ops`
```c
typedef struct ipi_ops {
    const char *name;
    wel2_err_t (*init_cpu)(uint32_t pcpu);
    void       (*send)(uint32_t pcpu_target);   /* IPI fisica di notifica; non bloccante */
    void       (*quiesce)(uint32_t pcpu);       /* azzera la IPI pendente sul core corrente */
} ipi_ops_t;
```
`ipi-aic-fast`: `IPI_RR_LOCAL_EL1`/`IPI_RR_GLOBAL_EL1`; layout campi da Linux (CPU [7:0], cluster [23:16], tipo [29:28]) e non da Asahi docs, che divergono [V: F0 §1 «discrepanze»]. `ipi-gic-sgi` (QEMU): SGI fisica 0 via `ICC_SGI1R_EL1` fisico a EL2.

### 8.3 `timer_src_ops`
```c
typedef struct timer_src_ops {
    const char *name;
    bool (*is_firing)(uint32_t pcpu, bool virt);            /* ENABLE && ISTATUS && !IMASK del guest */
    void (*mask_source)(uint32_t pcpu, bool virt);          /* M1: VM_TMR_FIQ_ENA_EL2; QEMU: ICENABLER del PPI fisico */
    void (*unmask_source)(uint32_t pcpu, bool virt);
} timer_src_ops_t;
```
Postcondizione di `mask_source`: il guest vede `CNTx_CTL` **immutato**.

### 8.4 `plat_ops`
```c
typedef struct plat_ops {
    const char *name;
    wel2_err_t (*early_init)(const void *fdt);              /* seriale minima, mappa EL2 */
    uint32_t   (*cpu_count)(void);
    uint64_t   (*pcpu_of_vcpu)(uint32_t vcpu);              /* mappa 1:1 */
    wel2_err_t (*cpu_start)(uint32_t pcpu, uint64_t entry_pa, uint64_t arg); /* M1: spin-table+SEV; QEMU: SMC PSCI CPU_ON */
    void       (*cpu_park)(void) __attribute__((noreturn)); /* CPU_OFF: WFI definitivo */
    void       (*reset)(void) __attribute__((noreturn));
    void       (*poweroff)(void) __attribute__((noreturn));
    void       (*uart_putc)(char c);
    const struct mem_region *(*regions)(uint32_t *n);       /* RAM, MMIO, riservate */
} plat_ops_t;
```
Errori: `cpu_start` su core già acceso → `WEL2_E_STATE` (il livello PSCI lo traduce in `ALREADY_ON`).

### 8.5 Registrazione dei gestori (`wel2-trap`)
```c
wel2_err_t trap_register_sysreg(uint32_t op0, op1, crn, crm, op2, sysreg_handler_t h, void *ctx);
wel2_err_t trap_register_mmio  (uint64_t base, uint64_t size, mmio_handler_t h, void *ctx);
wel2_err_t trap_register_hvc   (uint32_t fid_lo, uint32_t fid_hi, hvc_handler_t h, void *ctx);
```
Precondizioni: chiamate solo prima di `boot_start_guest()`; intervalli non sovrapposti (`WEL2_E_BUSY` altrimenti). Politica per accessi non registrati: **default-deny con log**, con risposta definita (sysreg: UNDEF al guest solo se non in lista RAZ/WI; HVC: `NOT_SUPPORTED`; MMIO fuori mappa: panic).

## 9. PSCI e SMCCC via HVC
Convenzione: `HVC #0`; ID in `w0`, argomenti `x1-x3`, risultato in `x0` (`x1-x3` azzerati quando non usati). Funzioni a 64 bit con prefisso `0xC4`; a 32 bit `0x84`. **[I]** gli ID sono a memoria dalle specifiche DEN0022/DEN0028.

| Funzione | ID | Risposta |
|---|---|---|
| `SMCCC_VERSION` | `0x8000_0000` | `0x0001_0001` (1.1) |
| `SMCCC_ARCH_FEATURES` | `0x8000_0001` | `NOT_SUPPORTED` per tutto (nessun workaround dichiarato) |
| `PSCI_VERSION` | `0x8400_0000` | `0x0001_0001` (PSCI 1.1, come QEMU [I]) |
| `CPU_SUSPEND` | `0x8400_0001`/`0xC400_0001` | `WFI` e `SUCCESS` (stato di standby); stati power-down → `INVALID_PARAMETERS` |
| `CPU_OFF` | `0x8400_0002` | non ritorna; la vCPU diventa `OFF`; `cpu_park()` |
| `CPU_ON` | `0x8400_0003`/`0xC400_0003` | `x1`=MPIDR virtuale, `x2`=entry (PA, allineato 4 byte), `x3`=context_id (→ `x0` all'ingresso). Errori: `INVALID_PARAMETERS` (MPIDR inesistente), `INVALID_ADDRESS` (entry non in RAM), `ALREADY_ON`, `ON_PENDING`, `INTERNAL_FAILURE` |
| `AFFINITY_INFO` | `0x8400_0004`/`0xC400_0004` | 0 ON, 1 OFF, 2 ON_PENDING |
| `MIGRATE_INFO_TYPE` | `0x8400_0006` | 2 (Trusted OS non migrabile / non presente) |
| `SYSTEM_OFF` | `0x8400_0008` | non ritorna: `plat_ops.poweroff` |
| `SYSTEM_RESET` | `0x8400_0009` | non ritorna: `plat_ops.reset` |
| `PSCI_FEATURES` | `0x8400_000A` | `SUCCESS` (0) per le funzioni sopra, `NOT_SUPPORTED` (-1) per le altre |
| qualsiasi altro `HVC` | — | `x0 = -1` (`NOT_SUPPORTED`), log-once. **Mai** UNDEF né hang (F-14) |
| `SMC` dal guest (EC 0x17, `TSC=1`) | — | `x0 = -1`, `ELR` avanzato |
Codici: `SUCCESS 0, NOT_SUPPORTED -1, INVALID_PARAMETERS -2, DENIED -3, ALREADY_ON -4, ON_PENDING -5, INTERNAL_FAILURE -6, NOT_PRESENT -7, DISABLED -8, INVALID_ADDRESS -9`.

Stato all'ingresso di una vCPU accesa con `CPU_ON`: EL1h, `DAIF` mascherati, MMU spenta, `x0=context_id`, altri registri azzerati; GIC virtuale della vCPU in reset (`ProcessorSleep=1` finché Windows non lo azzera). Un `CPU_ON` verso il core che sta chiamando → `ALREADY_ON`.

Funzioni vendor (hypervisor service, `0xC600_0000` +):
| ID | Funzione | Uso |
|---|---|---|
| `0xC600_0000` | `WEL2_VERSION` | restituisce `'WEL2'<<32 \| versione` |
| `0xC600_0001` | `WEL2_STATS` | copia i contatori nel buffer indicato (PA, lunghezza) |
| `0xC600_0002` | `WEL2_LOG` | accoda una riga nel ring (usabile da EDK2/driver di test) |
Un guest che non le conosce non le usa mai.

`HVC` con immediato diverso da 0 (es. `HVC #1`, convenzione dei hypercall Hyper-V) → `NOT_SUPPORTED`, così Windows conclude che non c'è hypervisor Hyper-V [I].

## 10. Convenzioni di naming ed errori
- Funzioni: `wel2_<modulo>_<verbo>` o `<modulo>_<verbo>` interno; costanti `WEL2_*`; tipi `*_t`; backend `<famiglia>-<piattaforma>`.
- Registri: nome architetturale in minuscolo con suffisso `_el1/_el2` nei wrapper `sysreg_read_icc_iar1_el1()`.
- Errori: sempre `wel2_err_t`; funzioni che non possono fallire ritornano `void`; i `panic` passano da `log_panic(code, fmt, …)` con codici stabili (`PANIC_S2_FAULT=1, PANIC_ISV0=2, PANIC_SERROR=3, PANIC_LOCK_TIMEOUT=4, PANIC_BAD_FDT=5, …`).
- Log: una riga = `[t=<cnt> cpu=<n> mod=<m> ev=<e>] campi`; i campi sono interi esadecimali; niente stringhe lunghe; il ring tiene 64 KB per CPU.
- Nessun `TODO` nel ramo principale; i limiti noti stanno in `RISKS.md`.

## 11. Esempi d'uso (sequenze attese)
1. **Avvio di una CPU secondaria (Windows):** `HVC #0` con `x0=0xC4000003`, `x1=1` (MPIDR vCPU1), `x2=entry`, `x3=ctx` → `psci_handle` → `plat_ops.cpu_start(pcpu1, trampolino, vcpu1)` → ritorna `0` → la pCPU1 esegue il trampolino a EL2, inizializza per-CPU, imposta lo stato del reset del vGIC e fa `ERET` a `entry` con `x0=ctx`.
2. **Abilitazione di un SPI da parte di Windows:** scrittura a `GICD_ISENABLER1` (`0xF_0000_0104`) bit 25 → trap MMIO → `vgic_mmio_write` → stato `enabled[57]=1` → azione `pirq.unmask(25)` e `pirq.set_target(25, mask(IROUTER[57]))`.
3. **Timer:** vedi ARCHITECTURE §6.2; il test M0 verifica la sequenza con una sorgente finta.
