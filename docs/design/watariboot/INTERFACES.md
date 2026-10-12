# INTERFACES: contratti fra componenti

- Progetto: Watari Boot · Data: 2026-10-12 · Versione: 0.1 · Stato: draft
- Documenti collegati: [PRD](PRD.md) · [ARCHITECTURE](ARCHITECTURE.md) · [TEST_STRATEGY](TEST_STRATEGY.md) · [RISKS](RISKS.md)

## Legenda
- `[verificato: fonte]`: riscontrato su fonte citata.
- `[ipotesi]`: non verificato. Le sezioni marcate **IPOTESI (dipende da Fase 0)** non vanno implementate in modo definitivo finche' lo spike di Fase 0 non e' concluso. Il codice scritto prima va isolato dietro un'interfaccia sostituibile.
- `[proposta]`: scelta di design nostra (non un fatto esterno); modificabile con ADR.

---
## 1. Handoff m1n1 -> EDK2  [proposta; formato non esistente upstream, da definire]
Stato: nessuna fonte verificata su un meccanismo di handoff m1n1 -> EDK2 esistente. Il contratto sotto e' una proposta. [ipotesi] m1n1 puo' caricare un payload e passare un puntatore all'ADT (comportamento tipico di m1n1 con payload Linux/boot args Apple; da verificare in `src/` di m1n1).

