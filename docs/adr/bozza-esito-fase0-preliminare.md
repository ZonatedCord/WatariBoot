# ADR (bozza): Esito preliminare della Fase 0 e soglia del piano B

- Data: 2026-10-13 · Stato: **proposto**. Il numero si assegna al merge. Non modifica ADR-0001 né ADR-0004: ne precisa le condizioni di revisione.
- Collegamenti: [re/f0-verdetto-preliminare.md](../../re/f0-verdetto-preliminare.md) · [ADR-0001](0001-boot-nativo-senza-hypervisor.md) · [ADR-0004](0004-irq-invece-di-fiq.md) · [bozza strategia timer](bozza-strategia-timer-fiq.md) · [ESPERIMENTO-F0](../design/watariboot/ESPERIMENTO-F0.md) · [RISKS R-01, R-02, R-03, R-06](../design/watariboot/RISKS.md)

## Contesto
Le ricerche A1-A6 e A8-A10 sono concluse. Lo stato delle prove, severo:
- Nessuna prova dimostra che un'estensione HAL di terze parti possa registrare un controller di interrupt non-GIC. La specifica CSRT prevede il tipo "Interrupt Controller", ma gli unici esempi pubblici di estensioni sono timer e DMA (A1).
- Il precedente del Raspberry Pi 3 indica che il supporto al controller non-GIC era **dentro l'HAL Microsoft** (A4, confidenza circa 75%). Non è un precedente per un controller fornito da terzi.
- Il timer architetturale del M1 è solo FIQ; nessuna pista per averlo in IRQ è dimostrata (A3, A9). Il parking protocol senza GIC è compatibile con la specifica, ma non si sa se Windows moderno lo usi (A2).
- La strategia "niente FIQ" non vale per AICv2 (M1 Pro/Max e successivi), che non ha le IPI legacy (A3).
- QEMU non può togliere il GIC: il Gate B dà un "no" affidabile ma solo un "sì" debole (A5).
- Stima soggettiva dello stato delle conoscenze, solo per interrupt, timer e avvio dei core: nativo puro circa 12%, piano B (shim EL2 con vGIC) circa 53%, altro o fallimento circa 35%.

## Decisione proposta
1. **Proseguire con il Gate B**, con ordine: V0, V1, poi **V4** (caricamento di un'estensione HAL da CSRT) e **V6** (timer consegnato come FIQ) in priorità, poi V2 e V3 come verifiche rapide. Il modello AIC per QEMU (A8, circa 8-9 giorni) e il test hardware HW-1 si fanno solo se i risultati lo giustificano: il modello solo se esiste un'interfaccia di registrazione di un controller, HW-1 solo se V6 è negativo.
2. **Prima di V4, leggere l'header `nthalext.h` del WDK**, se l'utente consente il download (backup Time Machine confermato, licenza accettata dall'utente). Non va copiato nel repository (ADR-0005).
3. **Definire adesso la soglia che fa scattare la discussione sul piano B**, prima di vedere i risultati. La discussione parte quando si verifica **una** di queste condizioni:
   - T1: né l'header né V4 mostrano alcuna via per registrare un controller di interrupt;
   - T2: V2 e V4 producono bugcheck attribuibili al fatto che Windows richiede un GIC dichiarato;
   - T3: V6 negativo netto, HW-1 negativo (o non eseguibile) e nessuna estensione timer praticabile;
   - T4: nessuna chiusura entro circa 4 settimane di lavoro agente (stop globale di A5), da riportare come inconclusiva.
4. **Il piano B lo decide l'utente.** Il verdetto della Fase 0 viene presentato così com'è, nessun cambio di strategia automatico (ADR-0001). Nel frattempo si prepara, senza adottarlo, uno studio di progetto del piano B con `morasca-agent`, includendo anche l'alternativa meno invasiva di un solo stub PSCI/HVC a EL2 per l'avvio dei core.
5. Un esito positivo in QEMU si scrive "precondizioni non rifiutate in QEMU", mai "il M1 funzionerà": la conferma è la Fase 3 su hardware.

## Alternative
- **A. Fermare subito la strada nativa e passare al piano B.** Pro: evita settimane di esperimenti. Contro: le prove non dimostrano che il nativo sia impossibile (A1: circa 20% di sì), e il piano B stesso ha costo alto e contraddice ADR-0001. Scartata per ora, da riconsiderare se T1 si verifica.
- **B. Continuare con il piano originale (Gate B completo con modello AIC subito).** Pro: banco di prova più fedele. Contro: 8-9 giorni di lavoro prima di sapere se esiste un'interfaccia da provare; il GIC di QEMU resta presente. Scartata nella forma anticipata.
- **C. Fare reverse engineering di `ntoskrnl` e HAL con Ghidra (come nel PLAN originale).** Pro: risponde direttamente alla domanda decisiva. Contro: contrario alla scelta di scatola nera della bozza ADR di Fase 0; rischio legale e di metodo (R-13); richiede un binario Microsoft. Non proposta qui; resta una decisione dell'utente se il passo 0 e V4 non bastano.
- **D. Patch dell'HAL.** Non nativo come soluzione finale (ADR-0001, piano B b). Esclusa.
- **E. Rimandare il giudizio all'hardware (Fase 3).** Pro: l'unica prova definitiva. Contro: richiede prima firmware EDK2 e debug host, e un fallimento alla Fase 3 costerebbe molto di più di un fallimento ora. Scartata come unica strategia.

## Conseguenze
- La Fase 0 ha un criterio di chiusura esplicito (documentato in `re/f0-verdetto-preliminare.md` sez. 4) e una soglia fissata in anticipo, che riduce il rischio di spostare l'asticella a posteriori.
- Il Gate B resta lungo (3-5 settimane di calendario [ipotesi]) e con spazio disco insufficiente (75-115 GiB necessari, circa 53 GiB liberi, A5): serve una decisione dell'utente su disco esterno o altro host del debugger.
- Ogni download (WDK, QEMU, ISO, toolchain) richiede il backup Time Machine confermato dall'utente.
- Se T1, T2 o T3 si verificano, l'utente dovrà scegliere fra piano B (EL2 con emulazione di GICD e GICR, che non è "minimo" e va stimato [ipotesi]), uno stub PSCI/HVC limitato al solo avvio dei core, reverse engineering, oppure l'abbandono dell'obiettivo nativo. Il costo del piano B non è ancora noto.
- ADR-0004 resta "Accettata con ipotesi non verificate": il timer solo-FIQ la rende la decisione più fragile.
- Documentazione da aggiornare a fine sessione: `docs/STATUS.md`, diario di sessione, `RISKS.md` (R-01 e R-03: la probabilità passa da M ad A-M [ipotesi]).
