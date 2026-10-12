# F0 / A7: verdetto preliminare della Fase 0 (issue #10)

Data: 2026-10-13. Autore: esecutore Sonnet (task A7), da verificare da Opus. Sintesi delle prove A1-A6, A8-A10 e del diario `ralph/PROGRESS-fase0.md`. **Nessun fatto nuovo è stato raccolto**: questo documento pesa le prove già presenti. Nessun binario o codice Microsoft usato.

Legenda: **[V: file]** = affermato e verificato nel file citato (che a sua volta cita la fonte primaria). **[I]** = ipotesi o giudizio mio, non verificato. Le percentuali sono giudizi soggettivi, non misure.

File citati con sigle: A1 = `re/f0-halext-doc-pubblica.md`; A2 = `re/f0-acpi-requisiti-windows.md`; A3 = `re/f0-m1-fiq-irq.md`; A4 = `re/f0-rpi3-precedente.md`; A5/A10 = `docs/design/watariboot/ESPERIMENTO-F0.md`; A6 = `tools/acpigen/README.md`; A8 = `re/f0-qemu-aic-modelli.md`; A9 = `re/f0-timer-irq-hw.md`; PROG = `ralph/PROGRESS-fase0.md`.

## 0. Sintesi in cinque righe

1. Nessuna prova raccolta dimostra che Windows ARM64 accetti un controller di interrupt non-GIC fornito da **terze parti**. Il solo precedente di non-GIC (Pi 3) era codice **dentro l'HAL Microsoft**.
2. Nessuna prova dimostra il contrario: la specifica CSRT prevede il tipo "Interrupt Controller", ma non esiste un esempio pubblico né l'interfaccia di registrazione è nota.
3. Il timer del M1 è solo FIQ; le quattro piste per evitarlo sono tutte non dimostrate.
4. Il Gate B in QEMU (con GIC sempre presente) può dare un "no" affidabile, ma solo un "sì" debole. Il "sì" definitivo arriva solo dall'hardware (Fase 3).
5. Stima: nativo puro circa 12%, piano B (EL2) circa 53%, altro/fallimento circa 35%. Raccomandazione: proseguire con il Gate B a basso costo, con priorità a V4 e V6, e fissare in anticipo la soglia del piano B (decide l'utente).

## 1. Le domande critiche

| # | Domanda | Risposta allo stato delle prove | Prove | Confidenza |
|---|---|---|---|---|
| 1 | Un'estensione HAL può registrare un controller di interrupt non-GIC? | **Non determinabile.** Sulla carta il tipo CSRT "Interrupt Controller" (tipo 0x0001, sottotipo 0x0001) esiste. Ma gli unici esempi pubblici di estensioni sono timer e DMA (i.MX); nel timer il GSI viene consegnato da un controller che Windows già conosce. Nessuna funzione di registrazione di un controller è stata vista (l'header `nthalext.h` non è stato letto). Firma con "EKU speciale" richiesta dalla documentazione HCK 8.1; effetto del test-signing sconosciuto. Il precedente Pi 3 non lo dimostra (vedi sez. 2). P(sì) circa 20% | A1 sez. 1, 2, 3, 5; A4 verdetto; A2 sez. 1 (CSRT) | **Bassa** (la stima è ampia: 10-30%) |
| 2 | La MADT può avere GICC senza GICD? | **Probabile a livello di parsing, ignoto a livello di boot.** Microsoft non dichiara il GICD obbligatorio. Il Pi 3 ha GICC senza GICD e Windows parte. Ma sul Pi 3 il campo "GICR base" doveva valere 1 ("Windows 10 on Raspberry Pi 3 does not boot otherwise"), segno che l'HAL **sceglie il driver del controller dai campi della MADT** [I: quale campo sia decisivo]. Un controller sconosciuto (AIC) con MADT senza GICD potrebbe non essere riconosciuto. `acpigen` oggi emette un GICD segnaposto con base 0: va tolto | A2 sez. 2, 5 (righe 6, 7); A4 sez. 1 | **Media** per "il parsing non rifiuta" (circa 60%); **bassa** per "Windows prosegue in modo utile" |
| 3 | Il parking protocol funziona senza GIC? | **Compatibile con la specifica, non provato su Windows.** La specifica del 2014 non vieta il polling; un loop WFE sulla mailbox è conforme [I, sostenuto da U-Boot]. L'interrupt di "wake" non è specificato e per Windows è deciso dall'HAL, quindi passa dal driver del controller (dipende dalla domanda 1). Nessuna prova che Windows ARM64 moderno usi ancora il parking; il Pi 3 usava PSCI via ATF, i blob MS-IoT dichiaravano MPPP solo per ARM32. Il core 0 deve essere il primo GICC. Discrepanze bloccanti in `acpigen` (versione e indirizzo di parking a 0) | A2 sez. 3, 4, 5; A4 sez. 4; ESPERIMENTO V3 | **Bassa-media**: circa 40% che Windows usi il parking senza GIC. Ripiego [I]: PSCI con conduit HVC servito da un piccolo stub residente a EL2 (il M1 non ha EL3): ancora da valutare con l'utente |
| 4 | Il timer funziona senza FIQ? | **Non dimostrato; quattro piste, nessuna chiusa.** Il timer architetturale del t8103 è solo FIQ nel DT e nella documentazione Asahi; XNU stesso lo usa in FIQ e non esiste un tick in IRQ su Apple Silicon. Piste in sez. 1.1 | A3 sez. 2, 3, 6; A9 sez. 1, 2, 4, 6; PROG | **Bassa**: circa 28% che almeno una pista indipendente dall'AIC (HW-1, V6) riesca; circa 64% che il timer si risolva se la domanda 1 è già un sì (si aggiunge il watchdog) |
| 5 | Le IPI funzionano senza FIQ? | **Come hardware sì, su AICv1 (t8103); come interfaccia Windows ignoto.** Le IPI legacy dell'AIC in IRQ hanno fatto girare Linux SMP a 8 core sul t8103 nella prima versione (codice storico, non esercitato oggi su t8103). Limiti: una sola IPI "other" per CPU, senza mittente né payload (serve un multiplexer software), latenza non misurata, mappatura indice AIC/MPIDR da confermare, viste per-CPU `0x5000+` non documentate. Come la HAL di Windows invii le IPI (e quali sorgenti si aspetti) dipende dall'interfaccia della domanda 1 | A3 sez. 4, 5, 6.2; A2 sez. 3.3 punto 7 | **Media** per l'hardware (circa 70%); **bassa** per l'integrazione con Windows |
| 6 | La strategia vale per AICv2 (M1 Pro/Max e successivi)? | **No, non così com'è.** AICv2 non ha le IPI legacy [V: commit Linux 2cf6821166 citato in A3]; ha una mappa di registri diversa (IRQ_CFG, maschere a offset diversi). Servirebbero hwirq software (`SW_SET`) o Fast IPI (cioè FIQ) [I]. Per il solo M1 base il problema non si pone, ma contraddice l'obiettivo "tutti gli Apple Silicon" | A3 sez. 4.1, 6.2 (rischio 3); A8 sez. 2.1 | **Alta** che non valga così com'è; **bassa** sull'alternativa |

