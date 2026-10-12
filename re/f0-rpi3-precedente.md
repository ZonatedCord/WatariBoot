# F0 / A4: il precedente Raspberry Pi 3 (Windows senza GIC)

Issue #7. Ricerca solo in lettura (`gh api`, WebSearch/WebFetch). Nessun download, nessun binario Microsoft.
Convenzione: [verificato: fonte] oppure [ipotesi].

## Sintesi

Sul Pi 3 (BCM2837) Windows non ha ricevuto il controller di interrupt da un componente di terze parti.
Il firmware ACPI descrive il BCM2836 "ARM local" come se fosse una GIC (GICC con base 0x40000000), e il **supporto
al controller Broadcom è dentro l'HAL di Windows** (arm32 per IoT Core, arm64 per le build ARM64 successive).
Non esiste, nei repository esaminati, alcuna HAL extension, alcun driver di terze parti e nessun gruppo CSRT per il controller di interrupt.
Il precedente quindi NON dimostra che una terza parte possa fornire un interrupt controller a Windows ARM64.

## Prove raccolte

### 1. MADT finta del Pi 3 (la prova principale)
- [verificato: edk2-platforms `Platform/RaspberryPi/AcpiTables/Madt.aslc`] La MADT del Pi 3 contiene 4 strutture GICC (una per core) e, solo per Pi != 3,
  la GIC Distributor (`#if (RPI_MODEL != 3)`). Quindi sul Pi 3 **non c'è nessun GICD**.
- [verificato: stesso file + `RPi3.dsc` righe 345 e 424] Il campo "Physical Base Address" del GICC è `PcdGicInterruptInterfaceBase = 0x40000000`,
  cioè l'indirizzo dei registri "ARM local" del BCM2836 (non una GIC). `PcdInterruptBaseAddress` vale lo stesso 0x40000000.
- [verificato: stesso file, `RPi3.dsc` righe 425-428] `PerformanceInterruptGsiv` = 9 per tutti i core (nella numerazione locale BCM2836, 9 = PMU_FAST;
  [verificato: binding Linux brcm,bcm2836-l1-intc, riportato dalla ricerca web]).
- [verificato: commento in `Madt.aslc`] "Use 1 for GICR Base Address below, since Windows 10 on Raspberry Pi 3 does not boot otherwise, and this is
  the value that Microsoft had in their IoT blobs." Cioè Windows legge i campi GICC e un valore sbagliato impedisce il boot.
- [verificato: andreiw/RaspberryPiPkg `AcpiTables/Madt.aslc`, blob binario preso dalle tabelle MS-IoT] Commento: "Even though the BCM2836 doesn't contain a GIC, these
  GICC definitions enable multi-core support (with PSCI). Mind the signatures in the header, they must be kept."
  Decodifica dei byte (mia): OEM ID "BC2836", OEM Table ID "EDK2    ", Creator ID "MSFT", revisione 3, lunghezza 0x16C = 44 + 4 x 80;
  GICC con flags=1, PerfGsiv=9, ParkedAddress=0, ParkingVersion=0, PhysicalBase=0x40000000, GICV/GICH/VGIC=0.
- [ipotesi] L'HAL riconosce il Pi 3 dai campi sopra (base 0x40000000, GICR=1 e/o OEM ID "BC2836") e non da un driver; non ho prove su quale campo sia decisivo.
- [verificato: pftf/RPi3 Readme, tianocore RPi3 Readme] Windows 10 ARM64 (GUI completo) risulta avviabile con questo firmware senza altri componenti.
  Linux in modalità ACPI invece va in crash in `acpi_register_gsi()` ([verificato: messaggio di commit del Readme di pftf/RPi3]), segno che la "GIC" è finta.

### 2. Nessuna HAL extension né driver per l'interrupt controller
- [verificato: `gh api repos/ms-iot/rpi-iotcore/contents/drivers`] I driver sono: RpiLanPropertyChange, audio, gpio, i2c, mailbox, misc, pwm, sd, spi, uart. Nessun HalExt,
  nessun driver del controller di interrupt. I file `interrupt.c` trovati appartengono ai driver mailbox, vchiq, PL011 e PWM/DMA (gestori di ISR dei dispositivi, non il controller).
  `raspberrypi/windows-drivers` ha la stessa struttura (stessa lista di directory).
- [verificato: `worproject/RPi-Windows-Drivers`] Il repository contiene solo README e release di binari. Il README parla di driver di periferica e di override DSDT/SSDT, mai di interrupt controller.
  Limite: i binari delle release non sono stati ispezionati.
