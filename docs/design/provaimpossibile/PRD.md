# PRD: ProvaImpossibile

- Progetto: ProvaImpossibile (Windows ARM64 nativo su Apple Silicon, primo target MacBook Pro 13" M1, SoC t8103, board j293)
- Data: 2026-10-12 · Versione: 0.1 · Stato: draft
- Documenti collegati: [ARCHITECTURE](ARCHITECTURE.md) · [INTERFACES](INTERFACES.md) · [TEST_STRATEGY](TEST_STRATEGY.md) · [RISKS](RISKS.md) · ADR: [0001](../../adr/0001-boot-nativo-senza-hypervisor.md), [0002](../../adr/0002-edk2-invece-di-uboot.md), [0003](../../adr/0003-parking-protocol-invece-di-psci.md), [0004](../../adr/0004-irq-invece-di-fiq.md), [0005](../../adr/0005-nessun-binario-microsoft-nel-repo.md)

## Convenzione di marcatura
Ogni affermazione hardware o su Windows e' marcata `[verificato: fonte]` oppure `[ipotesi]`. Le affermazioni che arrivano solo da `docs/PLAN.md` e non sono state riscontrate su fonti indipendenti sono `[ipotesi]` (anche se plausibili). Quasi tutto cio' che riguarda gli interni di Windows resta `[ipotesi]` finche' la Fase 0 non e' conclusa.

## 1. Problema
Un MacBook Pro M1 13" non puo' avviare Windows ARM64 in modo nativo. Le uniche strade esistenti usano una VM/hypervisor, che non da' accesso diretto all'hardware (GPU, tutti i core alle frequenze piene). Il progetto Asahi ha escluso il boot nativo di Windows perche' ritiene necessario modificare il kernel Windows [ipotesi: affermazione riportata dal piano, fonte primaria non verificata]. Il progetto verifica se esiste un percorso senza modificare Windows, usando solo i meccanismi di estensione previsti dalla piattaforma.

## 2. Obiettivi
- O1. Boot nativo di Windows ARM64 non modificato sul M1 (catena m1n1 -> EDK2 -> Windows), senza hypervisor ne' emulatore ([ADR-0001](../../adr/0001-boot-nativo-senza-hypervisor.md)).
- O2. Uso di tutti gli 8 core (4 P + 4 E) con scheduler consapevole delle classi di efficienza.
- O3. Installazione e avvio da SSD interno (ANS2) con desktop utilizzabile con periferiche USB.
- O4. Gestione energetica (frequenze, sleep) e periferiche interne (tastiera/trackpad, Wi-Fi, audio, batteria).
- O5. A lungo termine: GPU tramite driver WDDM.
- O6. Generalizzazione ad altri Apple Silicon (ACPI generate dall'ADT a ogni avvio).
- O7. Riproducibilita': ogni traguardo e' ricostruibile da repo + istruzioni, con dati Microsoft fuori dal repo ([ADR-0005](../../adr/0005-nessun-binario-microsoft-nel-repo.md)).

## 3. Non-obiettivi
- NG1. Modificare ntoskrnl, HAL o altri binari Microsoft (patch del kernel solo come piano B di ricerca, non distribuibile, e solo per decisione esplicita dell'utente).
- NG2. Hypervisor/VM come soluzione finale (salvo decisione esplicita dell'utente su piano B).
- NG3. Redistribuire codice o binari Microsoft.
- NG4. Toccare macOS o la recovery; operazioni sul disco fuori dall'installer Asahi.
- NG5. Prestazioni GPU o Thunderbolt completo nelle prime fasi (GPU: solo framebuffer/Basic Display fino a Fase 5).
- NG6. Supporto di Windows x64 o di versioni di Windows diverse da quella usata per la Fase 0 (Windows 11 24H2 ARM64) prima del completamento delle fasi 0-4.

## 4. Utenti e attori
| Attore | Descrizione |
|---|---|
| Sviluppatore/proprietario | Marco: conosce C/C++, non firmware; esegue sull'hardware, decide piano B |
| Agente capo (Opus) | Progetta, verifica, riferisce |
| Agenti esecutori (Sonnet) | Scrivono codice e documenti su task definiti |
| Debug host | Raspberry Pi 5 o portatile, necessario da Fase 2 |
| Utenti futuri (comunita') | Possessori di Apple Silicon interessati; rilevanti solo da Fase 6 e dalla firma dei driver |

## 5. User stories
- US1. Come sviluppatore voglio un verdetto si'/no con prove sulla possibilita' di registrare un controller non-GIC via estensione HAL, per decidere la strada prima di scrivere firmware.
- US2. Come sviluppatore voglio provare l'estensione HAL in QEMU con WinDbg, per validarla senza rischiare l'hardware.
- US3. Come sviluppatore voglio una shell UEFI sul MacBook con 8 core visibili, per avere una base stabile.
- US4. Come sviluppatore voglio che il kernel Windows giri nativo e arrivi al caricamento dei driver, per provare l'ipotesi centrale.
- US5. Come utente voglio installare Windows sull'SSD interno e usare tastiera/mouse USB, per avere un desktop.
- US6. Come utente voglio frequenze piene, tastiera/trackpad interni e sospensione, per usare il Mac come portatile.
- US7. Come sviluppatore voglio che macOS resti avviabile in ogni momento, per non rischiare il dispositivo.

## 6. Requisiti funzionali
| ID | Requisito | Fase | Modulo ([ARCHITECTURE](ARCHITECTURE.md)) |
|---|---|---|---|
| FR-1 | Report Fase 0 con verdetto si'/no e prove (indirizzi, funzioni, pseudocodice Ghidra) | 0 | M-RE |
| FR-2 | EDK2 espone tabelle ACPI (MADT, GTDT, CSRT, PPTT, DSDT, DBG2, ...) generate dall'ADT | 1-2 | M-ACPI-GEN, M-EDK2 |
| FR-3 | Core secondari avviabili via ACPI Multiprocessor Parking Protocol | 2 | M-PARK |
| FR-4 | GOP su framebuffer fornito da m1n1 | 2 | M-EDK2 |
| FR-5 | Estensione HAL AIC: registrazione controller, mask/unmask, EOI, routing, IPI | 3 | M-HALEXT |
| FR-6 | Sorgente timer in IRQ (estensione HAL timer); FIQ mascherate | 3 | M-HALEXT |
| FR-7 | Boot da USB fino al caricamento dei driver | 3 | tutti i precedenti + M-XHCI |
| FR-8 | Miniport StorPort ANS2 (con SART) per installazione e boot da SSD interno | 4 | M-ANS |
| FR-9 | xHCI descritto in ACPI, gestito dal driver standard Windows | 4 | M-XHCI, M-ACPI-GEN |
| FR-10 | Gestione IOMMU (DART): bypass dove possibile, altrimenti mappature nei driver | 3-4 | M-DART |
| FR-11 | PEP + `_CPC` per DVFS; classi di efficienza P/E via PPTT | 5 | M-PEP, M-ACPI-GEN |
| FR-12 | Driver KMDF HID per tastiera e trackpad interni | 5 | M-SPIHID |
| FR-13 | Wi-Fi, audio, batteria (SMC), sospensione | 5 | moduli futuri (vedi ARCHITECTURE, punti di estensione) |
| FR-14 | Avvio progetto WDDM per GPU AGX | 5+ | M-GPU (solo avvio) |
| FR-15 | Generazione ACPI da ADT a ogni avvio, supporto AIC2 | 6 | M-ACPI-GEN, M-HALEXT |
| FR-16 | Installazione non distruttiva: macOS e recovery intatti | 2+ | procedura (installer Asahi) |

## 7. Requisiti non funzionali
- NFR-1 Mantenibilita': un modulo = una responsabilita'; header di provenienza su codice Asahi; ogni decisione in un ADR. Moduli: tutti.
- NFR-2 Riproducibilita': toolchain con versioni pinnate, build in container/script in `tools/`, hash dei binari Microsoft usati registrati (non i binari), seed e fixture dei test fissati. Moduli: tools, tutti.
- NFR-3 Modularita': interfacce esplicite (INTERFACES.md), grafo delle dipendenze aciclico, piattaforma separata dal modello di SoC (tabella dati ADT-driven). Moduli: tutti.
- NFR-4 Estensibilita': nuovo SoC = nuova descrizione dati, non modifica di codice core; nuovo driver = nuovo modulo con ID ACPI. Moduli: M-ACPI-GEN, M-HALEXT.
- NFR-5 Sicurezza hardware: nessuna operazione su disco/boot senza backup e conferma dell'utente; macOS/recovery non toccati.
- NFR-6 Legale: nessun binario/codice Microsoft nel repo; codice Asahi riusato rispettando la licenza.
- NFR-7 Tracciabilita': ogni affermazione tecnica marcata verificato/ipotesi; ogni conclusione di Fase 0 con evidenza.
- NFR-8 Osservabilita': ogni fase espone un canale di log (seriale/framebuffer/m1n1 debug) indicato in TEST_STRATEGY.

## 8. Criteri di successo misurabili per fase
| Fase | Criterio | Evidenza |
|---|---|---|
| 0 | Documento in `re/` con verdetto si'/no sulla registrazione di un controller non-GIC via estensione HAL, e interfaccia; ogni conclusione con indirizzo/funzione/pseudocodice; esiti anche su: caricamento estensioni via CSRT, gestione FIQ nel vettore eccezioni, caso BCM2836 | File in `re/`, revisione di Opus |
| 1 | In QEMU, con MADT senza GIC, la nostra estensione HAL viene caricata e invocata da Windows (non solo presente su disco) | Log WinDbg con modulo caricato e breakpoint raggiunto; 3 avvii consecutivi con stesso esito |
| 2 | Shell UEFI sul MacBook che enumera 8 core (4 P + 4 E) con mailbox parking inizializzate; macOS ancora avviabile | Foto/video, log seriale/m1n1, verifica boot macOS |
| 3 | Kernel Windows nativo arriva al caricamento dei driver (inizio fase 1 del boot kernel con driver boot-start) | Log debug host, foto; tempo di boot fino al punto X registrato; 3 avvii su 3 |
| 4 | Installazione completata su SSD interno e desktop raggiunto con tastiera/mouse USB | Video installazione + desktop; riavvio da SSD riuscito 5 volte consecutive |
| 5 | Geekbench e Cinebench eseguiti con tutti gli 8 core; risultati riportati in rapporto a macOS sullo stesso Mac (soglia di accettazione da definire dopo la prima misura: aperto) | Report benchmark con versione, condizioni termiche, ripetizioni |
| 6 | Seconda piattaforma (es. M1 Pro/Max o M2) raggiunge shell UEFI con ACPI generate dall'ADT senza modifiche al codice di piattaforma | Log + diff che mostra solo dati nuovi |

Soglie numeriche di prestazione: non fissate in anticipo per non inventare cifre; verranno definite alla fine della Fase 4 su misure reali (domanda aperta).

## 9. Scope e out-of-scope
In scope: fasi 0-6 del piano. Out-of-scope: distribuzione pubblica firmata (richiede Partner Center), Linux/altri OS, Windows x64, GPU accelerata completa prima della Fase 5+, Thunderbolt/DisplayPort alt-mode completo.

## 10. Rischi (sintesi)
Dettaglio in [RISKS.md](RISKS.md). Principali: R-01 spike negativo (HAL non accetta controller non-GIC); R-02 Windows richiede GIC nella MADT; R-03 timer solo FIQ; R-04 debug senza secondo computer; R-05 brick.

## 11. Riferimenti ADR
ADR-0001 boot nativo senza hypervisor · ADR-0002 EDK2 invece di U-Boot · ADR-0003 parking protocol invece di PSCI · ADR-0004 IRQ invece di FIQ · ADR-0005 nessun binario Microsoft nel repo. Lo stack complessivo (C per firmware/driver, EDK2, WDK) e' imposto dal piano; la scelta del linguaggio per le parti non fissate (es. Rust per componenti host) non e' presa ed e' una domanda aperta.

## 12. Domande aperte non bloccanti
- Soglie di prestazione per Fase 5 (vedi sopra).
- Versione esatta di Windows 11 ARM64 e build della Fase 0 da congelare (hash).
- Tecnologia del canale di debug in Fase 3 (KDCOM su UART via DBG2 vs altro) - vedi RISKS R-04.
