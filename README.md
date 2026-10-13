# Watari Boot

**渡 (watari): attraversare, passare da una riva all'altra.**

> Watari Boot: Windows nativo su Apple Silicon

**Windows ARM64 in dual boot nativo su Apple Silicon.** Senza hypervisor, emulatore o VM, e senza modificare Windows.

La catena di avvio: m1n1 (bootloader, da [Asahi Linux](https://github.com/AsahiLinux)) → EDK2 (UEFI + ACPI generate dall'ADT di Apple) → Windows ARM64. Windows impara a usare l'hardware Apple tramite i suoi meccanismi di estensione ufficiali: estensioni HAL, ACPI, driver KMDF/StorPort e PEP.

Primo target: MacBook Pro 13" M1 (t8103).

## Stato
Fase 0: lo spike sul HAL di Windows e i controller di interrupt non-GIC. Il dettaglio è in [docs/STATUS.md](docs/STATUS.md).

## Token e costi
<!-- TOKEN-COUNTER:START -->
**Token totali: 109,694,464 · Costo stimato: $58.25** (aggiornato 2026-10-13; marco: 2 sessioni, $58.25)

| Modello | Input | Output | Cache scritta | Cache letta | Costo stimato |
|---|---:|---:|---:|---:|---:|
| `claude-opus-5-5` | 662 | 303,002 | 3,019,947 | 72,579,510 | $44.74 |
| `claude-sonnet-5-5` | 658 | 106,019 | 2,485,718 | 31,198,948 | $13.52 |

<sub>Stima a prezzi API Anthropic, calcolata dai transcript locali di Claude Code con `tools/token-usage.py`. Con un abbonamento (Pro/Max) la spesa reale è il canone: questo è il costo equivalente a consumo. L'output dei subagenti può essere sottostimato nei log.</sub>
<!-- TOKEN-COUNTER:END -->

## Documenti
- [Piano](docs/PLAN.md)
- [Stato](docs/STATUS.md)
- [Diario delle sessioni](docs/sessions/)
- [Come si collabora](docs/CONTRIBUTING.md) · [Avvio per un nuovo collaboratore](docs/ONBOARDING.md)
- [Decisioni architetturali](docs/adr/)
- [Design](docs/design/watariboot/)

## Avvertenze
Progetto di ricerca sperimentale. Nessun codice o binario Microsoft è incluso in questo repository.
