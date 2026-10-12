# Watari Boot: Windows ARM64 nativo su MacBook Pro 13" M1

## Context
Obiettivo dell'utente: **dual boot nativo** di Windows ARM64 su MacBook Pro M1 (13", 2020), sfruttando tutta la potenza dell'hardware.
- Niente hypervisor, emulatore o VM.
- Windows deve usare tutti gli 8 core (4 P + 4 E), le frequenze piene e, a lungo termine, la GPU.
- Poi generalizzare a tutti gli Apple Silicon.

Nessuno l'ha mai fatto. Asahi l'ha escluso, perché ritiene che serva modificare il kernel di Windows. Questo piano punta a un percorso **senza modificare Windows**, con i meccanismi di estensione che Windows ARM64 prevede:
- **estensioni HAL**, con risorse dichiarate nella tabella ACPI CSRT;
- **tabelle ACPI**;
- **driver KMDF/StorPort**;
- **PEP** (Platform Extension Plugin, il plugin di Windows per la gestione energetica).

Profilo dell'utente: conosce C e C++, niente firmware. Io scrivo il codice e spiego, lui esegue sull'hardware. Non ha un secondo computer: le fasi 0 e 1 si fanno solo sul Mac, dalla fase 2 serve un debug host (basta un Raspberry Pi 5, circa 80 €).

## I blocchi tecnici e come li aggiriamo in modo nativo
| Blocco | Approccio nativo |
|---|---|
| Windows conosce solo il controller di interrupt GIC, il M1 ha l'AIC | **Estensione HAL "AIC"** che registra l'AIC come controller di interrupt nel HAL di Windows. È **l'ipotesi da verificare per prima** (Fase 0). |
| Timer e IPI di Apple arrivano come FIQ (Windows potrebbe non gestirli) | Non usare FIQ: IPI tramite i registri IPI dell'AIC (in IRQ, come faceva Linux all'inizio), e una sorgente timer in IRQ fornita da un'estensione HAL timer (meccanismo documentato da Microsoft). Le FIQ restano mascherate. |
| Il M1 non ha EL3/PSCI per accendere i core secondari | **ACPI Multiprocessor Parking Protocol**, scritto da Microsoft e supportato da Windows: EDK2 parcheggia i core in una mailbox. |
| Mancano UEFI e ACPI | Piattaforma **EDK2** avviata da m1n1 (solo come bootloader: inizializza e poi esce di scena). ACPI generate dall'ADT. Core P/E descritti con PPTT e classi di efficienza, così lo scheduler di Windows li usa bene. |
| SSD (Apple ANS2, non su PCIe) | **Miniport StorPort nativo** per ANS. La parte NVMe è riusabile, l'init di ANS e SART si porta dal driver Asahi. |
| USB/Thunderbolt | m1n1/EDK2 inizializzano PHY e DWC3 in modalità host, poi si descrive il controller come **xHCI via ACPI**: il driver standard di Windows (UsbXhci, come sui Qualcomm) lo gestisce nativamente. |
| IOMMU (DART, solo pagine da 16K, Windows usa pagine da 4K) | DART in bypass dove l'hardware lo permette, altrimenti mappature gestite dai nostri driver. |
| Potenza: DVFS, P/E core, sleep | **PEP** + `_CPC` in ACPI per le frequenze (registri PMGR, logica portata da Asahi). |
| GPU AGX | **Driver WDDM**: obiettivo di lungo termine, con il driver Rust di Asahi e Mesa (Honeykrisp/Asahi) come riferimento. All'inizio Basic Display su framebuffer (GOP). |
| Tastiera e trackpad interni (SPI/MTP) | Driver KMDF HID. Si porta la logica dei driver Asahi `spi-hid`/`apple-mtp`. |

Note legali: il reverse engineering per l'interoperabilità è consentito nell'UE (Dir. 2009/24/CE, art. 6). Non redistribuiamo codice Microsoft. Si sviluppa con `bcdedit /set testsigning on`; per la distribuzione pubblica serve la firma Microsoft via Partner Center.

## Componenti upstream da riusare
- `AsahiLinux/m1n1`:
  - `src/aic.c`, il driver AIC (registri, IPI);
  - `src/adt.c`, il parser dell'ADT;
  - init di PMGR, framebuffer e USB, e codice per accendere le CPU.
