# A2: requisiti ACPI di Windows ARM64 e Multiprocessor Parking Protocol

Issue #5. Ricerca solo da fonti pubbliche (Microsoft Learn, specifica Microsoft del parking protocol, Linux, U-Boot, edk2-platforms). Nessun binario o simbolo Microsoft. Ogni affermazione e' marcata **[verificato: fonte]** o **[ipotesi]**.

Nota di metodo: le pagine Microsoft Learn su ACPI sono scritte sull'ACPI 5.0 e non elencano esplicitamente SPCR, PPTT, IORT. Il comportamento reale di Windows su questi punti non e' documentato nelle fonti trovate: dove manca, e' dichiarato.

## 1. Tabelle ACPI

Fonti: [MS-ACPI] https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-system-description-tables (aggiornata 2025-04), [MS-DBG2] https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-debug-port-table, [LNX] https://docs.kernel.org/arch/arm64/acpi_object_usage.html (cosa si aspetta Linux, NON Windows).

| Tabella | Windows | Dettagli |
|---|---|---|
| RSDP/XSDT | Obbligatoria | RSDP trovato dalla EFI system table; se ci sono sia RSDT sia XSDT, Windows preferisce XSDT. [verificato: MS-ACPI] |
| FADT | Obbligatoria | Con piattaforme SoC: `HARDWARE_REDUCED_ACPI` (Flags bit 20) va impostato se non c'e' l'hardware fisso ACPI; con il flag tutti i campi dell'hardware ACPI sono ignorati. [verificato: MS-ACPI] Il testo MS dice che i campi per sleep/resume "must be present" ma ignorati. Flag ARM: vedi sotto. |
| MADT | Obbligatoria | Vedi sez. 2. |
| GTDT | Obbligatoria di fatto | MS: "ACPI's GTDT can be used to leverage the built-in support" per il timer generico Arm (formulazione debole, non "required") [verificato: MS-ACPI]. Per Linux e' Required [verificato: LNX]. Per Windows l'obbligatorieta' esplicita e' [ipotesi]. |
| DBG2 | Obbligatoria per Windows | "Microsoft requires a debug port on all systems"; la presenza di DBG2 indica la porta. Revision 0; header 36 B + OffsetDbgDeviceInfo (off 36) + NumberDbgDeviceInfo (off 40). Per ogni dispositivo: Revision 0, Port Type 0x8000 seriale, subtype ad es. 0x0003 PL011, 0x000E SBSA UART, 0x0012 16550 con GAS; il NamespaceString deve riferirsi a un nodo del DSDT oppure essere "." se non esiste. [verificato: MS-DBG2]. Non esiste un subtype Apple: per M1 serve un subtype noto o un'estensione KD (da cercare; [ipotesi]). |
| SPCR | Non nominata nelle pagine MS trovate | Required per Linux [verificato: LNX]. Per Windows non verificato [ipotesi: serve per la console, il debug passa da DBG2]. Esempi Pi3 in edk2-platforms: `SpcrPl011.aslc`, `SpcrMiniUart.aslc` [verificato: elenco file tianocore/edk2-platforms Platform/RaspberryPi/AcpiTables]. |
| PPTT | Facoltativa (non nominata da MS) | Optional per Linux [verificato: LNX]. edk2-platforms RaspberryPi la fornisce (`Pptt.aslc`) [verificato]. |
| CSRT | Obbligatoria SOLO se si usano CSR non standard | "must be included in the RSDT if non-standard CSRs are used"; ogni resource group deve avere anche un device nel namespace ACPI (DSDT) con gli stessi identificatori del group header, cosi' la HAL extension e' aggiornabile con Windows Update. Windows usa l'ID del group per caricare la HAL extension. [verificato: MS-ACPI]. Riguarda direttamente `halext-aic`. Specifica: https://cdrdv2.intel.com/v1/dl/getContent/772760. |
| IORT | Non nominata da MS; facoltativa per Linux | Optional (richiesta per SMMU/ITS) [verificato: LNX]. Per M1 (DART, non SMMU) non descrivibile con IORT standard: [ipotesi]. Esiste in edk2-platforms RaspberryPi (`Iort.aslc`) [verificato: elenco file]. |
| DSDT | Obbligatoria | Contiene i device e va referenziata dal FADT (X_DSDT). SSDT facoltative. [verificato: MS-ACPI, LNX] |
| Altre citate da MS | BGRT, FPDT facoltative; WSMT, iBFT per scenari specifici | [verificato: MS-ACPI] |

