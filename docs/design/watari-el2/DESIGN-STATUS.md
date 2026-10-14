# DESIGN-STATUS: Watari EL2

- Progetto: Watari Boot, sottosistema Watari EL2 · Data: 2026-10-14 · Versione: 0.1 · Stato: draft
- Documenti: [PRD](PRD.md) · [ARCHITECTURE](ARCHITECTURE.md) · [INTERFACES](INTERFACES.md) · [TEST_STRATEGY](TEST_STRATEGY.md) · [RISKS](RISKS.md)
- ADR (bozze): `docs/adr/draft-el2-base-m1n1-or-new.md`, `docs/adr/draft-el2-license.md`; stack in `ARCHITECTURE.md` §2 (C11 freestanding + clang) e `draft-el2-base-m1n1-or-new.md` (nessun ADR-0001 separato: lo stack è imposto dal contesto del progetto).

## Fasi
- [x] 1. Discovery — done (lettura dei documenti del repository; greenfield per il sottosistema, `edk2-apple/WatariQemuPkg/` esiste ma contiene solo driver di prova per QEMU)
- [x] 2. Analysis — done (fatti, rischi e valutazione dei 4 requisiti in RISKS.md; ANALYSIS non è un file separato per scelta del committente: i contenuti sono in RISKS §2-§6)
- [x] 3. Stack recommendation — done (ADR base)
- [x] 4. Architecture — done
- [x] 5. Design patterns — done (ARCHITECTURE §4.1)
- [x] 6. Design-in-the-small — done (INTERFACES)
- [x] 7. Test strategy — done
- [x] 8. PRD assembly — done
- [~] 9. Self-review — done con riserve (vedi sotto)
- [x] 10. Report — done

## Quality gate
- Mantenibilità — pass (responsabilità singole, errori, naming in INTERFACES §10)
- Riproducibilità — pass (TEST_STRATEGY §7)
- Modularità — pass (grafo aciclico, backend dietro `*_ops`)
- Estensibilità — pass (ARCHITECTURE §9)

## Esito dei controlli meccanici
- Nessun `TBD`/`TODO` residuo nei documenti (le parole compaiono solo come regole di stile o nel riferimento a NTASP).
- Tracciabilità FR/NFR → moduli: ARCHITECTURE §10.
- Codifiche dei registri e valori numerici di GICv3/PSCI/SMCCC sono **[I]** (manuale ARM non disponibile in locale): conferma obbligatoria in M0 prima di scrivere codice.
- Fonti verificate in sessione: licenze m1n1 (README, `LICENSE`, `hv_vm.c`, `main.c`), conduit PSCI di QEMU `virt.c`, workaround SEIS di KVM (commit `d11a327ed9`, `df652bcf1136`), E2H bloccato (titolo patch Linux), VTCR di m1n1. Non verificato: licenza di `irq-apple-aic.c` (HTTP 429), EL e registri di consegna di m1n1.
- Non eseguiti per vincolo di ruolo: creazione del branch `marco/design-watari-el2` (Bash limitato a `mkdir -p` e `git add`) — va creato da Opus; i file sono in `git add` sul ramo corrente.

## Blocking questions
- (nessuna)

## Non-blocking questions
- Q-1 … Q-6 in `RISKS.md` §5.
