# ADR-0003: ACPI Multiprocessor Parking Protocol invece di PSCI

- Stato: Accettata (dal piano), con punti aperti
- Data: 2026-10-12 · Collegamenti: [INTERFACES 4](../design/watariboot/INTERFACES.md), [RISKS R-06](../design/watariboot/RISKS.md)

## Contesto
Il M1 non ha EL3/PSCI [ipotesi, dal piano]. Servono i core secondari. Windows supporta il parking protocol [ipotesi, dal piano: non verificato su fonte Microsoft qui]. La struttura della mailbox e' verificata dal lato Linux [verificato: https://codebrowser.dev/linux/linux/arch/arm64/kernel/acpi_parking_protocol.c.html].

## Decisione
EDK2 parcheggia i core secondari in una mailbox per core (cpu_id, reserved, entry_point), annunciata nella MADT (GICC, parked address e parking version). Windows li avvia scrivendo la mailbox.

## Alternative
- PSCI (SMC/HVC): non disponibile senza EL3/firmware PSCI; fornirlo via EL2 e' un hypervisor in sostanza (contrario ad ADR-0001).
- Spin-table (da device tree): non e' ACPI; non usabile da Windows [ipotesi].
- Avvio single-core: fallback di emergenza (R-06).

## Conseguenze
- Il wake usa un IPI: senza GIC va definito (polling WFE e/o IPI AIC) - da verificare in Fase 0.
- Dipendenza da come la MADT (GICC) e' interpretata da Windows senza GIC (R-02).
- La specifica ufficiale non e' stata letta: obbligo di lettura prima di implementare.
