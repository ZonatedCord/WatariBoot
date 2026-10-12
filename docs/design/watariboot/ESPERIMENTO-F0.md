# ESPERIMENTO-F0: progetto del Gate B in QEMU (task A5)

- Progetto: Watari Boot · Data: 2026-10-12 · Stato: **progetto, non eseguito** · Autore: esecutore Sonnet (task A5), da verificare da Opus
- Collegamenti: [bozza ADR Fase 0 sperimentale](../../adr/bozza-fase0-sperimentale.md) · [ADR-0001](../../adr/0001-boot-nativo-senza-hypervisor.md) · [ADR-0003](../../adr/0003-parking-protocol-invece-di-psci.md) · [ADR-0004](../../adr/0004-irq-invece-di-fiq.md) · [INTERFACES](INTERFACES.md) · [RISKS](RISKS.md) · [re/f0-m1-fiq-irq.md](../../../re/f0-m1-fiq-irq.md) · [tools/acpigen](../../../tools/acpigen/README.md)

## 0. Convenzioni e vincoli

- `[verificato: fonte]` = letto sulla fonte citata durante questo task. `[ipotesi]` = non verificato. `[ipotesi, da confermare con A1/A2/A4]` = dipende dalle ricerche A1 (estensioni HAL), A2 (requisiti ACPI e parking protocol), A4 (Raspberry Pi 3), non ancora disponibili.
- `[UMANO]` = passo che esegue Marco, non un agente.
- **Scatola nera**: nessun binario, simbolo o dump Microsoft viene scaricato o analizzato dal progetto (bozza ADR Fase 0). Si osserva solo il comportamento di Windows dall'esterno (bugcheck, messaggi del debugger, esito del boot). L'ISO e il WDK ufficiali sono *strumenti*, usati senza disassemblarli.
- **Nulla di questo documento è stato eseguito.** Prima di qualunque download o installazione vale la regola assoluta del backup Time Machine con conferma esplicita di Marco (vedi sez. 5).
- Tutte le opzioni di QEMU e di `bcdedit` citate sono prese dalla documentazione ufficiale (link in sez. 9). Dove un'opzione non è stata trovata nella documentazione è scritto.

## 1. Che cosa vogliamo sapere (domande del Gate B)

| ID | Domanda | Varianti che ci rispondono |
|---|---|---|
| Q1 | Il nostro generatore `acpigen` produce tabelle che Windows accetta come quelle di QEMU? (validazione dello strumento) | V1 |
| Q2 | Windows ARM64 enumera le CPU e arriva al boot se la MADT ha le GICC ma **non** la GICD? | V2 |
| Q3 | Windows avvia i core secondari con il Parking Protocol (mailbox in GICC) invece di PSCI? | V3 |
| Q4 | Windows carica un'estensione HAL agganciata a un gruppo CSRT nostro? E con quale interfaccia/messaggi? | V4 |
| Q5 | Quanto dipende Windows dal timer architetturale descritto in GTDT (collegato al fatto che sul M1 il timer è solo FIQ, [re/f0-m1-fiq-irq.md](../../../re/f0-m1-fiq-irq.md))? | V5 |

La domanda decisiva ("accetta un controller non-GIC fornito da un'estensione HAL?") **non** ha una variante QEMU che la chiuda da sola: vedi sez. 4 per i limiti. Le varianti riducono l'incertezza sulle sue precondizioni (V2, V3, V4, V5).

## 2. Ambiente

### 2.1 QEMU `virt` aarch64 con HVF

