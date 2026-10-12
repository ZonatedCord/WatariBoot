# ADR-0001: Boot nativo senza hypervisor

- Stato: Accettata (dal piano approvato `docs/PLAN.md`); revocabile solo con decisione esplicita dell'utente
- Data: 2026-10-12 · Collegamenti: [PRD](../design/provaimpossibile/PRD.md), [RISKS R-01](../design/provaimpossibile/RISKS.md)

## Contesto
L'obiettivo e' usare tutti gli 8 core, le frequenze piene e, a lungo termine, la GPU del M1, senza VM/emulatore. Asahi ha escluso Windows nativo ritenendo necessaria una modifica del kernel [ipotesi: affermazione riportata dal piano, fonte non verificata]. Si ritiene [ipotesi] che esistano estensioni ufficiali (HAL extension via CSRT, tabelle ACPI, driver) sufficienti.

## Decisione
Windows ARM64 non modificato gira direttamente sull'hardware; la catena e' m1n1 -> EDK2 -> Windows. Nessun hypervisor, emulatore o VM come soluzione finale. QEMU e' solo banco di prova.

## Alternative
- Hypervisor completo (stile Parallels/VM): scartato, contrario all'obiettivo.
- Shim minimo a EL2 per i soli interrupt (interfaccia GIC di virtualizzazione): piano B (a), non scelto; decide l'utente se la Fase 0 e' negativa [ipotesi: fattibilita' non verificata].
- Patch del kernel Windows: piano B (b), solo ricerca, non distribuibile.

## Conseguenze
- Tutto dipende dallo spike di Fase 0 (R-01).
- L'architettura e' tenuta a interfacce stabili cosi' che un eventuale piano B cambi solo il generatore ACPI e il firmware (ARCHITECTURE sez. 7).
- Ogni deviazione dal nativo richiede un ADR nuovo e conferma dell'utente.
