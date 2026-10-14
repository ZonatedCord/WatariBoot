# ADR (bozza): Fase 0 sperimentale, senza analisi dei binari Microsoft

- Data: 2026-10-12 · Stato: **proposto**. Il numero si assegna al merge (vedi `docs/CONTRIBUTING.md`).

## Contesto
La domanda decisiva della Fase 0 è: *Windows ARM64 accetta un controller di interrupt non-GIC (l'AIC) fornito da un'estensione HAL?* Il piano originale prevedeva di rispondere analizzando ntoskrnl ARM64 con i simboli pubblici. Durante la sessione 01, il task per procurarsi e analizzare quei binari è stato fermato da un filtro di sicurezza, e si è deciso di non riproporlo.

## Decisione
La Fase 0 si risolve in due modi:
1. **Documentazione pubblica** (Microsoft Learn, specifica CSRT, WDK/HLK, specifica del parking protocol) e **precedenti pubblici** (Windows su Raspberry Pi 3, che non ha GIC). È il Gate A, eseguibile subito.
2. **Esperimenti a scatola nera in QEMU**: varianti delle tabelle ACPI e un'estensione HAL di prova compilata con il WDK ufficiale, osservando il comportamento di Windows (bugcheck, debugger, caricamento). È il Gate B, che richiede backup, spazio libero e l'ISO ufficiale.

Nel repository e negli strumenti del progetto non si disassemblano binari Microsoft.

## Conseguenze
- Il verdetto si basa su prove sperimentali riproducibili. È più pulito anche dal punto di vista legale, ma potrebbe essere meno preciso sui dettagli interni.
- Serve un ambiente di sviluppo Windows (VM Windows ARM con il WDK), usato **solo come strumento**: non è la soluzione finale e non contrasta con ADR-0001.
