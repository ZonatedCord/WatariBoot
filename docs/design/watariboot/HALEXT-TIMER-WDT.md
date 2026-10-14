# A11: estensione HAL timer sul watchdog del M1 (issue #19)

Data: 2026-10-13. Autore: esecutore Sonnet (task A11), da verificare da Opus. Solo letture con `gh api` e WebFetch. Nessun clone, nessun binario o simbolo Microsoft, nessuna riga di codice copiata.

Legenda: **[V: fonte]** = verificato sulla fonte; **[I]** = ipotesi.

Sigle (riferimenti letti):

| Sigla | Repo e riferimento |
|---|---|
| `IC` | ms-iot/imx-iotcore, ramo `public_preview`, HEAD `c73879479ed0`, cartella `hals/halext/` (licenza MIT) |
| `IE` | ms-iot/imx-edk2-platforms, ramo `imx`, HEAD `4b82cdd47cde` (tabelle ACPI del BSP) |
| `L` | AsahiLinux/linux, ramo `asahi`, HEAD `77cb8f24c238` |
| `M` | AsahiLinux/m1n1, `main`, HEAD `fbec52a81f2b` |
| `D` | AsahiLinux/docs, `main`, HEAD `0d1f8917fa88` |
| `U` | AsahiLinux/u-boot, `drivers/watchdog/apple_wdt.c` |

Le righe sono quelle di `cat -n`.

---

## 1. L'interfaccia HAL timer, come la usa il codice i.MX

L'header `nthalext.h` non è pubblico nei repo letti. Tutto ciò che segue è **ricavato dall'uso** nei sorgenti i.MX, quindi vale solo per ciò che il codice i.MX tocca.

### 1.1 Ingresso e registrazione