| Voce | Scelta | Stato |
|---|---|---|
| Binario | `qemu-system-aarch64` (QEMU recente da Homebrew o compilato) | scelta; installazione [UMANO] dopo backup |
| Macchina | `-machine virt` | [verificato: https://www.qemu.org/docs/master/system/arm/virt.html] |
| Acceleratore | `-accel hvf` | `hvf` è fra gli acceleratori elencati dalla documentazione di invocazione, sintassi `-accel name[,prop=value]` [verificato: https://www.qemu.org/docs/master/system/invocation.html]; la pagina virt elenca `host` come unico CPU model supportato con HVF (oltre a KVM) e `max` = `host` per KVM/HVF [verificato: pagina virt] |
| CPU | `-cpu host` (con HVF; equivale a `max`) | [verificato: pagina virt] |
| `gic-version` | valori `2`, `3`, `4`, `x-5`, `host`, `max`. **Non esiste un valore "none/disabled"** | [verificato: pagina virt] |
| Rimozione del GIC | **Non possibile con `-machine virt`**: il GIC è sempre istanziato dalla macchina | [verificato: nessuna opzione per rimuoverlo nella pagina virt; le uniche opzioni correlate sono `gic-version`, `virtualization`, `msi`]. Il "non-GIC" va quindi simulato solo nelle **tabelle ACPI** (vedi V2) |
| Versione GIC da usare | `gic-version=3` come scelta iniziale (è quella usata negli esempi QEMU per tpm-tis e coerente con la probabile aspettativa di Windows) | [ipotesi, da confermare con A2: quale versione GIC richiede Windows ARM64]. Se HVF non accetta v3 con la versione di QEMU installata: provare `2` o `host`; il comportamento di HVF con il GIC è [ipotesi]: controllare con `qemu-system-aarch64 -machine virt,help` e le note di rilascio della versione installata |
| EL2 | `virtualization=off` (default) | [verificato: pagina virt, default `off`]. Windows deve girare a EL1, come sul M1 senza hypervisor (ADR-0001) |
| `acpi` | `acpi=on` (o `auto`) | [verificato: pagina virt, valori `on/off/auto`] |
| `msi` | lasciare il default; se V2 va in crisi sulle IORT/ITS, provare `msi=off` | [verificato: opzione `msi` documentata; effetto su Windows: ipotesi] |
| Memoria / core | `-m 8G -smp 4` (V3: `-smp 4` almeno, meglio 8 per somigliare al M1) | scelta; il limite dei core con HVF dipende dall'host [ipotesi] |
| Firmware | `-drive if=pflash,format=raw,file=...` con EDK2 compilato da noi (sez. 2.2) | schema pflash [verificato: esempio TPM nella documentazione QEMU, https://www.qemu.org/docs/master/specs/tpm.html] |
| Grafica / input | `ramfb` o `virtio-gpu-pci`, tastiera e mouse `usb-kbd`/`usb-mouse` su `qemu-xhci` | [ipotesi] Windows ha driver inbox per xHCI e display base; virtio-gpu non ha driver inbox |
| Disco | `-device nvme` (driver inbox di Windows) sull'immagine qcow2; ISO come `usb-storage` | [ipotesi]. Evita `virtio-blk` che richiede i driver `virtio-win` durante il setup |
| TPM | `swtpm` + `-chardev socket` + `-tpmdev emulator` + `-device tpm-tis-device,tpmdev=tpm0` | [verificato: https://www.qemu.org/docs/master/specs/tpm.html]. Necessità per Windows 11: [ipotesi] richiesto dal setup (da A2/verificare) |
| NIC per il debug | `-device e1000e,netdev=...` | KDNET supporta Intel con device ID `10D3` [verificato: lista Microsoft sotto]; che il modello `e1000e` di QEMU presenti proprio quell'ID: [ipotesi, controllare con `lspci`/Gestione dispositivi nel guest]. In alternativa `-device e1000` (ID `100E` è nella lista) [ipotesi su ID emulato] |

Lista KDNET: https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/supported-ethernet-nics-for-network-kernel-debugging-in-windows-10 [verificato: la pagina elenca Intel 8086:10D3 e 8086:100E; per la versione di Windows 11 ARM64 si deve controllare `VerifiedNicList.xml` del WDK, come dice la pagina].

Esempio di riga di comando (**schema**, da adattare; ogni opzione è fra quelle verificate sopra tranne dove indicato):
```
qemu-system-aarch64 -machine virt,gic-version=3,acpi=on -accel hvf -cpu host -smp 4 -m 8G \
  -drive if=pflash,format=raw,file=FW_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,file=FW_VARS.fd \
  -serial <chardev> -display <da scegliere> ...
```
`-serial` e `-chardev` non sono stati letti nella documentazione in questo task (la pagina era troncata): **[ipotesi]** sintassi da confermare su https://www.qemu.org/docs/master/system/invocation.html (sezione "Character device options") prima di scriverli negli script.

### 2.2 EDK2 `ArmVirtQemu` compilato da noi, con tabelle ACPI sostituibili

**Fatto verificato che esclude la strada facile**: l'opzione `-acpitable` di QEMU è documentata **solo per il target i386** [verificato: https://www.qemu.org/docs/master/system/invocation.html, "i386 target only"]. Su `virt` aarch64 non è disponibile. Inoltre con `acpi=on` QEMU genera da sé MADT (con GICC, GICD e GICR), GTDT, DBG2, SPCR, IORT e FADT con flag PSCI [verificato: `hw/arm/virt-acpi-build.c` di QEMU: `build_madt`, `build_gtdt`, `build_dbg2`, `build_spcr`, `build_iort`; `ACPI_FADT_ARM_PSCI_COMPLIANT` per il conduit SMC/HVC]. Le tabelle arrivano al firmware via fw_cfg.

**Dove si innesta EDK2** [verificato: `ArmVirtPkg/ArmVirtQemu.dsc` di tianocore/edk2]: la piattaforma include `OvmfPkg/AcpiPlatformDxe/AcpiPlatformDxe.inf` (driver "QemuFwCfgAcpiPlatform", entry point `AcpiPlatformEntryPoint`), che legge le tabelle da fw_cfg e le installa via `EFI_ACPI_TABLE_PROTOCOL` (il modulo dichiara quel protocollo come dipendenza); il DSC ha anche `OvmfPkg/PlatformHasAcpiDtDxe` e il PCD `PcdForceNoAcpi` (variabile NV `ForceNoAcpi`) e una configurazione `TPM2_CONFIG_ENABLE`.

Strategie per sostituire le tabelle, in ordine di preferenza:

| # | Strategia | Pro | Contro | Stato |
|---|---|---|---|---|
| S1 | **Piattaforma nostra `WatariQemuPkg`** (fork minimo di `ArmVirtQemu`) con un driver DXE `WatariAcpiOverride`: dopo `AcpiPlatformDxe`, scorre XSDT, e per ogni firma presente nel suo Firmware Volume (file `FREEFORM` generati da `acpigen`) sostituisce la tabella QEMU (uninstall + install via `EFI_ACPI_TABLE_PROTOCOL`, ricalcolando checksum e XSDT). Le tabelle restano fuori dal codice: sono file binari nel FV, rigenerabili senza ricompilare il driver | riproduce esattamente il meccanismo che userà `edk2-apple` (tabelle da generatore → protocollo ACPI); nessuna patch a QEMU | scrivere e provare il driver (circa 1 giorno di lavoro agente, [ipotesi]); `UninstallAcpiTable` richiede l'handle `TableKey` restituito all'installazione: se non lo si ha, l'override deve intercettare l'installazione, non sostituire a posteriori [ipotesi: da leggere in `MdeModulePkg/Universal/Acpi/AcpiTableDxe`] | proposta |
| S2 | Patch locale a `AcpiPlatformDxe` che, per le firme "sovrascritte", ignora la tabella di fw_cfg e carica la nostra dal FV | un solo punto di modifica | divergenza dal driver upstream, da rifare a ogni aggiornamento | proposta (ripiego di S1) |
| S3 | `-machine virt,acpi=off` + firmware che installa **tutte** le tabelle (DSDT inclusa) da generatore | controllo totale | serve un DSDT/FADT/IORT/MCFG/DBG2/SPCR completi: troppo lavoro per Gate B | scartata per ora |
| S4 | Patch al sorgente di QEMU (`virt-acpi-build.c`) | veloce da scrivere | rompe la riproducibilità, richiede compilare QEMU | scartata |

**Tabelle che restano di QEMU** (non le sostituiamo): RSDP/XSDT, FADT (salvo V3), DSDT, MCFG, IORT, SPCR, DBG2, SRAT/PPTT se presenti. **Tabelle nostre**: MADT, GTDT, CSRT e, per V3, una FADT modificata (bit PSCI) e per V4 un SSDT con il device del gruppo CSRT.

**Lacune del nostro `acpigen` rispetto a quanto serve** [verificato: lettura di `tools/acpigen/include/acpigen.h` e `src/main.c`]:
1. La CLI accetta solo `<madt|gtdt|pptt|csrt> -o file` e usa **solo** il modello t8103 cablato (`t8103_cpus`, GICD assente nella CLI, 8 CPU, `parked_address = 0`). Non ci sono opzioni per scegliere modello, numero di CPU, GICD, parking, GSIV.
2. `acpigen_build_madt` emette GICC e al più una GICD; **non emette la struttura GIC Redistributor (tipo 0x0E)** che QEMU `virt` con GICv3 mette in MADT (`build_append_gicr`) [verificato: sorgente QEMU]. Il campo `gicr_base_address` nella GICC esiste ma è un altro meccanismo.
3. Non c'è un generatore di **FADT** né di **SSDT/DSDT**.

Lavoro necessario su `acpigen` (task per un esecutore, non fatto qui): (a) opzione `--model {t8103|qemu-virt}` e flag `--no-gicd`, `--parking`, `--ncpus`; (b) modello `qemu-virt` con base GICD/GICR e GSIV timer **letti dal DT/ACPI di QEMU in esecuzione** (non inventati: vedi V1); (c) struttura GICR; (d) FADT con `arm_boot_arch` configurabile; (e) SSDT minimo con il device `_HID`/`_UID` legato al gruppo CSRT (compilato con `iasl`, vedi sez. 5) o in alternativa AML generato a mano [ipotesi sulla fattibilità].

**Come ricavare i valori della baseline** (per V1, senza inventarli): avviare Windows o una shell UEFI con le tabelle QEMU di serie e salvarle (shell UEFI di EDK2 comando `acpiview`/`dmpstore`: [ipotesi], controllare nel pacchetto ShellPkg), oppure leggere `virt-acpi-build.c` e `hw/arm/virt.c` per le basi di GICD/GICR [verificato: i file esistono su github.com/qemu/qemu; i valori numerici non sono stati letti in questo task]. Il confronto binario fra tabelle di QEMU e tabelle `acpigen` è il test di V1.

Compilazione EDK2: richiede toolchain AArch64 (GCC cross o clang) e `python3`, `nasm`/`iasl` secondo la piattaforma [ipotesi]; è un **download/installazione** quindi [UMANO]: backup prima.

### 2.3 ISO Windows 11 ARM64

- ISO ufficiale Microsoft di Windows 11 ARM64 [UMANO]: download da microsoft.com (pagina "Download Windows 11 Arm64"; URL esatto da verificare da Marco al momento, non fissato qui: [ipotesi]). Dimensione stimata ~5-7 GB [ipotesi].
- Congelare **build e hash SHA-256** dell'ISO usata e registrarli nel diario (R-11). Non mettere l'ISO nel repository: va in `/Volumes/Arthur Fonzarelli/WatariBoot-data/` (disco esterno; va collegato) (CLAUDE.md di progetto).
- Licenza/attivazione: Windows senza chiave funziona in modalità non attivata per test; condizioni d'uso: [ipotesi, controllare i termini Microsoft] [UMANO].
- Requisiti del setup (TPM 2.0, Secure Boot, RAM, disco): [ipotesi, da confermare con A2/documentazione Microsoft]. Il bypass dei controlli del setup è una pratica di comunità non documentata da Microsoft: **non** la si usa se esiste l'alternativa swtpm (sopra).

### 2.4 Spazio disco stimato (tutte stime [ipotesi])

Spazio libero oggi sul Mac: 53 GiB (da `df -h` su `/System/Volumes/Data`, [verificato: eseguito in questo task]).

| Voce | Stima |
|---|---|
| QEMU (Homebrew + dipendenze) o compilato | 1-2 GB |
| Toolchain EDK2 + albero edk2 + build | 3-6 GB |
| ISO Windows 11 ARM64 | 5-7 GB |
| Immagine qcow2 del target (sparsa; usata davvero) | 20-30 GB (nominale 64 GB) |
| VM del debugger (Windows ARM64 + WinDbg + WDK + Visual Studio Build Tools) | 40-60 GB |
| Snapshot/backup immagini, log, dump | 5-10 GB |
| **Totale** | **~75-115 GB** |

**Conclusione: 53 GiB liberi non bastano per l'intero Gate B** nella configurazione "VM debugger sul Mac". Opzioni: SSD esterno per le immagini [UMANO], liberare spazio, oppure usare un PC Windows già disponibile come host del debugger (nessun costo di spazio) [UMANO, ipotesi che esista]. Decisione e spazio vanno confermati prima di scaricare qualunque cosa.

## 3. Debug

### 3.1 Architettura di debug

Windows va debuggato con WinDbg, che gira solo su Windows [ipotesi: nessuna versione per macOS nota; verificare]. Il progetto non ha un secondo computer (PLAN). Schema proposto: due guest sullo stesso Mac.

```
Mac (HVF)
 ├─ QEMU "target": Windows 11 ARM64 sotto test + EDK2 nostro + (kernel debug attivo)
 └─ VM "debugger": Windows ARM64 (o altro) con WinDbg + WDK  [UMANO]
        collegamento: rete virtuale tra i due QEMU (KDNET)  oppure  seriale virtuale
```

Il target e la VM debugger si collegano con `-netdev socket` (punto-punto TCP, `listen`/`connect`) o con una rete condivisa di QEMU: **[ipotesi]** sintassi e comportamento esatti da leggere in https://www.qemu.org/docs/master/system/invocation.html (sezione Network options, non letta in questo task). In alternativa la VM debugger può essere un altro prodotto di virtualizzazione sulla stessa rete [UMANO, ipotesi].

### 3.2 Trasporti di debug

| Trasporto | Come | Pro | Contro |
|---|---|---|---|
| **KDNET** | NIC supportata nel target (sopra) | veloce, standard Microsoft, funziona con `bcdedit /dbgsettings net hostip:... port:...` | richiede NIC con ID in `VerifiedNicList.xml`; dipende dal fatto che l'ID emulato da QEMU ci sia [ipotesi]; richiede che il bus PCIe/MSI del target funzioni |
| **Seriale (KDCOM)** | `bcdedit /dbgsettings serial debugport:1 baudrate:115200` | indipendente da PCIe/NIC; **DBG2 e SPCR sono generate da QEMU** [verificato: `build_dbg2`, `build_spcr` in virt-acpi-build.c] e Windows sceglie il trasporto di debug leggendo DBG2 [verificato: https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-system-description-tables, sezione DBG2] | serve esporre l'UART PL011 di QEMU verso WinDbg: come (pipe/TCP) è [ipotesi]; compatibilità del tipo di UART con KDCOM: [ipotesi, da A2/A4]; è il trasporto realistico su M1, quindi vale la pena provarlo |
| Solo schermata + dump | schermata di bugcheck via QEMU, nessun debugger | zero infrastruttura | informazione minima; il comando QEMU per catturare lo schermo (`screendump` del monitor) è [ipotesi, da verificare nella doc del monitor] |

Piano: V0 e V1 con **KDNET e seriale entrambi provati** (se uno non si aggancia lo si sa subito, prima di dare la colpa a una variante). Dalla V2 in poi si usa il trasporto che in V0 ha funzionato.

### 3.3 Comandi `bcdedit` (tutti da eseguire nel guest target, prompt amministratore, poi riavvio)

Fonti: https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/bcdedit--dbgsettings e https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/bcdedit--set [verificato: sintassi letta in questo task].

```
bcdedit /debug on                                       (abilita il debug del kernel per la voce corrente)
bcdedit /dbgsettings serial debugport:1 baudrate:115200
bcdedit /dbgsettings net hostip:<IP_DEBUGGER> port:50000 [key:<chiave>|newkey] [nodhcp]
bcdedit /set testsigning on                             (per V4: carica codice kernel test-signed)
bcdedit /set nocrashautoreboot on                       (non riavviare dopo bugcheck: si legge il codice)
bcdedit /set bootlog yes                                (log dei driver caricati, Ntbtlog.txt)
bcdedit /set sos on                                     (mostra i nomi dei driver durante il boot)
(non impostare mai "quietboot on": la pagina bcdedit /set dice che impedisce di vedere i dati di bugcheck)
bcdedit /enum                                           (mostra le voci e i GUID)
```
Note verificate dalla documentazione: `/dbgsettings` configura ma **non abilita** il debug, serve `/debug`; la porta KDNET deve essere 49152 o superiore; con `net` servono `HOSTIP` e `PORT`; per il seriale `DEBUGPORT` e `BAUDRATE` sono opzionali (default COM1, 115200); prima di impostare opzioni BCD può servire sospendere BitLocker e disattivare Secure Boot; `nointegritychecks` non si può impostare con Secure Boot attivo; `testsigning` richiede riavvio. Il comando per leggere `ForceNoAcpi` o altre variabili UEFI: non pertinente.
**Secure Boot**: nel firmware EDK2 di `ArmVirtQemu` la disponibilità/stato di Secure Boot è [ipotesi]; va deciso e registrato per ogni run (influenza `testsigning`).

### 3.4 Log lato QEMU e firmware

- Seriale del firmware: la console UART PL011 di EDK2 va su `-serial` ([ipotesi sulla sintassi, vedi 2.1]); registrare ogni run su file con timestamp. Nota: se la stessa UART è usata dal debugger Windows, il log del firmware e KDCOM si contendono la porta; usare la seconda UART di `virt` (la documentazione QEMU consiglia edk2-stable202311 o più recente per guest che usano entrambe le UART [verificato: risultato di ricerca su docs QEMU `virt`; da ricontrollare sulla pagina]) [ipotesi sul dettaglio].
- Log dei messaggi di errore del guest lato QEMU (opzioni `-d`, `-D`): [ipotesi, controllare la documentazione QEMU prima di usarle].
- Per ogni run archiviare: riga di comando QEMU completa, hash di firmware/tabelle, hash dell'ISO, versione di QEMU, log seriale, log WinDbg (`.logopen`), schermata finale, codice di bugcheck con i quattro parametri.

### 3.5 Cosa guardare in WinDbg (comandi standard del debugger, da confermare nella documentazione WinDbg prima dell'uso)

`!analyze -v` dopo un bugcheck, `.bugcheck`, `lm` (elenco moduli: la nostra estensione HAL compare?), `!irql`, `!pcr`, `!ipi`, `!ioapic`/equivalente per ARM [ipotesi], `!acpi`/`!amli` per le tabelle. [ipotesi: esistenza e output di ciascuna su ARM64]. Il documento non inventa altro: l'elenco va ripulito dopo il primo run (V0).

## 4. Matrice delle varianti

### 4.1 Cosa dimostrano e cosa no (nota onesta)

**In QEMU il GIC esiste comunque** (`virt` non lo può rimuovere, sez. 2.1). Conseguenze:

| Possono dimostrare | NON possono dimostrare |
|---|---|
| Come Windows **tratta le tabelle** quando mancano o sono incoerenti (GICD assente, parking, GTDT, CSRT): errori, bugcheck, gestione dei fallimenti all'avvio | Che Windows funzioni con un controller **non-GIC** davvero funzionante: il GIC reale di QEMU resta presente e potrebbe essere usato da Windows anche se la MADT non lo dichiara (accessi MMIO a indirizzi noti) [ipotesi: improbabile ma non escludibile] |
| Se Windows **carica** un'estensione HAL da CSRT e quali chiamate/messaggi emette (V4) | Se l'estensione HAL può **registrare un controller di interrupt** e farlo usare a Windows: per farlo servirebbe un'estensione che implementi un controller completo (bridge sul GIC di QEMU = reimplementare un GIC driver, lavoro enorme, [ipotesi]) |
| Se Windows enumera i core e li avvia con il **parking protocol** quando il firmware li parcheggia (V3), almeno con GIC presente | Il **wakeup senza GIC** (l'IPI del parking protocol è un SGI nel mondo GIC, vedi INTERFACES sez. 4): su M1 sarà AIC; in QEMU l'SGI viene dal GIC vero (V3 lo usa, quindi non prova nulla sul caso AIC) |
| Che Windows ARM64 **gira su core Apple M1** a EL1 (con HVF il guest esegue sui core reali): utile per R-07 (ID register, granularità pagine, istruzioni) [ipotesi: HVF maschera/espone alcuni registri, quindi la prova è parziale] | Il comportamento con le **FIQ** del M1: QEMU `virt` non instrada il timer come FIQ AIC. Non si riproduce il vincolo critico di [re/f0-m1-fiq-irq.md](../../../re/f0-m1-fiq-irq.md) (timer solo-FIQ) |
| Il **processo** (iterazione rapida, strumenti di debug, generatore di tabelle, driver DXE di override) | Timing, SError, cache coherency, DART, ANS, PMGR, AIC reale: tutto ciò che è hardware M1 |

Quindi Gate B può dare **un verdetto negativo forte** (se Windows rifiuta già le precondizioni nelle tabelle, la strada è in dubbio), ma solo un **verdetto positivo debole** ("le precondizioni nelle tabelle sono accettate"). Il sì definitivo viene solo dall'hardware (Fase 3) o da A1/A2/A4 combinati con V4.

### 4.2 Tabella

Convenzione: "baseline QEMU" = tabelle generate da QEMU `virt` con `gic-version=3` e `acpi=on`. I comandi `acpigen` sono quelli **da avere dopo l'estensione di sez. 2.2** (le opzioni `--model/--no-gicd/--parking/...` non esistono ancora: oggi la CLI accetta solo `acpigen <madt|gtdt|pptt|csrt> -o file`, [verificato: `src/main.c`]).

| Var. | Tabelle ACPI usate | Comando `acpigen` (futuro, tranne dove indicato) | Cosa osserviamo | Esiti possibili → conclusione | Tempo stimato |
|---|---|---|---|---|---|
| **V0** baseline | tutte quelle di QEMU, firmware EDK2 nostro **senza** override | nessuno (`WatariAcpiOverride` disattivato) | Windows installa e arriva al desktop; tutti i core visibili (`-smp`); KDNET e/o seriale agganciati; `bcdedit` applicati; snapshot dell'immagine | **OK**: ambiente valido, si prosegue. **Windows non parte / non si installa**: problema di ambiente (HVF, firmware, TPM, driver disco), non di Watari: si risolve prima di tutto, nessuna conclusione su Q1-Q5. **Debug non si aggancia**: cambiare trasporto prima di V1 | 1-3 giorni (installazione, configurazione, due trasporti di debug) [ipotesi] |
| **V1** generatore | MADT, GTDT sostituite da `acpigen` equivalenti alla baseline QEMU (stesse CPU, GICC, GICD, GICR, GSIV timer, flag); resto QEMU | `acpigen madt --model qemu-virt --ncpus N -o madt.bin`; `acpigen gtdt --model qemu-virt -o gtdt.bin` | Confronto binario con le tabelle QEMU (stessi campi, checksum validi); Windows si comporta come V0 | **Come V0**: `acpigen` e `WatariAcpiOverride` validati, V2-V5 sono interpretabili. **Differenze nel boot o bug-check**: bug nel generatore o nell'override (non in Windows): correggere e rifare. Questo è il test che separa "Windows rifiuta" da "la nostra tabella è sbagliata" (falso negativo, sez. 6) | 1-2 giorni, più il lavoro sul generatore [ipotesi] |
| **V2** GICC senza GICD | MADT con 4-8 GICC e **senza GICD (e senza GICR)**; GTDT e il resto come V1 | `acpigen madt --model qemu-virt --no-gicd --ncpus N -o madt.bin` | Dove si ferma il boot; codice e parametri di bugcheck; messaggi su seriale/debugger; se Windows enumera le CPU prima di fallire; se la fase è prima/dopo `HalInitSystem` [ipotesi sui nomi] | **Boot completo o fermo in modo "non per il controller" (es. nessun interrupt ma nessun rifiuto)**: segnale che la mancanza di GICD non è controllata presto → R-02 ridimensionato (positivo debole, vedi sotto). **Bugcheck/halt presto con causa riconducibile a assenza di controller di interrupt**: Windows pretende un GIC dichiarato → R-02 confermato; il piano passa al "GIC fittizio nel DSDT/ACPI intercettato da estensione" (INTERFACES 3.1) e al piano B. **Rifiuto generico non attribuibile**: inconcludente, ripetere con V2b (GICD presente ma indirizzo non valido) per separare "manca la struttura" da "manca l'hardware" | 2-4 giorni [ipotesi] |
| **V3** parking protocol | MADT: GICC con `parking_version` e `parked_address` validi per ogni core, **GICD presente** (per isolare la domanda); FADT con `arm_boot_arch` **senza** PSCI_COMPLIANT (QEMU la mette con SMC/HVC, [verificato: virt-acpi-build.c]); resto V1 | `acpigen madt --model qemu-virt --parking --parked-base 0x... -o madt.bin`; `acpigen fadt --model qemu-virt --no-psci -o fadt.bin` (FADT: da creare) | Se Windows avvia i core secondari (`!pcr`/Task Manager: N core attivi); se scrive nella mailbox (osservabile con watchpoint o log del driver DXE di parcheggio); eventuali timeout/bugcheck; sequenza di scrittura (entry point prima di cpu_id) per confrontare con [verificato: Linux, `acpi_parking_protocol.c` come in INTERFACES 4] | **Core secondari avviati**: Windows supporta il parking protocol come previsto e la nostra mailbox è conforme (positivo, ma con GIC presente, vedi limiti). **Windows resta a 1 core senza errori**: la MADT/FADT non è stata interpretata come parking, oppure Windows richiede PSCI; ricontrollare i flag FADT e la versione del protocollo (**ipotesi, da A2**). **Bugcheck all'avvio dei core**: anomalia nella mailbox, indirizzo non mappato, memoria non riservata (UEFI memory map) → correggere e ripetere. **Mai scrive la mailbox**: Windows non usa il protocollo → ADR-0003 in dubbio | 4-7 giorni: serve il codice di parcheggio EDK2 (i core in QEMU partono spenti e vanno accesi con PSCI `CPU_ON` dal firmware e messi in loop di polling, [ipotesi sul meccanismo QEMU/PSCI]) |
| **V3b** (opzionale) parking + senza GICD | come V3 con la MADT di V2 | `--parking --no-gicd` | come V3 | solo se V2 e V3 sono entrambi non negativi | 1-2 giorni |
| **V4** CSRT + estensione HAL di prova | MADT/GTDT come V1; **CSRT** con un nostro resource group (vendor ID nostro, non riservato Microsoft); **SSDT** con il device ACPI collegato al gruppo (richiesto dal documento Microsoft: ogni resource group deve avere un device nello spazio dei nomi con identificatori coincidenti, [verificato: https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-system-description-tables, sezione CSRT]) | `acpigen csrt --model watari-test -o csrt.bin` (la CLI attuale `acpigen csrt` genera il gruppo "APLE" del modello t8103, [verificato: `t8103_model.c`]); SSDT: da creare | Se Windows tenta di caricare la DLL (`lm` mostra il modulo; messaggio `DbgPrint` dalla DLL; voce nel `Ntbtlog.txt`); se il caricamento fallisce, con quale errore; se il device ACPI viene enumerato; se `testsigning` basta o la DLL viene rifiutata; dove nel boot avviene | **DLL caricata e il nostro punto d'ingresso eseguito**: il meccanismo CSRT → estensione HAL funziona con firma di test per codice nostro (positivo per Q4; ricava la forma reale dell'interfaccia, da confrontare con A1). **Non viene caricata**: cause da separare — (a) CSRT/SSDT errati (V1-like validazione), (b) formato del gruppo CSRT o associazione INF/registro diversi da quelli assunti, (c) Windows carica estensioni solo per tipi di risorsa che conosce → conclusione sul "vuoto di documentazione"; il progetto deve allora ottenere le informazioni da A1/WDK, non indovinare. **Caricata ma Windows bugchecka alla prima chiamata**: ottimo segnale informativo: l'interfaccia esiste, i parametri sono sbagliati | 5-10 giorni: compilare la DLL richiede VM Windows con WDK/SDK [UMANO]; dipende fortemente da A1 per entry point e firma [ipotesi, da confermare con A1] |
| **V5** timer | MADT come V1; **GTDT** (a) assente, (b) con GSIV non valido, (c) con flag/PPI alterati; resto V1 | (a) nessun file GTDT: l'override *rimuove* la tabella; (b),(c) `acpigen gtdt --model qemu-virt --virt-gsiv N ...` (opzioni da creare) | Se Windows parte senza GTDT; se il tick di sistema funziona (orologio, `Get-Date`, `!pcr`); messaggi/bugcheck; quanto tarda prima che il sistema si blocchi; effetto sul contatore `QueryPerformanceCounter` vs. interrupt del timer | **(a) Windows parte e il tempo scorre**: Windows ha una sorgente di tick alternativa (diversa dal GTDT) usabile senza descrizione ACPI: se esiste un tick non-GIC, riduce R-03 [ipotesi, andrebbe separato da ciò che QEMU/HVF espone]. **(a) Windows non parte / tempo fermo / bugcheck "clock"**: il timer architetturale descritto in GTDT è obbligatorio e lo si riceve come interrupt GIC; sul M1 (timer solo FIQ, [re/f0-m1-fiq-irq.md](../../../re/f0-m1-fiq-irq.md)) la sola via resta un'estensione HAL timer o il piano B → R-03 resta critico e si cerca in A1 il meccanismo "timer da estensione HAL". **(b)/(c)**: misura la tolleranza a GSIV non coerenti con il controller (utile per l'indirizzamento GSIV scelto per l'AIC). **Nota**: V5 non è il vincolo FIQ; per quello non c'è prova in QEMU (4.1) | 2-4 giorni [ipotesi] |
| **V6** timer come FIQ | tabelle come V1 (MADT/GTDT di V1); firmware o QEMU modificati per consegnare il timer (INTID 30 e 27) come FIQ: opzione A = driver DXE che mette i PPI in Group 0 e abilita `ICC_IGRPEN0_EL1`; opzione B = patch `virt.c` con uscita timer collegata a `ARM_CPU_FIQ` | nessuno per `acpigen`; `-accel hvf,kernel-irqchip=off` (ripiego `-accel tcg`), dettagli in 4.4 | Bugcheck, blocco, assenza di clock tick, boot completo con orologio che avanza; traccia delle FIQ consegnate | **Funziona**: Windows gestisce la FIQ del timer, decisivo per la strategia del timer (ADR-0004, R-03). **Bugcheck/blocco**: non tollera la FIQ, serve tick via IRQ o piano B. **Nessun tick**: FIQ ignorata. **FIQ non dimostrata**: inconcludente. Dettaglio in 4.4.4 | 3-4 giorni [ipotesi] |

### 4.3 Dipendenze e punti incerti trasversali

- **Cosa significa "arriva al desktop"**: criteri misurabili per ogni run: (1) logo/spinner, (2) schermata di login/OOBE, (3) desktop con Task Manager, (4) PowerShell risponde; si registra l'ultimo traguardo raggiunto. Se il boot si ferma presto si legge il debugger prima di tutto.
- **IORT/ITS e MSI** (tabelle QEMU) possono fare riferimento al GIC/ITS anche quando la MADT non li dichiara; se V2 si ferma per questo, la causa non è "Windows pretende il GIC in MADT" ma "ACPI incoerente": provare `msi=off` [verificato che l'opzione esiste; effetto: ipotesi].
- **Differenza tra "accetta" e "usa"**: per ogni variante annotare se il bug-check avviene prima o dopo la prima chiamata al codice del controller (nel V4 per l'estensione).
- Quali nomi interni di funzioni citare (es. `HalpInterruptRegisterController`) viene da PLAN/INTERFACES ma è [ipotesi]: non sono usati come criterio di osservazione; i criteri sono i comportamenti visibili dall'esterno.

### 4.4 V6: timer consegnato come FIQ

Task A10 (issue #18). Scopo: sul M1 il timer ARM arriva solo come FIQ ([re/f0-m1-fiq-irq.md](../../../re/f0-m1-fiq-irq.md) sez. 2). V5 non lo riproduce (4.1): in QEMU il timer è un PPI del GIC consegnato come IRQ. V6 chiede a scatola nera: **Windows ARM64 tollera o gestisce una FIQ del timer?** Fonti QEMU lette in questo task su `master` di github.com/qemu/qemu (la versione installata da Marco potrebbe differire: ricontrollare i file indicati).

#### 4.4.1 Come far arrivare un PPI come FIQ in un GICv3 (QEMU)

- **Regola di consegna.** In `gicv3_cpuif_update()` (`hw/intc/arm_gicv3_cpuif.c`): un interrupt di Group 0 è sempre segnalato come FIQ (`case GICV3_G0: isfiq = true`); uno di Group 1 NS è segnalato come IRQ quando la CPU è non-secure. Il commento del codice rimanda alla sez. 4.6.2 della specifica GICv3 [verificato: arm_gicv3_cpuif.c, `gicv3_cpuif_update`]. La specifica Arm non è stata riletta direttamente: [ipotesi] che la sez. 4.6.2 dica la stessa cosa.
- **Gruppo del PPI.** `gicv3_irq_group()` (`gicv3_internal.h`): per INTID < 32 il bit di `GICR_IGROUPR0` a 1 significa Group 1 NS; a 0, con `GICD_CTLR.DS=1`, significa **Group 0** [verificato]. Il PPI del timer fisico NS EL1 è INTID 30 (`ARCH_TIMER_NS_EL1_IRQ`), quello virtuale INTID 27 (`ARCH_TIMER_VIRT_IRQ`), il timer hyp NS EL2 INTID 26 [verificato: `include/hw/arm/bsa.h`].
- **GIC a un solo security state.** Se `virt` non ha `secure=on`, la proprietà `has-security-extensions` del GIC è falsa e al reset `GICD_CTLR = DS | ARE` [verificato: `arm_gicv3_common.c` reset; `virt.c` righe 1417-1419 con `has-security-extensions = vms->secure`]. Con DS=1 un guest non-secure può scrivere `GICR_IGROUPR0` [verificato: `arm_gicv3_redist.c`, `case GICR_IGROUPR0` scrive solo se `attrs.secure || DS`]. Quindi **non servono né `secure=on` né `virtualization=on`**: bastano `gic-version=3` e una scrittura a `GICR_IGROUPR0` con il bit 30 (e 27) a 0. `secure=on` aggiungerebbe un EL3 e la regola `SCR_EL3.FIQ` (la FIQ andrebbe a EL3, che non c'è firmware per gestire): da evitare [ipotesi, ragionamento dal modello Arm; non provato].
- **Abilitazioni necessarie perché la FIQ parta.** `icc_hppi_can_preempt()` richiede `icc_igrpen[grp] != 0`, cioè **`ICC_IGRPEN0_EL1`=1** per il Group 0 [verificato: arm_gicv3_cpuif.c:991]. Con DS=1 `GICD_CTLR.EnableGrp0` (bit 0) è scrivibile [verificato: arm_gicv3_dist.c, `case GICD_CTLR`, maschera `EN_GRP0 | EN_GRP1NS`]. Servono anche enable del PPI in `GICR_ISENABLER0` e priorità adeguata/PMR (come per ogni interrupt).
- **Reset del gruppo.** Al reset `gicr_igroupr0 = 0` (Group 0) a meno di `irq_reset_nonsecure`, impostato solo per il boot diretto di un kernel con GIC security-aware [verificato: arm_gicv3_common.c righe 512-526, 574-600].
- **Chi lo configura: il nostro EDK2.** Il driver `ArmPkg/Drivers/ArmGicDxe/GicV3/ArmGicV3Dxe.c` di EDK2, se `GICD_CTLR.DS=1`, scrive `0xffffffff` nel registro IGROUPR della redistributor frame SGI (tutto in Group 1 NS) e fa lo stesso per gli SPI [verificato: ArmGicV3Dxe.c, blocco "Disable Security (DS)"; la scrittura riguarda la redistributor `mGicRedistributorBase` della CPU che esegue il driver: sui core secondari il valore resta quello di reset, cioè Group 0 [ipotesi]]. Per V6 va aggiunto, **dopo** quel driver e per **ogni** CPU, un passo che azzera il bit del PPI e abilita il Group 0 (vedi 4.4.3). TF-A non c'entra in `virt` senza `secure=on`.

#### 4.4.2 HVF o TCG

- Con HVF `gic-version=3` è accettato [verificato: `virt.c` ~2711-2717]. Ma l'irqchip HVF ha due modalità: **in-kernel** (GIC di Hypervisor.framework, `hv_gic_create`) e **userspace** (GIC emulato da QEMU). L'opzione è `-accel hvf,kernel-irqchip=on|off|split` (split non supportato) [verificato: `accel/hvf/hvf-all.c` e `hvf-accel-ops.c`]. Nelle macchine `virt` fino alla 11.0 il default per HVF è userspace; per le successive è in-kernel [verificato: `virt.c` `get_kernel_irqchip_default` e `hvf_no_kernel_irqchip_default = true` in `virt_machine_11_0_options`]. Il default dipende quindi dalla versione di QEMU: **va sempre esplicitato**.
- **Il GIC in-kernel di Apple non è sotto il nostro controllo**: se rispetti IGROUPR/Group 0 come la specifica, non si sa [ipotesi]. **Raccomandato: `kernel-irqchip=off`**, cioè il codice GICv3 di QEMU letto sopra. In quella modalità HVF inietta una FIQ al guest con `hv_vcpu_set_pending_interrupt(..., HV_INTERRUPT_TYPE_FIQ, ...)` quando la CPU QEMU ha `CPU_INTERRUPT_FIQ` [verificato: `target/arm/hvf/hvf.c`, `hvf_inject_interrupts`], e il timer virtuale passa da `HV_EXIT_REASON_VTIMER_ACTIVATED` a `qemu_set_irq(gt_timer_outputs[GTIMER_VIRT])` [verificato: hvf.c righe ~2580-2583]. Il timer fisico (CNTP_*) in modalità userspace è emulato da QEMU (accessi intercettati [verificato: hvf.c, gestione `SYSREG_CNTP_*`]). Che l'intero percorso funzioni per una FIQ di timer in pratica: [ipotesi, da provare].
- **TCG** (`-accel tcg`, `-cpu max`) esegue lo stesso modello GIC e la stessa logica FIQ senza dipendere da Hypervisor.framework ed è più lento (ordini di grandezza, non misurati); per un test di boot di Windows può essere molto lento [ipotesi]. È il ripiego se HVF userspace non inietta la FIQ.
- **Comando più probabile** [ipotesi: assemblato da opzioni verificate, non eseguito]:
```
qemu-system-aarch64 -machine virt,gic-version=3,acpi=on,secure=off,virtualization=off \
  -accel hvf,kernel-irqchip=off -cpu host -smp 4 -m 8G \
  -drive if=pflash,format=raw,file=WATARI_FW_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,file=WATARI_FW_VARS.fd ... (resto come V0)
```
  Ripiego: `-accel tcg -cpu max`. Opzioni di tracing per vedere le FIQ lato QEMU: eventi `gicv3_cpuif_set_irqs` e `hvf_inject_fiq` esistono nel codice [verificato: `trace_gicv3_cpuif_set_irqs`, `trace_hvf_inject_fiq`]; la sintassi `-trace` è [ipotesi, controllare la documentazione prima].

#### 4.4.3 Come forzarlo senza modificare Windows (opzioni, valutate)

| Opz. | Metodo | Pro | Contro | Giudizio |
|---|---|---|---|---|
| **A** | Patch al nostro EDK2 (driver DXE `WatariTimerFiq`, dopo `ArmGicV3Dxe`): su ogni CPU azzera i bit 27 e 30 di `GICR_IGROUPR0`, imposta `GICD_CTLR.EnableGrp0` e `ICC_IGRPEN0_EL1=1`. Nessuna patch a QEMU | Nessuna modifica a QEMU; riproduce un'operazione di firmware vera | **Windows riprogramma il GIC**: il suo driver GICv3 probabilmente imposta di nuovo i PPI in Group 1 (e abilita solo IGRPEN1) [ipotesi, non verificato: non si analizza Windows]. In quel caso la FIQ sparisce prima di arrivare e V6 darebbe un falso risultato "IRQ normale". Va controllato leggendo IGROUPR0 dopo il boot (tramite il debugger o un breakpoint QEMU) | Provarla per prima: costo basso, e il fatto che Windows la annulli o no è già un dato |
| **B** | Patch QEMU in `hw/arm/virt.c` (~righe 1293-1307): per `GTIMER_PHYS` e `GTIMER_VIRT` collegare l'uscita del timer **direttamente** a `qdev_get_gpio_in(cpudev, ARM_CPU_FIQ)` invece che al PPI del GIC (le FIQ del GIC sono collegate allo stesso ingresso, riga ~1326: serve una porta OR a 2 ingressi) | **Indipendente da ciò che Windows fa al GIC**; è il modello più fedele al M1, dove il timer "bypasses AIC. It is wired straight to FIQ" ([re/f0-m1-fiq-irq.md](../../../re/f0-m1-fiq-irq.md) sez. 0) | Richiede compilare QEMU (download/installazione, backup); la modifica è nostra, ~10 righe, ma va tenuta allineata; ACPI GTDT dichiara ancora GSIV 30/27: da decidere se lasciarlo [ipotesi: Windows lo usa per scegliere/validare il timer]. Sotto HVF in userspace la FIQ passa dal percorso `hvf_inject_interrupts`: [ipotesi] funziona | **Raccomandata come metodo definitivo** se A è annullata da Windows (o per default, perché non dipende da Windows) |
| **C** | Patch QEMU nel modello del GIC: forzare Group 0 per gli INTID 26-30 ignorando `GICR_IGROUPR0` (`gicv3_irq_group`) | Tiene il percorso "via GIC" (IGRPEN0 ancora necessario) | Più invasiva e meno fedele: nasconde al guest la propria scrittura su IGROUPR; mantiene dipendenza da `ICC_IGRPEN0_EL1` che Windows probabilmente non abilita [ipotesi] | Scartata (B è più semplice e più fedele) |
| **D** | `secure=on` con TF-A/EDK2 che instrada via `SCR_EL3.FIQ` | Usa configurazione "standard" Arm | Serve firmware EL3 che gestisca la FIQ e la reinoltri a EL1: lavoro grosso, non è ciò che avviene sul M1 | Scartata |

Esito della valutazione: **A come primo tentativo a basso costo, B come metodo di riferimento.** Con A, se il risultato è "tutto normale" bisogna prima dimostrare che la FIQ è davvero arrivata (traccia `hvf_inject_fiq` o `gicv3_cpuif_set_irqs ... fiqlevel=1`), altrimenti non è un risultato. Con B la consegna FIQ è garantita dalla macchina, quindi il risultato è interpretabile.

#### 4.4.4 Cosa osservare e come leggere gli esiti

Registrare, come nelle altre varianti (sez. 3.4): comando QEMU, versione QEMU, hash ISO, build del kernel Windows, tracce `hvf_inject_fiq` / `gicv3_cpuif_set_irqs`, log seriale/debugger, screenshot. Precondizione: V0 e V1 verdi (stessa regola di V5).

| Osservazione | Come | Lettura |
|---|---|---|
| **a. Bugcheck** | codice e parametri; dopo `bcdedit /set nocrashautoreboot on`; `!analyze -v` | Un bugcheck legato a un'eccezione non gestita o a un interrupt inatteso (nomi esatti dei codici: [ipotesi], leggerli, non indovinarli) → **Windows non tollera la FIQ**: nessun vettore/handler FIQ utile. |
| **b. Blocco senza bugcheck** | schermo fermo, nessun I/O, il debugger non riceve | Possibile FIQ asserita e mai servita (tempesta/loop): Windows non mette la sorgente a tacere → "non gestita". Distinguere da un blocco per altri motivi con V0/V1 e con il contatore delle FIQ iniettate nella traccia QEMU (cresce in modo continuo = tempesta). |
| **c. Nessun clock tick** | boot che prosegue ma orologio fermo, `Get-Date` non avanza, thread che non vengono schedulati con timeout, tick DPC assenti | Windows ignora la FIQ e non riceve il tick via IRQ: la FIQ è "tollerata ma inutile" per il timer. Non conclude sulla gestione della FIQ in sé: il timer resta di fatto inutilizzabile. |
| **d. Funziona** | boot completo, orologio che avanza, Task Manager, e la traccia mostra FIQ consegnate | **Windows gestisce la FIQ del timer** (o la mappa su un percorso interno): vedi sotto. Verificare che le FIQ siano davvero consegnate e non solo programmate (altrimenti falso positivo, E-3-like). |
| **e. Uso del debugger** | breakpoint su vettore FIQ del guest (`VBAR_EL1 + offset` della tabella dei vettori AArch64 per FIQ da EL1: calcolo da verificare nella specifica Arm [ipotesi]), `.bugcheck`, `lm`, `!pcr`, `!irql` (esistenza su ARM64: [ipotesi]) | Se il debugger è agganciato (seriale/KDNET), il kernel Windows si fermerà al bugcheck o rimarrà in break: confrontare i percorsi con V0. Se il debug cambia il risultato, registrarlo (E-6). |

Conclusioni per esito:
- **d (positivo)**: **decisivo per la strategia del timer.** Significa che Windows ARM64 accetta una FIQ del timer (almeno in QEMU, con questa versione di Windows): la strategia "mai FIQ" ([ADR-0004](../../adr/0004-irq-invece-di-fiq.md)) e il bisogno di un timer alternativo via IRQ (watchdog IRQ 338, [re/f0-m1-fiq-irq.md](../../../re/f0-m1-fiq-irq.md) sez. 3) perderebbero urgenza: l'AIC/FIQ del M1 potrebbe essere mappato nativamente, con un'estensione HAL che gestisce la FIQ o con un'AIC trasparente. Resta da dimostrare sull'hardware M1 e ripetere su più build di Windows (R-11).
- **a o b (negativo)**: Windows non tollera la FIQ del timer (solo nelle condizioni di QEMU). Conferma R-03/V5: servono un'altra sorgente di tick via IRQ (estensione HAL timer con WDT o `ACC_OVRD`) o il piano B (guscio a EL2 che converte FIQ in IRQ virtuale, contrario ad ADR-0001). Cautela: con la metodologia A, prima escludere che la configurazione GIC di Windows abbia annullato la FIQ; con B il negativo è netto.
- **c**: la FIQ è ignorata; per Windows il timer non esiste → stesso trattamento di a/b per la strategia, ma più "dolce": indica che Windows sopravvive a una FIQ spuria e che l'unico problema è dare il tick via IRQ.
- **Inconcludente**: se non si dimostra che la FIQ sia stata consegnata (traccia assente), oppure se V0/V1 non sono verdi, oppure se HVF userspace non inietta correttamente (passare a TCG e ripetere).

#### 4.4.5 Limiti di V6

- QEMU modella una FIQ "pura" GICv3, **non** il comportamento dell'AIC del M1 (nessun registro di stato globale, sorgenti multiple, mascheramento via `CNTP_CTL_EL0.IMASK`, [re/f0-m1-fiq-irq.md](../../../re/f0-m1-fiq-irq.md) sez. 1). Anche un esito positivo non prova che Windows gestisca le altre FIQ del M1 (fast IPI, PMU): solo il percorso FIQ → handler del timer.
- Il GIC in-kernel di HVF non è modificabile; con `kernel-irqchip=off` le prestazioni e il comportamento temporale del guest possono differire [ipotesi].
- Con A il test dipende dalla riprogrammazione del GIC da parte di Windows; con B si aggira, ma si modifica QEMU (binario non standard, build da riprodurre). Con B, ACPI GTDT/MADT/GICD possono restare incoerenti con la FIQ: Windows potrebbe rifiutare per questo e non per la FIQ (vedi E-1/E-2).
- Quale timer usi Windows ARM64 (fisico CNTP, virtuale CNTV, EL2) non è documentato qui [ipotesi]: instradare come FIQ **entrambi** (INTID 30 e 27).
- Costo stimato: A circa 2 giorni (driver DXE + prove), B circa 1-2 giorni più compilare QEMU [ipotesi]. Download/installazioni: solo dopo backup Time Machine con conferma esplicita di Marco.

## 5. Prerequisiti umani [UMANO]

Tutti i passi sotto richiedono **prima**: backup completo Time Machine, conferma esplicita di Marco nella chat, spazio libero confermato (sez. 2.4). Gli agenti Sonnet non scaricano né installano nulla.

| # | Passo | Dettagli |
|---|---|---|
| H1 | Backup Time Machine | Già confermato per la Fase 0 in `docs/STATUS.md` (commit "Backup Time Machine confermato"), **da riconfermare prima di ogni nuova installazione** [regola assoluta] |
| H2 | Spazio disco | Decidere dove stanno le immagini (SSD esterno, disco interno liberato, o PC Windows come host debugger). Obiettivo ≥ 100 GB disponibili, vedi 2.4 |
| H3 | Installare QEMU | es. Homebrew (`brew install qemu`) [ipotesi sul comando, controllare la formula], o compilazione; verificare con `qemu-system-aarch64 --version` |
| H4 | Installare toolchain EDK2 e compilare firmware | passi a cura di un esecutore, dopo conferma |
| H5 | Scaricare l'ISO Windows 11 ARM64 | sito Microsoft ufficiale; registrare versione/build e SHA-256; salvarla fuori dal repo in `/Volumes/Arthur Fonzarelli/WatariBoot-data/` (disco esterno; va collegato) |
| H6 | Windows di sviluppo con WDK e debugger | una VM Windows ARM (o PC Windows): Windows SDK, WDK, Visual Studio Build Tools per compilare la DLL di V4 e per usare WinDbg. Software da scaricare dai siti Microsoft [UMANO]. Strumento di virtualizzazione per la VM sul Mac: scelta di Marco [ipotesi: UTM/Parallels/VMware; ognuna ha licenze e prezzi propri, da verificare] |
| H7 | Licenze | Windows (non attivato per test: verificare i termini [ipotesi]); strumento di virtualizzazione della VM debugger; WDK/SDK/VS Build Tools: licenze Microsoft da leggere e accettare **da Marco**. L'accettazione dei termini non la fa un agente |
| H8 | `swtpm` | per il TPM virtuale; installazione [UMANO]; sintassi QEMU verificata (2.1) |
| H9 | `iasl` (compilatore ASL) | per l'SSDT di V4; è un download [UMANO]; alternativa: SSDT generato da `acpigen` (da sviluppare) |
| H10 | Firma di test | nessun acquisto: `bcdedit /set testsigning on` nel guest target; certificato di test generato con gli strumenti del WDK (procedura da A1/documentazione Microsoft) |
| H11 | Account Microsoft/login | Windows può richiedere un account in OOBE: scelta di Marco [ipotesi: modalità senza account dipende dalla build]. Gli agenti non inseriscono credenziali |

## 6. Ordine di esecuzione e criteri di stop

Ordine: **V0 → V1 → V2 → V3 → V5 → V4** (V3b dopo V2/V3). V4 è per ultima perché dipende da A1 e dal toolchain Windows e perché ha il costo maggiore; V5 è economica e informativa, si fa prima.

| Passo | Prerequisito | Criterio di successo | Criterio di stop |
|---|---|---|---|
| 0 | H1-H5 | Windows installato e funzionante sotto QEMU+HVF; snapshot "V0-ok" | Se dopo ~5 giorni V0 non parte: **stop** del Gate B, rivalutare l'ambiente (TCG invece di HVF, altra versione QEMU) prima di toccare le tabelle |
| V1 | override DXE e `acpigen` esteso | tabelle identiche a QEMU per i campi di interesse, boot uguale a V0 | Se V1 differisce da V0 e la causa non si trova in ~3 giorni: **stop**, non si interpreta nessuna variante successiva (il generatore o l'override non sono affidabili) |
| V2 | V1 verde | esito netto (boot o bugcheck attribuibile) | Se per tre ripetizioni indipendenti l'esito non è distinguibile (rifiuto generico): congelare il risultato come "inconcludente" e passare a V3 senza insistere |
| V3 | V1 verde + driver di parcheggio | N core attivi, mailbox scritta | Se il driver di parcheggio EDK2 non funziona in QEMU entro ~1 settimana: sospendere V3 e annotarlo; il parcheggio sarà comunque da scrivere in `edk2-apple` |
| V5 | V1 verde | esito per (a), (b), (c) | nessuno specifico |
| V4 | V1 verde, A1 disponibile, H6, H9, H10 | DLL caricata o motivo del mancato caricamento identificato | Se mancano informazioni di A1 sull'interfaccia: **non si indovina**: V4 si sospende e si aspetta A1 |

**Stop globale del Gate B** (qualunque di questi): (1) due varianti consecutive (V2 e V3) con bugcheck che indicano che Windows pretende GIC/PSCI → verdetto "strada nativa in dubbio", si passa alla discussione con Marco sul piano B (ADR-0001); (2) esaurimento delle risorse concordate (tempo totale > ~4 settimane di lavoro agente, [ipotesi]). **Tutti i verdetti vanno a Marco**, nessun cambio di strategia automatico.

Stima totale se tutto va liscio: 3-5 settimane di calendario (lavoro condiviso agenti + Marco, che esegue download e passi in VM) [ipotesi]; il collo di bottiglia sono V0 (ambiente Windows+debug) e V3 (codice di parcheggio).

## 7. Rischi dell'esperimento e interpretazione di falsi negativi e falsi positivi

| ID | Rischio | Tipo di errore | Come si controlla |
|---|---|---|---|
| E-1 | Tabella generata male (checksum, lunghezza, campi) o override DXE che installa male | **Falso negativo** (Windows sembra rifiutare ma è colpa nostra) | V1 obbligatoria prima di ogni altra; verifica con un decodificatore ACPI (es. `iasl -d`) e confronto binario con QEMU; esito di V1 uguale a V0 |
| E-2 | Altre tabelle QEMU (IORT/ITS/MCFG/DSDT/FADT) che si riferiscono al GIC o a PSCI e quindi producono errori non dovuti alla MADT | **Falso negativo** per V2/V3 | Guardare la causa del bugcheck, non solo il fatto; V2b; tentare `msi=off`; per V3 la FADT senza PSCI è parte della variante |
| E-3 | Il GIC vero di QEMU è comunque attivo: Windows potrebbe funzionare "per caso" accedendo al GIC senza averlo in MADT | **Falso positivo** per V2/V3b | Registrare gli accessi MMIO alla regione GIC (tracing di QEMU: opzioni da documentare [ipotesi]); non concludere "Windows accetta il non-GIC" ma solo "accetta MADT senza GICD" |
| E-4 | Firmware/immagine diversi fra le run (nessuna riproducibilità) | entrambi | Comando QEMU, versioni, hash e tabelle archiviati; snapshot del disco prima di ogni run (`qcow2`, [ipotesi sul flusso]); una sola variabile cambia per volta |
| E-5 | HVF presenta al guest una CPU diversa dal M1 reale (registri mascherati, EL1 invece di EL2, niente FIQ/AIC) | **Falso positivo** su compatibilità CPU/timer | Dichiarare il limite nei risultati; V5 non prova nulla sul vincolo FIQ |
| E-6 | Debugger non si aggancia o altera il comportamento (KDNET/seriale cambiano il boot) | falso negativo / interferenza | provare V0 con e senza debug; confrontare tempi e percorso; se il debug cambia il risultato, registrarlo |
| E-7 | Aggiornamenti di Windows cambiano il comportamento (R-11) | entrambi | Congelare l'ISO, non fare Windows Update; registrare la build del kernel nel log di ogni run |
| E-8 | V4: l'estensione non si carica per ragioni di firma (testsigning, Secure Boot attivo, catene) e non per CSRT | **Falso negativo** | Verificare in V0 che un driver di prova test-signed qualsiasi si carichi con `testsigning on`; annotare lo stato Secure Boot |
| E-9 | Informazioni di A1/A2/A4 contraddicono le assunzioni di V3/V4 (es. versione del protocollo, tipo di risorsa CSRT) | entrambi | Riscrivere le sezioni marcate [ipotesi, da confermare con A1/A2/A4] prima dell'esecuzione; non eseguire V3/V4 senza A2/A1 |
| E-10 | Rischio per il Mac: nessuno diretto (tutto in VM), ma i download sono rischiosi per spazio e integrità | operativo | backup (H1), spazio (2.4), download solo da fonti ufficiali con hash registrato |
| E-11 | Costo/tempo > benefici: il Gate B è lungo e con verdetto positivo debole | strategico | decidere con Marco dopo V1-V2: se V2 è negativo in modo netto, il valore degli altri esperimenti cambia |

Regola di lettura dei risultati: un esito **positivo** in QEMU si scrive "le precondizioni delle tabelle non sono state rifiutate in QEMU" e **non** "il M1 funzionerà". Un esito **negativo** si scrive "Windows rifiuta questa variante di tabella in QEMU con questa causa" solo dopo che V1 e il controllo di E-2 sono verdi.

## 8. Esito atteso e variante decisiva

| Domanda | Variante | Esito più informativo |
|---|---|---|
| R-02 / Q2 | **V2** | Bugcheck chiaramente legato all'assenza di GICD → Windows pretende un GIC in MADT (impatto alto: ripiego "GIC fittizio" o piano B). Boot completo → il vincolo non c'è nelle tabelle |
| R-06 / Q3 | **V3** | Core avviati con la mailbox e senza PSCI → ADR-0003 regge sul lato Windows; resta aperto il wakeup via AIC |
| Q4 | **V4** | Caricamento della DLL da CSRT con il punto d'ingresso eseguito → unico esperimento che avvicina la domanda decisiva (ma non la chiude) |
| R-03 / Q5 | **V5(a)** | Windows non parte senza GTDT → il timer è architetturale obbligatorio, quindi o estensione HAL timer o piano B |

Il risultato **decisivo contro** la strada nativa sarebbe: V2 negativo netto *e* (V4 non caricabile con informazioni di A1 che confermano l'impossibilità). Il risultato **decisivo a favore** (solo come precondizione) sarebbe: V2 e V3 positivi, V4 carica la DLL e A1 conferma l'interfaccia di registrazione del controller; la conferma finale rimane l'hardware (Fase 3).

## 9. Fonti consultate in questo task

- QEMU `virt`: https://www.qemu.org/docs/master/system/arm/virt.html (gic-version, virtualization, acpi, msi, CPU, HVF).
- QEMU invocazione: https://www.qemu.org/docs/master/system/invocation.html (`-acpitable` solo i386; `-accel`).
- QEMU TPM: https://www.qemu.org/docs/master/specs/tpm.html (swtpm + `tpm-tis-device` su ARM virt).
- QEMU sorgente: `hw/arm/virt-acpi-build.c` (MADT/GICR, GTDT, DBG2, SPCR, IORT, flag PSCI nella FADT) su github.com/qemu/qemu.
- EDK2: `ArmVirtPkg/ArmVirtQemu.dsc` e `OvmfPkg/AcpiPlatformDxe/AcpiPlatformDxe.inf` su github.com/tianocore/edk2.
- Microsoft: https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-system-description-tables (MADT/GIC inbox, GTDT, CSRT/HAL extension e requisito del device ACPI, DBG2, FADT hardware-reduced); https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/bcdedit--dbgsettings ; https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/bcdedit--set ; https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/supported-ethernet-nics-for-network-kernel-debugging-in-windows-10 .
- Repository: `docs/PLAN.md`, ADR 0001/0003/0004/bozza Fase 0, INTERFACES, RISKS, `re/f0-m1-fiq-irq.md`, `tools/acpigen`.

## 10. Prossimi passi proposti (nessuno eseguito)

1. Attendere A1, A2, A4 e aggiornare le sezioni marcate; in particolare: versione GIC attesa da Windows, requisiti di FADT/parking, entry point e firma dell'estensione HAL.
2. Task per un esecutore: estendere `acpigen` (modello `qemu-virt`, opzioni, GICR, FADT, SSDT) con test.
3. Task per un esecutore (dopo H1/H4): `WatariQemuPkg` + driver `WatariAcpiOverride`.
4. Decidere con Marco: dove stanno le immagini (spazio) e quale strumento per la VM debugger.
5. Solo dopo conferma di backup: H3, H5, V0.
