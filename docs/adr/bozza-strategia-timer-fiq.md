# ADR (bozza): Strategia per il timer del M1, che arriva solo come FIQ

- Data: 2026-10-13 · Stato: **proposto**. Il numero si assegna al merge. Rivede ADR-0004.

## Contesto
In A3 (`re/f0-m1-fiq-irq.md`) abbiamo verificato che il timer architetturale ARM del t8103 arriva **solo come FIQ** (`t8103.dtsi`, `arm,armv8-timer`, `AIC_FIQ`). Non c'è nessuna via documentata per averlo in IRQ. ADR-0004 ("niente FIQ") regge solo se Windows può fare a meno del timer architetturale, oppure se troviamo un'alternativa.

## Decisione (proposta)
Si esplorano in parallelo quattro piste, tutte realizzabili da noi e **senza hypervisor**:
1. **Hardware (A9 → HW-1):** verificare sul M1 il bit IMP-DEF "CONVERT_SIQ_TO_IRQ" e altri meccanismi Apple.
2. **Windows e le FIQ (A10 → V6 in QEMU):** se Windows ARM64 gestisce una FIQ del timer, il vincolo sparisce.
3. **Estensione HAL timer sul watchdog (A11):** usare il watchdog `23d2b0000` (AIC IRQ 338) come sorgente del clock, distribuito agli altri core con le IPI.
4. **Banco di prova senza GIC (A8):** un modello AIC in QEMU, per provare l'estensione HAL su un sistema realmente non-GIC.

Il primo esito positivo, in quest'ordine di preferenza (1, 2, 3), decide. La pista 4 serve a tutte e tre.

## Conseguenze
- La Fase 0 si allunga con A8–A11 e con HW-1, che richiede il debug host.
- Se le piste 1, 2 e 3 falliscono tutte, si torna al piano B di ADR-0001 e decide l'utente.
