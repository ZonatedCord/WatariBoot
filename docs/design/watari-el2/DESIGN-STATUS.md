# DESIGN-STATUS: Watari EL2

- Project: Watari Boot, subsystem Watari EL2 · Date: 2026-10-14 · Version: 0.1 · Status: draft
- Documents: [PRD](PRD.md) · [ARCHITECTURE](ARCHITECTURE.md) · [INTERFACES](INTERFACES.md) · [TEST_STRATEGY](TEST_STRATEGY.md) · [RISKS](RISKS.md)
- ADRs (drafts): `docs/adr/draft-el2-base-m1n1-or-new.md`, `docs/adr/draft-el2-license.md`; stack in `ARCHITECTURE.md` §2 (C11 freestanding + clang) and `draft-el2-base-m1n1-or-new.md` (no separate ADR-0001: the stack is imposed by the project context).

## Phases
- [x] 1. Discovery — done (reading of the repository documents; greenfield for the subsystem, `edk2-apple/WatariQemuPkg/` exists but contains only test drivers for QEMU)
- [x] 2. Analysis — done (facts, risks and assessment of the 4 requirements in RISKS.md; ANALYSIS is not a separate file by the commissioner's choice: the contents are in RISKS §2-§6)
- [x] 3. Stack recommendation — done (base ADR)
- [x] 4. Architecture — done
- [x] 5. Design patterns — done (ARCHITECTURE §4.1)
- [x] 6. Design-in-the-small — done (INTERFACES)
- [x] 7. Test strategy — done
- [x] 8. PRD assembly — done
- [~] 9. Self-review — done with reservations (see below)
- [x] 10. Report — done

## Quality gate
- Maintainability — pass (single responsibilities, errors, naming in INTERFACES §10)
- Reproducibility — pass (TEST_STRATEGY §7)
- Modularity — pass (acyclic graph, backends behind `*_ops`)
- Extensibility — pass (ARCHITECTURE §9)

## Outcome of the mechanical checks
- No residual `TBD`/`TODO` in the documents (the words appear only as style rules or in the reference to NTASP).
- FR/NFR → module traceability: ARCHITECTURE §10.
- Register encodings and numeric values of GICv3/PSCI/SMCCC are **[hypothesis]** (ARM manual not available locally): confirmation is mandatory in M0 before writing code.
- Sources verified in session: m1n1 licenses (README, `LICENSE`, `hv_vm.c`, `main.c`), QEMU `virt.c` PSCI conduit, KVM SEIS workaround (commits `d11a327ed9`, `df652bcf1136`), E2H locked (Linux patch title), m1n1 VTCR. Not verified: license of `irq-apple-aic.c` (HTTP 429), m1n1's EL and hand-off registers.
- Not executed due to role constraint: creation of the branch `marco/design-watari-el2` (Bash limited to `mkdir -p` and `git add`) — must be created by Opus; the files are in `git add` on the current branch.

## Blocking questions
- (none)

## Non-blocking questions
- Q-1 … Q-6 in `RISKS.md` §5.
