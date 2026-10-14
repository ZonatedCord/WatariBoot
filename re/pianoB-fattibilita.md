# Piano B (strato EL2 con GICv3 virtuale): fattibilita e costo, stato al 2026-10-14

Metodo: solo lettura (`gh api`, WebSearch, WebFetch), niente clone, niente download, niente installazioni. Etichette: [verificato: fonte] = letto direttamente; [ipotesi] = deduzione mia. Percorsi abbreviati: `m1n1/` = AsahiLinux/m1n1 `src/hv/`; `NTASP/` = NT-for-ASi/m1n1_windows `src/`; `KVM/` = torvalds/linux `arch/arm64/kvm/`.

## 0. Verdetto

Il piano B e` **teoricamente possibile: SI, confidenza alta (~85%)**. Non c'e` nessun ostacolo architetturale; i rischi sono di ingegneria e di tempo, non di fattibilita.

Le tre prove piu` forti:
1. **Windows ARM64 avvia su un GICv3 con CPU interface virtuale e distributor emulato.** Nel nostro repo V1 (QEMU/HVF, GICD + GICR + MSI frame + PSCI) arriva alla schermata di blocco [verificato: docs/FALLIMENTI.md F-11, F-14 "V1 ... la raggiunge a 120 s"]. In piu` Windows 11 ARM64 gira come guest KVM su M1 sotto Asahi Linux (`-cpu host -enable-kvm`, ramfb) [verificato: AsahiLinux/docs `docs/sw/windows-11-vm.md`]: la combinazione "M1 + vGICv3 hardware + Windows" e` gia` in uso, anche se con ARM virtio e non con periferiche reali.
2. **L'M1 ha l'interfaccia CPU GICv3 virtuale in hardware, con 8 list register.** [verificato: NTASP/hv_vgic.c righe 51-62: "8 list registers", 32 livelli di priorita`, 16 bit di ID, TDIR supportato, no GICv4, no legacy; KVM/hyp/vgic-v3-sr.c ricava `max_lr_idx` da `ICH_VTR_EL2`, e KVM fa girare guest GICv3 su M1 dal 5.14.] Va emulato solo il distributor/redistributor (MMIO), cosa che lo stage-2 consente.
3. **I pezzi di hypervisor su M1 esistono e funzionano**: m1n1 `hv` (stage-2, trap MMIO, piu` vCPU, timer guest via FIQ, PSCI/smp) e il patch set KVM "Initial host support for the Apple M1" (Zyngier, 2021, in 5.14). E NTASP li ha gia` uniti in un `hv_vgic.c` (incompleto).

Stima: (a) prototipo QEMU TCG con Windows che avvia sopra il nostro strato: **8-12 settimane** (intervallo 6-16); (b) Windows su MacBook vero fino alla schermata di blocco con framebuffer e USB: **ulteriori 14-24 settimane** dopo (a), di cui buona parte parallelizzabile (driver USB/PHY, bring-up su hardware). Totale realistico **5-8 mesi**, un solo sviluppatore con agenti [ipotesi, vedi par. 9]. Rischi principali: par. 9.

## 1. GIC virtuale sul M1

- **Interfaccia CPU virtuale in hardware.** [verificato: NTASP/hv_vgic.c 51-62] Apple "implementa le registri dell'interfaccia CPU GIC": 32 livelli di priorita`/preemption (5 bit), ID a 16 bit, 3 livelli di affinita`, `ICC_SRE_EL2.SRE` riservato a 1 (niente GICv2), TDIR (FEAT_GICv3_TDIR), **8 list register**, no iniezione diretta (non GICv4, niente NMI), no bypass IRQ/FIQ, nessun range esteso SPI/PPI. Il codice NTASP usa `ICH_LR0..7_EL2`, `ICH_VMCR_EL2`, `ICH_HCR_EL2`, `ICH_ELRSR_EL2` [verificato: hv_vgic.c 1578-1700].
- **Errata M1/M2: SEIS rotto.** Su M1, M1 Pro/Max e M2 il vGIC genera SError locali quando il guest scrive male i registri GIC; KVM ha un workaround ristretto a queste CPU [verificato: commit torvalds/linux d11a327ed9 "Restrict SEIS workaround to known broken systems", cae889302e "List M1 Pro/Max", decb17aeb8 "Add Apple M2"]. NTASP lo sa ("an errata here exists on pre-M3 SoCs that can result in a host SError") [verificato: hv_vgic.c 53-55]. Il nostro strato deve gestire gli SError da `ICH`/`ICC`, o annunciare SEIS=0 (KVM: `vgic_hcr |= ICH_HCR_EL2_vSGIEOICount`... il dettaglio va letto in `vgic-v3.c`) [ipotesi sul metodo].
- **Maintenance interrupt.** L'M1 ha un maintenance interrupt per CPU, ma non e` cablato in un controller mascherabile: arriva come evento FIQ-like. [verificato: lore "Apple M1 KVM", commit 43c5c868bd "vgic: Allow registration of a non-maskable maintenance interrupt", 13aad0c00b "irqchip/apple-aic: Register vgic maintenance interrupt with KVM"; AsahiLinux/linux `irq-apple-aic.c` ~righe 424-445: dopo il loop eventi legge `ICH_MISR_EL2`, se non zero chiama `AIC_FIQ_HWIRQ(AIC_VGIC_MI)`; se KVM non lo ha gestito disabilita `ICH_HCR_EL2.En`; enum `AIC_VGIC_MI` riga 237, mapping riga 1108.] Nel nostro strato EL2 il MI va letto da `ICH_MISR_EL2` a ogni uscita/IRQ, senza dipendere da un controller.
- **Distributor/redistributor in software.** KVM li emula con trap MMIO stage-2. Dimensione: `vgic.c` 1350 righe, `vgic-mmio.c` 1131, `vgic-mmio-v3.c` 1172, `vgic-v3.c` 1055, `vgic-init.c` 845, `vgic-kvm-device.c` 819 (la parte API per userspace, non serve a noi), `vgic-v4.c` 555, `vgic-its.c` 2853 (ITS), `hyp/vgic-v3-sr.c` 1272 [verificato: conteggio righe via `gh api`, HEAD 2026-10-14]. Il nucleo che ci serve (MMIO v3 + stato LR + dispatch SGI) e` circa **4-5 mila righe in KVM**, ma KVM e` generico (live migration, nested, GICv2, GICv4). Per noi senza ITS (le MSI di PCIe Apple non usano ITS) la stima e` **2.500-4.000 righe C** [ipotesi].
- **Windows non richiede ITS.** [verificato: F-14/F-11: V1 senza ITS, con GICD+GICR+MSI frame, raggiunge la schermata di blocco].

## 2. m1n1 hypervisor (AsahiLinux/m1n1 `src/hv/`)

Righe: `hv.c` 513, `hv_vm.c` 1433, `hv_exc.c` 683, `hv_virtio.c` 308, `hv_vuart.c` 150, `hv_aic.c` 95, `hv_wdt.c` 171, `hv_asm.S` 196, `hv.h` 136: circa **3.700 righe** [verificato: conteggio via `gh api`].

Cosa sa fare:
- **Stage-2**: `hv_vm.c` programma `VTCR_EL2`/`VTTBR_EL2` (granulo 16 KB, PA completo), mappe `hv_map_hw` (identita` RAM), `hv_map_hw_ro`, `hv_map_sw`, `hv_map_hook` (trap MMIO con callback in C), `hv_pa_rw` [verificato: hv_vm.c 134-142, 309-398, 973]. Mappa identita` pronta; stage-2 a 16 KB e` indipendente dal granulo dello stage-1 di Windows (4 KB) [ipotesi, architettura ARM].
- **Piu` vCPU**: `hv_start_secondary`, `hv_init_secondary` copiano HCR/VTCR/VTTBR/... sui core secondari; `smp_call4`; `hv_started_cpus[]` [verificato: hv.c 284-370]. Ma la gestione eccezioni e` "slow single-threaded path": solo una CPU alla volta e` "interrompibile" per il proxy (`hv_pinned_cpu`/`interruptible_cpu`) [verificato: hv_exc.c 599-640].
- **Timer guest**: `hv_update_fiq()` imposta `HCR_EL2.VF` quando il timer fisico/virtuale EL02 e` `ISTATUS|ENABLE` e gestisce `VM_TMR_FIQ_ENA_EL2` (`ENA_P`, `ENA_V`); `hv_exc_fiq()` maschera `CNTP_CTL`/`CNTV_CTL` con `IMASK` e inoltra [verificato: hv_exc.c 152-185, 599-640; `SYS_IMP_APL_VM_TMR_FIQ_ENA_EL2` e` anche in AsahiLinux/linux irq-apple-aic.c riga 187]. Quindi m1n1 NON fa un vGIC: inietta una FIQ virtuale e il guest parla con l'AIC direttamente (Linux/macOS).
- **IPI**: m1n1 usa le Fast IPI `SYS_IMP_APL_IPI_SR_EL1` e le segnala al guest come FIQ virtuale (`ipi_pending`) [verificato: hv_exc.c 171, 649-657].
- **Overhead**: nessuna misura pubblicata nei sorgenti letti; il progetto lo presenta come strumento di tracing, non di produzione [ipotesi dalla documentazione `m1n1-hypervisor.md`, che parla solo di debug di macOS con il proxyclient].
- **Il proxy e` indispensabile?** Nel codice C: ogni eccezione non gestita (`hv_exc_proxy` -> `uartproxy_run`) cede il controllo al proxy Python via UART/USB e **il guest si ferma finche` l'host risponde** [verificato: hv_exc.c 43-131, `uartproxy_run(&start)` riga 71; docs: `run_guest.py` dal proxyclient Python, DFU port, riga 116-130 di m1n1-hypervisor.md]. Le hook MMIO in C girano senza proxy, ma setup (mappe, hook, caricamento del guest, hv_start) e` pilotato da Python. Per un uso "di produzione" il C va ristrutturato in un avvio autonomo: setup statico in C, eccezioni inattese = log + reset invece di proxy. [ipotesi, lavoro stimato 2-3 settimane]. Il watchdog (`hv_wdt.c`, 171 righe) e` anch'esso pensato per il debug.

## 3. NTASP `m1n1_windows`

- Righe: `hv_vgic.c` **1900**, `hv_psci.c` **1744** [verificato: `gh api ... | wc -l`]. Licenza: SPDX `BSD-2-Clause-Patent OR MIT` (vgic), `MIT OR BSD-3-Clause` (psci) [verificato: re/ntasp-analisi.md par. 6]. L'intestazione di hv_vgic.c dice "Inspiration borrowed from the KVM vGIC driver": rischio di contaminazione GPL-2.0 da verificare file per file [verificato: header; conseguenza: ipotesi].
- **Stato di completezza** (marcatori nel sorgente) [verificato: hv_vgic.c]: ISPENDR/ICPENDR/ISACTIVER/ICACTIVER "not implemented" (righe 487, 527, 565, 602); "bank GICD_IGROUPR0 for cores 0-7 ... since we're booting with 1 core atm, we can ignore" (368, 708); molte `TODO: do the AIC operation associated with this` (1082-1224); "we only support the timer interrupt right now" (634, 795); GICD_SGIR e ICC_SGI1R: azioni "TODO: actually do the action here" (1005, 1014); ITS mappato ma "Disabled for now" (1880-1887). `ENABLE_VGIC_MODULE` e` una macro di compilazione.
- **Iniezione timer**: `hv_exc.c` di NTASP chiama `hv_vgic3_inject_irq` per i timer quando c'e` un LR libero, altrimenti accoda in `timer_queue` (righe 186-239, "TODO: proper injection") [verificato]. Priorita` lette per INTID 17/18: da controllare (PPI timer EL1 sono 27/30 in GIC standard) [ipotesi: potrebbe essere una mappatura interna].
- **PSCI**: `hv_psci.c` e` "a straight port of the implementation in ARM Trusted Firmware-A", con `CPU_ON` (righe 487-488, 1687), alberi di power domain per T8103/T8112/T600x, ma molti "TODO" (SYSTEM_OFF come placeholder reboot, deep sleep non fatto) [verificato].
- **Windows ci fa il boot?** Non c'e` prova pubblica. Il README di Mu dice "hangs on the handoff between bootloader and kernel"; il sito dice "barely booting ... only Windows to Go boots tested" (vedi `re/ntasp-analisi.md` par. 4) [verificato per quanto pubblico; in tensione tra loro].
- **Riuso**: come **riferimento di struttura e di registri** (offset GICD/GICR, layout LR, albero PSCI) ha valore; come codice da portare e` immaturo (1 core, pending/active mancanti, nessun SMP provato). Meglio un nostro vGIC piccolo e testato, guardando NTASP e KVM come riferimento [ipotesi]. Il giudizio di qualita` del loro lato UEFI (`re/ntasp-audit-qualita.md`: "da riscrivere") suggerisce cautela anche qui.

## 4. Timer e FIQ nel piano B

- Sull'M1 i timer ARM (CNTP/CNTV) sono cablati come FIQ e non sono PPI di un GIC: l'AIC li espone come eventi FIQ e c'e` un registro `VM_TMR_FIQ_ENA_EL2` per i timer "guest" (EL02) [verificato: AsahiLinux/linux irq-apple-aic.c 187-189, 500-526, 580-600: `AIC_TMR_EL02_PHYS/VIRT`, `VM_TMR_FIQ_ENABLE_P/V`].
- **m1n1 (hv_exc.c)**: `hv_update_fiq()` e `hv_exc_fiq()` (vedi par. 2) trattano il timer guest come FIQ virtuale.
- **KVM su M1**: la FIQ fisica e` gestita come IRQ mentre gira un guest ("Handle physical FIQ as an IRQ while running a guest", nel branch kvm-arm64/m1 del 2021) [verificato: sintesi dalla serie lore "KVM: arm64: Initial host support for the Apple M1" v3/v4, merge 32e92b71b3, 2021-06-06; il corpo del patch non e` stato letto]. Il KVM generico poi presenta al guest il timer come PPI 27/30 del vGIC.
- **Nel nostro strato**: HCR_EL2 imposta `IMO|FMO` (KVM lo fa in `hyp/vgic-v3-sr.c` righe 467-482) [verificato]; la FIQ del timer arriva a EL2, si legge `CNTP/CNTV_CTL`, si maschera il timer (`IMASK`), si inietta in un LR l'INTID PPI 27 (virtuale) o 30 (fisico non sicuro EL1), e si riarma quando Windows scrive il timer (trap `CNTx_CTL_EL0` oppure `VM_TMR_FIQ_ENA` come m1n1). **Fattibile**, con il costo di 1 uscita per tick (Windows ha un tick da 1-15 ms) [ipotesi].
- **PMU FIQ**: gestire come m1n1 (`PMCR0_IMODE_FIQ|IACT`) o disabilitarlo, altrimenti tempesta di FIQ [verificato: hv_exc.c 642-650; vedi audit-qualita D9].