| Elemento | Contratto proposto |
|---|---|
| Entry | EDK2 ricevuto in RAM a indirizzo fisico fisso, esecuzione a EL2 o EL1 (da decidere: [ipotesi] il M1 parte a EL2; verificare), MMU off o identity-mapped, interrupt mascherati (IRQ e FIQ) |
| Registro x0 | puntatore fisico a `PI_HANDOFF` (struttura sotto) |
| ADT | blob ADT in memoria, immutato rispetto a quello di iBoot, non rilocato dopo l'handoff |
| Framebuffer | base fisica, larghezza, altezza, stride, formato pixel, dimensione |
| Mappa memoria | elenco di regioni (base, size, tipo: usabile/riservata firmware/MMIO) calcolato da m1n1 |
| UART/debug | base MMIO UART di debug + velocita' |
| Cosa m1n1 deve avere fatto | PMGR dei blocchi richiesti, clock, framebuffer, PHY/DWC3 USB in host (da piano; [ipotesi] la fattibilita' di ciascun punto dipende dal supporto m1n1 per t8103) |
| Cosa EDK2 non deve piu' chiedere a m1n1 | nessuna chiamata di ritorno dopo l'handoff |

```c
/* proposta, versione 1 */
typedef struct {
  UINT32 Magic;        /* 'PIHF' */
  UINT32 Version;      /* 1 */
  UINT64 AdtBase;  UINT64 AdtSize;
  UINT64 FbBase;   UINT32 FbWidth, FbHeight, FbStride, FbPixelFormat;
  UINT64 MemMapBase; UINT32 MemMapCount, MemMapEntrySize;
  UINT64 UartBase; UINT32 UartBaud;
  UINT64 Reserved[8];
} PI_HANDOFF;
```
Errori: Magic/Version errati -> EDK2 si ferma con messaggio su framebuffer/UART. Compatibilita': campi aggiunti solo in `Reserved`; cambio di significato = incremento di Version.

## 2. Modello dati ADT -> tabelle  [proposta]
`M-ADT-PARSE` espone un modello neutro: lista CPU (id, cluster, classe P/E, MPIDR [ipotesi: fornito dall'ADT]), blocchi MMIO, interrupt (numero AIC), clock/PMGR. M-ACPI-GEN consuma solo questo modello. Contratto: nessun dato letto due volte dall'ADT in moduli diversi.

## 3. Tabelle ACPI

Regola generale: i layout binari delle tabelle ACPI standard sono definiti dalla specifica ACPI; qui si dichiara solo come li popoliamo. [ipotesi] I campi esatti devono essere riscontrati con la specifica ACPI corrente prima dell'implementazione (Fase 1).

### 3.1 MADT  **IPOTESI (dipende da Fase 0)**
- [ipotesi] Una MADT "senza GIC" (nessuna struttura GICD) e' accettata da Windows se un'estensione HAL fornisce il controller. Questo e' il punto centrale e **non e' verificato**.
- [ipotesi] Windows potrebbe richiedere strutture GICC per enumerare i processori (la struttura GICC contiene ACPI Processor UID, flag, parking protocol version e parked address). [verificato: Linux `acpi_parking_protocol.c`] il kernel Linux ricava indirizzo di parcheggio e `parking_version` dalla struttura GICC della MADT. Quindi per il parking protocol si usa GICC *senza* esporre un GIC funzionante: coesistenza da validare in Fase 0/1 (RISKS R-02).
- Alternativa se Windows pretende GICD: descrivere un GIC fittizio (dispositivo non accessibile) e intercettarlo con l'estensione HAL - **ipotesi di piano B interno**, non nel piano originale.
- Una GICC per core (8), con ACPI Processor UID sequenziale, `Parking Protocol Version` != 0 e `Parked Address` = mailbox del core [verificato: Linux, controlla `mailbox_addr && version`].

### 3.2 GTDT  **IPOTESI (dipende da Fase 0)**
- [ipotesi] Windows ARM64 si aspetta il timer architetturale descritto in GTDT con interrupt (tipicamente GSIV di PPI GIC). Sul M1 il timer arriva come FIQ [verificato: irq-apple-aic.c, FIQ per timer ARM]. Strategia di piano: sorgente timer in IRQ fornita da estensione HAL timer [ipotesi: meccanismo da confermare in Fase 0; [ipotesi] sulla possibilita' hardware di instradare il timer in IRQ - da verificare con documentazione Asahi/m1n1].
- Campi da definire dopo Fase 0: GSIV per timer secure/non-secure/virtuale/hyp, flag di polarita'/trigger, eventuale uso di Platform Timer / Memory-mapped timer.

### 3.3 CSRT  **IPOTESI (dipende da Fase 0)**
- [verificato: Linux `drivers/acpi/csrt.c`; Microsoft HLK] CSRT e' una tabella ACPI con risorse per dispositivi non nel DSDT (esempio DMA); Windows associa una HAL extension a un PNP ID ACPI.
- [ipotesi] Layout: gruppi di risorse con Vendor ID, Device ID, Subsystem ID, Revision, Resource Descriptors (tipo, sottotipo, UID); la HAL extension di AIC e quella timer richiedono un gruppo ciascuna con ID dedicati. Valori degli ID: **da decidere** (non si usano ID Microsoft riservati; da definire con il nostro vendor ID).
- [ipotesi] Il nome del file DLL e l'associazione CSRT -> DLL (INF/registro/`HalExtension`) sono dettagli di Fase 0.

### 3.4 PPTT
- [ipotesi, dal piano] Core P/E descritti con gerarchia processore/cluster/cache e classi di efficienza (campo Efficiency Class in ACPI PPTT per processor hierarchy node, da specifica ACPI >= 6.x). Contratto: 1 nodo cluster P (4 core), 1 nodo cluster E (4 core), cache L1/L2 per cluster; valori letti da ADT. [ipotesi] Le dimensioni di cache dall'ADT sono disponibili; da verificare.
- Verifica: confronto con ciò che Windows riporta (Task Manager/`Get-CimInstance`) in Fase 4.

### 3.5 DSDT/SSDT
Dispositivi minimi (ID da assegnare, non inventati qui):
| Nodo | Scopo | Fase |
|---|---|---|
| Device AIC (HID nostro) | Aggancio dell'estensione HAL | 3 |
| Device timer IRQ | Aggancio estensione timer | 3 |
| xHCI (compatibile con `PNP0D10`? [ipotesi]) | Controller USB per UsbXhci | 4 |
| ANS | Aggancio del miniport StorPort | 4 |
| Processori con `_CPC` | DVFS | 5 |
| SPI/HID, batteria, PEP | periferiche | 5 |
Contratto: ogni device ha `_HID`/`_UID`, `_CRS` con MMIO e interrupt AIC, `_STA`. Gli interrupt nel `_CRS` usano GSIV logici assegnati dall'estensione HAL [ipotesi: modello GSIV da verificare in Fase 0].

### 3.6 Altre tabelle
FADT (flag HW-reduced [ipotesi: necessario, non c'e' hardware ACPI legacy]), DBG2 per il debug seriale [ipotesi: usato da KDCOM], SPCR, XSDT/RSDP. Tutte da verificare contro le specifiche in Fase 1.

## 4. Mailbox del parking protocol
- [verificato: https://codebrowser.dev/linux/linux/arch/arm64/kernel/acpi_parking_protocol.c.html] Struttura per core:
```c
struct parking_protocol_mailbox {  /* little-endian */
  UINT32 cpu_id;      /* firmware: 0xFFFFFFFF finche' parcheggiata */
  UINT32 reserved;
  UINT64 entry_point; /* indirizzo fisico di ingresso dell'OS */
};
```
- [verificato: stessa fonte] Sequenza OS: scrive entry_point, poi cpu_id con l'ID dell'interfaccia GIC della CPU, poi invia IPI di risveglio. Nota Linux: mapping device nGnRE al posto di nGnRnE dichiarato "innocuo".
- [verificato: stessa fonte, dettaglio ottenuto da un riassunto, da riguardare nella specifica] Ogni CPU ha una pagina da 4K con codice di spin e mailbox (riferimento a patch U-Boot).
- [ipotesi] Specifica ufficiale ("Multi-processor Startup for ARM Platforms") non letta: ordine di barriere, attributi di memoria e comportamento esatto del "wake" vanno controllati prima dell'implementazione.
- **Punto di design aperto [ipotesi]**: il protocollo usa un IPI di risveglio (nel mondo GIC un SGI). Qui non c'e' GIC; il core parcheggiato dovrebbe attendere su `WFE`/polling della mailbox oppure su IPI AIC (due IPI per CPU [verificato: irq-apple-aic.c]). Il codice di parcheggio EDK2 fa polling di `cpu_id` con `WFE` e l'invio di un evento/IPI AIC come sveglia opzionale. Se Windows invia il wakeup tramite l'interfaccia del controller registrato dall'estensione HAL, l'estensione deve implementare `sendIPI`. **Da verificare in Fase 0** (come Windows invia l'IPI di avvio).
- `cpu_id` atteso = identificativo nella GICC (campo CPU Interface Number); in assenza di GIC useremo un indice 0..7 [proposta].
- Errori: se entry_point resta non valido, il core resta parcheggiato; EDK2 non riceve notifica; il test di Fase 2 deve verificare che tutti e 8 siano nello stato "parcheggiato" prima dell'handoff a bootmgr.

## 5. Interfaccia estensione HAL  **IPOTESI (dipende da Fase 0)**
Nulla qui e' verificato. Le funzioni e le strutture seguenti sono segnaposto concettuali che Fase 0 deve confermare, rinominare o scartare.

Fatti verificati: [verificato: Microsoft HLK, link in ARCHITECTURE sez. 1] esistono HAL Extension DLL agganciate a un PNP ID ACPI e caricate/valutate in base a CSRT.

Ipotesi di lavoro (da piano):
| Elemento | Ipotesi | Stato |
|---|---|---|
| Punto di ingresso | Funzione esportata di inizializzazione della estensione (`HalExtension`-like) | non verificato |
| Registrazione controller | Chiamata equivalente a `HalpInterruptRegisterController` con tabella funzioni (`_INTERRUPT_FUNCTION_TABLE`) e un `KnownType` per controller non-GIC | non verificato: esistenza e accessibilita' da estensioni di terzi sconosciute |
| Funzioni del controller | enable/disable linea, EOI, accept interrupt, set affinity, send IPI, query/mapping GSIV | non verificato |
| Timer | registrazione sorgente timer da HAL ext (meccanismo "documentato" secondo il piano) | non verificato |
| Import | l'estensione puo' importare solo da `hal.dll`/`ntoskrnl.exe` secondo il test HLK "no imports" [ipotesi, pagina HLK incompleta] | non verificato |
| Firma | test-signing in sviluppo; firma Microsoft per distribuzione | piano |

Obbligo di processo: ogni funzione in questa tabella passa a `[verificato]` solo con riferimento a un report in `re/` (indirizzo, funzione, pseudocodice). Fino ad allora il codice di `halext-aic` implementa un'interfaccia interna `IIrqController` (registrazione, mask, unmask, eoi, setAffinity, sendIpi, mapGsiv) che isola il back-end e permette di cambiare l'ABI esterna senza toccare la logica AIC [proposta].

### 5.1 Interfaccia interna dell'AIC [proposta, parte hardware verificata]
[verificato: irq-apple-aic.c] 896 IRQ level-triggered, mask/affinity per IRQ, 2 IPI per CPU, registri evento/IPI-send/IPI-ack/IPI-mask (offset esatti nel sorgente Linux; non riprodotti qui). Il porting parte dal sorgente Linux con header di provenienza (licenza da controllare).
Precondizione: FIQ mascherate (PSTATE.F=1) in tutto il percorso Windows [ipotesi: Windows non le smaschera]. Postcondizione: nessun uso di Fast IPI, nessuna dipendenza da FIQ per il timer (ADR-0004).

## 6. Driver
- ANS StorPort: ingressi StorPort standard (HwFindAdapter, HwInitialize, HwStartIo, HwInterrupt) [verificato: struttura standard dei miniport StorPort nel WDK, da riconfermare]. Parte NVMe riusabile, init ANS/SART da portare [ipotesi, dal piano].
- DART/SART: libreria interna usata da ANS/SPI-HID. Pagine DART da 16K vs 4K di Windows [ipotesi, dal piano]; contratto: `DartMap(dev, va, pa, len)` con len multiplo di 16K, o bypass.
- PEP: callback del Platform Extension Plugin [ipotesi: interfaccia dal WDK]. Registri PMGR per DVFS [ipotesi].
- SPI HID: minidriver KMDF HID; protocollo MTP/SPI da driver Asahi [ipotesi].

## 7. Convenzioni
Nomi: prefisso `Pi` per tipi di piattaforma, `Aic` per AIC; errori come codici `EFI_STATUS` (firmware) e `NTSTATUS` (Windows); ogni modulo documenta precondizioni/postcondizioni nei commenti.
