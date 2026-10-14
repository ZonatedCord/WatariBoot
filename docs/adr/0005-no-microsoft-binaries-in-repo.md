# ADR-0005: No Microsoft binaries or code in the repository

- Status: Accepted (constraint in `CLAUDE.md`)
- Date: 2026-10-12 · Links: [PRD NFR-6](../design/watariboot/PRD.md), [RISKS R-13](../design/watariboot/RISKS.md)

## Context
ARM64 ntoskrnl, public PDBs, ISOs and dumps are needed for analysis and testing. The plan cites the lawfulness of reverse engineering for interoperability in the EU (Dir. 2009/24/EC, art. 6) [not verified here; this is not legal advice]. The repository is intended to become public.

## Decision
No Microsoft binaries, PDBs, ISOs, dumps or large blocks of decompiled code in the repo. The files live in `/Volumes/Arthur Fonzarelli/WatariBoot-data/` (external disk; it must be connected). `re/` contains notes in our own words, with addresses. `.gitignore` excludes such files. Name, version and SHA-256 (not the content) are recorded for reproducibility.

## Alternatives (rejected)
- Include the binaries in a private repo: rejected, risk of accidental distribution and redistribution problems.
- Private Git LFS: same risk.

## Consequences
- Reproducibility requires whoever rebuilds to download the files from official sources: a script and hashes are needed.
- Phase 0 evidence is only described, not attached; reports cite addresses and functions.
- Possible loss of detail in the evidence: offset by precise notes and version/hash.
