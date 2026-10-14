# ADR-0006: Project name "Watari Boot"

- Date: 2026-10-12 · Status: **accepted**

## Context
The project was provisionally called "ProvaImpossibile" ("ImpossibleTest"). A definitive name was needed that would make clear at once what it is about (like "Asahi Linux") and would not create trademark problems.

## Decision
The name is **Watari Boot**. 渡 (watari) means "to cross, to pass from one bank to the other"; "Boot" says it is a system boot project.
It must always be accompanied by the fixed subtitle: **"Windows nativo su Apple Silicon"** (in English: "Native Windows on Apple Silicon").

## Alternatives (rejected)
- **AsahiWindows**: "Windows" is a Microsoft trademark, which forbids its use in the names of third-party products (precedent: Lindows → Linspire, after a Microsoft lawsuit). "Asahi" would suggest that the project belongs to, or is endorsed by, Asahi Linux.
- **Watari Silicon / Labs / Project / Hashi / Zero / Gate**: they convey less clearly what the project is about.

## Consequences
- "Windows", "Apple" and "Apple Silicon" appear only in descriptive form (subtitle, descriptions), never in the name, logo or organization name.
- Repo: `ZonatedCord/WatariBoot` (private). Local folder: `~/Documents/02_PROGETTI/WatariBoot`. External data folder: `/Volumes/Arthur Fonzarelli/WatariBoot-data/` (external disk; it must be connected).
