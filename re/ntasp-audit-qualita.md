# Audit di qualità del codice NTASP (apple_silicon_platforms_mu), stato al 2026-10-12

Metodo: solo lettura via `gh api` (contents, commits, trees, search) e nessun file salvato nel repository. Repo: `NT-for-ASi/apple_silicon_platforms_mu`, HEAD `861dcc49` (2026-09-06, branch `main`).
Riferimenti Asahi: `AsahiLinux/linux` `drivers/irqchip/irq-apple-aic.c`, `AsahiLinux/m1n1` `src/aic.c` + `src/aic_regs.h`, `arch/arm64/boot/dts/apple/t8103.dtsi`.
Etichette: [verificato] = letto direttamente nei sorgenti citati; [ipotesi] = deduzione. I numeri di riga sono quelli del file all'HEAD.
Percorsi abbreviati: `AIC/` = `Silicon/Apple/AppleSiliconPkg/Drivers/AppleAicDxe/`, `T810X/` = `Silicon/Apple/T810XFamilyPkg/`.

## 0. Verdetto sintetico

| Componente | Voto | Motivo in una riga |
|---|---|---|
| AppleAicDxe, parte AICv1 (quella che serve a t8103) | **da riscrivere** | `AicV1/AppleAicV1Dxe.c` e` uno stub con `//TODO: this driver.`; il percorso v1 passa per il codice v2 con offset sbagliati |
| AppleAicDxe, parte AICv2 e glue comune | **da riscrivere** (scheletro riusabile solo come traccia) | bug verificati nelle macro dei registri, nel decode dell'evento, nella gestione FIQ/PMC |
| MADT_Static (T810X) | **da riscrivere** | descrive un GICv3 virtuale (distributor, redistributor, ITS), valori hard-coded e inventati |
| GTDT (T810X e generico) | **da riscrivere** | GSIV arbitrari 17..20, indirizzo watchdog di un altro SoC, due GTDT installate |
| FADT (j293) | **da riscrivere** | PSCI via SMC "trappata" dall'hypervisor; campo minor revision errato |
| CSRT (T810X) | **da riscrivere (non esiste)** | il file e` solo commenti e non e` nemmeno nell'INF |
| BERT (T810X) | **da scartare** | non compila (`BOOT_ERROR_REGION_BASE` indefinito) e non e` nell'INF |
| PPTT (T810X) | **da riusare con correzioni** | topologia coerente con DTS; manca solo quanto e` intenzionalmente tolto (cache) |
| DSDT j293 (albero CPU, UART, XHC) | **da riusare con correzioni** | struttura valida, ma IRQ UART diverso dal DTS e HID XHCI sospetto |
| DBG2 j293 | **da riusare con correzioni** | corretto nella sostanza, allineamento non verificato |
| Struttura package / DSC / FDF | **da riusare con correzioni** | impianto Mu standard, ma residui di copia-incolla e configurazione incoerente |
| Test e CI | **assenti** | nessun test, nessun workflow, 0 run di Actions |

Conclusione: il codice NTASP lato UEFI non e` di qualita` sufficiente per un riuso diretto nel percorso nativo. Si riusano idee e dati (elenco dei registri, albero CPU/cluster, PPTT, mappe MMIO dopo averle verificate col DTS), non il codice. Dettagli sotto.

## 1. AppleAicDxe

### 1.1 Stato dichiarato
- `AIC/Readme.md` righe 45-47: "AICv2 driver finished, AICv1 in progress". Riga 7: "work in progress, bugs are expected". Riga 33: si consiglia di usare un hypervisor con vGIC "while the AIC support is experimental". [verificato]
- Il t8103 usa AICv1 (ADT `aic,1`, compatible Linux `apple,t8103-aic`, `aic1_local_fipi_info`; m1n1 `aic_init`: "aic,1" => v1). [verificato in irq-apple-aic.c righe 265-330 e m1n1 aic.c]. Quindi per il nostro SoC la parte "finita" (v2) non e` quella giusta.

### 1.2 Difetti verificati (con prove)

D1. AICv1 non implementato. `AIC/AicV1/AppleAicV1Dxe.c` righe 19-23: la funzione contiene solo `//TODO: this driver.` e `return EFI_SUCCESS`. `AIC/AicV1/AArch64/AppleAicV1.S` contiene solo un `#include`. In `AIC/AppleAicDxe.c` righe 41-43 `APPLE_AIC_VERSION_1` cade nel `case` di `AppleAicV2DxeInit(...)`: il v1 gira sul codice v2. [verificato]

