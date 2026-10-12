# ADR-0004: Evitare le FIQ, usare IRQ per IPI e timer

- Stato: Accettata (dal piano), con ipotesi tecniche non verificate
- Data: 2026-10-12 · Collegamenti: [ARCHITECTURE sez. 5](../design/watariboot/ARCHITECTURE.md), [RISKS R-03](../design/watariboot/RISKS.md)

## Contesto
[verificato: `drivers/irqchip/irq-apple-aic.c` di Linux] L'AIC consegna come FIQ gli eventi hard-wired (timer ARM, Fast IPI, performance counter); ha anche due IPI per CPU e 896 IRQ level-triggered. Windows potrebbe non gestire FIQ [ipotesi: da analizzare nel vettore eccezioni in Fase 0].

## Decisione
- IPI: tramite i registri IPI dell'AIC in modalita' IRQ (come faceva Linux all'inizio [ipotesi: dal piano]), non Fast IPI.
- Timer: sorgente in IRQ fornita da un'estensione HAL timer (meccanismo ritenuto documentato da Microsoft [ipotesi]).
- Le FIQ restano mascherate.

## Alternative
- Gestire le FIQ in Windows: richiederebbe modifica del vettore eccezioni (non nativo).
- Intercettare FIQ in uno shim EL2 e ri-iniettare IRQ: piano B, ritenuto hypervisor (ADR-0001).
- Polling del timer: prestazioni insufficienti.

## Conseguenze
- [ipotesi] Serve che il M1 possa instradare il timer come IRQ (non verificato; R-03).
- Prestazioni IPI inferiori a Fast IPI (non misurate).
- Un FIQ inatteso mascherato resta invisibile: serve diagnostica (contatori, test di Fase 3).
