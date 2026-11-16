<!-- SPDX-License-Identifier: MIT -->
# ADR (draft): limited reading of Microsoft boot code for interoperability, after the black-box route

- **Status:** accepted by Marco in chat on 2026-11-16 (Italian: "prima catch, poi decompiliamo e mettiamo solo conclusioni"; authorship confirmed the same day).
- **Amends:** F-01 / `docs/adr/draft-phase0-experimental.md` (no analysis or disassembly of Microsoft binaries).

## Context
On the real M1 (2026-11-16, M3a Monday), Windows bootmgr, started by our EDK2, took an EL1 synchronous exception with ESR 0x02000000 (EC 0x00, UNDEFINED). The fault PC is inside `BOOTAA64.EFI`, not in our code [verified: DxeCore symbolization, return address after `blr` in `CoreStartImage`]. The EL2 layer saw nothing, because the instruction never trapped to EL2.

## Decision
1. **First, the black-box route.** The layer gets a "catch" mode that traps PMU, debug, IMPDEF and `ACTLR_EL1` accesses to EL2. It logs the encoding and PC, then emulates the register (RAZ/WI). EDK2 reports ESR/ELR/FAR to the layer. The M1's ID registers are read in the read-only rung `ID0`. This uses only architectural information, as any hypervisor does.
2. **Only if that does not identify or get past the fault:** limited decompilation of the Microsoft boot code, within Art. 6 of Directive 2009/24/EC:
   - only to achieve interoperability of our firmware and layer with Windows;
   - only because the information is not available otherwise (step 1 tried and documented first);
   - only the parts needed: the code around the faulting PC, not whole binaries;
   - no Microsoft code, bytes or disassembly listings in the repo, in issues, in PRs or in any public place. The binaries stay outside the repo, as today.
3. **Only conclusions go into the repo**, written in our own words, for example: "bootmgr reads PMCR_EL0 during X; the layer emulates it as Y". Each conclusion is marked `[verified: interop reading, YYYY-MM-DD]`.

## Consequences
- F-01 stays the default. This exception applies only after step 1, is recorded per use in the session diary (what was read, by whom, why), and is limited to the boot path (bootmgr, winload, early kernel/HAL).
- [hypothesis] The Windows licence terms forbid reverse engineering "except where applicable law permits"; Art. 6 is the applicable law for EU contributors. A legal opinion should be sought before any publication beyond the repo.