D2. Macro maschera IRQ sbagliata. `Include/Library/AppleAicLib.h` riga 103: `#define AIC_MASK_BIT(num) BIT(num) & GENMASK(4, 0)`. Espansione: `(1UL << num) & 0x1f`, senza parentesi esterne. Per `num >= 5` il risultato e` 0, quindi `MASK_SET`/`MASK_CLR` ricevono 0 e nessuna IRQ oltre la 4 puo` essere mascherata o smascherata, e `AppleAicReadInterruptState` ritorna sempre FALSE. m1n1 `aic.c` riga 10 fa giusto: `BIT((x) & GENMASK(4, 0))`. [verificato] Gravita` massima: l'unico controllo per-IRQ del driver e` inerte.

D3. Registro evento v1 sbagliato. `AIC/AicV2/AppleAicV2Dxe.c` riga 195: per v1 `mAicV2EventReg = AicV2Base;` (offset 0). Il registro evento v1 e` `base + 0x2004` (Linux `AIC_EVENT 0x2004`, m1n1 `AIC_EVENT 0x2004`). `AppleAicLib.h` definisce `AIC_V1_EVENT_REG 0x2004` ma non viene mai usato. [verificato] Ogni ack su t8103 legge `AIC_RESERVED/VERSION` invece dell'evento.

D4. Enable/config sbagliato su v1. `AppleAicV2Dxe.c` righe 535 e 478: `AIC_V2_CONFIG` (0x14) scritto anche su v1. Linux abilita `AIC2_CONFIG` solo per `version == 2 || 3` (irq-apple-aic.c, init). Su v1 si scrive un offset non documentato. [verificato]

D5. Target CPU v1 mai programmato. Linux scrive `target_cpu + i*4 = 1` per tutte le IRQ v1 all'init (irq-apple-aic.c righe 1082-1084) e `BIT(cpu)` per ogni IRQ che si abilita (riga 461). In Mu `AIC_TARGET_CPU` e` usato solo come "start offset" per calcolare gli offset (riga 194); non c'e` nessuna scrittura. [verificato per assenza nei file letti] Senza questo le IRQ v1 non sono instradate a nessuna CPU [ipotesi sull'effetto, il comportamento Linux e` verificato].

D6. Evento non decodificato, indice fuori limiti. `AppleAicV2Dxe.c` righe 247-248: `AicInterrupt` (evento grezzo: die[31:24], tipo[23:16], numero[15:0]) e` usato direttamente come indice in `AicRegisteredInterruptHandlers[...]`. Linux (righe 401-419) estrae `type` e `irq` con `FIELD_GET` e distingue IRQ (1) da IPI (4). In piu` l'array e` allocato con `NumIrqs` elementi (`AppleAicCommonDxe.c` righe 123-126) mentre i controlli usano `MaxIrqs` (`AppleAicCommonDxe.c` riga 52): scrittura/lettura fuori limiti possibile. Nessun loop "until event == 0" come in Linux, e nessun trattamento dell'evento 0 (spurio). [verificato]

D7. Ack in contesto FIQ. `AppleAicV2Dxe.c` riga 247: la lettura del registro evento avviene PRIMA di distinguere FIQ da IRQ. Leggere l'evento "acks and masks" (commento a riga 132): in una FIQ si consuma e maschera una IRQ pendente che poi non viene consegnata (righe 258-336 non la gestiscono). Linux non legge l'evento nel ramo FIQ. [verificato]

D8. Rilevamento Apple sbagliato. `AIC/AppleAicLib.c` riga 30: `if (Midr & 0x61000000)` e` un test di bit, non un confronto di implementer: 0x41 (ARM) & 0x61 != 0 passa. Doveva essere `((Midr >> 24) == 0x61)`. [verificato] Inoltre righe 44-50: `aic,3` viene trattato come v2 (il Readme riga 9 dice che v3 e` diverso), e se manca il nodo `aic` si usa `ASSERT(FALSE)` e poi si dereferenzia comunque il puntatore NULL nelle build RELEASE (righe 34-42 di AppleAicLib.c, 147-151 di AppleAicV2Dxe.c). [verificato]

D9. Gestione PMC FIQ non conforme. `AppleAicV2Dxe.c` righe 321-328: controlla solo `BIT11` (IACT) senza verificare IMODE == FIQ, poi scrive i bit 18/17/16/0 di PMCR0. Linux (righe 602-603) controlla `(PMCR0 & (IMODE|IACT)) == (IMODE_FIQ|IACT)`. Con il codice Mu l'IMODE non viene mai portato a OFF, quindi probabile FIQ storm [ipotesi: non ho letto la definizione dei bit PMCR0 in `asm/apple_m1_pmu.h`]. Le righe 329-335 (UPMCR0) seguono invece Linux.