### Flag FADT ARM (ArmBootArchFlags, ACPI 5.1+)
- `PSCI_COMPLIANT` indica che l'hardware e' PSCI; `PSCI_USE_HVC` sceglie HVC invece di SMC come conduit. [verificato: patch ACPICA su lkml https://lkml.iu.edu/hypermail/linux/kernel/1407.3/00390.html]
- Cosa succede se sono a 0: Linux, con `PSCI_COMPLIANT=0`, non usa PSCI e ripiega sul parking protocol (se descritto nel MADT) [verificato: codice `acpi_parking_protocol.c` e doc LNX, che chiama il parking "possible but discouraged"]. Per **Windows** la pagina MS non dice cosa accade con PSCI_COMPLIANT=0 e non dichiara PSCI obbligatorio: [ipotesi] che Windows ARM64 selezioni il metodo di start in base al MADT (parking address valido) e al flag, ma non ho trovato evidenza.
- Il Raspberry Pi 3 con edk2-platforms mette `HW_REDUCED_ACPI` e `EFI_ACPI_6_3_ARM_PSCI_COMPLIANT` (conduit SMC) [verificato: Platform/RaspberryPi/AcpiTables/Fadt.aslc, righe Flags e ArmBootArchFlags]. Quindi Pi3 NON e' un precedente di Windows con parking.

## 2. MADT su Windows

- Fonte [MS-ACPI]: per Arm ACPI 5.0 aggiunge i descrittori GIC (5.2.12.14) e GIC Distributor (5.2.12.15); Windows ha supporto inbox per GIC e GICD. "For Arm platforms, one descriptor is listed for each GIC, followed by one for each GIC Distributor. The GIC corresponding to the boot processor must be the first entry." [verificato]
- Nessuna dichiarazione ufficiale trovata che il GICD sia obbligatorio, ne' che il GICR o l'ITS lo siano. Il testo MS descrive solo cosa Windows sa interpretare. [verificato: assenza nella pagina; l'obbligo e' [ipotesi]]
- Come Windows enumera le CPU: dalle GICC del MADT. Per il parking: "The OS learns the Processor ID and mailbox physical address for each processor by consuming the GICC structures in the ACPI MADT" [verificato: specifica MP Startup, sez. 3.3]. "Systems that use ACPI must describe the processors through the ACPI-defined MADT table" (sez. 3.7) [verificato].
- Precedente di GICC senza GICD: il MADT Pi3 omette la GICD (`#if (RPI_MODEL != 3)`), usa GICC con base fisica 0x40000000 (PcdGicInterruptInterfaceBase di RPi3.dsc), e commenta che "Windows 10 on Raspberry Pi 3 does not boot" se il campo GICR Base non e' 1, valore preso dai blob Microsoft IoT [verificato: Platform/RaspberryPi/AcpiTables/Madt.aslc, RPi3/RPi3.dsc]. Il GTDT Pi3 usa GSIV 0,1,3,2 (interrupt locali BCM2836, non PPI GIC) con flag di polarita' [verificato: Gtdt.aslc, RPi3.dsc PcdArmArchTimer*]. Non ho trovato come Windows gestisca l'interrupt controller Pi3 (HAL extension o altro): [ipotesi] che sia una HAL extension, cioe' proprio lo schema che ci serve per l'AIC.
- Linux su arm64 richiede MADT con sole strutture GIC 0x0A-0x0F e UID sincronizzati con i Processor Device del DSDT [verificato: LNX].

## 3. Multiprocessor Parking Protocol

Specifica ufficiale: Microsoft, "Multi-processor Startup for ARM Platforms", Version 1, 23 settembre 2014 (copia su https://cdrdv2-public.intel.com/772757/mp-startup-for-arm-platforms.pdf, letta integralmente, 11 pagine). Tutto quanto segue e' **[verificato: spec MP Startup, sez. indicata]** salvo diversa marcatura. Revisioni: 2010, 2011 (public release 14/11/2011), 20/12/2012 (barriere, strongly-ordered, correzione mailbox), 23/9/2014 (Processor ID per ARMv8, MADT aggiornata).

### 3.1 Mailbox (sez. 3.4, Fig. 3)
- Blocco da **4 KB**, indirizzo **allineato a 4 KB**, una pagina fisica separata per processore. Riservata dal firmware (memoria "permanently in use"), il firmware non puo' rilocarla. Tutto little endian.
- Diviso in due metà da 2 KB: sezione OS e sezione firmware.
- Layout (Fig. 3, offsets derivati dalle dimensioni; coincidono con `struct parking_protocol_mailbox` di Linux e con `ACPI_PP_CPU_ID_OFFSET 0`, `ACPI_PP_CPU_JMP_OFFSET 8` di U-Boot):
  - 0x000: Processor ID, 4 B
  - 0x004: Reserved, 4 B
  - 0x008: Jump Address, 8 B
  - 0x010 - 0x7FF: riservato all'uso OS (il firmware non deve scriverlo)
  - 0x800 - 0xFFF: riservato al firmware (l'OS non lo accede)
- Solo Processor ID e Jump Address possono essere scritti dal firmware nella sezione OS.
- Processor ID (sez. 1): per GICv1/v2 e' il valore del campo **CPU Interface Number** della GICC. GICv3 con compatibility mode: ID GICv1/v2 per i primi otto, 0 per gli altri. GICv3 senza compat mode: 0 per tutti.
- Il campo **Parking Protocol Version** del MADT deve combaciare con la versione del documento ("1") (sez. 1).
- Jump Address a 64 bit; sotto 4 GB solo su Arm 32 bit.

### 3.2 Stato parcheggiato e sequenza firmware (sez. 3.1, 3.2, 3.5)
- Prima di cedere l'OS il firmware inizializza **Processor ID = tutti 1 (0xFFFFFFFF)** e **Jump Address = 0** (anche se la piattaforma parte da memoria non inizializzata; se il contenuto e' preservato non va toccato).
- Stato dell'AP all'ingresso (Tab. 1, AArch64): EL2 se c'e' virtualizzazione altrimenti EL1; little endian (SCTLR_ELx.EE=0); DAIF tutti mascherati {1,1,1,1}; **GIC CPU interface enabled**; **MMU, cache, branch prediction disabled** (SCTLR_ELx.{I,C,M}=0). Tutti i processori nello stesso inner shareable domain; il firmware inizializza lo stato non architetturale; cache invalidate prima di entrare in coerenza.
- Algoritmo AP (Fig. 2): controlla se Processor ID == il proprio e Jump Address != 0; altrimenti **WFI** e al risveglio ricontrolla. Se OK: legge Jump Address, **scrive 0 su Jump Address** (ack), mette l'indirizzo della mailbox in X0 e fa `BR` al Jump Address. Se le due condizioni non valgono e' un falso risveglio e torna in WFI. Con le interruzioni mascherate, l'uscita da WFI riprende all'istruzione successiva.
- Dettaglio del 2012: nota sulla nota che un core puo' anche essere spento prima dell'OS, purche' si riaccenda autonomamente su un interrupt software e segua i passi (sez. 3.1).

### 3.3 Sequenza OS (sez. 3.3, 3.5)
1. Legge Processor ID e indirizzo mailbox dalle GICC del MADT.
2. Mappa la mailbox come strongly-ordered (**Device-nGnRnE** su ARMv8). Linux invece usa `ioremap` (nGnRE) e lo dichiara una deviazione "harmless" [verificato: acpi_parking_protocol.c].
3. Verifica che il core sia parcheggiato: Processor ID == tutti 1.
4. Scrive Jump Address e Processor ID (nell'ordine imposto da 3.5: se cambia il Jump Address mentre il core e' gia' armato: Processor ID a tutti 1, DSB, Jump Address, DSB, poi Processor ID corretto). Linux scrive entry_point e poi cpu_id [verificato: Linux].
5. **DSB dopo ogni scrittura**.
6. Non avvia AP prima di `ExitBootServices()`.
7. **"will send an interrupt to the target application processor to signal that the mailbox is ready"** (sez. 3.3). La spec NON specifica quale interrupt: nessun numero di SGI, nessuna menzione di SGI/GIC IPI, e nella sez. 2.2 dichiara che "ARM architecture does not provide ... designated IPI". Quindi la domanda "quale SGI" non ha risposta nella specifica: e' una scelta dell'OS. Linux usa il normale IPI di reschedule (`smp_send_reschedule`) [verificato: arch/arm64/kernel/smp.c, `arch_send_wakeup_ipi`, "we can safely handle spurious scheduler IPIs"]. Per Windows l'interrupt effettivo e' deciso dalla HAL, non documentato nelle fonti: [ipotesi] che sia un IPI/SGI mediato dal driver dell'interrupt controller (nel nostro caso `halext-aic`).
8. BSP mailbox (sez. 3.6): anche il BSP ne ha una, usata per il resume da stati di sleep con memoria mantenuta.

### 3.4 Confronto con le implementazioni
- **Linux** `arch/arm64/kernel/acpi_parking_protocol.c` [verificato: gh api torvalds/linux]: struct `{le32 cpu_id; le32 reserved; le64 entry_point;}`; valida solo se `parked_address && parking_version` (cioe' versione 0 o indirizzo 0 = parking assente); richiede `cpu_id == ~0U` prima di avviare, altrimenti `-ENXIO`; scrive `entry_point` (indirizzo fisico di `secondary_entry`) poi `cpu_id = cpu_interface_number` (NON UID, NON MPIDR); poi IPI; in postboot avvisa se `entry_point` non e' stato azzerato.
- **U-Boot** `arch/arm/cpu/armv8/acpi_park_v8.S` e `arch/arm/include/asm/acpi_table.h` [verificato: gh api u-boot/u-boot]: `ACPI_PP_CPU_ID_INVALID 0xffffffff`, `ACPI_PP_JMP_ADR_INVALID 0`, `ACPI_PP_PAGE_SIZE 4096`, `ACPI_PP_CPU_ID_OFFSET 0`, `ACPI_PP_CPU_JMP_OFFSET 8`, `ACPI_PP_CPU_CODE_OFFSET 2048` (codice di spin nella meta' firmware, +0x800), `ACPI_PP_VERSION 1`. Il codice AP: scrive 0xFFFFFFFF in cpu_id, `dsb sy`, poi loop **`wfe`** (non WFI): ricarica cpu_id; se == invalid riprova; legge jump address; se 0 riprova; altrimenti azzera il jump address, `dsb sy`, `br x1`. Non usa atomiche (MMU/cache off) e identifica il core cercando il proprio MPIDR (aff3:aff2:aff1:aff0 compattato) tra le pagine: quindi U-Boot assume che il campo cpu_id contenga l'MPIDR compattato, che NON e' quanto dice la spec (cpu interface number, o 0 su GICv3). Deviazione da tenere presente.
- **edk2**: `DynamicTablesPkg/Library/Acpi/Arm/AcpiMadtLibArm/MadtGenerator.c` e i `ConfigurationManager.h` di edk2-platforms (Juno, N1Sdp, Morello, VExpress, SGI, NXP) hanno i campi `ParkingProtocolVersion` e `ParkedAddress` della GICC, ma ho trovato solo la generazione della tabella, **nessuna implementazione firmware del loop di parcheggio** nelle ricerche su tianocore/edk2 e edk2-platforms [verificato: gh api search/code]. Le piattaforme di riferimento usano PSCI da ATF.
- **Pi3 pftf/edk2-platforms RPi3**: usa PSCI (ATF), non parking (vedi sez. 1) [verificato].

## 4. Punto critico: parking senza GIC (M1)

Risposte, con livello di confidenza:

1. **La specifica richiede un GIC?** Non per il meccanismo di parking in se'. Il GIC compare in due punti: (a) Tab. 1 elenca "GIC CPU Interface: Enabled" nello stato d'ingresso; (b) Processor ID definito in termini di CPU Interface Number GICC (con 0 per GICv3 senza compat). Il risveglio e' descritto in modo generico come "an interrupt" e "doorbell mechanism"; sez. 2.2 dice che oggi l'interrupt controller usato e' il GIC ma non lo prescrive. [verificato: spec]. Su M1 la riga "GIC CPU interface enabled" va letta come "interrupt controller in grado di consegnare l'interrupt di wake" (AIC): [ipotesi].
2. **L'AP puo' fare polling invece di aspettare un'interruzione?** La spec descrive WFI come stato di attesa (Fig. 1/2 e sez. 3.1: "they check their mailbox ... and then go into a wait for interrupt (WFI) state") ma i soli requisiti osservabili dall'OS sono: Processor ID = tutti 1 e Jump Address = 0 a riposo; ack scrivendo 0 sul Jump Address; salto con X0 = indirizzo mailbox. Non c'e' nessun requisito che vieti il polling e nessun timeout dichiarato. Una pagina "falso risveglio" e' esplicitamente tollerata ("assume it was a false wake and go return to the WFI state"). Conclusione: un loop **WFE** che ricontrolla la mailbox (come U-Boot, che non si affida a un SEV dell'OS) e' conforme in termini osservabili [ipotesi supportata dall'implementazione U-Boot verificata e dal testo; non esiste una dichiarazione esplicita "polling consentito"]. La spec NON prevede che l'OS emetta SEV: quindi un WFE puro dipende dal fatto che un interrupt pendente (anche mascherato, DAIF=1111) sveglia WFE come WFI (comportamento ARM ARM, ricordato a memoria, [ipotesi: da verificare nell'ARM ARM]; e comunque per Linux/U-Boot l'IPI c'e'). Alternative robuste su M1: (a) WFE con event stream del timer (`CNTKCTL_EL1.EVNTEN`) per un risveglio periodico garantito; oppure (b) polling puro con `yield`. Costo: consumo e calore dei core parcheggiati finche' non partono (breve, solo fino all'avvio dell'OS) [ipotesi].
3. **Se Windows richiede comunque di inviare l'interrupt**: non e' un problema per un loop in polling (l'interrupt e' solo un doorbell). Ma l'invio dell'IPI da parte della HAL verso un core ancora nel firmware passa dal driver AIC (`halext-aic`): se la HAL non sa mandare IPI all'AIC prima che il core sia online, o se Windows aspetta un handshake su quel canale, il polling lo maschera. [ipotesi]
4. **Windows su Raspberry Pi 3 senza GIC**: il Pi3 non e' un precedente di parking. Usa PSCI da ATF (FADT `PSCI_COMPLIANT`), con MADT che ha le GICC (base 0x40000000) ma niente GICD, e GTDT con GSIV 0-3 [verificato: edk2-platforms]. E' un precedente utile per "MADT senza GICD e GSIV non-GIC", non per "start via mailbox".
5. **Rischio principale**: non c'e' nessuna fonte che dica che Windows ARM64 moderno supporti ancora il parking protocol (la spec e' del 2014 per ARMv7/ARMv8 iniziale; la pagina Linux lo definisce "discouraged" e dice che i server Arm supportano solo PSCI). Non ho trovato nessuna piattaforma Windows ARM64 commerciale nota che usi il parking. [ipotesi: Windows lo supporta per compatibilita', ma e' da provare su QEMU o hardware con una HAL reale]. Piano B se non funziona: emulare PSCI nel firmware (conduit HVC con un mini-hypervisor EL2 o un trap su SMC non possibile senza EL3) cioe' una decisione fuori scope per A2.

Verdetto: il parking senza GIC e' compatibile con il testo della specifica con un loop WFE/poll che rispetta il protocollo della mailbox; la parte non verificabile da fonti pubbliche e' (i) quale interrupt/IPI Windows invii e (ii) se la HAL ARM64 attuale usi ancora questo metodo.

## 5. Confronto con `tools/acpigen`

Codice letto: `include/acpigen.h`, `src/madt.c`, `gtdt.c`, `pptt.c`, `csrt.c`, `t8103_model.c`.

| # | Elemento | Stato attuale | Requisito / fonte | Azione |
|---|---|---|---|---|
| 1 | GICC `parking_protocol_version` | 0 (`CPU()` in t8103_model.c) | Spec: versione "1". Linux considera il parking assente se versione o indirizzo sono 0. U-Boot `ACPI_PP_VERSION 1`. | Mettere 1. **Discrepanza bloccante.** |
| 2 | GICC `parked_address` | 0 | 4 KB, allineato 4 KB, una pagina fisica distinta per CPU, riservata dal firmware (memoria "reserved", non rilocabile). | Allocare 8 pagine (anche BSP, sez. 3.6) e validare allineamento in `acpigen_build_madt` (oggi nessun controllo). |
| 3 | GICC `cpu_interface_number` | `= i` (0..7), UID = 0..7 | Il Processor ID della mailbox deve coincidere con questo valore (GICv2); su GICv3 senza compat la spec dice 0 (ambiguo con piu' CPU). Linux scrive proprio `cpu_interface_number`. Valore 0xFFFFFFFF riservato (invalid). | OK finche' i valori sono unici e != 0xFFFFFFFF. Aggiungere un controllo. Documentare la scelta (non-GIC: la spec e' ambigua). |
| 4 | GICC `mpidr` | DTS reg (0x0-0x3, 0x10100-0x10103) | Campo MPIDR di Aff3:Aff0 [verificato: ACPI]. | Ipotesi: coincidenza con il registro MPIDR_EL1 reale (gia' dichiarata). |
| 5 | Primo GICC = boot processor | cpu0 = icestorm0 (UID 0) | "The GIC corresponding to the boot processor must be the first entry." [MS-ACPI] | Verificare quale core fa partire m1n1/UEFI su M1 e riordinare di conseguenza, [ipotesi] che il boot CPU non sia icestorm0. |
| 6 | GICC `physical_base_address` / `gicv` / `gich` / `gicr` | 0 | Pi3 mette base = 0x40000000, GICR base = 1, e dice che Windows non parte senza il valore 1 [verificato: Madt.aslc]. | Provare GICR=1 (o altro valore non zero) in QEMU/test, [ipotesi]; valutare effetto sul driver GIC inbox che Windows carica. |
| 7 | `acpi_gicd` | `t8103_gicd`: base 0, version 3, segnaposto | MS non dice che serve; Pi3 la omette. Una GICD fasulla con base 0 puo' far partire il driver GIC inbox su hardware inesistente. | Passare `gicd = NULL` come impostazione predefinita (parametro gia' opzionale). |
| 8 | GICR/ITS | non generati (struct tipi 0x0E/0x0F assenti) | MS: solo GIC e GICD descritti; nessuna dichiarazione di GICR/ITS. | Nessuna azione per ora. |
| 9 | MADT header revision | 6, `local_apic_address` 0, flags 0 | Coerente con ACPI 6.5 e con Pi3 (rev da `EFI_ACPI_6_3_...REVISION`). | OK. |
| 10 | GTDT | rev 3, cnt_*_base = ~0, GSIV 29/30/27/26, flags active-low, virtual_pl2 0 | Formato OK rispetto ad ACPI 6.5. Pi3 usa GSIV 0,1,3,2 con flag polarita' [verificato] e nessun PPI GIC: Windows accetta GSIV arbitrari se l'interrupt controller li risolve. | I GSIV sono segnaposto per l'AIC (FIQ): da riallineare con `halext-aic` (task A8-A10). Non e' un bug del generatore. Considerare di impostare `ALWAYS_ON` se il timer M1 lo e'. |
| 11 | GTDT `virtual_pl2_gsiv` | 0 | Campo introdotto con rev 3 (ACPI 6.3); nessun problema. | OK. |
| 12 | PPTT | package -> cluster -> core, rev 3, `ID_VALID` sui core con UID | ACPI 6.5 5.2.30.1; facoltativa per Windows (non nominata da MS) e Linux. | OK. Flag `IDENTICAL_IMPL` non settato: corretto per cluster misti. |
| 13 | CSRT | un gruppo `APLE`, un descrittore Interrupt | MS: CSRT obbligatoria se si usano CSR non standard (AIC) e ogni gruppo deve avere un device nel DSDT con gli stessi identificatori. Pi3 usa CSRT per il DMA. | Manca il device nel DSDT (non gestito da acpigen). Il group header ID e il device DSDT devono combaciare. |
| 14 | Tabelle mancanti nel generatore | n/a | FADT (HW_REDUCED_ACPI; ArmBootArchFlags), DBG2, SPCR, DSDT/SSDT, XSDT/RSDP sono richieste/attese | Nuove task: FADT con `HW_REDUCED_ACPI` e flag PSCI a 0 (coerente col parking), DBG2 con subtype seriale, DSDT con CPU Device (_UID = UID del MADT), device AIC per il CSRT. |
| 15 | Sincronizzazione DSDT/MADT | non applicabile | Device Processor `_UID` deve coincidere con gli UID MADT [verificato: LNX]. | Tenere in mente quando si scrive il DSDT. |

## 6. Domande aperte (da non assumere)

- Windows ARM64 attuale usa ancora il parking protocol? Quale interrupt invia? (non documentato; test su hardware o QEMU con HAL reale)
- Cosa fa Windows se `PSCI_COMPLIANT=0` e `parking_protocol_version=0`? Non documentato.
- Windows richiede un subtype DBG2 specifico? Per M1 serve un subtype noto (16550 con GAS, 0x0012, e' il piu' flessibile secondo MS).
- Un core in WFE con interrupt AIC mascherato (DAIF=1111) esce dallo stato di wait? Da verificare nell'ARM ARM; l'alternativa e' l'event stream.
- Compat dell'AIC con "GIC CPU interface enabled" della Tab. 1: la riga e' vuota di significato su M1 senza GIC.

## Fonti
- Microsoft, Multi-processor Startup for ARM Platforms v1, 2014-09-23: https://cdrdv2-public.intel.com/772757/mp-startup-for-arm-platforms.pdf
- Microsoft Learn, ACPI System Description Tables: https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-system-description-tables
- Microsoft Learn, DBG2: https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-debug-port-table
- Linux ACPI Tables su arm64: https://docs.kernel.org/arch/arm64/acpi_object_usage.html
- Linux `arch/arm64/kernel/acpi_parking_protocol.c`, `arch/arm64/kernel/smp.c` (gh api torvalds/linux)
- U-Boot `arch/arm/cpu/armv8/acpi_park_v8.S`, `arch/arm/include/asm/acpi_table.h` (gh api u-boot/u-boot)
- edk2-platforms `Platform/RaspberryPi/AcpiTables/{Fadt,Madt,Gtdt,Csrt}.aslc`, `RPi3/RPi3.dsc`, `RPi3/Readme.md` (gh api tianocore/edk2-platforms)
- Introduzione dei flag ARM Boot Architecture in FADT: https://lkml.iu.edu/hypermail/linux/kernel/1407.3/00390.html