- L'estensione include `<nthalext.h>` e `"iMX7Timers.h"` **[V: IC:HalExtiMX7Timers/HalExtension.c:18-19]**.
- Punto d'ingresso: `NTSTATUS AddResourceGroup(ULONG Handle, PCSRT_RESOURCE_GROUP_HEADER ResourceGroup)` **[V: IC:MX7/HalExtension.c:230-234; MX6/HalExtension.c:869-873]**. Il commento dice che registra il timer con l'HAL.
- Enumerazione: `GetNextResourceDescriptor(Handle, ResourceGroup, Precedente, CSRT_RD_TYPE_TIMER, CSRT_RD_SUBTYPE_TIMER, CSRT_RD_UID_ANY)` in un ciclo, fino a NULL **[V: IC:MX7:270-282; MX6:903-913]**.
- Registrazione: `RegisterResourceDescriptor(Handle, ResourceGroup, &HeaderDelDescrittore, &NewTimer)` con `NewTimer` di tipo `TIMER_INITIALIZATION_BLOCK` **[V: IC:MX7:352-363; MX6:299-302, 614-617]**.
- Prima di registrare: `HalRegisterPermanentAddressUsage(PhysicalAddress, Size)** **[V: IC:MX7:294-299; MX6:261-265]**. Il mapping MMIO si fa con `HalMapIoSpace(PhysicalAddress, Size, MmNonCached)`: in MX6 durante la registrazione **[V: MX6:271-276]**, in MX7 dentro `Initialize` e solo se non ancora mappato **[V: MX7:416-427]**.
- Accesso ai registri: `READ_REGISTER_ULONG/WRITE_REGISTER_ULONG` in MX7 **[V: MX7:211,227]**; versioni `..._NOFENCE_...` in MX6 **[V: MX6:218,233]**.

### 1.2 `TIMER_INITIALIZATION_BLOCK`: campi osservati

Inizializzazione: `RtlZeroMemory(&NewTimer,...)` poi macro `INITIALIZE_TIMER_HEADER(&NewTimer)` **[V: MX7:315-316; MX6:282-283]**. Campi usati:

| Campo | Uso osservato | Fonte |
|---|---|---|
| `InternalData`, `InternalDataSize` | puntatore e dimensione di una struttura privata dell'estensione. L'HAL la **copia** e la ripassa come `TimerDataPtr` a ogni callback: il codice passa l'indirizzo di una variabile locale dello stack, quindi la copia è l'unica spiegazione possibile [I su "copia"; V sull'uso] | MX7:317-318; MX6:288-289 |
| `CounterBitWidth` | larghezza del contatore (32 in tutti i casi) | MX7:319; MX6:290,592 |
| `CounterFrequency` | Hz | MX7:321; MX6:291-292,593 |
| `MaxDivisor` | 1 in tutti i casi osservati | MX7:323; MX6:294,594 |
| `KnownType` | sempre `TimerUnknown` | MX7:320; MX6:296,611 |
| `Identifier` | uguale all'`Uid` del descrittore CSRT (solo MX6) | MX6:297,612 |
| `Capabilities` | maschera di bit, vedi 1.3 | MX7:324; MX6:295,603-606 |
| `Interrupt.Mode`, `Interrupt.Polarity`, `Interrupt.Gsi` | impostati solo se la capacità è `TIMER_GENERATES_LINE_BASED_INTERRUPTS`. Valori: `LevelSensitive`, `InterruptActiveHigh`, GSI preso dal CSRT | MX7:330-335; MX6:608-610 |
| `FunctionTable.Initialize / QueryCounter / ArmTimer / Stop / AcknowledgeInterrupt` | cinque callback, vedi 1.4. Non tutti obbligatori: i timer MX6 ne registrano sottoinsiemi | MX7:341-346; MX6:285-286, 585-588 |

### 1.3 Capacità (bit)

I valori numerici sono definiti nel CSRT del BSP i.MX7, con il commento "refer to nthalext.h" **[V: IE:Silicon/NXP/iMX7Pkg/AcpiTables/Csrt.aslc:40-63]**. Quelli che contano per noi:

| Bit | Nome | Valore |
|---|---|---|
| per-CPU | `TIMER_PER_PROCESSOR` | 0x1 |
| contatore leggibile | `TIMER_COUNTER_READABLE` | 0x2 |
| contatore scrivibile | `TIMER_COUNTER_WRITABLE` | 0x4 |
| con divisore | `TIMER_HAS_DIVISOR` | 0x8 |
| pseudo-periodico | `TIMER_PSEUDO_PERIODIC_CAPABLE` | 0x10 |
| one-shot | `TIMER_ONE_SHOT_CAPABLE` | 0x20 |
| periodico | `TIMER_PERIODIC_CAPABLE` | 0x40 |
| solo rollover | `TIMER_ROLLOVER_INTERRUPTS_ONLY` | 0x80 |
| interrupt di linea | `TIMER_GENERATES_LINE_BASED_INTERRUPTS` | 0x200 |
| MSI / interno | `TIMER_GENERATES_MSI_INTERRUPTS` / `..._INTERNAL_INTERRUPTS` | 0x400 / 0x800 |
| cambio di frequenza senza perdite | `TIMER_LOSSLESS_RATE_CHANGE` | 0x1000 |
| always on | `TIMER_ALWAYS_ON` | 0x8000 |
| dati interni per CPU | `TIMER_PER_PROCESSOR_INTERNAL_DATA` | 0x10000 |
| genera reset | `TIMER_GENERATES_RESET` | 0x20000 |
| reset: interrupt di pre-reset | `TIMER_GENERATES_PRE_RESET_LINE_BASED_INTERRUPTS` | 0x80000 |
| ausiliario | `TIMER_AUXILIARY` | 0x100000 |

Nota: i valori sono quelli ricopiati nel `.aslc` di Microsoft; non ho letto `nthalext.h`, quindi la corrispondenza con l'header reale è [I] (il commento nel file la afferma).

Capacità effettivamente **usate** dal codice:
- Timer di interrupt (EPIT i.MX6 e GPT i.MX7): `PERIODIC_CAPABLE | ONE_SHOT_CAPABLE | GENERATES_LINE_BASED_INTERRUPTS | ALWAYS_ON` **[V: MX6:603-606; IE:iMX7 Csrt.aslc:173-176]**.
- Contatore puro (GPT i.MX6): solo `TIMER_COUNTER_READABLE` **[V: MX6:295]**.
- `TIMER_PER_PROCESSOR`, `..._INTERNAL_DATA`, `GENERATES_RESET` e `PRE_RESET`: **definiti ma mai assegnati** a nessun timer i.MX **[V: grep dei due HalExtension.c e dei due Csrt.aslc]**. Quindi dal codice i.MX non si ricava come Windows tratti un timer per-CPU.
- Il commento del GPT i.MX7: "A 32 bit GPT up counter, which cannot generate interrupts and cannot be stopped" è nel blocco dei descrittori **[V: MX7:172-176]**, ma lo stesso GPT è registrato con interrupt di linea nel CSRT [V: Csrt.aslc:173-176]: il commento è vecchio, il CSRT prevale [I].
- Il commento del timer EPIT: "Timer is not always on, but Windows requires an always on timer, so we must give it one" **[V: MX6:596-601]**. Il codice dichiara `TIMER_ALWAYS_ON` anche per un timer che non lo è, e il commento dice che si toglie il flag solo se si fornisce un altro timer adatto. Significa che Windows esige almeno un timer marcato always-on tra quelli registrati [V come intento del commento; il meccanismo interno è I].

### 1.4 Callback (firme e comportamento)

Tipi: `PTIMER_INITIALIZE`, `PTIMER_QUERY_COUNTER`, `PTIMER_ACKNOWLEDGE_INTERRUPT`, `PTIMER_ARM_TIMER`, `PTIMER_STOP` **[V: MX7:95-99]**.

| Callback | Firma | Contratto osservato |
|---|---|---|
| Initialize | `NTSTATUS f(PVOID TimerDataPtr)` | mappa l'MMIO, azzera/spegne, configura clock e prescaler. Il commento dice che non si può usare una funzione di stall perché potrebbe dipendere da questo stesso timer **[V: MX7:373-553, 481-485]** |
| QueryCounter | `ULONGLONG f(PVOID)` | legge il contatore hardware **[V: MX7:556-587; MX6:503-532]** |
| ArmTimer | `NTSTATUS f(PVOID, TIMER_MODE Mode, ULONGLONG TickCount)` | "tra TickCount tick dal presente" genera l'interrupt; `Mode` è `TimerModeOneShot` o `TimerModePeriodic` ("pseudo-periodic or one-shot" nel commento). Ritorna `STATUS_INVALID_PARAMETER` se TickCount non entra nel registro **[V: MX7:651-761, 699-702; MX6:696-775]** |
| AcknowledgeInterrupt | `VOID f(PVOID)` | chiamata dopo l'interrupt: ripulisce lo stato; in one-shot ferma anche l'interrupt/contatore **[V: MX7:589-649; MX6:777-821]** |
| Stop | `VOID f(PVOID)` | ferma il timer e ripulisce **[V: MX7:763-813; MX6:823-867]** |

Dettagli utili al progetto:
- **Periodico**: i.MX6 EPIT usa il ricaricamento hardware (`RLD=1`, registro Load = TickCount) e la modalità la ricorda in `PeriodInTicks` **[V: MX6:664-670, 748-764]**; i.MX7 GPT usa la modalità "restart" (`FRR=0`) con compare1 = TickCount-1 **[V: MX7:728, 754-755]**. In entrambi la periodicità la fa l'hardware, non l'ISR.
- **Ordine di programmazione** (MX7): spegne il timer, spegne e ripulisce gli interrupt, scrive il compare, abilita l'interrupt, poi accende il timer con il contatore azzerato (`ENMOD=1`) **[V: MX7:704-758]**. Quindi `ArmTimer` rimisura da "adesso" (reset del contatore), non rispetto a un tempo assoluto.
- **Chi ferma il one-shot**: l'estensione, nella `AcknowledgeInterrupt` (MX7 spegne le abilitazioni, MX6 riscrive CR senza EN) **[V: MX7:617-629; MX6:809-812]**.
- L'ISR non è dell'estensione: l'estensione non registra un handler, dichiara solo il GSI. Nessuna funzione tipo `ConnectInterrupt` compare nel codice i.MX **[V: grep, assente]**. Chi chiama `AcknowledgeInterrupt` è l'HAL [I, dedotto dal nome e dalla mancanza di ISR].

### 1.5 Due ruoli: contatore e timer di interrupt

Nel CSRT i.MX6 il gruppo timer ha **due** descrittori con nomi espliciti: "Counter (GPT)" e "Clock Timer (EPIT1)" **[V: IE:iMX6Pkg/AcpiTables/Csrt.aslc:64-68, 183, 199]**.
- GPT: nessun interrupt (`Interrupt = 0`, commento "No interrupt on this counter"), solo `QueryCounter` **[V: Csrt.aslc:196; MX6:285-295]**.
- EPIT1: solo interrupt e arm/stop, nessun `QueryCounter` **[V: MX6:585-588, 603-606]**.
- Quindi per il caso i.MX6 la HAL ha un contatore (leggibile) e un timer a conto alla rovescia (programmabile), come entità distinte. Il caso i.MX7 ha invece un solo GPT registrato (senza `TIMER_COUNTER_READABLE`), più una GTDT per il timer generico ARM **[V: IE:iMX7Pkg/AcpiTables/Gtdt.aslc:17-39 (PL1 GSIV 29/30, virtuale 27, PL2 26)]**. Chi fornisce il contatore nel caso i.MX7 è con ogni probabilità il timer generico via GTDT [I].

### 1.6 Come l'estensione viene agganciata (CSRT, INF)

- Il gruppo risorse ha `VendorId`, `DeviceId` (i.MX6 0x0101, i.MX7 0x0112) **[V: IE:iMX6 Csrt.aslc:170-181; iMX7 Csrt.aslc:139-150]**; l'INF abbina `ACPI\NXP0112` e scrive `OSExtensionDatabase\VEN_NXPI&DEV_0112\HALExtension` = `%13%\HalExtiMX7Timers.dll` **[V: IC:MX7/HalExtiMX7Timers.inf:28-36]**; classe Computer, `PnPLockdown=1`, `ExcludeFromSelect=*` **[V: .inf:6-14]**.
- Il formato del descrittore dopo l'header è **libero** (lo decide l'autore dell'estensione): i.MX6 e i.MX7 hanno layout diversi, anche per il timer **[V: MX6:51-62 vs MX7:53-74]**; il CSRT i.MX7 dice "to maintain backward compatibility only new member here" **[V: IE:iMX7 Csrt.aslc:96]**. Coerente con la pagina Microsoft: "format and use ... defined by the HAL extension writer" **[V: learn.microsoft.com ... acpi-system-description-tables, sezione CSRT]**.
- Il GSI nel CSRT i.MX7 è `DEFINE_IMX7_INTERRUPT(55)` = 55 + `TOTAL_PRIVATE_INTERRUPT` (32) **[V: IE:iMX7Pkg/Include/iMX7.h:48-49,84]**, cioè **numero di SPI del GIC + 32 = ID GIC**. Quindi il GSI è un ID del GIC, e a consegnarlo è il GIC descritto nella MADT (la MADT i.MX7 ha GICC e GICD **[V: IE:iMX7 Madt.aslc:20-34]**). Questo è il punto del §3.2.

### 1.7 Cosa il codice i.MX NON dice (buchi dell'interfaccia)

- Nessun uso di `TIMER_PER_PROCESSOR`: la semantica per-CPU è sconosciuta dal codice.
- Nessun esempio di timer a **64 bit** o con `TickCount` oltre 32 bit (entrambi rifiutano/assumono 32 bit **[V: MX7:699-702; MX6:739-742]**). Il valore massimo accettato da `ArmTimer` dipende da `CounterBitWidth` e `MaxDivisor` [I].
- Nessun esempio su come l'HAL scelga fra timer GTDT e timer da estensione quando ci sono entrambi (i.MX7). [I: l'HAL preferisce quello marcato ALWAYS_ON/di capacità adatta.]
- Nessuna indicazione sul comportamento a sistema già avviato (cambio di frequenza, `TIMER_LOSSLESS_RATE_CHANGE`).
- Nessun esempio che usi `Interrupt.Mode = EdgeTriggered` o `ActiveLow`.

---

## 2. Il watchdog del M1 dalle fonti Asahi

### 2.1 Mappa dei registri (base `0x23d2b0000`, dimensione `0x4000`)

DT: `watchdog@23d2b0000`, `compatible = "apple,t8103-wdt", "apple,wdt"`, `reg = <0x2 0x3d2b0000 0x0 0x4000>`, `clocks = <&clkref>`, `interrupts = <AIC_IRQ 338 IRQ_TYPE_LEVEL_HIGH>` **[V: L:arch/arm64/boot/dts/apple/t8103.dtsi:1223-1229]**. `clkref` è un `fixed-clock` da 24 MHz **[V: t8103.dtsi:390-395]**.

Registri secondo il driver Linux **[V: L:drivers/watchdog/apple_wdt.c:37-52]**:

| Offset | Nome | Note |
|---|---|---|
| 0x00 | WD0_CUR_TIME | contatore, +1 per tick di clkref, scrivibile **[V: :27-28]** |
| 0x04 | WD0_BITE_TIME | compare di reset |
| 0x08 | WD0_BARK_TIME | compare di interrupt (**esiste solo per WD0**) |
| 0x0c | WD0_CTRL | |
| 0x10 / 0x14 / 0x1c | WD1_CUR / BITE / CTRL | quello che Linux usa davvero |
| 0x20 / 0x24 / 0x2c | WD2_CUR / BITE / CTRL | |

CTRL: bit 0 `IRQ_EN`, bit 1 `IRQ_STATUS` (scrivere 1 per ripulire), bit 2 `RESET_EN` **[V: :50-52, 32-35]**.

Larghezza: 32 bit. Il driver calcola `max_hw_heartbeat_ms = U32_MAX / clk_rate * 1000` **[V: :183]** (con 24 MHz sono 178 s, cioè 178 secondi interi). `D` conferma "24MHz 32-bit timer ... maximum timeout is just short of three minutes" **[V: D:docs/hw/soc/wdt.md:9,13]**. Periodo completo del contatore: 2^32 / 24e6 = 178,96 s [calcolo].

### 2.2 Interrupt: cosa è documentato

- Commento del driver: "WD0 additionally supports a configurable interrupt. This information can be used to implement pretimeout support at a later time." **[V: apple_wdt.c:24-25]**. Nel paragrafo successivo: con `IRQ_EN` e `CUR_TIME >= BARK_TIME` scatta l'interrupt e si setta `IRQ_STATUS`; si ripulisce scrivendo 1 in `IRQ_STATUS` **[V: :32-35]**. Il commento cita `WD1` per CUR/BARK per errore di battitura: il registro BARK esiste solo a 0x08 [V come layout; l'errore è I].
- **Il driver non usa mai l'interrupt**: la probe non chiede nessun IRQ e non scrive mai `IRQ_EN` o BARK **[V: :157-196, grep `irq`]**. Il DT lo dichiara comunque (e il binding lo richiede: `interrupts: maxItems: 1`, `required`) **[V: L:Documentation/devicetree/bindings/watchdog/apple,wdt.yaml:45-52]**.
- Nessun'altra fonte letta lo usa: la ricerca `wdt bark` nell'organizzazione AsahiLinux dà solo `hv_wdt.c`, un watchdog software dell'hypervisor (breadcrumbs), e il driver U-Boot **[V: gh search; M:src/hv/hv_wdt.c:51-60]**. Nel driver U-Boot l'offset 0x14 si chiama `BARK_TIME` ma è il compare di **reset** di WD1 (`start` scrive timeout lì e `RESET_EN` in 0x1c) **[V: U:12-14,31-45]**: i nomi bark/bite sono usati in modo incoerente fra le fonti, quindi **non ci si può fidare dei soli nomi**.
- Quindi: la modalità "WD0 + BARK + IRQ 338" è **documentata solo da un commento, mai esercitata** da nessun codice pubblico che ho trovato. Resta **[I]** finché non c'è una prova su hardware.

### 2.3 Reset: cosa NON deve succedere

- `RESET_EN` + `CUR >= BITE` ⇒ reset dell'intera macchina **[V: apple_wdt.c:29-31]**. Linux, con `RESET_EN` scritto in WD1_CTRL, vede il reset "dopo fino a ~120-125 ms" **[V: :130-135]**.
- Discrepanza sul tipo di reset: Linux dice "WD0 resets the machine to recovery mode... WD1 and WD2 trigger a normal machine reset" **[V: :22-24]**; `D` dice che i registri 0x0/0x4/0xc **e 0x20/0x24/0x2c** portano in recovery **[V: D:wdt.md:15]**. Su WD2 le due fonti **si contraddicono**. Per noi: qualsiasi `RESET_EN` su qualunque WDx è un rischio.
- Stato iniziale: il macho iniziale (m1n1) parte con il watchdog attivo, compare 120 s **[V: D:wdt.md:7,9]**. m1n1 lo disattiva scrivendo 0 in `base+0x1c` (WD1) e, per `wdt-version` 2 o 3, anche in un secondo registro `reg[2]` **[V: M:src/wdt.c:10,24-31,33-48]**. **Su WD0 e WD2 m1n1 non scrive nulla**: il loro stato iniziale è **sconosciuto** [I]. Su t8103 `wdt-version` non è letto da noi [I].
- `wdt_reboot` di m1n1 usa WD1: alarm 0x100000, count 0, ctl 4 **[V: M:src/wdt.c:51-59]**: conferma che WD1 = reset immediato.

### 2.4 Come si ottiene un interrupt periodico senza reset (ricetta teorica)

Tutto **[I]**, ricavato dalla semantica del commento:
1. `WD0_CTRL = 0` (RESET_EN=0, IRQ_EN=0), `IRQ_STATUS` ripulito scrivendo 2.
2. `WD0_BITE_TIME = 0xFFFFFFFF` (il massimo) per essere lontani dal reset anche se `RESET_EN` venisse attivato per sbaglio.
3. `WD0_CUR_TIME = 0`, `WD0_BARK_TIME = N`, `WD0_CTRL = IRQ_EN`.
4. All'interrupt: scrivere 2 (IRQ_STATUS) in CTRL; riarmare con `CUR_TIME = 0` (o `CUR = CUR - N`) e BARK = N.
Domande aperte (nessuna fonte): il bit `IRQ_STATUS` è un pending latch o segue il confronto (se `CUR >= BARK` resta vero, lo status può rialzarsi subito dopo la pulizia); lo stato di WD0_CTRL all'avvio; se il confronto è `>=` con wrap o se il contatore continua oltre BARK; se scrivere CTRL con bit di sola lettura ha effetti collaterali; se il contatore si ferma in stati di sleep. Nessuna di queste è chiusa dalle fonti.

---

## 3. Progetto: il watchdog come timer di un'estensione HAL

### 3.1 Mappatura sull'interfaccia (proposta)

Seguendo il precedente i.MX6 (un contatore leggibile + un timer di interrupt) **[V: §1.5]**, proposta di **due** registrazioni nello stesso resource group:

| Ruolo | Sorgente hardware | Capacità | Note |
|---|---|---|---|
| Contatore leggibile | `CNTPCT_EL0` (contatore fisico, 24 MHz) **oppure** `WD0_CUR_TIME` | `TIMER_COUNTER_READABLE` | `CNTPCT` è a 64 bit e non genera interrupt; WD0_CUR è a 32 bit, scrivibile da chiunque (anche da macOS/m1n1) e va in wrap dopo 179 s: **sconsigliato come contatore** [I]. Il contatore ARM va esposto con la GTDT (standard, sezione 3.5) o con una registrazione di estensione che legge `CNTPCT_EL0` [I; non ho un esempio pubblico di un'estensione che legga un registro di sistema] |
| Timer di interrupt | WD0 (CUR/BARK/CTRL) | `ONE_SHOT_CAPABLE | PERIODIC_CAPABLE | GENERATES_LINE_BASED_INTERRUPTS | ALWAYS_ON` | come EPIT i.MX6 **[V: MX6:603-606]** |

Parametri di registrazione del timer di interrupt: `CounterBitWidth = 32`, `CounterFrequency = 24000000`, `MaxDivisor = 1`, `KnownType = TimerUnknown`, `Interrupt.Mode = LevelSensitive`, `Polarity = InterruptActiveHigh` (coerenti con `IRQ_TYPE_LEVEL_HIGH` del DT **[V: t8103.dtsi:1228]**), `Gsi = <da decidere, §3.2>`.

Questa proposta è parallela a ciò che fa il BSP i.MX; il suo funzionamento sul M1 è [I].

### 3.2 Il GSI: chi lo consegna? (collegamento con A1)

Fatti:
- Il codice i.MX dichiara un GSI che è un **ID del GIC** e lascia la consegna all'HAL che già conosce il GIC dalla MADT **[V: §1.6]**. L'estensione non ha né un ISR né un ConnectInterrupt **[V: §1.4]**.
- L'IRQ 338 è un numero dell'**AIC** (`AIC_IRQ 338`), non del GIC **[V: t8103.dtsi:1228]**. Sul M1 non c'è GIC: serve un livello che consegni un interrupt AIC a Windows.
- La domanda se un'estensione HAL possa fornire un controller di interrupt non-GIC è l'**A1** ed è **non determinabile** dalle fonti pubbliche (confidenza bassa) **[V: re/f0-halext-public-docs.md §5]**. Nessuna API di registrazione di controller risulta nel codice i.MX **[V: grep]**.

Quindi tre scenari, con gli stessi ingredienti ma esito diverso:

| Scenario | Come arriva il GSI | Stato |
|---|---|---|
| S1: un'estensione/driver fornisce un controller AIC a Windows | l'HAL consegna il GSI 338 (mappato nel suo spazio GSI) all'estensione timer come per l'i.MX | dipende da A1: **non determinabile** |
| S2: il firmware/EDK2 (o un guscio minimo) emula una GICv2/v3 sopra l'AIC | il GSI è un ID GIC virtuale; l'HAL timer lo riceve come per l'i.MX. Ma una GIC emulata richiede intercettare gli accessi MMIO: serve EL2 (contrario al vincolo "niente hypervisor") [I] | piano B, decide l'utente |
| S3: nessun controller riconosciuto | l'estensione timer dichiara l'interrupt ma nessuno lo consegna: il timer non scatta mai, Windows si blocca al primo `ArmTimer` [I] | caso da escludere con A1/V4 |

**Dipendenza critica: senza una risposta positiva ad A1 (o un guscio S2), il watchdog non ha modo di arrivare a Windows come interrupt, a prescindere dalla correttezza del progetto del timer.** Oltre a ciò: il campo `Interrupt.Gsi` è un `ULONG` che l'HAL interpreta nello spazio GSI del controller registrato [I].

### 3.3 Un solo timer per tutto il SoC

Cosa si sa:
- Il WDT è **un unico blocco MMIO** a livello di SoC con un solo IRQ AIC (338) **[V: DT]**. L'AIC può fissare il core destinatario di un hwirq con `TARGET_CPU` (affinità), dato già verificato in `re/f0-m1-fiq-irq.md` §3.3 (`aic_irq_set_affinity`, riferito ivi).
- Nel BSP i.MX6 con 4 core (HummingBoard i.MX6Quad, SABRE i.MX6Q elencati nel README **[V: IC:README, tabella piattaforme]**) la CSRT registra **un solo** timer di interrupt (EPIT1) **senza** `TIMER_PER_PROCESSOR` **[V: IE:iMX6 Csrt.aslc:199-213; MX6:603-606]**, e il tree di `IE` non ha `Gtdt.aslc` per iMX6 (solo per iMX7) **[V: elenco dell'albero di imx-edk2-platforms]**: l'i.MX6 è Cortex-A9, senza timer generico ARM [I]. Quindi, per l'i.MX6 SMP, il timer di sistema era **uno solo e globale**. Il fatto che Windows IoT girasse davvero su più core con questa configurazione è [I]: il README elenca i.MX6 Quad come supportato ma non dice a quanti core.
- Come Windows ripartisca il tick tra i core con un solo timer globale: **non documentato nelle fonti lette**. [I: l'HAL usa il timer registrato sul BSP e invia IPI agli altri core quando serve; ciò richiede IPI funzionanti, vedi A4 e `re/f0-m1-fiq-irq.md` §4.]

Conseguenze per il M1 [I]:
1. La distribuzione ai core avviene con IPI AIC (legacy IPI su t8103, vedi `re/f0-m1-fiq-irq.md` §4) per quanto riguarda l'HAL, non per l'estensione.
2. Se Windows esige un timer **per processore** (`TIMER_PER_PROCESSOR`) per i tick locali o per il timer-wheel di scadenze precise, un solo WD0 non basta. Non c'è una fonte che lo dica o lo escluda.
3. L'affinità dell'IRQ 338: assegnarla a un solo core fisso (il boot CPU) e lasciare il resto alle IPI.

### 3.4 One-shot, periodico e giro a 32 bit

**ArmTimer(Mode, TickCount)** da implementare su WD0 [I]:
- `TickCount` ≤ 0xFFFFFFFF, altrimenti `STATUS_INVALID_PARAMETER` come i.MX **[V: MX7:699-702]**. Con 24 MHz il massimo è 178 s: il registro basta per ogni attesa del tick di Windows; per attese più lunghe l'HAL può riarmare [I].
- **One-shot**: `RESET_EN=0`, scrivere `CUR_TIME = 0`, `BARK_TIME = TickCount`, `CTRL = IRQ_EN`. Il confronto è `CUR >= BARK`, quindi il valore minimo accettabile è > 0 (con BARK=0 scatta subito).
- **Periodico**: non c'è alcun ricaricamento automatico. Ci sono due strade:
  - (a) "pseudo-periodico" in `AcknowledgeInterrupt`: ripulire `IRQ_STATUS`, poi `CUR_TIME = 0` (o sottrarre il periodo per non perdere tick) e lasciare BARK invariato. Il precedente i.MX6 EPIT ricarica da hardware **[V: MX6:664-670]**; qui la ricarica è manuale, e la pseudo-periodicità deriva dalla latenza ISR (deriva cumulata);
  - (b) dichiarare **solo** `ONE_SHOT_CAPABLE` e (come suggerisce il bit `TIMER_PSEUDO_PERIODIC_CAPABLE` **[V: IE:iMX7 Csrt.aslc:47]**) lasciare che la HAL riarmi: la presenza di quel bit suggerisce che l'HAL sa emulare un periodico con riarmi [I].
  - Raccomandazione: dichiarare `PSEUDO_PERIODIC` o one-shot, **non** `PERIODIC_CAPABLE`, per non prometterne la precisione. [I]
- **Giro a 32 bit**:
  - azzerando `CUR_TIME` a ogni `ArmTimer` il contatore non arriva mai a fare il giro (max 178 s);
  - la scrittura di `CUR_TIME` e il momento reale non sono atomici: tra la lettura/azzeramento e l'armamento passano i tick di un paio di accessi MMIO (non quantificato) **[I]**; la deriva del tempo di sistema derivata da questo timer va evitata: la base dei tempi (QPC / interrupt time) dovrebbe venire da un contatore a 64 bit che non si azzera mai, cioè `CNTPCT_EL0` o la sua copia in AIC (da verificare, nota in `re/f0-m1-fiq-irq.md` §2) **[I]**;
  - alternativa senza azzeramento: BARK = CUR_corrente + N (somma modulo 2^32). Ma il confronto `>=` **non modulare** fa scattare subito se la somma supera 2^32 **[V: confronto `>=` nel commento; il comportamento al wrap è I]**; quindi: se CUR + N non entra nei 32 bit, bisogna azzerare CUR o armare in due passi.
- **AcknowledgeInterrupt**: scrivere `IRQ_STATUS` (bit 1) per ripulire; in one-shot anche `IRQ_EN=0`, come fa l'estensione i.MX7 **[V: MX7:617-629]**.
- **Stop**: `CTRL = 0` e pulizia di `IRQ_STATUS`.
- **Initialize**: mappare l'MMIO (`HalMapIoSpace`, `MmNonCached`), spegnere WD0 come sopra, e **non toccare WD1/WD2** (reset). Imporre `RESET_EN=0` e BITE al massimo. Non usare funzioni di stall (stessa avvertenza del codice i.MX **[V: MX7:481-485]**).

### 3.5 Risoluzione e precisione

- Risoluzione: 1 tick = 41,67 ns (24 MHz) **[V: DT + calcolo]**. Il valore dichiarato nel campo `CounterFrequency` è 24.000.000. Windows dichiara tempi di sistema a 100 ns: la risoluzione hardware è molto più fine del tick di sistema (15,6 ms o 1 ms tipici, [I]).
- Precisione assoluta: il clock di riferimento è lo stesso oscillatore a 24 MHz del contatore ARM (nel DT è lo stesso `clkref` che alimenta anche i nodi del timer generico? **non verificato**: nel DT t8103 il nodo `timer` non ha `clock-frequency`; `CNTFRQ` = 24 MHz è riportato in `D`/m1n1 [V: re/f0-timer-irq-hw.md §5]). Che WD0 e `CNTPCT` abbiano la stessa origine di clock è [I].
- Deriva e latenza: la latenza dell'accesso MMIO al WDT non è quantificata in nessuna fonte [I]; l'errore per ogni armamento è almeno di qualche accesso MMIO + latenza interrupt AIC + il giro IPI (se il tick deve arrivare a un altro core).
- Jitter dovuto alla latenza ISR: se la ricarica è manuale (periodico), l'errore si accumula; per questo la base dei tempi deve essere il contatore a 64 bit, non il WD.
- Il blocco WD è in un dominio che sopravvive agli stati di sleep? Sconosciuto [I]. Se il contatore si ferma in idle, `ALWAYS_ON` sarebbe una promessa falsa.

---

## 4. Rischi, prerequisiti, verifiche

### 4.1 Rischi

| # | Rischio | Gravità | Stato |
|---|---|---|---|
| R1 | Nessun controller di interrupt riconosciuto da Windows che consegni l'IRQ AIC 338 (A1) | **critico** | non determinabile |
| R2 | L'interrupt BARK di WD0 non funziona come descritto: mai collaudato in nessun codice pubblico | **critico** | solo un commento **[V]** |
| R3 | `RESET_EN`/BITE su WD0 (recovery) o WD1/WD2 (reset): un bug nell'estensione riavvia il Mac, WD0 e a volte WD2 in recovery | alto | **[V]** documentato; stato iniziale di WD0/WD2 sconosciuto |
| R4 | Il watchdog non è nostro: macOS/m1n1/iBoot lo usano; se resta attivo e non servito il Mac riavvia dopo 120 s (WD1). L'estensione dovrebbe prima assicurarsi che WD1 sia spento (lo fa m1n1) | alto | **[V]** |
| R5 | Un solo timer per tutto il SoC (nessuna evidenza su `TIMER_PER_PROCESSOR`) | medio-alto | **[I]** |
| R6 | La HAL esige un timer `ALWAYS_ON` e un contatore: se WD0 non è davvero always-on (sleep, idle), la dichiarazione è falsa | medio | **[V]** (esigenza); stato hardware **[I]** |
| R7 | Il wrap e il confronto `>=` non modulare complicano il periodico | medio | progettazione |
| R8 | Firma dell'estensione HAL (EKU speciale) e caricamento con test-signing: non determinabili **[V: re/f0-halext-public-docs.md §1]** | alto | V4 |
| R9 | AICv2 (M1 Pro/Max e successivi): l'hwirq del WDT e la struttura del blocco cambiano; "tutti gli Apple Silicon" non è garantito. I compatibili per i SoC più nuovi puntano tutti a `apple,t8103-wdt` come fallback **[V: apple,wdt.yaml:16-25]**, ma l'IRQ e la mappa AIC vanno riletti per ogni SoC | medio | **[I]** |
| R10 | Mancanza di prove sul fatto che Windows accetti un timer di sistema che arrivi tramite IPI/AIC su un solo core | medio | **[I]** |

### 4.2 Prerequisiti (in ordine)

1. **A1/V4**: Windows carica un'estensione agganciata a un gruppo CSRT nostro e riconosce un controller di interrupt non-GIC (o un guscio S2 deciso dall'utente).
2. **HW**: prova su M1 dell'interrupt di WD0 (§4.3, passo H1), con backup confermato.
3. Un controller AIC funzionante per Windows (A4), anche solo per un IRQ.
4. Una GTDT o un'estensione che fornisca il contatore a 64 bit e il tempo di sistema, senza interrupt del timer generico (FIQ).
5. Decisione di progetto su come impedire che Windows usi il timer generico ARM (che arriva in FIQ): non pubblicare la GTDT, o pubblicarla con timer non usabili. Se la GTDT è assente, Windows ARM64 potrebbe rifiutarsi di avviare, o usare comunque il generico [I: nessuna fonte letta]. Per l'i.MX7 coesistono GTDT e GPT da estensione **[V: §1.5]** e l'HAL scelse uno dei due: quale e in base a quale regola è sconosciuto.

### 4.3 Cosa si può verificare in QEMU (modello AIC di A8) e cosa no

In QEMU (con un modello AIC + un blocco WDT modellato da noi), **dopo A8** [I]:
- Il caricamento dell'estensione (INF, `OSExtensionDatabase`, CSRT con ID nostro): sì, è una questione di Windows, non dell'hardware.
- La **struttura dell'interfaccia**: se `RegisterResourceDescriptor` accetta il nostro blocco, quali campi vengono letti, se `ArmTimer`/`AcknowledgeInterrupt` sono chiamate con i parametri attesi, la modalità one-shot/periodica richiesta, le dimensioni di `TickCount` effettive. Con un WDT modellato da noi si può contare quante volte e con quali valori Windows chiama `ArmTimer` in un tipico avvio.
- Se Windows va avanti senza GIC con il solo AIC modellato (parte di V2/V4), e se un timer da estensione sostituisce quello della GTDT.
- Il flusso GSI → controller → `AcknowledgeInterrupt`.
- Il comportamento del tempo di sistema quando c'è un solo timer globale con IPI emulate.

Solo su hardware:
- Se WD0 genera davvero l'IRQ 338 con `IRQ_EN`/BARK e come si comporta `IRQ_STATUS` (latch vs livello), il wrap, lo stato iniziale di WD0/WD2, l'effetto di `RESET_EN` su WD0.
- La latenza MMIO del WDT, la precisione, il jitter del tick.
- Il blocco `WD0` in idle (si ferma o no).
- Se il clock del WDT e `CNTPCT` hanno la stessa origine.
- L'affinità AIC dell'IRQ 338 e la consegna a un core scelto.
- Lo stato lasciato da iBoot/m1n1/macOS.
Un modello QEMU del WDT scritto da noi riflette le nostre **ipotesi**, quindi QEMU non può validare i punti ipotetici di §2.4: serve H1.

**H1 (proposta di test su hardware, solo lettura/scrittura di registri, nessuna scrittura su disco)**, da eseguire con m1n1 in proxy mode e **solo dopo il backup Time Machine confermato dall'utente** e la conferma esplicita prima di toccare il boot (vedi protocollo in `re/f0-timer-irq-hw.md` §5): (1) leggere `WD0_CTRL` e `CUR` per capire se WD0 conta già; (2) portare `RESET_EN=0`, BITE=0xFFFFFFFF; (3) BARK a pochi ms, `IRQ_EN=1`; (4) osservare l'hwirq AIC 338 (stato HW_STATE) e l'`IRQ_STATUS`; (5) ripristinare i valori letti al punto 1. Il rischio è quello di R3.

---

## 5. Conclusione

**Fattibilità: possibile sulla carta, non dimostrabile oggi.** L'interfaccia HAL timer è abbastanza semplice (cinque callback, una struttura di registrazione con frequenza, larghezza, capacità e GSI) e il watchdog ha un contatore a 24 MHz, un registro di confronto e un IRQ di livello: la mappatura è naturale. Ma il progetto poggia su due ipotesi non verificate.

**Dipendenze critiche:**
1. **A1: chi consegna il GSI.** L'estensione timer dichiara un GSI e lascia la consegna al controller già noto all'HAL (il GIC nel caso i.MX). Sul M1 l'IRQ 338 è dell'AIC e Windows non conosce l'AIC: senza una soluzione per il controller (estensione, firmware o guscio S2), l'interrupt non arriva.
2. **Il bark di WD0 è solo un commento.** Nessun codice pubblico lo usa. Va provato su hardware (H1), con il rischio di recovery mode.
3. **Un solo timer globale:** il precedente i.MX6 (4 core, un solo timer senza `TIMER_PER_PROCESSOR`) indica che si può, ma serve IPI funzionante (A4), e che Windows lo accetti su M1 è un'ipotesi.
4. Il contatore e il tempo di sistema non devono dipendere da WD0 (32 bit, 179 s): servono `CNTPCT` o equivalente, con la questione GTDT/FIQ da risolvere.

### Fuori ambito ma utile (da valutare)

`Wack0/NtHalExts` (GitHub) contiene estensioni HAL per Windows 8.x ARMv7: `halextgit` (timer generico ARM, legge GTDT) e `halextbcm2709` (**controller di interrupt** BCM2709, "backported from rs1-era HAL"). Il README dice che le estensioni per controller di interrupt "were added in 789x but removed again in 799x" e richiedono pattern matching su funzioni non esportate **[V: README di Wack0/NtHalExts]**. È quindi rilevante per A1/A4 (indica che l'API per controller di interrupt è esistita, ma non è stabile nelle build recenti), ma il repo include header e librerie del DDK Microsoft di Windows 8 ("reproduced here"): **non ho usato né copiato nulla** e non va incluso nel nostro repository. Non è stato letto oltre al README.

## Fonti

1. `ms-iot/imx-iotcore` ramo `public_preview`: `hals/halext/HalExtiMX6Timers/{HalExtension.c,iMX6Timers.h}`, `hals/halext/HalExtiMX7Timers/{HalExtension.c,iMX7Timers.h,HalExtiMX7Timers.inf}`.
2. `ms-iot/imx-edk2-platforms` ramo `imx`: `Silicon/NXP/iMX6Pkg/AcpiTables/Csrt.aslc`, `Silicon/NXP/iMX7Pkg/AcpiTables/{Csrt.aslc,Gtdt.aslc,Madt.aslc}`, `Silicon/NXP/iMX7Pkg/Include/iMX7.h`.
3. `AsahiLinux/linux` ramo `asahi`: `drivers/watchdog/apple_wdt.c` (commit iniziale `4ed224aeaf`, 5 gennaio 2022, Sven Peter), `arch/arm64/boot/dts/apple/t8103.dtsi`, `Documentation/devicetree/bindings/watchdog/apple,wdt.yaml`.
4. `AsahiLinux/m1n1`: `src/wdt.c`, `src/hv/hv_wdt.c`. `AsahiLinux/docs`: `docs/hw/soc/wdt.md`. `AsahiLinux/u-boot`: `drivers/watchdog/apple_wdt.c`.
5. https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-system-description-tables (GTDT, CSRT, HAL extension).
6. `Wack0/NtHalExts` (solo README).
