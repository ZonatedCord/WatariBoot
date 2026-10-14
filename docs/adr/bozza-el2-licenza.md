# ADR (bozza): strategia di licenza per Watari EL2 (KVM GPL-2.0, m1n1 GPL-2.0-or-later dalla 2.0.0)

- Data: 2026-10-14 · Stato: **proposto** (da approvare da Marco; non è un parere legale). Il numero si assegna al merge.
- Collegamenti: `docs/adr/bozza-el2-base-m1n1-o-nuovo.md`, `docs/design/watari-el2/RISKS.md` R-08, `CLAUDE.md` ("il codice Asahi si riusa rispettando la licenza, con un header di provenienza").

## Contesto (fatti verificati il 2026-10-14)
- **m1n1:** README: MIT fino alla 1.9.9; dalla 2.0.0 «GPL-2.0-or-later; any code checked in thereafter is available under the terms of the GPL exclusively». `LICENSE` = GPL-2, `LICENSE.MIT` presente, `3rdparty_licenses/`. `src/hv/hv_vm.c` e `src/main.c` hanno `SPDX: GPL-2.0-or-later`. **Il brief dava m1n1 come MIT: per il codice attuale non lo è.**
- **KVM** (`arch/arm64/kvm/`, parte di Linux): GPL-2.0 (solo v2) [I per l'SPDX per file; la licenza di Linux nel suo insieme è GPL-2.0].
- **Linux `irq-apple-aic.c`:** licenza non verificata (richiesta bloccata con HTTP 429); l'audit di Mu cita `m1n1/src/aic.c` come GPL-2.0-or-later [V: `re/ntasp-audit-qualita.md` §3.3]. Da controllare prima di consultarlo come riferimento esteso.
- **NTASP `m1n1_windows`:** dichiarato MIT (`hv_vgic.c`: `BSD-2-Clause-Patent OR MIT`; `hv_psci.c`: `MIT OR BSD-3-Clause`), ma l'header del vGIC dice «Inspiration borrowed from the KVM vGIC driver»: stato incerto [V: `re/ntasp-analisi.md` §6].
- **EDK2:** BSD-2-Clause-Patent. **Windows:** nessun codice Microsoft nel repository (ADR-0005).
- Il repository Watari Boot non ha ancora un file `LICENSE` nella radice (verificato con `ls`).
- Fatti di interfaccia (indirizzi dei registri, posizioni dei bit, ordine delle operazioni richiesto dall'hardware o dalla specifica ARM) non sono espressione creativa; lo sono la struttura, i nomi, i commenti e le scelte di implementazione di un'opera.

## Decisione
1. **Il codice di Watari EL2 è scritto da specifica e non copia codice** da KVM, m1n1 (nessuna versione), NTASP, Mu o Linux. Fonti ammesse come *lettura*: specifiche ARM (GICv3, PSCI, SMCCC), documenti Asahi (`AsahiLinux/docs`), codice dei progetti sopra per capire **fatti** (offset, bit, sequenze). Le informazioni estratte vanno nei documenti `re/` con parole nostre (regola già in vigore), non nel codice incollato.
2. **Processo "leggi, chiudi, scrivi":** chi legge un file GPL scrive il fatto in `re/` e non scrive il modulo corrispondente; il modulo lo scrive un altro agente (o Opus) a partire dalla nota in `re/` e dalla specifica. Prima del merge, controllo di similarità (confronto di identificatori, struttura delle funzioni, commenti) del file nuovo con la fonte consultata; esito annotato nella PR.
3. **Licenza proposta per il nuovo codice dello strato: MIT** (SPDX `MIT`), con header di provenienza dove un fatto proviene da una fonte (es. `Fact source: AsahiLinux/linux irq-apple-aic.c, register offsets only`). La scelta finale della licenza del repository spetta a Marco (nessun `LICENSE` oggi).
4. **Fallback esplicito:** se un blocco di codice di m1n1 o di KVM risulta indispensabile, si accetta di rilasciare **l'intera immagine "Watari EL2"** sotto **GPL-2.0-or-later** (se solo m1n1) o **GPL-2.0-only** (se anche KVM); il codice copiato sta in `el2/third_party/<fonte>/` con il suo header e il testo della licenza, mai mescolato nei moduli propri. Questa scelta richiede una nuova ADR e la conferma di Marco. I backend `plat-*` e `pirq-*` sono la sede naturale di un eventuale codice derivato (sono già isolati dietro interfacce).
5. **Confini con le altre componenti:** EDK2 (BSD-2-Clause-Patent) e Windows (binario proprietario) sono caricati come immagini separate dallo strato, non collegati nello stesso eseguibile (aggregazione, non opera derivata) [I, non parere legale]. Gli header dell'immagine concatenata dichiarano le singole licenze.
6. **Documenti `re/` e `docs/`:** restano come oggi (nostri, parole nostre).
7. **Divieto di uso di NTASP come sorgente di codice** (coerente con ADR-0007); la lettura per i fatti è ammessa.

## Conseguenze
- Costo: si riscrivono funzioni che già esistono altrove; compensato dal controllo sulla qualità e dalla testabilità.
- Beneficio: il repository resta libero di scegliere la licenza; nessun obbligo GPL sul codice nostro se nessuna riga è copiata.
- Rischio residuo: somiglianza involontaria (anche tra implementazioni indipendenti di un registro GIC la struttura tende a convergere). Mitigazione: punto 2 e documentazione dell'origine dei fatti.
- Se il processo viene violato (es. codice incollato), il rimedio è riscrivere il modulo o passare al fallback del punto 4.
- Da fare: (a) verificare la licenza di `irq-apple-aic.c` e di `src/aic.c` di m1n1 in un secondo momento; (b) decidere la licenza della radice del repository; (c) aggiungere una riga a `docs/CONTRIBUTING.md` con il processo del punto 2.

## Alternative
1. **Copiare da m1n1 hv e rilasciare tutto GPL-2.0-or-later.** Pro: veloce. Contro: vincola l'intero repository o l'immagine; il vGIC non c'è comunque; non risolve la testabilità. Resta come fallback.
2. **Derivare il vGIC da KVM (GPL-2.0).** Pro: mappe di registri mature, errata SEIS già gestita. Contro: GPL-2.0-only si somma e l'immagine non potrebbe essere MIT; molto codice generico (migrazione, nested) da tagliare. Scartata; KVM resta riferimento di **fatti** (quali registri intercettare per l'errata).
3. **Riusare NTASP `hv_vgic.c` (MIT dichiarato).** Scartata: licenza non chiara (derivato GPL), qualità non provata, ADR-0007.
4. **Licenza GPL fin dall'inizio per tutto il sottosistema.** Possibile se Marco lo preferisce (semplifica l'uso di codice Asahi); non necessaria se vale la decisione 1.