## 5. IPI e SMP

- **Windows -> ICC_SGI1R_EL1**: trap a EL2 (`ICH_HCR_EL2.TALL1`... o TC; KVM usa `vgic_v3_dispatch_sgi` in `vgic-mmio-v3.c` riga 1101 [verificato]) e routing verso la vCPU destinataria; la vCPU di destinazione viene "sbloccata" da `kvm_vcpu_kick` (vgic.c righe 452/508/850) [verificato].
- **Sul M1** l'IPI fisico e` AIC: AICv1 ha IPI legacy (`AIC_IPI_SEND 0x2008` con bit per CPU) e Fast IPI (registri `IPI_*` di sistema) [verificato: irq-apple-aic.c riga 88-93, 843; hv_exc.c 649]. Linux su M1 usa IPI via AIC nella variante `aic1_fipi_info` / `aic1_local_fipi_info` [verificato: irq-apple-aic.c 272-316]. Nel nostro strato: SGI di Windows -> aggiunge l'SGI al pending della vCPU target -> invia un IPI AIC (legacy o fast) alla CPU fisica -> a EL2 la FIQ/IRQ IPI inietta nell'LR. Hypervisor 1:1 vCPU = core fisico, nessuno scheduler. [ipotesi di progetto, supportata dai singoli meccanismi verificati]. m1n1 fa gia` il pezzo inferiore (`smp_send_ipi`, hv.c righe 375-401).
- **Nota di sicurezza**: il nostro stesso `docs/FALLIMENTI.md` F-15 mostra che Windows con parking protocol va in 0x5C; con PSCI+vGIC (V1) invece e` stato visto avviare con `-smp 4`. Con il piano B si usa PSCI (CPU_ON), che e` il percorso gia` collaudato. [verificato: F-15, V1]

## 6. Periferiche in passthrough

- Stage-2 in identita` per RAM e MMIO periferico (`hv_map_hw` per tutte le regioni, con trap solo sulle regioni GIC finte e su quelle da proteggere) e` esattamente cio` che fa m1n1 per macOS: NVMe/ANS, USB (dwc3 + atcphy), DART, display (dcp/DisplayPort) restano reali e il guest li pilota [verificato: macOS gira sotto m1n1 hv con periferiche reali, docs `m1n1-hypervisor.md`].
- **Interrupt AIC -> SPI virtuali.** L'AIC non e` un GIC: gli IRQ sono eventi con registri `AIC_EVENT 0x2004`, `MASK_SET/CLR`, `TARGET_CPU` (v1) [verificato: audit-qualita D2-D5]. Strato EL2: abilitare solo gli IRQ che Windows vuole (`GICD_ISENABLER` trap), IRQ fisica -> EL2 (HCR.IMO), legge l'evento, maschera l'IRQ nell'AIC, inietta SPI `32+n` nell'LR, e smaschera all'EOI. Dato che `LR.HW=1` richiede un INTID fisico GIC (che non esiste), serve **EOI software**: bit `EOI` dell'LR + maintenance interrupt, oppure trap su `ICC_EOIR1_EL1` (TDIR/TC) [ipotesi, ma e` il modo in cui NTASP prevede di fare: parametro `hw_status`, `ICH_LR_MAINTENANCE_IRQ` in `hv_vgic3_inject_irq`, hv_vgic.c 1707-1722, verificato].
- **DART (IOMMU)**: Windows programma i DART con indirizzi fisici = indirizzi guest = fisici (identita`): funziona. Il rischio e` che un driver sbagli e faccia DMA sulla memoria dello strato EL2 (nessuna protezione perche` l'M1 ha DART, non SMMU); mitigazione: lo strato programma i DART per negare la propria regione, o lascia il DART di sola `hv` non mappato [ipotesi]. NTASP ha `hv_smmu.c` ma non l'ho letto.
- **Cose che lo impedirebbero (nessuna trovata, ma da verificare)**: (i) memoria dello strato EL2 deve essere tolta dalla mappa di memoria che si da` a Windows (UEFI memory map): banale [ipotesi]; (ii) gli SPI hanno un solo livello di priorita` e target di CPU, quindi 1 IRQ per volta per core: ok; (iii) Windows deve pilotare ANS (NVMe Apple): driver StorPort da scrivere, indipendentemente dal piano B (`drivers/`, il lavoro e` lo stesso sia nel piano A che in B). Il passthrough non lo impedisce.
- Una periferica che sul M1 usa **MSI PCIe** (Wi-Fi, ecc.) richiede MSI -> SPI virtuale: fuori dallo scopo del traguardo (b).

## 7. Prototipo in QEMU

- **HVF con EL2 nel guest: NO su questo Mac.** QEMU rifiuta: "mach-virt: HVF does not support providing Virtualization extensions to the guest CPU" [verificato: WebSearch -> patch Alexander Graf "hw/arm/virt: Extend nested and mte checks to hvf" (2021) e commit QEMU 2025 che limita un test Xen annidato a TCG: "QEMU only supports accelerating EL0 and EL1"]. Apple espone la virtualizzazione annidata solo nel Virtualization.framework (`isNestedVirtualizationSupported`, solo M3 o successivi, macOS 15) [verificato: forum Parallels/hada.io via WebSearch], non in Hypervisor.framework. **Il nostro Mac e` un M1: non c'e` nested in nessun caso.** (La domanda "M3+ con macOS 15" e` confermata ma irrilevante per noi.)
- **TCG con `virtualization=on`**: QEMU emula EL2 e il GICv3 con le estensioni di virtualizzazione (registri `ICH_LR*`, `ICH_VTR`, maintenance interrupt) [verificato: qemu hw/intc/arm_gicv3_cpuif.c (funzioni `ich_lr_*`); hw/arm/virt.c riga 1363 `has-virtualization-extensions`, ARCH_GIC_MAINT_IRQ]. Quindi un nostro strato EL2 puo` girare in TCG. Limiti: (i) **lento** (Windows 11 ARM64 sotto TCG: ordine di decine di minuti per il boot, qualche ora per l'OOBE) [ipotesi]; (ii) il GIC emulato da QEMU ha un distributor reale mentre l'M1 no: il nostro strato deve comportarsi come se non lo vedesse (ignorare la GICD vera e usare solo la CPU interface). Va fatto un modello "AIC" minimo in QEMU per provare il percorso IRQ AIC -> SPI, e anche questo e` lavoro nuovo (vedi F-06: oggi non c'e` un AIC in QEMU).
- **Alternativa: sviluppare direttamente su M1 con m1n1 tethered** (host = secondo computer), come fa NTASP: veloce, ma richiede il debug host e rischia hang dell'hardware. Combinazione sensata: strato scritto in C portabile (EL2), prima su TCG, poi su M1.
- Con HVF, per il test del solo vGIC *software*, si potrebbe far girare lo strato come "guest EL1" senza EL2? **No**: serve EL2 per i trap [ipotesi forte].

## 8. Prestazioni attese

- Con vGIC a interfaccia CPU hardware, l'overhead per uscite di MMIO (GICD/GICR) e` pagato solo nei pochi accessi di configurazione; l'ack/EOI/priorita` degli IRQ avviene in hardware senza uscite [verificato per architettura GICv3 e per il fatto che l'M1 implementa i registri ICV_*; KVM lo sfrutta]. Su ARM i costi dominanti sono l'iniezione degli interrupt e i trap, non il stage-2 [verificato: Dall et al., "ARM Virtualization: Performance and Architectural Implications", ISCA 2016, https://www.cs.columbia.edu/~nieh/pubs/isca2016_armvirt.pdf: overhead per carichi reali comparabile a x86, e dominato dal design software dell'hypervisor, non dalla velocita` delle transizioni].
- Il nostro caso e` migliore del KVM generico: nessuno scheduler, 1 vCPU = 1 core, niente emulazione di dispositivi, stage-2 identita` con mappe a blocco grandi (TLB footprint basso; il granulo 16 KB, mappe fino a 32 MB/1 GB, di m1n1 e` buono) [ipotesi]. Per carichi CPU puri e GPU/disk passthrough l'overhead atteso e` **sotto il 3%**, il nostro stimato "1%" del PLAN.md e` plausibile ma non misurato [ipotesi]. Il costo vero e` sulle interruzioni: ogni IRQ periferica = IRQ fisica a EL2 + iniezione + EOI software (2-3 uscite, ordine 1-3 microsecondi ciascuna) [ipotesi; da misurare], rilevante solo ad alti ritmi di IRQ (NVMe a pieno regime, rete).
- Lo stage-2 va attivato con TLB tag (VMID) e senza DSB inutili; evitare il "slow single-threaded path" di m1n1 [verificato per il comportamento di m1n1: hv_exc.c 599-640].

## 9. Verdetto, stima e rischi

**Possibile: SI, confidenza ~85%.** Non ho trovato nessun ostacolo di principio. Quello che non e` provato: Windows che usa le periferiche *reali* dell'M1 (ANS, USB, DART) sopra l'hypervisor: nessuno l'ha pubblicato (NTASP ci prova da piu` di un anno senza un boot documentato).

Stima del lavoro (uno sviluppatore + agenti Sonnet, ore di calendario realistiche, non di CPU) [ipotesi]:

| Parte | Settimane |
|---|---|
| Progetto dettagliato + ADR (morasca-agent), scelta del riuso m1n1 vs scrittura nostra | 1 |
| Scheletro EL2: entry da UEFI, stage-2 identita`, trap sysreg, eccezioni, log seriale (riuso m1n1 hv_vm/hv_exc dove possibile) | 2-3 |
| vGICv3: GICD/GICR MMIO, LR, pending/active, priorita`, SGI via `ICC_SGI1R`, timer PPI, MI; test unitari su QEMU | 3-4 |
| PSCI (CPU_ON, SYSTEM_RESET/OFF, VERSION) + SMP 4/8 core | 1 |
| Integrazione con il firmware/ACPI (MADT/GTDT con GICD finto, FADT con PSCI via HVC) e boot Windows in TCG | 1-2 |
| **(a) Totale prototipo QEMU TCG con Windows** | **8-12 (6-16)** |
| Porting su M1 reale: startup autonomo senza proxy, watchdog, SError/SEIS, AIC -> SPI, IPI AIC | 4-6 |
| Windows: storage (USB mass storage con XHCI standard dopo bring-up PHY; ANS NVMe e` un driver nuovo, 6-10 sett. da solo) | 4-10 |
| Display da framebuffer iBoot/GOP; tastiera USB; tastiera interna SPI-HID opzionale | 2-4 |
| Debug su hardware (hang, DART, cache, errata) | 4-8 |
| **(b) Aggiunta fino alla schermata di blocco** | **14-24** |

Percorso minimo consigliato per ridurre (b): avvio da **USB-C** con tastiera e disco USB (XHCI standard) e **senza** ANS nativo, come fa NTASP ("Windows to Go") [verificato: re/ntasp-analisi.md par. 4, 5]; ANS e SPI dopo il traguardo.

Rischi principali, in ordine:
1. **Errata SEIS/SError dell'M1 sul vGIC** (trap non gestiti = panic dell'hypervisor) [verificato: commit KVM d11a327ed9, cae889302e]. Alto, ma KVM mostra la strada.
2. **Fedelta` del vGIC verso Windows**: Windows controlla GICD_TYPER, IIDR, GICR_TYPER, `GICD_CTLR.RWP` ecc.; un registro sbagliato = hang senza bugcheck (come F-14). Si mitiga con confronti contro QEMU GICv3 sul bring-up V1 (che funziona).
3. **Il codice m1n1 hv e` pensato per il debug** (proxy, single-thread, watchdog): rendere autonomo il C e` lavoro non banale e senza test automatici [verificato par. 2].
4. **Interrupt AIC -> SPI senza `LR.HW`**: EOI software e maintenance interrupt non mascherabile, rischio di tempeste/starvation se sbagliato [verificato par. 1; il resto e` ipotesi].
5. **Driver Windows (ANS, DART/PEP, SPI-HID)**: il piano B non li elimina; il costo e` lo stesso del piano A. Firma dei driver per la distribuzione (PLAN.md).
6. **Vincolo di progetto**: il CLAUDE.md di Watari Boot dice "niente hypervisor ... salvo decisione esplicita dell'utente": serve la decisione di Marco prima di partire; il piano B e` anche, in pratica, la strada di NTASP (hobby project, 1 persona attiva), quindi c'e` una possibile duplicazione di sforzi e di scoperte.
7. **Licenze**: codice derivato da KVM = GPL-2.0; m1n1 e` MIT; mischiare richiede attenzione agli header (CLAUDE.md di progetto).
8. **Debug host**: per il bring-up su M1 serve un secondo computer (DFU + proxy) o una seriale; il piano non lo cita se non per la Fase 2.

Prossimo passo suggerito (non fatto): sessione di progettazione `morasca-agent` -> `docs/design/pianoB/` e uno spike di 1 settimana: strato EL2 minimo in QEMU TCG che attiva stage-2 + ICH_* e inietta un solo PPI timer in un guest Linux (prova che il percorso funziona prima di affrontare Windows).

## 10. Fonti
- Repo: NT-for-ASi/m1n1_windows (`src/hv_vgic.c`, `hv_psci.c`, `hv_exc.c`, `Makefile`); AsahiLinux/m1n1 (`src/hv/*`); AsahiLinux/linux ramo `asahi` (`drivers/irqchip/irq-apple-aic.c`); torvalds/linux (`arch/arm64/kvm/vgic/*`, `hyp/vgic-v3-sr.c`, commit d11a327ed9, cae889302e, decb17aeb8, 43c5c868bd, 13aad0c00b, e4f7417e96, 32e92b71b3, 08f4f41c1e); qemu/qemu (`hw/intc/arm_gicv3_cpuif.c`, `hw/arm/virt.c`); AsahiLinux/docs (`docs/sw/m1n1-hypervisor.md`, `windows-11-vm.md`).
- Web: https://lore.kernel.org/kvm/87cztgvyns.wl-maz@kernel.org/T/ (serie "Initial host support for the Apple M1", la pagina diretta ha risposto 403, ho usato i riassunti dei risultati di ricerca); https://lkml.iu.edu/hypermail/linux/kernel/2301.0/01203.html ("Allow registration of a non-maskable maintenance interrupt"); https://patchew.org/QEMU/20211123122859.22452-1-agraf@csgraf.de/ ; https://qemu.googlesource.com/qemu/+/72846594b089f03bed1bb46958c910afcb859d49 ; https://forum.parallels.com/threads/macos-15-sequoia-nested-virtualization-for-m3-macs.364397/ ; https://www.cs.columbia.edu/~nieh/pubs/isca2016_armvirt.pdf .
- Limiti: non ho letto il corpo dei patch KVM "Handle physical FIQ as an IRQ" e "Be tolerant to the lack of maintenance interrupt" (solo titoli e riassunto); non ho letto `hv_smmu.c` ne` `hv_vtpm.c` di NTASP; il numero di righe di `hv_vgic.c` include commenti e codice morto; nessuna misura di prestazioni e` stata fatta, tutti i numeri del par. 8 sono stime.