D10. IPI non implementati. `AppleAicV2Dxe.c` righe 259-268: le fast IPI vengono solo "ack"-ate con un `DEBUG` ("Fast IPIs not supported yet"). Le IPI legacy v1 (`AIC_IPI_SEND 0x2008`, `ACK 0x200c`, `MASK_SET 0x2024`, `MASK_CLR 0x2028`) sono definite in `AppleAicLib.h` (righe 62-68) ma mai usate. Il Readme (riga 25) afferma che con AICv1 "will defer to using legacy IPIs": non e` vero nel codice. Nessun SW IPI (`AIC_EVENT_TYPE_IPI`, `AIC_CPU_IPI_*` 0x5008...). [verificato]

D11. Barriere di memoria assenti. Nel driver non ci sono `DsbSy`/`Isb`/`MemoryFence`. Per l'MMIO device-nGnRnE `MmioRead/Write32` bastano per l'ordine, ma Linux usa `readl` non relaxed nell'ack proprio per ordinare le letture DMA dopo l'IRQ, e fa `isb()` dopo le scritture sysreg dei timer FIQ. Il driver Mu non lo fa (righe 247, 128, 284). [verificato per assenza; la necessita` pratica e` un'ipotesi]

D12. Errori ignorati. `Status = AppleAicV2CalculateRegisterOffsets();` (riga 508) non viene mai controllato; `AllocatePool` di `AicInfoStruct` (riga 501) non viene controllato per NULL; in `CpuArchProtocolNotify` (`AppleAicCommonDxe.c` righe 95-104) lo `Status` della prima chiamata e` sovrascritto prima del controllo; `AppleAicV2SetIrqTriggerType` ritorna `EFI_SUCCESS` senza fare nulla (riga 434-435). [verificato]

