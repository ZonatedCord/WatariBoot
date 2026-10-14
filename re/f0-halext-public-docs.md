# A1: estensioni HAL di Windows nella documentazione pubblica (issue #4)

Data: 2026-10-13. Autore: esecutore Sonnet (task A1), da verificare da Opus. Solo fonti pubbliche; nessun binario o simbolo Microsoft letto, nessun download su disco.
Legenda: `[verificato: fonte]` = letto sulla fonte; `[ipotesi]` = non verificato.

## 1. Cosa sono, come si caricano

- Definizione ufficiale: le estensioni HAL sono moduli specifici di SoC, DLL, che adattano l'HAL di Windows a un'interfaccia hardware di una classe di "Core System Resource" (CSR). CSR = "funzioni hardware condivise come controller di interrupt, timer e controller DMA" [verificato: https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-system-description-tables, sezione CSRT].
- Nel testo Microsoft, per interrupt controller e timer "standard" Windows ha supporto integrato basato su MADT e GTDT. Le estensioni HAL servono per i dispositivi non standard; il testo cita esplicitamente solo il DMA come caso in attesa di standard [stessa fonte].
- Caricamento: la tabella ACPI "CSRT" (nell'RSDT/XSDT, solo se la piattaforma usa CSR non standard) descrive "resource group". "Windows usa l'identificatore del resource group per trovare e caricare l'estensione HAL" [stessa fonte]. I descrittori di risorsa dentro il gruppo hanno formato e uso "definiti dall'autore dell'estensione".
- Ogni resource group deve avere anche un device nel namespace ACPI (DSDT); gli ID nell'header del gruppo devono coincidere con quelli del nodo; questo permette il servicing via Windows Update [stessa fonte].
- Associazione ID -> DLL nel registro: l'INF dell'i.MX6 scrive `HKLM,"SYSTEM\CurrentControlSet\Control\OSExtensionDatabase\VEN_NXPI&DEV_0101","HALExtension",,"%13%\HalExtiMX6Timers.dll"` e abbina il device `ACPI\NXP0101` (classe Computer, `ExcludeFromSelect=*`, `PnPLockdown=1`). La DLL va nel Driver Store (dirid 13), non in System32 [verificato: https://github.com/ms-iot/imx-iotcore/blob/public_preview/hals/halext/HalExtiMX6Timers/HalExtiMX6Timers.inf].
- Codice dell'estensione: include `<nthalext.h>`; punto d'ingresso `AddResourceGroup(Handle, PCSRT_RESOURCE_GROUP_HEADER)`; enumera i descrittori con `GetNextResourceDescriptor(Handle, Group, Prev, Type, Subtype, Uid)` e li registra con `RegisterResourceDescriptor(Handle, Group, &Desc->Header, &NewTimer)`. Usa anche `HalMapIoSpace` e `HalRegisterPermanentAddressUsage` [verificato: https://github.com/ms-iot/imx-iotcore/blob/public_preview/hals/halext/HalExtiMX6Timers/HalExtension.c righe ~237-310, 535-625, 868-947].
- Firma: la pagina archiviata dell'HCK 8.1 (Windows RT) dice che l'estensione "richiede un certificato di firma con EKU speciale", che il CSRT deve avere un descrittore valido e che il registro `OSExtensionDatabase` deve avere la voce [verificato: https://learn.microsoft.com/en-us/previous-versions/windows/hardware/hck/hh997835(v=vs.85)]. Requisiti collegati citati: `Device.DevFund.HALExtension.HALSignatureAttributes`, `System.Fundamentals.HAL.IfCSRTPresent`.
- Funziona con test-signing? Non determinabile dalle fonti pubbliche lette: nessuna documentazione dice se la verifica dell'EKU viene ignorata con test-signing o con policy di debug. [ipotesi: il controllo è di Code Integrity del kernel/loader HAL e il test-signing potrebbe bastare, ma l'EKU "speciale" potrebbe essere richiesto comunque]. Da verificare in V4.

## 2. Tipi di risorsa CSRT

- Spec CSRT (Microsoft, 2011/2017) e implementazioni open source: tipo 0x0001 = Interrupt con sottotipi 0x0000 = Interrupt Line e **0x0001 = Interrupt Controller**; tipo 0x0002 = Timer (sottotipo 0x0000); tipo 0x0003 = DMA (sottotipi 0x0000 canale, 0x0001 controller) [verificato: ACPICA `source/include/actbl2.h` righe 832-842 `ACPI_CSRT_TYPE_INTERRUPT/TIMER/DMA`, `ACPI_CSRT_XRUPT_CONTROLLER`; test FWTS https://github.com/ColinIanKing/fwts/blob/master/src/acpi/csrt/csrt.c righe 146-178]. Il PDF della spec (uefi.org CSRT v2.pdf, e intel cdrdv2 772760) non è leggibile con gli strumenti a disposizione (403 / PDF compresso): il testo originale resta da leggere a mano [ipotesi su campi non letti].
- Quindi **sulla carta il tipo "Interrupt Controller" esiste** nella specifica CSRT.
- Prove pubbliche di un'estensione HAL che fornisce un controller di interrupt: **nessuna trovata**. Estensioni HAL pubbliche trovate (tutte in ms-iot/imx-iotcore, ramo `public_preview`, cartella `hals/halext/`, licenza MIT):
  - `HalExtiMX6Timers` (GPT, EPIT; INF per `ACPI\NXP0101`)
  - `HalExtiMX7Timers`
  - `HalExtiMXDma` (SDMA, con header per i.MX6Q/6SX/6UL/8M)
  [verificato: elenco albero del repo con `gh api repos/ms-iot/imx-iotcore/git/trees/public_preview?recursive=1`]
- Nessuna estensione HAL trovata in ms-iot/rpi-iotcore, raspberrypi/windows-drivers (solo driver PWM/SDHC/LAN, nessun halext/CSRT) e nell'albero di microsoft/Windows-driver-samples (nessun file con `halext` o `csrt` nel nome) [verificato: grep sugli alberi dei repo]. Il nome ipotizzato `HalExtPL080`/`HalExtBcm*` non risulta pubblico [verificato per assenza nei repo citati; non esclude che esistano in altri repo].
- Nessun file ASL/CSRT pubblico trovato in imx-iotcore (grep su `csrt` e `.asl` sull'albero: nulla); le tabelle ACPI stanno in repo di firmware separati, non esaminati [ipotesi].

## 3. Estensioni HAL per timer

- Esistono e sono pubbliche: l'API è dichiarata in `nthalext.h` (non letto direttamente; il WDK non è nel repo Windows-driver-samples, quindi l'header si conosce solo dall'uso nei sorgenti iMX) [verificato indirettamente: HalExtension.c sopra].
- Interfaccia osservata dall'uso, struttura di registrazione del timer: `INITIALIZE_TIMER_HEADER`, `FunctionTable` con `Initialize`, `QueryCounter`, `AcknowledgeInterrupt`, `ArmTimer`, `Stop`; campi `CounterBitWidth`, `CounterFrequency`, `MaxDivisor`, `Capabilities` (`TIMER_PERIODIC_CAPABLE`, `TIMER_ONE_SHOT_CAPABLE`, `TIMER_GENERATES_LINE_BASED_INTERRUPTS`, `TIMER_ALWAYS_ON`, `TIMER_COUNTER_READABLE`), `KnownType`, `Identifier`, e **`Interrupt.Mode`, `Interrupt.Polarity`, `Interrupt.Gsi`** [verificato: HalExtension.c righe 280-300, 570-615].
- Punto cruciale: l'estensione timer dichiara l'interrupt come **GSI** (numero di interrupt globale) con modo e polarità. Il timer non gestisce l'interrupt da solo: il GSI viene instradato dal controller di interrupt già noto a Windows. Nell'i.MX6 è un interrupt di linea (level, active high) del GIC [verificato: righe 608-610]. Un commento nel sorgente dice anche che "Windows richiede un timer always-on" [verificato: righe ~595-600].
- Conseguenza per il M1 [ipotesi]: un'estensione timer potrebbe sostituire il timer ARM generico (che sul M1 arriva come FIQ, vedi [re/f0-m1-fiq-irq.md](f0-m1-fiq-irq.md)) con un timer che emette un interrupt di linea, ma il GSI andrebbe comunque consegnato da un controller riconosciuto (GIC) o da un'estensione che faccia da controller.

## 4. Windows ARM64 e GIC

- Fonte ufficiale letta: "Per le piattaforme SoC Arm, ACPI 5.0 aggiunge descrittori per GIC e GIC Distributor. Windows include supporto inbox per GIC e GIC Distributor... il GIC corrispondente al processore di boot deve essere la prima voce" [verificato: pagina ACPI System Description Tables, sezione MADT]. Questo documenta il supporto, **non** dice testualmente che altri controller siano vietati.
- Una dichiarazione ufficiale che Windows ARM64 *richieda* GIC/GICv3 (requisiti Windows on Arm, SBSA/BSA): **non trovata** nelle ricerche fatte [non determinabile dalle fonti pubbliche lette]. SBSA/BSA di Arm mettono il GIC fra i componenti obbligatori (per SBSA; la revisione 5.0 vieta controller non standard) [verificato per il contenuto generico: risultato di ricerca su Linaro SystemReady Client https://linaro.atlassian.net/wiki/spaces/CLIENTPC/pages/28737798189/Arm+SystemReady+for+Client+Devices e note SBSA su documentation-service.arm.com]; ma BSA/SBSA vincolano l'hardware, non provano il comportamento di Windows. Riferimenti precisi a clausole non letti [ipotesi].
- Controesempio indiretto: Raspberry Pi 2/3 (BCM2836, senza GIC) avevano un BSP Windows IoT [verificato: https://github.com/ms-iot/rpi-iotcore/blob/master/README.md]. Come Windows gestisse l'interrupt controller del Pi nel firmware (tabelle ACPI) non è documentato nelle fonti lette; è il tema del task A4, e **non** si può concludere che usasse una estensione HAL [ipotesi].

## 5. Verdetto preliminare

**Un'estensione HAL può fornire un controller di interrupt non-GIC? Non determinabile dalle fonti pubbliche.**
Confidenza: bassa (circa 30% sì, 20% no, 50% incertezza).

A favore (leggermente):
- La specifica CSRT definisce il sottotipo "Interrupt Controller" [verificato: ACPICA/FWTS].
- Microsoft elenca gli interrupt controller tra le CSR e dice che le estensioni servono ai dispositivi non standard [verificato: pagina Microsoft CSRT].

Contro / non provato:
- La stessa pagina Microsoft cita come caso reale solo il DMA; le uniche estensioni pubbliche sono timer e DMA (imx-iotcore), nessuna di interrupt controller [verificato per assenza nei repo esaminati].
- L'API timer osservata consegna il GSI a un controller che Windows già conosce (il GIC); non c'è un esempio pubblico dell'API per registrare un controller [verificato: HalExtension.c]. Non ho visto in `nthalext.h` alcuna funzione di registrazione di un controller (non ho letto l'header) [ipotesi che non esista nella versione pubblica].
- Firma con EKU speciale: bloccante o no con test-signing è sconosciuto.

Da verificare sperimentalmente (variante V4 in `docs/design/watariboot/EXPERIMENT-F0.md`):
1. Se Windows carica un'estensione agganciata a un gruppo CSRT nostro (ID, voce `OSExtensionDatabase`, device ACPI abbinato) con firma di test, e con quali messaggi di errore se rifiutata.
2. Se un resource group con tipo 0x0001/sottotipo 0x0001 viene preso in carico da un'estensione e quale interfaccia di registrazione viene richiesta.
3. Se Windows ARM64 va avanti senza la GICD nella MADT quando c'è una estensione (collegato a V2).
4. Se un timer registrato da estensione sostituisce quello GTDT, e se l'interrupt "line-based" via GSI funziona senza GIC.

## Fonti principali

1. https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-system-description-tables (CSRT, HAL extension, supporto GIC inbox)
2. https://github.com/ms-iot/imx-iotcore/tree/public_preview/hals/halext (codice e INF di estensioni HAL timer e DMA)
3. https://learn.microsoft.com/en-us/previous-versions/windows/hardware/hck/hh997835(v=vs.85) (firma con EKU speciale, OSExtensionDatabase)
4. ACPICA `actbl2.h` e FWTS `csrt.c` (tipi e sottotipi CSRT, incluso Interrupt Controller)