- `AsahiLinux/linux`:
  - `drivers/irqchip/irq-apple-aic.c`;
  - `drivers/nvme/host/apple.c` (ANS);
  - `drivers/iommu/apple-dart.c`;
  - `drivers/soc/apple/*`;
  - driver Rust della GPU;
  - DTS `t8103-j293` (il MacBook Pro 13" M1).
- `tianocore/edk2` + `edk2-platforms`: `ArmVirtPkg` e `Platform/RaspberryPi` come modello (UEFI + ACPI + parking protocol + CSRT).
- `raspberrypi/windows-drivers` e `worproject`: esempi reali di BSP Windows ARM64 su hardware non standard.
- Kit di Microsoft: WDK (esempi di estensione HAL, StorPort, KMDF, PEP) e simboli pubblici di `ntoskrnl.exe` ARM64 dal symbol server.

## Struttura (`~/Documents/02_PROGETTI/WatariBoot`)
```
re/            analisi Ghidra di ntoskrnl/HAL ARM64 e note
edk2-apple/    AppleSiliconPkg: m1n1 → EDK2, ACPI generate dall'ADT, parking protocol, CSRT
halext-aic/    estensione HAL: controller di interrupt AIC + timer IRQ
drivers/       ans-storport/, dart/, pep/, spi-hid/ (KMDF)
tools/         build, immagini, script per QEMU
docs/          diario tecnico, ADR, mappa dell'hardware
```

## Fasi (ognuna con traguardo verificabile)

**Fase 0: lo spike decisivo (solo Mac)**
- Scaricare `ntoskrnl.exe` ARM64 (Windows 11 24H2) e i suoi PDB pubblici, e analizzarli con Ghidra.
- Mappare `HalpInterruptRegisterController`, `_INTERRUPT_FUNCTION_TABLE`, `KnownType` e il caricamento delle estensioni HAL (CSRT, `HalpInitializeHalExtensions` / `HalExtension` exports).
- Capire come Windows gestiva il controller non-GIC del Raspberry Pi 3 (BCM2836).
- Capire cosa fa il vettore delle eccezioni di Windows con una FIQ.
- ✅ Traguardo: un documento che dice **sì/no, con prove**, se un'estensione HAL può registrare un controller di interrupt non-GIC, e con quale interfaccia. **Questa fase decide la strada.**

**Fase 1: prototipo in QEMU (solo Mac)**
- EDK2 con MADT *senza GIC*, CSRT e parking protocol.
- Stub dell'estensione HAL che registra un controller di interrupt finto.
- Windows ARM64 avviato in QEMU con debug del kernel (KDNET o seriale) su macOS, guardando fin dove arriva la fase di boot.
- ✅ Traguardo: Windows carica la nostra estensione HAL e prova a usarla, verificato con WinDbg.

**Fase 2: sul MacBook (serve il debug host)**
- Asahi installer, opzione "UEFI environment only", su una partizione di circa 80 GB: macOS e la recovery restano intatti.
- m1n1 → EDK2 nativo, con GOP, ACPI generate dall'ADT e i core parcheggiati.
- ✅ Traguardo: shell UEFI di EDK2 sul MacBook, che vede tutti e 8 i core.

**Fase 3: il primo boot di Windows nativo**
- Estensione HAL dell'AIC (portata da `irq-apple-aic.c`) + timer IRQ.
- Boot da chiavetta USB.
- Debug: framebuffer, poi KDCOM sulla UART di debug via USB-C (cavo da debug o m1n1 tramite debug host).
- ✅ Traguardo: **il kernel di Windows gira nativo sul M1** e arriva al boot dei driver.

**Fase 4: desktop**
- Miniport StorPort per ANS, così Windows si installa sull'SSD interno.
- xHCI via ACPI per tastiera e mouse USB.
- ✅ Traguardo: **desktop di Windows nativo su MacBook Pro M1**, installato sull'SSD.

**Fase 5: potenza piena**
- PEP + `_CPC`/DVFS, classi di efficienza dei core P/E.
- Tastiera e trackpad (SPI), Wi-Fi, audio, batteria (SMC), sospensione.
- Avvio del progetto del driver WDDM per la GPU AGX.
- ✅ Traguardo: benchmark (Geekbench, Cinebench) confrontati con macOS sullo stesso Mac.

**Fase 6: tutti gli Apple Silicon**
- ACPI generate dall'ADT a ogni avvio.
- AIC2 per M1 Pro/Max/Ultra e M2+, che m1n1 supporta già.

## Rischi e piano B
- **Spike negativo** (il HAL non accetta controller non-GIC da estensioni). Le alternative, in ordine:
  - (a) usare l'interfaccia GIC del CPU che il M1 implementa per la virtualizzazione (registri `ICH_*`), con uno shim minimo a EL2 che gestisce solo gli interrupt: tutto il resto resta nativo e passthrough, con overhead stimato intorno all'1%;
  - (b) patch del kernel per la ricerca, non distribuibile.
  Se ci si arriva, la scelta la fai tu.
- **Brick**: tutto passa da una partizione separata di Asahi. Nel caso peggiore serve un restore via DFU da un altro Mac.
- **Firma dei driver**: con test-signing si sviluppa; per la distribuzione serve la firma Microsoft.

## Verifica
- Fase 0: report in `re/` con indirizzi, funzioni e pseudocodice di Ghidra a sostegno di ogni conclusione.
- Fase 1: log di WinDbg che mostrano la nostra estensione HAL caricata in QEMU.
- Fasi 2-5: foto o video di ogni traguardo sull'hardware, log del debug host, e benchmark Windows vs macOS.

## Organizzazione e workflow (vale per tutte le sessioni)
- **Ruoli**:
  - io (Opus 5.5) sono il capo: ragiono, progetto, divido il lavoro, verifico e rispondo all'utente;
  - gli agenti **Sonnet 5.5** (Agent tool, `model: "sonnet"`) eseguono i task, mi riportano i risultati, e io controllo prima di riferire;
  - l'utente parla solo con me.
- **Persistenza della regola**:
  - in `~/.claude/CLAUDE.md` (globale, quindi vale in ogni conversazione futura);
  - nel `CLAUDE.md` del repository;
  - in una memoria `feedback`.
  La memoria attuale è legata al percorso `Downloads/ProvaImpossibile`, che verrà eliminato: per questo la regola va anche nei due `CLAUDE.md`.
- **Documentazione** in `docs/`:
  - `docs/sessions/AAAA-MM-GG-NN.md`: diario di **ogni fine sessione** (cosa è stato fatto, decisioni, risultati, prossimi passi);
  - `docs/STATUS.md`: lo stato attuale, sempre aggiornato, e il punto da cui ripartire;
  - `docs/design/watariboot/`: output della sessione di progettazione;
  - `docs/adr/`: le decisioni architetturali.

## Esecuzione all'uscita dal plan mode (in ordine)
1. **Spostamento**: `~/Downloads/ProvaImpossibile` è vuota. Creare `~/Documents/02_PROGETTI/ProvaImpossibile` ed eliminare la cartella vuota in Downloads.
2. **Scheletro del repository**:
   - le cartelle della struttura sopra, più `docs/sessions`, `docs/adr` e `docs/STATUS.md`;
   - `README.md` (visione e stato);
   - `CLAUDE.md` del progetto (workflow capo/esecutori, regola del diario a fine sessione, vincoli: nativo, nessun hypervisor salvo decisione esplicita dell'utente, nessun codice Microsoft nel repository);
   - `.gitignore`, che esclude binari Microsoft (ntoskrnl, PDB, ISO), output di build e database di Ghidra;
   - questo piano copiato in `docs/PLAN.md`.
3. **Regola globale**: aggiungere a `~/.claude/CLAUDE.md` la sezione sul workflow Opus capo + esecutori Sonnet. Salvare la memoria `feedback`.
4. **Sessione di progettazione** con `morasca-agent` (`model: sonnet`), a partire da questo piano: PRD, architettura con diagrammi, rischi, strategia di test e ADR in `docs/design/watariboot/`. Poi rivedo l'output e correggo le imprecisioni tecniche.
5. **GitHub**: `git init`, primo commit, `gh repo create ZonatedCord/ProvaImpossibile --private --source . --push`.
6. **Avvio della Fase 0**, delegata a Sonnet:
   - agente A: setup di toolchain e Ghidra, e download di ntoskrnl ARM64 con i PDB (fuori dal repository);
   - agente B: ricerca su come Windows gestiva il controller di interrupt del Raspberry Pi 3 e sulle estensioni HAL/CSRT.
   Io sintetizzo i risultati in `re/` e in `STATUS.md`.
7. **Fine sessione**: diario in `docs/sessions/`, aggiornamento di `STATUS.md`, commit e push.

Intanto l'utente si procura un debug host (Raspberry Pi 5 o un vecchio portatile) per la Fase 2.