D13. Layering. `AppleAicLib` e` una *library* ma usa `extern AIC_INFO_STRUCT *AicInfoStruct` definita nel *driver* (`AppleAicV2Dxe.c` riga 25) e uno stato statico proprio `mAicVersion` duplicato in due file (`AppleAicLib.c` riga 19 e `AppleAicV2Dxe.c` riga 30). Il DSC stesso ha il TODO "merge AppleAicLib into AppleAicDxe" (`AppleSiliconPkg.dsc.inc` riga 210). [verificato]

D14. Timer FIQ hard-coded. Le sorgenti 17/18/19 sono numeri software inventati e filtrati in 4 punti diversi (`AppleAicLib.c` 161, 198; `AppleAicV2Dxe.c` 126, 283-305); la `ExitBootServices` (righe 458-480) maschera le IRQ e disabilita il controller AIC ma non tocca i timer FIQ. Windows nativo si aspetterebbe un controller ancora abilitato [ipotesi]. [verificato per il codice]

D15. Codice morto e TODO. `AppleAicDxe.h` riga 50 "TODO: AICv1 API"; `AppleAicDxe.c` righe 45-48 "TODO: add AICv3"; `AppleAicV2Dxe.c` riga 88 ("figure out if we need to read HW_STATE or IRQ_CFG"), 216 ("what is this reg meant for?"), 404 ("some interrupts are level low... account for these"), 551-561 (blocco commentato per la disabilitazione IRQ nei core secondari); `AppleAicLib.h` riga 80 sezione "AICv1 bitmasks" vuota; `AppleAicLib.c` riga 233 `DEBUG` senza `\n`. Multi-core esplicitamente non supportato (riga 502, "multi-core support not added yet"). [verificato]

### 1.3 Cosa e` corretto (equo)
- Offset calcolati: per v1, dopo `AIC_TARGET_CPU 0x3000 + 4*1024` si ottengono SW_SET 0x4000, SW_CLR 0x4080, MASK_SET 0x4100, MASK_CLR 0x4180, HW_STATE 0x4200, coerenti con `aic_regs.h` di m1n1 (righe `AIC_SW_SET`...`AIC_MASK_CLR`). La formula riproduce quella di m1n1 `aic23_init` (righe 102-112) e Linux (righe 1007-1030). [verificato]
- `AIC_INFO 0x0004` e `AIC_MAX_IRQ 0x400` per v1 sono giusti; i registri v2 (INFO1/INFO3, campi DIE) sono giusti. [verificato]
- La sequenza di handoff del timer verso `TimerDxe` (controllo `CNTP_CTL`: ENABLE & !IMASK & ISTATUS) segue `TIMER_FIRING` di Linux. [verificato]
- Il glue di registrazione dei protocolli (`HardwareInterrupt`/`HardwareInterrupt2`, evento `ExitBootServices`, notify su CpuArch) segue lo schema di `ArmGicDxe` ed e` funzionale come scheletro.

### 1.4 Storia
Cronologia `git log` sul path (12 commit): `a701dbed` 2023-01-22 (spostamento di cartella; la storia anteriore e` stata perduta nel path), ... `474e0aac` 2023-11-13 "remove redundant code in AICv2 driver" (+2/-3), poi solo commit di manutenzione generale (`8dbbcad5` 2026-05-29 "fix Mu build", 21 file). Nessuna modifica funzionale al driver AIC dopo novembre 2023; `AicV1` e` fermo agli header del 2022-09-29. [verificato] L'autore stesso ha disabilitato AIC per default su M2 (`2c5ab300` 2025-11-15 "disable AIC_BUILD by default for MacBook Pro Early 2023"), ma l'ha lasciato attivo di default sul MacBook Pro 2020 (`MacBookProLate2020.dsc` riga 35: `AIC_BUILD = TRUE`). [verificato]

### 1.5 Voto: da riscrivere
Per un driver di cui ci fidiamo servono almeno: decode evento, loop di ack, programmazione TARGET_CPU, init v1 corretta, IPI v1 e fast IPI, barriere. Questo e` piu` del 50% del driver. Meglio scriverlo nuovo, guardando m1n1 `aic.c` (MIT/GPL, vedi licenza) e Linux come riferimento, e al limite riprendere lo scheletro del glue (`AppleAicCommonDxe.c`) dopo avergli corretto D6, D12.

## 2. Tabelle ACPI T8103 / j293

La piattaforma "MacBook Pro 13 M1 (j293)" nel repo e` `Platform/MacBookProLate2020Pkg` (SMBIOS `MacBookPro17,1`) che include `T810XFamilyPkg` e `MacBookProTouchBarFamilyPkg`. Nota: tutta la parte j293 e` stata aggiunta il 2025-10-26/27 da contributori esterni (`7c469c3b`, `77a353e9`, `0a1985bf`, `eca3d937` di Jamison Smith) e rifinita con una serie di commit di correzione. [verificato]

### 2.1 MADT (`T810X/AcpiTables/MADT_Static.aslc`, `19df1750` 2025-10-27 "Add WIP SMP work", +114/-8)
- Header: "temporary implementation until dynamic allocations in AcpiPlatformDxe are implemented" e "for the Mac Mini platform" (righe 8-11), per un file usato anche su MacBook. [verificato]
- Mescola tipi 6.3 e 6.4: `EFI_ACPI_6_3_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER`, `EFI_ACPI_6_3_GIC_STRUCTURE`, `EFI_ACPI_6_0_GIC_DISTRIBUTOR_STRUCTURE`, `EFI_ACPI_6_4_GICR_STRUCTURE`, `EFI_ACPI_6_4_GIC_ITS_STRUCTURE` (righe 53-59) con revisione di tabella 6.3. Ammissibile ma incoerente. [verificato]
- MPIDR dei core: E-core `GET_MPIDR(0,0,0,i)` => 0x0..0x3, P-core `GET_MPIDR(0,1,1,i)` => 0x10100..0x10103 (macro in `Include/Acpi/AcpiHeader.h` righe 46-50). Coincidono con `cpu@0..3` e `cpu@10100..10103` di `t8103.dtsi` (righe 63-161). [verificato] Questa e` la parte buona.
- Valori hard-coded: PMU IRQ per core 50, 54, 54, 54, 58, 62, 34, 38 (righe 85-107) senza alcun commento sull'origine; i valori 34/38 sui core 6 e 7 rompono lo schema. GSIV di manutenzione vGIC = 25 su tutti i core. Il DTS di Asahi non usa questi numeri. [verificato per il valore; l'origine e` ipotesi: numeri scelti per il vGIC di m1n1]
- Commenti copia-incolla: "Icestorm-2" ripetuto per il core 3 (riga 93). [verificato]
- Distributor `0xf00000000`, redistributor `0xf10000000` (da `T810XFamilyPkg.dsc.inc` righe 46-53, commento: "virtualized by trusted firmware... T8103 has no onboard GIC"), ITS fisso `0xF20000000` (riga 113, non derivato da PCD). Sono indirizzi inventati dall'hypervisor, non MMIO reali. [verificato]
- Conflitto build: il DSC attiva `AIC_BUILD = TRUE` per j293, e in tal caso `PcdGicDistributorBase/PcdGicRedistributorsBase` non sono impostati (guardia `!if $(AIC_BUILD) == FALSE`), ma `CPUAcpiTables.inf` li consuma comunque e `MADT_Static` e` inserito incondizionatamente nell'FDF (`MacBookProLate2020.fdf` riga 231). Quindi con AIC la MADT descrive un GIC al valore di default dei PCD [ipotesi: il default nel `.dec` di ArmPkg e` 0]. [verificato per la struttura del build]
- Checksum: `__ACPI_HEADER` (`AcpiHeader.h` righe 34-44) mette 0; il calcolo avviene all'installazione (`AcpiPlatform.c` riga ~440: `CalculateCheckSum8`) e dal protocollo `AcpiTableDxe` [verificato per `AcpiPlatform.c`; l'effetto sulla MADT statica e` ipotesi standard EDK2].
- Parking protocol: nessun campo valorizzato (Parking Protocol Version e Parked Address non gestiti nella `EFI_ACPI_6_3_GICC_STRUCTURE_INIT`; ricerca "park" nel repo = 0 risultati). [verificato per l'assenza]
Voto: da riscrivere. Il blocco GIC/ITS va tolto, e il resto va derivato dall'ADT/dal nostro design.

### 2.2 GTDT (`T810X/AcpiTables/GTDT.aslc`, `Silicon/Apple/AppleSiliconPkg/AcpiTables/GTDT.aslc`)
- GSIV timer: Secure PL1 = 19 (`T810XFamilyPkg.dsc.inc` riga 14, "placeholder"), phys = 17, virt = 18, hyp = 20 ("this disables the HV timer"). Sono numeri software: su un GIC reale sarebbero PPI 1..4; il timer fisico GIC standard e` 30 (PPI14), il virtuale 27. Flags: level, active-high, `LOSE_CONTEXT`. [verificato]
- Watchdog: `WATCHDOG_COUNT 0` (riga 62), quindi non emesso, ma gli indirizzi definiti sono `0x2922B0010` / `0x2922B001C` e GSIV `0x277` (righe 65-67), mentre il watchdog del t8103 e` a `0x23d2b0000` (`t8103.dtsi` riga 1223). L'indirizzo e` di un altro SoC (probabilmente t600x). [verificato per il confronto; l'attribuzione a t600x e` ipotesi]
- Il commento a riga 48 cita il driver Linux Apple WDT; sequenza di reset (0x4 a +0x1c, zeri a +0x10/+0x14) non verificata. [ipotesi]
- Doppione: `FDF` installa sia `GenericAcpiTables.inf` (che ha `GTDT.aslc`, `GenericAcpiTables.inf` righe 19-20) sia `CPUAcpiTables.inf` (che ha `GTDT.aslc`) (`MacBookProLate2020.fdf` righe 228 e 231): due GTDT nel firmware. L'ACPI ammette una sola GTDT. [verificato; effetto su Windows ipotesi]
- Storia: `038ceb36` "Temporary use standard timer interrupts" e `e27ee083` "fix GTDT for T602x" mostrano che il valore e` stato cambiato piu` volte senza spiegazione. [verificato]
Voto: da riscrivere.

### 2.3 CSRT e BERT (T810X)
- `T810X/AcpiTables/CSRT.aslc` (650 byte): solo header di licenza e commento "Why do we need a CSRT?", nessuna struttura dati, nessun `#include`. `CPUAcpiTables.inf` righe 19-22 elenca solo `GTDT.aslc`, `MADT_Static.aslc`, `PPTT.aslc`. Quindi la CSRT non viene mai generata per T810X. [verificato] Questo smentisce la lettura "la CSRT segnala l'AIC all'OS" di `ntasp-analisi.md` sezione 3, se intesa come funzionante per M1: e` solo un'intenzione. (Il T600X ha una CSRT da 1066 byte, non verificata.)
- `T810X/AcpiTables/BERT.aslc`: usa `BOOT_ERROR_REGION_BASE` (riga 36) mentre la `#define` e` commentata (riga 25): non compila, non e` nell'INF. Commento "Original source file from AmpereAltraPkg", "Tonga/Sicily". [verificato]
Voto: CSRT da scrivere da zero; BERT da scartare.

### 2.4 PPTT (`T810X/AcpiTables/PPTT.aslc`, `19df1750`/`44cd132c` Adrian Dumitru)
- Gerarchia package -> 2 cluster -> 4 core, ACPI Processor ID = `Cluster*4 + Cpu` (riga 32) coerente con gli `AcpiCpuUid` 0..7 della MADT e della DSDT. Lunghezze 0x14 e `OFFSET_OF` per i genitori corrette. Cache rimosse di proposito (`024391f5` "Simplify PPTT, remove cache info"). [verificato]
- Dipende da macro `PPTT_PROCESSOR_*_FLAGS` che vengono da `Acpi/SgiAcpiHeader.h` (header preso da Arm SGI) [verificato]; i flag effettivi non sono stati letti [ipotesi che siano corretti].
Voto: da riusare con correzioni (verificare i flag, aggiungere cache L1/L2 se serve).

### 2.5 DSDT j293 (`Platform/MacBookProLate2020Pkg/AcpiTables/DSDT.asl`)
- Struttura: `DefinitionBlock("DSDT.aml","DSDT",0x02,"Apple","J293",0x8103)`; albero SOC > CLU0 (E: CPU0-3) > CLU1 (P: CPU4-7) con `ACPI0010` (processor container) e `ACPI0007` (processor), `_UID` coerenti con la MADT. Valido. [verificato]
- UART `COM0` (`APPL8900`) a `0x235200000` giusto (DTS riga 878), ma interrupt `1097` (riga 190) contro `AIC_IRQ 605` del DTS (riga 883). Per XHC1 invece `857` e` identico al DTS (riga 1612), con `0x502280000` giusto. [verificato] Non so se 1097 sia un numero GIC virtuale (m1n1 mappa le IRQ con un offset?) [ipotesi]; in ogni caso e` incoerente col resto.
- `_HID "PNP0D15"` per XHC1 (righe 140, 107): l'ID standard per xHCI e` `PNP0D10`; `PNP0D15` e` AHCI [ipotesi, da registro PNP ID di memoria, da controllare]. Windows aggancerebbe il driver sbagliato.
- `CLPI`/`PLPI` (righe 26-103) definite ma i metodi `_LPI` sono commentati; la FADT dichiara `LOW_POWER_S0_IDLE_CAPABLE` (FADT riga 73): incoerenza. Il blocco XHC0 e` commentato (righe 105-137). [verificato]
- Commit di correzione ripetuti: `e52e310a` "Fixed some mild DSDT issues" (+69/-13), `a119b0bc` "Add usb-drd1 to DSDT", `6d1a9f54` "Revert interrupts since they seem to work now". [verificato]
Voto: da riusare con correzioni.

### 2.6 FADT (`Platform/MacBookProLate2020Pkg/AcpiTables/FADT.aslc`)
- Hardware-reduced ACPI, `PM_PROFILE_MOBILE`. Corretto come impostazione. [verificato]
- `ARM_PSCI_COMPLIANT` senza `HVC` (riga 76): conduit SMC. Commenti righe 24-33: "PSCI ... SMC event despite not being implemented on Apple, as we can trap SMCs to EL2... the recipient will be our custom hypervisor". Quindi la FADT presuppone un hypervisor che emuli PSCI. [verificato] Nel percorso nativo senza PSCI serve altro (parking protocol) e il flag va tolto.
- Campo "Minor revision of FADT" (riga 77) e` valorizzato con `EFI_ACPI_6_4_FIXED_ACPI_DESCRIPTION_TABLE_REVISION` (che e` la revisione maggiore, 6) invece della costante minor (4): annuncia ACPI 6.6. [ipotesi su costante: in EDK2 le due costanti sono distinte; verificato che il file usa la costante `_REVISION` in quel campo]
- `EFI_ACPI_6_3_PWR_BUTTON` impostato (flag "power button come control method") senza alcun device pulsante nella DSDT. [verificato]
- Indirizzi DSDT/FIRMWARE_CTRL a 0: riempiti dall'AcpiTableDxe (prassi EDK2) [ipotesi]. Reset register `NULL_GAS` con TODO sul watchdog Apple. [verificato]
Voto: da riscrivere per il percorso nativo.

### 2.7 DBG2 j293
Segue lo schema di AmpereAltra, sottotipo 0xA (nel registro DBG2 e` il tipo Samsung-like, coerente con la UART Apple S5L/Samsung) [ipotesi sul nome del sottotipo], namespace `\_SB.COM0`, base da `PcdAppleUartBase` (0x235200000, giusto) e lunghezza 0x1000. `#pragma pack()` a riga 101 senza un `#pragma pack(1)` corrispondente nel file: la struct `DBG2_DEBUG_DEVICE_INFORMATION` potrebbe avere padding prima di `AddressSize` (gli offset usano `OFFSET_OF`, quindi restano autoconsistenti) [ipotesi]. Voto: da riusare con correzioni.

## 3. Qualita` generale

### 3.1 Struttura e stile
- Impianto Mu standard (Platform/, Silicon/Apple/{AppleSiliconPkg, T8xxxFamilyPkg}), con piattaforme derivate per includi (`*.dsc.inc`, `*.fdf`). L'idea di riuso per famiglie e` giusta (README: "strong emphasis on code reuse"). [verificato]
- Residui di copia-incolla evidenti: `MacBookProLate2020.dsc` riga 1 "Mac Mini 2020 UEFI DSC file" e riga 48 `PcdAppleNumDwc3Controllers|2 # M1 Max case is hardcoded for now`; `MADT_Static.aslc` "for the Mac Mini platform"; `T810XFamilyPkg.dsc.inc` righe 33-37 e 39-43 duplicano identicamente `PcdCoreCount`/`PcdClusterCount`; CSRT/BERT/GTDT con "Tonga"/"Sicily"; `PlatformBuild.py` quasi identici tra piattaforme (dimensioni 10.8-11.2 KB). [verificato]
- Copertura incompleta dichiarata: `README.md` Status: "boots off external mass storage (not without hacks...), Windows loader initializes the RAMDisk but hangs at the handoff between bootloader and kernel". Disclaimer: "I probably do not know what I'm doing 100%". [verificato]
- Stile di codice: indentazione mista (2 e 4 spazi), blocchi di codice commentato (D15), nomi `Aic*` che sono ex `*GicDxe` rinominati. [verificato]

### 3.2 Test e CI
- Nessun test: nessun file con `test` nel path (ricerca nell'albero ricorsivo). Nessuna cartella `.github`, `.azurepipelines`, `.pipelines`; `gh api actions/runs` => `total_count: 0`. [verificato] Il build dipende da `PlatformBuild.py` di Mu eseguito a mano.
- Sono 3 issue aperte, repo "size" ~12 MB (senza submodule). [verificato]

### 3.3 Licenze e provenienza
- Licenza di repo BSD-2-Clause (`LICENSE` con i copyright Linaro, Red Hat, ARM, Intel, Apple Inc., EFIDroid, Bingxing Wang, Renegade Project). I file nuovi usano `BSD-2-Clause-Patent` (AIC) o `BSD-2-Clause-Patent OR MIT` (ACPI). [verificato]
- Problemi: `AppleAicLib.c`, `AppleAicDxe.h`, `AppleSysRegs.h` non hanno `SPDX-License-Identifier`. `ConvenienceMacros.h` riga 19 "borrowed from m1n1" sotto BSD-2-Clause-Patent. `m1n1/src/aic.c` riga 1 porta `SPDX-License-Identifier: GPL-2.0-or-later` e l'intero driver ricalca layout e logica di `aic.c`/`irq-apple-aic.c` (GPL-2.0) sotto licenza permissiva, con un solo "Credits to the Asahi Linux contributors" in `AppleAicDxe.c` riga 12 e nessun header di provenienza. [verificato per le due licenze; il rischio di incompatibilita` e` una mia valutazione, non un parere legale]. Per noi: non copiare il file; rifarlo da specifica e documentare la provenienza (CLAUDE.md richiede header di provenienza).
- Il file `.dsc` cita "WOA-Project/SurfaceDuoPkg" e "mu_andromeda_platforms" come origini. [verificato]

### 3.4 Frequenza e qualita` dei commit
- Contributori: 3 persone (amarioguy 343 commit, JamiNeedsSleep 22, graphine27 21 secondo `ntasp-analisi.md`); le piattaforme M1 le ha aggiunte un contributore esterno in 2 giorni (2025-10-26/27) con 15 commit tipo "Update DSDT.asl" x3, "Try 2", "Fixed ACPI stuffs", "Fixed ACPI tables (again) but i didnt loose my brain cells", "Fixed some weird acpi GUID errors", "Updated GUID so stuff wont freak out", poi fixup di amarioguy. [verificato nei messaggi]
- Messaggi del manutentore: "miscellaneous changes" (`3c4e19de`), "changes as of 6/11/2024 (GPIO and PCIe drivers verified somewhat working, working on USB-A controller bringup)" (`4f2cb26c`), "USB is being detected, bringup almost complete" (`e770157e`). Commit grandi e misti: `5d12c63b` 2023-05-07 "add Apple Silicon ACPI platform driver" +543/-13 in 13 file; `8dbbcad5` 2026-05-29 "fix Mu build" 21 file. [verificato]
- Attivita`: ultimo commit `861dcc49` 2026-09-06 ("do not reset on memory type information change"); lunghi buchi (2024-09 -> 2025-08; 2025-12 -> 2026-05). [verificato] Nessuna storia di review (PR minime).

## 4. Dipendenza dall'hypervisor

Presuppongono vGIC/PSCI emulati [tutto verificato nei file indicati]:
1. FADT: PSCI conforme, conduit SMC "trappata a EL2", commento "recipient will be our custom hypervisor" (FADT righe 24-33, 76).
2. MADT: GICv3 completo con ITS (distributor/redistributor/ITS su indirizzi virtuali), 8 GICC con PMU IRQ e GSIV vGIC (`MADT_Static.aslc`).
3. DSC: `PcdGicDistributorBase|0xf00000000`, `PcdGicRedistributorsBase|0xf10000000` (`T810XFamilyPkg.dsc.inc` 46-53); `HwResetSystemLib` = `ArmPsciResetSystemLib` (`AppleSiliconPkg.dsc.inc` riga 356); mappa `AppleArmGxfHvcLib` (`Library/AppleArmGxfHvcLib/*`).
4. GTDT: GSIV per PPI dell'architectural timer su base GIC (`GTDT.aslc`, `T810XFamilyPkg.dsc.inc` 14-17).
5. `PrePi/PrePi.c` riga 41 e `AppleArmGenericTimerPhyCounterLib.c` riga 96: commenti/assunzioni "running inside m1n1 or another hypervisor" / "issue reboot command through hypervisor shell" (non letto il contesto completo) [verificato per la presenza dei commenti].
6. Il README del driver AIC dice di preferire il vGIC (`Readme.md` 33). Il percorso AIC e` quello "sperimentale".

Da riscrivere per il percorso nativo: MADT (togliere GICD/GICR/ITS, decidere cosa vede Windows come interrupt controller: argomento HAL extension e CSRT, vedi `ntasp-analisi.md`), GTDT (nessun GIC, FIQ), FADT (via PSCI, serve parking protocol o altro), reset/shutdown, SMP. Si possono tenere: PPTT, albero CPU della DSDT, DBG2, MPIDR dei core, mappa MMIO (dopo controllo col DTS). Il parking protocol non esiste ne` come codice ne` come campo MADT valorizzato (ricerca "park": 0 risultati). [verificato]

Stima grossolana [ipotesi]: lato ACPI/firmware, ~70% dei file ACPI andrebbe riscritto per il nativo; il driver AIC va comunque riscritto per v1.

## 5. I 5 difetti piu` gravi

1. **AICv1 (il nostro SoC) e` uno stub**: `AicV1/AppleAicV1Dxe.c` 19-23 "TODO: this driver"; `AppleAicDxe.c` 41-43 manda v1 al codice v2. (D1)
2. **`AIC_MASK_BIT` sbagliato** (`AppleAicLib.h` 103): `BIT(num) & GENMASK(4,0)` annulla il masking/unmasking per tutte le IRQ >= 5; m1n1 riga 10 e` corretto. (D2)
3. **Registro evento e config v1 sbagliati e decode evento assente**: event=`base` invece di `base+0x2004`; CONFIG 0x14 al posto di niente; evento grezzo usato come indice nell'array (OOB, anche per `NumIrqs` vs `MaxIrqs`). (D3, D4, D6)
4. **Ack letto in contesto FIQ** (riga 247) con perdita di IRQ, **PMC FIQ non azzerata correttamente**, IPI non implementate (solo ack e DEBUG), nessuna programmazione TARGET_CPU v1. (D5, D7, D9, D10)
5. **ACPI hard-coded e legata all'hypervisor, non verificata col DTS**: MADT/GTDT con GIC virtuale e numeri inventati; GTDT duplicata (generico + T810X); CSRT inesistente e BERT che non compila; IRQ UART DSDT 1097 vs 605 del DTS; watchdog GTDT a indirizzo di altro SoC; FADT PSCI/SMC-to-EL2 e minor revision errata; assenti test e CI. (sez. 2, 3.2, 4)

Menzione: `MIDR & 0x61000000` come test del vendor (D8), e `AIC_BUILD = TRUE` di default per j293 con MADT sempre GIC (sez. 2.1).

## 6. Raccomandazioni

1. Non riusare il codice di AppleAicDxe; scrivere il nostro driver AICv1 da `m1n1/src/aic_regs.h` e `irq-apple-aic.c` (valutando prima la licenza) con test su QEMU o su m1n1 hypervisor. Si puo` copiare l'idea del glue (`RegisterInterruptSource`, `CpuArchProtocolNotify`) dal modello `ArmGicDxe`, non da Mu.
2. Dal repo NTASP recuperare solo i *dati*: struttura CPU/cluster (PPTT, albero DSDT), MPIDR, indirizzi XHC/UART gia` coincidenti col DTS, e confrontare ogni IRQ col DTS di Asahi prima di inserirlo.
3. Scrivere MADT/GTDT/FADT/CSRT nostri, generati dall'ADT (o con valori verificati), senza GIC virtuale e con parking protocol.
4. Se si riusa il repo come base di firmware Mu, introdurre CI (anche solo un build in Actions) e unit test dei decoder (evento AIC, macro `BIT`/`GENMASK`) prima di fidarsi di qualsiasi parte.
5. Limiti di questo audit: non ho letto `AcpiPlatform.c` per intero, `PrePi.c`, `MemoryInitPeiLib.c`, `PlatformBuild.py`, DART/PCIe/USB/NVMe; non ho compilato nulla ne` eseguito test; i giudizi su `PNP0D15`, costanti di revisione e bit PMCR0 restano ipotesi finche` non si controllano le fonti (ACPI spec/PNP ID registry/`asm/apple_m1_pmu.h`).
