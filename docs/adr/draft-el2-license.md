# ADR (draft): licensing strategy for Watari EL2 (KVM GPL-2.0, m1n1 GPL-2.0-or-later since 2.0.0)

- Date: 2026-10-14 · Status: **proposed** (to be approved by Marco; this is not legal advice). The number is assigned at merge.
- Links: `docs/adr/draft-el2-base-m1n1-or-new.md`, `docs/design/watari-el2/RISKS.md` R-08, `CLAUDE.md` ("Asahi code is reused respecting the license, with a provenance header").

## Context (facts verified on 2026-10-14)
- **m1n1:** README: MIT up to 1.9.9; since 2.0.0 «GPL-2.0-or-later; any code checked in thereafter is available under the terms of the GPL exclusively». `LICENSE` = GPL-2, `LICENSE.MIT` present, `3rdparty_licenses/`. `src/hv/hv_vm.c` and `src/main.c` have `SPDX: GPL-2.0-or-later`. **The brief gave m1n1 as MIT: for the current code it is not.**
- **KVM** (`arch/arm64/kvm/`, part of Linux): GPL-2.0 (v2 only) [hypothesis for the per-file SPDX; the license of Linux as a whole is GPL-2.0].
- **Linux `irq-apple-aic.c`:** license not verified (request blocked with HTTP 429); the Mu audit cites `m1n1/src/aic.c` as GPL-2.0-or-later [verified: `re/ntasp-quality-audit.md` §3.3]. To be checked before consulting it as an extended reference.
- **NTASP `m1n1_windows`:** declared MIT (`hv_vgic.c`: `BSD-2-Clause-Patent OR MIT`; `hv_psci.c`: `MIT OR BSD-3-Clause`), but the vGIC header says «Inspiration borrowed from the KVM vGIC driver»: uncertain status [verified: `re/ntasp-analysis.md` §6].
- **EDK2:** BSD-2-Clause-Patent. **Windows:** no Microsoft code in the repository (ADR-0005).
- The Watari Boot repository does not yet have a `LICENSE` file in the root (verified with `ls`).
- Interface facts (register addresses, bit positions, operation order required by the hardware or the ARM specification) are not creative expression; the structure, names, comments and implementation choices of a work are.

## Decision
1. **The Watari EL2 code is written from specification and copies no code** from KVM, m1n1 (any version), NTASP, Mu or Linux. Sources allowed for *reading*: ARM specifications (GICv3, PSCI, SMCCC), Asahi documents (`AsahiLinux/docs`), code of the projects above to understand **facts** (offsets, bits, sequences). Extracted information goes into `re/` documents in our own words (rule already in force), not as pasted code.
2. **"Read, close, write" process:** whoever reads a GPL file writes the fact into `re/` and does not write the corresponding module; the module is written by another agent (or Opus) starting from the `re/` note and the specification. Before merge, a similarity check (comparison of identifiers, function structure, comments) of the new file against the consulted source; outcome noted in the PR.
3. **Proposed license for the new layer code: MIT** (SPDX `MIT`), with a provenance header where a fact comes from a source (e.g. `Fact source: AsahiLinux/linux irq-apple-aic.c, register offsets only`). The final choice of the repository license is up to Marco (no `LICENSE` today).
4. **Explicit fallback:** if a block of m1n1 or KVM code turns out to be indispensable, we accept releasing the **entire "Watari EL2" image** under **GPL-2.0-or-later** (if only m1n1) or **GPL-2.0-only** (if KVM too); copied code lives in `el2/third_party/<source>/` with its header and the license text, never mixed into our own modules. This choice requires a new ADR and Marco's confirmation. The `plat-*` and `pirq-*` backends are the natural home of any derived code (they are already isolated behind interfaces).
5. **Boundaries with the other components:** EDK2 (BSD-2-Clause-Patent) and Windows (proprietary binary) are loaded as separate images by the layer, not linked in the same executable (aggregation, not derivative work) [hypothesis, not legal advice]. The headers of the concatenated image declare the individual licenses.
6. **`re/` and `docs/` documents:** stay as today (ours, in our own words).
7. **Ban on using NTASP as a code source** (consistent with ADR-0007); reading for facts is allowed.

## Consequences
- Cost: functions that already exist elsewhere are rewritten; offset by the quality control and by testability.
- Benefit: the repository stays free to choose its license; no GPL obligation on our code if no line is copied.
- Residual risk: unintentional similarity (even between independent implementations of a GIC register the structure tends to converge). Mitigation: point 2 and documentation of the origin of facts.
- If the process is violated (e.g. pasted code), the remedy is to rewrite the module or switch to the fallback in point 4.
- To do: (a) verify the license of `irq-apple-aic.c` and of m1n1's `src/aic.c` at a later time; (b) decide the license of the repository root; (c) add a line to `docs/CONTRIBUTING.md` with the process in point 2.

## Alternatives (rejected)
1. **Copy from m1n1 hv and release everything as GPL-2.0-or-later.** Pro: fast. Con: binds the whole repository or the image; the vGIC is not there anyway; does not solve testability. Remains as a fallback.
2. **Derive the vGIC from KVM (GPL-2.0).** Pro: mature register maps, SEIS errata already handled. Con: GPL-2.0-only adds up and the image could not be MIT; a lot of generic code (migration, nested) to cut. Rejected; KVM remains a reference for **facts** (which registers to intercept for the errata).
3. **Reuse NTASP `hv_vgic.c` (declared MIT).** Rejected: unclear license (GPL derivative), unproven quality, ADR-0007.
4. **GPL license from the start for the whole subsystem.** Possible if Marco prefers it (simplifies the use of Asahi code); not necessary if decision 1 holds.
