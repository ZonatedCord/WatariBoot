# RISKS: registro rischi

- Progetto: ProvaImpossibile · Data: 2026-10-12 · Versione: 0.1 · Stato: draft
- Documenti collegati: [PRD](PRD.md) · [ARCHITECTURE](ARCHITECTURE.md) · [INTERFACES](INTERFACES.md) · [TEST_STRATEGY](TEST_STRATEGY.md)
- Probabilita' e impatto sono stime qualitative del progettista (A/M/B), non misurate: `[ipotesi]`.

| ID | Rischio | Prob. | Impatto | Mitigazione | Piano B |
|---|---|---|---|---|---|
| R-01 | Spike negativo: l'HAL non accetta un controller non-GIC da estensioni di terzi [ipotesi, ipotesi centrale] | M | Critico | Fase 0 prima di tutto; evidenze in `re/`; isolare l'interfaccia (INTERFACES 5) | (a) shim a EL2 con interfaccia GIC di virtualizzazione (`ICH_*`) per soli interrupt, overhead stimato ~1% [ipotesi del piano, non misurato]; (b) patch kernel per ricerca, non distribuibile. Decide l'utente (ADR-0001) |
| R-02 | Windows richiede un GIC (GICD/GICR) nella MADT per enumerare CPU o per il parking [ipotesi] | M | Alto | Verificare in Fase 0/1 su ntoskrnl e in QEMU | GIC fittizio in ACPI intercettato dall'estensione; poi piano B di R-01 |
| R-03 | Timer solo FIQ (il timer ARM va su FIQ [verificato: irq-apple-aic.c]) e non instradabile in IRQ | M | Alto | Studiare registri/mode del M1 in m1n1/Asahi; sorgente timer alternativa in IRQ | Polling timer via estensione, oppure shim EL2; decidere con utente |
| R-04 | Debug senza secondo computer e canale di debug Windows non disponibile su hardware non standard (KDCOM richiede UART descritta; KDNET richiede NIC supportata) [ipotesi] | A | Alto | Pi 5 come debug host da Fase 2; DBG2 + UART di debug; log su framebuffer; m1n1 come proxy | Debug via m1n1 (memoria) senza WinDbg; bugcheck su framebuffer |
| R-05 | Brick/perdita dati del Mac | B | Critico | Solo partizione separata Asahi; backup; conferma prima di ogni operazione; macOS e recovery intatti | Restore via DFU da un altro Mac (richiede un secondo Mac che l'utente non ha [piano: nota]); necessario procurarsene l'accesso prima della Fase 2 |
| R-06 | Parking protocol: il wake via IPI non funziona senza GIC; semantica esatta non verificata sulla specifica | M | Alto | Leggere la specifica; polling con WFE nel parcheggio; invio IPI AIC | Singolo core (degradato) per le fasi 3-4; poi risolvere |
| R-07 | Comportamento CPU Apple fuori standard che Windows non tollera (registri IMPDEF, SError, page size/granularita', ID register) [ipotesi] | M | Alto | Analisi in Fase 0; test in Fase 3 con log di bugcheck | Se non rimediabile via firmware: shim EL2 |
| R-08 | IOMMU DART (pagine 16K) incompatibile con DMA di Windows (4K) | M | Medio-Alto | Bypass dove possibile; mappature nei driver; SART per ANS | Bounce buffer a 16K nei nostri driver |
| R-09 | SSD ANS2: init e SART complessi da portare; rischio di corruzione dati | M | Alto | Porting da driver Asahi; test su partizione dedicata; modello host | Avvio e installazione su USB esterno (SSD esterna) |
| R-10 | Firmware m1n1/EDK2: instabilita' USB/PHY/framebuffer | M | Medio | Riuso codice m1n1; test ripetuti | Boot da USB con controller gia' inizializzato da m1n1 |
| R-11 | Windows aggiorna e cambia il comportamento (build diversa dalla Fase 0) | M | Medio | Congelare build e hash; regressione dopo ogni aggiornamento | Rimanere su build congelata |
| R-12 | Firma dei driver: in sviluppo test-signing; per distribuzione serve Partner Center [dal piano] | A (per distribuzione) | Medio | Distinguere obiettivo ricerca / distribuzione | Distribuzione solo come istruzioni/test mode |
| R-13 | Legale: uso di dati Microsoft e reverse engineering [dal piano: consentito per interoperabilita' in UE, Dir. 2009/24/CE art. 6; non verificato qui, non e' consulenza legale] | B | Alto | Nessun binario/codice MS nel repo (ADR-0005); appunti con parole nostre | Parere legale se si pubblica |
| R-14 | Licenze del codice Asahi (MIT/GPL-2.0) incompatibili con la licenza scelta per il repo | M | Medio | Header di provenienza; scelta della licenza dopo audit | Riscrittura clean-room dei moduli GPL |
| R-15 | Complessita' per un utente senza esperienza firmware; progresso limitato dal tempo di esecuzione sull'hardware | A | Medio | Agenti scrivono, utente esegue; procedure passo-passo | Ridurre ambito alla Fase 3 come obiettivo di ricerca |
| R-16 | Dipendenza da upstream Asahi che cambia (m1n1, ADT) | M | Medio | Pinnare commit; header di provenienza | Fork mantenuto |
| R-17 | Fase 5: GPU WDDM molto grande e fuori portata | A | Medio | Basic Display su GOP; WDDM come progetto separato | Nessuna accelerazione |

## Note
Priorita' per la sessione immediata: R-01, R-02, R-03 sono risolvibili (almeno in parte) con la sola Fase 0 e non richiedono hardware.