- [verificato: CSRT in `Csrt.aslc`] La CSRT contiene un solo resource group, quello DMA (DeviceId DMA, controller + canali). Nessun gruppo per interrupt controller o timer.
  È la parte "Microsoft" (copyright Microsoft nel file), usata per il DMA (`EFI_ACPI_CSRT_DEVICE_ID_DMA`).
- [verificato: `Pep.asl`] Il PEP è un dispositivo virtuale `BCM2854` senza risorse; descrive processore, stati coordinati e dispositivi per la gestione energetica, non gli interrupt.
- [verificato: `edk2-platforms/Platform/RaspberryPi/Drivers`] `InterruptDxe` (Bcm283x) serve solo a UEFI (solo i 4 interrupt timer locali, registro +0x40); sparisce all'ExitBootServices.

### 3. Chi supportava il BCM nell'HAL
- [verificato: issue andreiw/RaspberryPiPkg#12, andreiw, aprile 2018] "there appears to be a check in Hal validating PIC is not a BCM PIC. That's why you need a debugger attached
  to patch out the 'bls' following the subtraction and compare." Quindi nelle build ARM64 di aprile 2018 (17125-17134) l'HAL **conosceva** il PIC Broadcom
  ma lo rifiutava; per avviare serviva una patch in memoria con WinDbg. Andreiw: "science project, given the need to hot-patch Win10 Hal today just to boot".
- [verificato: stessa issue, thchi12] Citazione di un utente: "Windows has built in support for BCM interrupts" (da un post zhihu; non verificato di persona). Un altro utente indica che
  le build RS5 sopra 17723 "should have official support for RPi3 boot" (affermazione di terzi, non confermata da Microsoft).
- [ipotesi, confidenza media] Dalla build RS5 in poi l'HAL ARM64 ha accettato il PIC BCM senza patch: nelle fonti successive (pftf/RPi3, WoR) non c'è più traccia di patch dell'HAL,
  e il commento sul valore "GICR=1" dice che il boot dipende dalla MADT dei blob MS.
- [verificato: andreiw readme] Il progetto HypDxe (hypervisor EL2 che sposta UEFI in EL1, intercetta SMC/PSCI, MMIO e log del kernel) serviva a debug e ricerca; il suo autore dice che
  serviva "to eventually support MMIO traps so I could emulate a GIC" (issue #130). HypDxe **non** è in edk2-platforms/pftf RPi3 ([verificato: lista `Platform/RaspberryPi/Drivers`]), quindi il boot finale non lo richiede.

### 4. Avvio dei core secondari
- [verificato: issue #12, andreiw] I blob ACPI MS-IoT originali riportavano **MPPP** (Multiprocessor Parking Protocol) e non PSCI nel FADT (IoT Core era ARM32).
  Per ARM64 andreiw ha usato **PSCI**: "Apr 24th 2018: SMP support in WoA" ([verificato: readme]).
- [verificato: `Fadt.aslc`] FADT con `EFI_ACPI_6_3_ARM_PSCI_COMPLIANT` (ArmBootArchFlags) e `HW_REDUCED_ACPI`. Il metodo è PSCI CPU_ON gestito da ARM Trusted Firmware (BL31) nel firmware.
  Nella MADT: `ParkedAddress=0`, `ParkingProtocolVersion=0`, MPIDR di ciascun core presente (`GET_MPID(0,n)`).
- Senza GIC il risveglio non usa SGI della GIC: è il firmware (TF-A) a svegliare i core (sul Pi 3 con i registri mailbox/spin del VideoCore/ARM local), invisibile a Windows, che chiama solo PSCI_CPU_ON.
  [ipotesi] per i dettagli interni di TF-A su Pi 3 (non verificati qui).
- [verificato: issue #12] Il primo bug con 4 core era PROCESS1_INITIALIZATION_FAILED (0x6b) con core secondari abilitati, tolto con "Processor Enabled=0", poi risolto (SMP funzionante).
- IPI: [ipotesi] Windows li invia tramite i registri "mailbox" per-core dell'ARM local (offset 0x80+), cioè il percorso nativo del BCM2836; non ho trovato conferma diretta.

### 5. Timer
- [verificato: `Gtdt.aslc`, `RPi3.dsc` righe 346-349] GTDT con GSIV: Secure PL1 = 0, NonSecure PL1 = 1, Virtual = 3, Hyp (NS PL2) = 2, con flag di polarità attiva-bassa.
  Sono i numeri sorgente locali del BCM2836 (CNTPSIRQ=0, CNTPNSIRQ=1, CNTHPIRQ=2, CNTVIRQ=3 [verificato: binding Linux]) e non PPI di una GIC (PPI sarebbero 16-31; 0-3 in una GIC sarebbero SGI).
- Quindi Windows usa il **generic timer ARM** (contatore a 19.2 MHz, [verificato: issue #12, andreiw "timer configured as expected (19.2Mhz)"]) e l'interrupt arriva tramite
  il registro "timer interrupt control" per-core dell'ARM local (offset 0x40, [verificato: `BCM2836_INTC_TIMER_CONTROL_OFFSET` in Bcm2836.h]).
- Il fatto che i GSIV siano 0-3 e siano accettati è un'altra prova che l'HAL interpreta i numeri secondo il modello BCM, non GIC [ipotesi].
- Periferiche (DSDT, es. `Interrupt(ResourceConsumer, Level, ActiveHigh, Exclusive) { BCM2836_USB_INTERRUPT }`): [verificato: `GpuDevs.asl`] usano la forma Extended Interrupt standard con numeri nel dominio BCM.
  I valori numerici non sono stati letti.

## Che cosa si può riusare come modello per l'M1

| Elemento | Riusabile? | Nota |
|---|---|---|
| Struttura CSRT (resource group DMA) | Sì, per il DMA/DART | Non contiene interrupt; è il meccanismo MS per risorse di sistema |
| MADT con GICC "finti" + nessun GICD | Solo come idea | Funziona **perché l'HAL ha già il codice BCM**; per l'AIC non c'è un equivalente noto [ipotesi] |
| PSCI via TF-A/firmware per i core secondari | Sì, la parte più portabile | Pi 3: PSCI_CPU_ON; per l'M1 serve un firmware che lo implementi (m1n1/EL2 stub) |
| Timer: GTDT con GSIV non-GIC | Parziale | Sul M1 il timer ARM arriva come FIQ/IRQ sulla AIC (vedi lavori A8-A10) |
| HAL extension / ACPI CSRT per interrupt controller | **No** | Non esiste sul Pi 3: nessun precedente |
| HypDxe (trap MMIO per emulare una GIC) | Come banco di prova | Violerebbe il vincolo "nativo"; è un hypervisor (piano B) |

## Verdetto

- Sul Pi 3 il controller di interrupt era **supporto interno a Windows** (HAL, scelto dai dati della MADT), non un componente di terze parti.
  Confidenza: **media-alta (circa 75%)**. Prove: assenza di HalExt/driver nei repo ms-iot, raspberrypi e WoR; commento
  "Hal validating PIC is not a BCM PIC"; MADT con base 0x40000000 e "valori dei blob Microsoft"; GSIV del timer 0-3.
- Il precedente **non** dimostra che Windows ARM64 giri senza GIC con un componente di terze parti: dimostra che l'HAL Microsoft include almeno un
  percorso non-GIC (BCM2836), ma solo per quel controller. Per l'M1 (AIC) un percorso equivalente non è replicabile senza modificare l'HAL
  (patch del binario, vietata come soluzione finale) oppure senza far apparire l'AIC come una GIC (emulazione/hypervisor, piano B).
- Confidenza che l'HAL ARM64 retail contenga ancora un ramo BCM attivo senza patch: **media (circa 60-65%)**, perché la prova più forte (andreiw) è del 2018 e dice che all'epoca era rifiutato.
- Lacune: non ho letto i binari (vietato), non ho accesso a worproject.com (la ricerca non lo ha restituito) e non ho verificato se le build Windows più recenti
  mantengono il ramo BCM. Una HAL extension (HalExt*, GUID in CSRT con DeviceId proprio) per interrupt controller resta, per Windows ARM64, un meccanismo **da dimostrare** sul M1,
  non un precedente.

## Fonti
- edk2-platforms: `Platform/RaspberryPi/AcpiTables/{Madt.aslc,Csrt.aslc,Gtdt.aslc,Fadt.aslc,Pep.asl,Dsdt.asl,GpuDevs.asl}`, `RPi3/RPi3.dsc`, `RPi3/Readme.md`,
  `Silicon/Broadcom/Bcm283x/{Include/IndustryStandard/Bcm2836.h,Drivers/InterruptDxe}`
- github.com/andreiw/RaspberryPiPkg (readme, `AcpiTables/Madt.aslc`, `Drivers/HypDxe`, issue #12, #88, #130), repo ora archiviato in worproject/RaspberryPiPkg
- github.com/ms-iot/rpi-iotcore, github.com/raspberrypi/windows-drivers, github.com/worproject/RPi-Windows-Drivers, github.com/pftf/RPi3
- Binding Linux `brcm,bcm2836-l1-intc` (numerazione delle sorgenti locali)
- windowslatest.com 2018/02/09 (primo boot di Windows 10 ARM64 su Pi 3 con un solo core, UNSUPPORTED_PROCESSOR casuale)