### 1.1 Le quattro piste sul timer

| Pista | Cosa serve | Prova | Probabilità (mia) |
|---|---|---|---|
| **HW-1**: bit 22 `CONVERT_SIQ_TO_IRQ` di `ACC_OVRD` | Che il bit converta il timer EL0 da FIQ a IRQ | Solo il nome. Zero usi in tutto l'organizzazione AsahiLinux e in XNU, bit a 0 anche sotto macOS, registro dominato da bit di power management, "SIQ" non definito. Il testo Asahi dice che i FIQ sono interni ai complessi dei core, quindi un'interruzione diretta all'AIC potrebbe escluderli per costruzione | circa 10% (A9 sez. 6) |
| **V6**: Windows gestisce una FIQ del timer (QEMU) | Che il kernel Windows abbia un vettore FIQ utile e tolleri la sorgente | Nessuna prova. Il metodo B (patch QEMU di circa 10 righe) è interpretabile; il metodo A (driver DXE sui gruppi del GIC) può essere annullato dal driver GIC di Windows. Anche un esito positivo vale solo per il percorso FIQ → timer, non per le altre FIQ | circa 20% [I: mia stima, nessuna prova nei file] |
| **Watchdog + estensione HAL timer** | IRQ 338 funzionante, un comparatore per tutto il SoC, contatore a 32 bit che gira in circa 179 s, rischio di reset in recovery se `RESET_EN` è sbagliato, distribuzione del tick agli altri core con le IPI | Solo un commento nel driver Linux, mai usato. **Dipende dalla domanda 1** (serve un controller AIC funzionante per ricevere l'IRQ) e dall'esistenza di estensioni timer su ARM64 (esistono per i.MX) | circa 50% se la domanda 1 è sì; non applicabile altrimenti |
| **PMU in modalità AIC** (`IMODE=2`) | Quale hwirq AIC, conteggio di cicli e non di tempo (DVFS) | Documentato il valore, ignoto l'hwirq | non stimata; candidata debole [I] |

Un fatto trasversale: se il timer resta FIQ, un guscio a EL2 che converte FIQ in IRQ virtuale è tecnicamente noto (`hv_update_fiq` di m1n1) ma è proprio ciò che ADR-0001 esclude.

## 2. Il fatto più pesante: il Pi 3 era supporto interno all'HAL

**Che cosa dicono le prove.**
- Sul Pi 3 il firmware descrive il BCM2836 "ARM local" come se fosse una GIC (GICC con base 0x40000000, nessun GICD). Non esiste alcuna estensione HAL, driver di terze parti o gruppo CSRT per il controller di interrupt nei repository esaminati; la CSRT contiene solo il gruppo DMA [V: A4 sez. 2].
- Nel 2018 l'HAL ARM64 **riconosceva e rifiutava** il PIC Broadcom; per avviare serviva una patch in memoria con il debugger [V: A4 sez. 3, issue andreiw]. Poi non c'è più traccia di patch [I, confidenza media].
- Le uniche estensioni HAL pubbliche sono timer e DMA [V: A1 sez. 2].
- Confidenza di A4: circa 75% che il supporto al controller Broadcom fosse interno a Windows.

**Che cosa implica per la strategia nativa.**
1. **L'ipotesi centrale di ADR-0001 e del piano resta un'ipotesi.** Il piano scrive "estensione HAL AIC che registra l'AIC come controller di interrupt" come meccanismo noto. Le prove dicono che oggi non c'è un caso pubblico in cui una terza parte lo abbia fatto. Il Pi 3 mostra che l'HAL **può** ospitare un controller non-GIC, non che **terzi** possano aggiungerne uno.
2. **Il percorso non-GIC dell'HAL è scelto dai dati della MADT** (valore GICR=1, base 0x40000000, OEM ID "BC2836" [I: quale campo]). Un'estensione CSRT è un meccanismo diverso. Se l'HAL sceglie il controller dalla MADT con un elenco chiuso, un AIC non verrebbe mai raggiunto, qualunque estensione si carichi [I].
3. **Il GIC "finto" non è replicabile.** Far credere all'HAL che l'AIC sia un GIC richiede o di emulare un GIC (EL2, piano B) o di patchare l'HAL (vietato come soluzione finale).
4. **Il metodo "scatola nera" ha un limite.** Se l'interfaccia di registrazione non è pubblica, il progetto "non indovina" (ESPERIMENTO sez. 6): senza leggere almeno un header del WDK o fare reverse engineering, V4 può dire solo "carica / non carica". Il PLAN originale prevedeva Ghidra su `ntoskrnl`; la bozza ADR di Fase 0 l'ha sostituita con la scatola nera. La conseguenza va decisa dall'utente (sez. 5).
5. **Aggiunge dubbio, non certezza di fallimento.** Windows ha un'astrazione interna del controller, dimostrata dall'esistenza del ramo BCM [I]; la domanda è solo se sia raggiungibile dall'esterno.

## 3. Stime di probabilità

Sono giudizi miei sullo **stato delle conoscenze**, riferiti **solo** a interrupt, timer e avvio dei core. Non includono i rischi delle altre fasi (storage ANS, DART, GPU), che si sommano.

| Esito | Stima | Motivazione |
|---|---|---|
| **(a) Nativo puro**: Windows ARM64 funziona con un'estensione HAL AIC, tabelle senza GIC, parking e senza FIQ | **circa 12%** (intervallo 5-20%) | Prodotto di più condizioni, ciascuna incerta e con qualche correlazione: l'estensione registra un controller (circa 20%) × il timer si risolve senza EL2 (circa 64% condizionato al sì) × MADT, parking e IPI si integrano (circa 75%) ≈ 10%. Alzato leggermente per la correlazione positiva fra le condizioni. Nessuna prova diretta a favore: il verdetto di A1 è "non determinabile" |
| **(b) Serve il piano B**: shim EL2 minimo che presenta un vGIC. Il M1 espone la virtualizzazione del GIC (`ICH_*`) | **circa 53%** | È l'esito che segue dalla probabile negativa della domanda 1 (circa 88% di "non nativo puro") per la fattibilità del piano B (circa 60% [I]). Prove parziali: Linux su M1 usa `ICH_HCR_EL2` e un FIQ virtuale per il vGIC (`AIC_VGIC_MI`) [V: A3 sez. 1, riga 8]; m1n1 sa convertire i FIQ in IRQ/FIQ virtuali con `HCR_EL2.VF` [V: A3 sez. 2, pista d]. **Caveat [I]**: l'interfaccia CPU virtuale GICv3 non basta; un guest vede GICD e GICR in MMIO, quindi lo shim deve anche emulare distributore e redistributori intercettando le pagine con la traduzione di stadio 2. Non è "minimo": è un piccolo hypervisor, contrario ad ADR-0001. L'overhead "circa 1%" del PLAN non è misurato |
| **(c) Altro** | **circa 35%** | Raggruppa: piano B non fattibile o non accettabile per l'utente; Windows incompatibile con la CPU Apple per ragioni non legate agli interrupt (R-07); un meccanismo non previsto che funziona (per esempio l'interfaccia di un'estensione scoperta leggendo il WDK); fermo del progetto per un blocco legale o di metodo (nessun reverse engineering). Sottostima inevitabile: non conosciamo le incognite sconosciute |

Controllo: 12 + 53 + 35 = 100. L'incertezza sulle stime è grande; l'ordine (b) > (c) > (a) è meno incerto dei valori assoluti.

## 4. Che cosa resta da dimostrare solo sperimentalmente, e in che ordine

Principio: **prima ciò che può uccidere la strada nativa a basso costo, poi ciò che costa di più**. Ogni passo richiede il backup Time Machine confermato dall'utente prima di scaricare o installare qualunque cosa, e dispone di criteri di stop già definiti in `ESPERIMENTO-F0.md` sez. 6.

| Ordine | Esperimento | Che cosa chiude | Criterio di esito | Costo (stima A5/A10/A8) |
|---|---|---|---|---|
| 0 | **Lettura di `nthalext.h`** dal WDK (testo, non binario) [I: l'header è nel WDK] | Se esiste un'API per registrare un controller di interrupt. È l'unica verifica che può abbassare (a) a circa 3% o alzarla in modo netto, senza hardware | Funzione di registrazione presente / assente. Richiede un download del WDK (backup, licenza H7, e decisione sull'uso di un header come "strumento", coerente con la scatola nera) | 1 giorno più il download [I] |
| 1 | **V0**: baseline QEMU + HVF + EDK2 | Validità dell'ambiente | Windows installato, debug agganciato; stop dopo circa 5 giorni | 1-3 giorni |
| 2 | **V1**: `acpigen` + override DXE equivalenti a QEMU | Separa "Windows rifiuta" da "la nostra tabella è sbagliata" (falso negativo) | Boot uguale a V0; stop dopo circa 3 giorni di divergenza | 1-2 giorni più lavoro su `acpigen` |
| 3a | **V4**: CSRT + estensione HAL di prova | **La domanda decisiva più vicina**: Windows carica la DLL da un nostro gruppo CSRT, con test-signing e quale interfaccia richiede (anche per tipo 0x0001/0x0001) | DLL caricata e punto d'ingresso eseguito / non caricata con causa identificata / caricata con bugcheck informativo. Limite: carica non vuol dire registra un controller | 5-10 giorni; dipende dall'esito del passo 0 e da VM con WDK |
| 3b | **V6**: timer come FIQ (metodo B, poi A) | Windows tollera una FIQ del timer? | d (boot e orologio avanzano con FIQ dimostrata) risolve il timer; a/b/c lo lasciano aperto | 3-4 giorni più la build di QEMU |
| 4 | **V2** (GICC senza GICD) e **V3** (parking) | Kill-switch rapido su R-02 e R-06. V2 negativo netto per cause legate al controller è un forte indizio contro il nativo | Regola di lettura: un "sì" in QEMU = "non rifiutato in QEMU", non "il M1 funzionerà" | 2-4 e 4-7 giorni |
| 5 | **Modello AICv1 in QEMU** (macchina "watari-virt" senza GIC) | Prova reale di un controller non-GIC con la nostra estensione, e dell'IPI legacy | Solo se V4 e lo studio dell'header indicano che un'interfaccia di registrazione esiste: altrimenti il modello non ha niente da provare | 8-9 giorni [I, A8] |
| 6 | **HW-1** (`ACC_OVRD[22]` sul M1) | Chiude la pista hardware | Solo se V6 è negativo: se Windows gestisce la FIQ, HW-1 non serve. Richiede debug host, m1n1 in modalità proxy e conferma dell'utente prima di toccare il boot | poche ore se l'ambiente c'è; circa 10% di successo |

**Criterio che chiude la Fase 0.** Distinguo due chiusure.

- **Chiusura positiva (si prosegue nativo)**, tutte le condizioni: (1) V0 e V1 verdi; (2) esiste un'interfaccia di registrazione di un controller (passo 0 o V4) e la nostra estensione viene caricata e invocata per quel tipo di risorsa; (3) il modello AIC in QEMU, senza GIC dichiarato, porta Windows oltre l'inizializzazione dell'HAL con timer e IPI consegnati dalla nostra estensione; (4) il timer ha una via chiusa (V6 positivo, oppure HW-1 positivo, oppure tick da watchdog dimostrato in sessione hardware). Anche così si scrive "precondizioni accettate in QEMU", **non** "il M1 funzionerà": il sì definitivo è la Fase 3.
- **Chiusura negativa (si va alla decisione sul piano B)**, basta una: (1) lo studio dell'header e V4 non trovano alcuna via per registrare un controller; (2) V2 e V4 danno bugcheck attribuibili al fatto che Windows pretende un GIC dichiarato; (3) V6 negativo netto e HW-1 negativo e nessuna estensione timer praticabile. Il piano B lo decide **l'utente** (ADR-0001), non una soglia automatica.
- **Chiusura inconclusiva**: oltre circa 4 settimane di lavoro agente (stop globale di A5) o V1 non verde. Si riporta all'utente così com'è.

## 5. Raccomandazione

1. **Non fermare la strada nativa ora**, ma non darla per scontata: le prove la lasciano a circa 12%, e i dati di A4 hanno peggiorato il quadro rispetto al PLAN.
2. **Prima di spendere settimane in QEMU, fare il passo 0**: leggere `nthalext.h`. È economico e può decidere quasi da solo la questione del controller. Serve una decisione esplicita dell'utente su due punti: scaricare il WDK (backup e licenza) e accettare la lettura di un header di un kit pubblico come "strumento" nel rispetto di ADR-0005 (nessun binario o codice Microsoft nel repository: l'header non va copiato nel repo).
3. **Proseguire con il Gate B dando priorità a V4 e V6**, dopo V0 e V1. V4 risponde alla domanda decisiva più vicina; V6 può togliere il rischio timer senza hardware. V2 e V3 sono kill-switch economici da fare subito dopo V1.
4. **Sviluppare in parallelo, a costo basso, le parti utili anche al piano B**: estensione di `acpigen` (senza GICD, parking con versione 1 e indirizzo valido, FADT, DBG2, DSDT) e firmware EDK2 con i core parcheggiati. Non scrivere il modello AIC per QEMU (8-9 giorni) finché V4 o l'header non mostrano che esiste un'interfaccia da provare.
5. **Fissare adesso, prima dei risultati, la soglia che fa scattare la discussione sul piano B** (vedi bozza ADR `docs/adr/bozza-esito-fase0-preliminare.md`), per evitare di spostare l'asticella a posteriori. Decide l'utente.
6. **Preparare il piano B senza adottarlo**: stimare con onestà il costo di uno shim EL2 con emulazione di GICD/GICR (non "minimo") e valutare in alternativa il solo stub PSCI/HVC per il parking. Sono lavori di progettazione, da affidare a `morasca-agent`, non di implementazione.

## 6. Limiti di questo documento

- Si basa su prove di sola lettura (repository pubblici, documentazione). Nessun binario o simbolo Microsoft è stato analizzato: per scelta, ma anche un limite di conoscenza.
- Le probabilità sono soggettive e correlate; il prodotto di sez. 3 è un'euristica, non un calcolo.
- Il Pi 3 è del 2018-2019; non è verificato che le build correnti di Windows 11 ARM64 conservino il ramo BCM (A4: confidenza 60-65%).
- Il testo della specifica CSRT non è stato letto direttamente (A1 sez. 2): i campi del gruppo e le regole di caricamento restano [I].
- La stima del 20% sulla domanda 1 e quella del 20% su V6 sono mie, senza fonte.
- Prove su parking e PSCI di Windows ARM64 moderno: assenti.
