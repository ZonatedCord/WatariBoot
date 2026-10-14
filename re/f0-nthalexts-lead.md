# Pista: Wack0/NtHalExts (`halextbcm2709`)

- Data: 2026-10-13 · Fonte: https://github.com/Wack0/NtHalExts (README letto da Opus via `gh api`; Unlicense; ultimo push 2024-09).
- **Attenzione:** il repo contiene header e librerie del DDK Microsoft (era Windows 8 M2). **Non si copiano** nella nostra repo (ADR-0005). Si leggono solo `README.md` e il codice originale dell'autore.

## Cosa dice il README [verificato]
- Le estensioni HAL sono state scritte per **ARMv7 NT** (Windows 8.x ARM32) e si caricano tramite tabelle **CSRT**.
- `halextgit`: estensione per il timer generico ARM.
- `halextbcm2709`: estensione per i **controller di interrupt BCM2709** (Raspberry Pi 2, **senza GIC**), "backported from rs1-era HAL".
- Dice anche: le estensioni HAL per i controller di interrupt *"were added in 789x but removed again in 799x"*. Per le build successive l'estensione **cerca per pattern le funzioni non più esportate** dall'HAL e riconosce le strutture dati che cambiano tra una build e l'altra (`halextapi.c`). Se qualcosa non torna, va in bugcheck.
- Testata sulle build 7957–8400 (Windows 8 pre-release), "dovrebbe" arrivare fino a 9600 (8.1), ma non è stato provato.

## Cosa significa per noi
1. Il meccanismo **ufficiale** "estensione HAL = controller di interrupt" è esistito per poco ed è stato **rimosso** prima di Windows 8 RTM. È coerente con A1 (nessun esempio pubblico) e con A4 (il BCM era supportato dentro l'HAL).
2. **Esiste però una terza via** (oltre a "nativo pulito" e "piano B"): un'estensione HAL che **non modifica nessun file di Windows**, ma trova e chiama funzioni interne non documentate per registrare un controller.
   - Pro: niente hypervisor; Windows resta originale su disco.
   - Contro: fragile, si può rompere a ogni build di Windows; richiede di conoscere le funzioni interne dell'HAL di Windows 11 ARM64, cioè **analisi dei binari**, che il nostro metodo oggi esclude (bozza ADR "Fase 0 sperimentale"); possibili problemi con PatchGuard o con HVCI [ipotesi]; collaudata solo su ARM32 e Windows 8.
3. Il codice originale di `halextbcm2709.c`/`halextapi.c` va **studiato** (solo lettura): come si struttura un controller di interrupt per l'HAL di NT, quali callback servono, come si gestiscono IPI e priorità. Anche se le API sono cambiate, è il modello più vicino a ciò che serve per l'AIC.

## Probabilità aggiornate (stime di Opus, non misure)
| Strada | A7 (prima) | Ora |
|---|---|---|
| Nativo "pulito" (solo meccanismi ufficiali) | ~12% | **~5%** |
| Nativo "con funzioni interne" (stile NtHalExts) | non considerata | **~20%** |
| Piano B (strato EL2 che presenta un GIC virtuale) | ~53% | **~50%** |
| Altro / blocco | ~35% | **~25%** |

## Decisioni che spettano all'utente
- Accettare la terza via richiede di **rivedere la regola "niente analisi dei binari Microsoft"**: se, come e con quali limiti legali (interoperabilità UE, Dir. 2009/24/CE art. 6).
- Fissare la soglia per il piano B.
