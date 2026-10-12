# ADR-0005: Nessun binario o codice Microsoft nel repository

- Stato: Accettata (vincolo in `CLAUDE.md`)
- Data: 2026-10-12 · Collegamenti: [PRD NFR-6](../design/provaimpossibile/PRD.md), [RISKS R-13](../design/provaimpossibile/RISKS.md)

## Contesto
Servono ntoskrnl ARM64, PDB pubblici, ISO e dump per analisi e test. Il piano cita la liceita' del reverse engineering per interoperabilita' nell'UE (Dir. 2009/24/CE, art. 6) [non verificato qui; non e' consulenza legale]. Il repository e' pensato per diventare pubblico.

## Decisione
Nessun binario, PDB, ISO, dump o grandi blocchi di codice decompilato Microsoft nel repo. I file stanno in `~/ProvaImpossibile-data/`. `re/` contiene note con parole nostre e indirizzi. `.gitignore` esclude tali file. Si registrano nome, versione e SHA-256 (non il contenuto) per la riproducibilita'.

## Alternative
- Includere i binari in un repo privato: scartato, rischio di diffusione accidentale e problemi di redistribuzione.
- Git LFS privato: stesso rischio.

## Conseguenze
- Riproducibilita' richiede che chi ricostruisce scarichi i file da fonti ufficiali: serve script e hash.
- Le prove di Fase 0 sono solo descritte, non allegate; i report citano indirizzi e funzioni.
- Possibile perdita di dettaglio nelle prove: compensata da note precise e versione/hash.
