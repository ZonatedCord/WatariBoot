<!-- SPDX-License-Identifier: MIT -->
# Lessons from other Windows-on-Apple-Silicon projects (documentation only)

Read on 2026-11-16 from public **documentation only** (no source code read, nothing copied; CLAUDE.md "write from specifications"). Use these as hints when a hardware run fails. Every item is the other project's own claim: **[hypothesis]** for us until we reproduce it.

## paulsmir/windows-on-m1 (J313 MacBook Air M1, m1n1-based EL2 hypervisor + Project Mu)
Sources: `documentation/ARCHITECTURE.md`, `LIMITATIONS.md`, `DEVELOPMENT_HISTORY.md` (raw.githubusercontent.com/paulsmir/windows-on-m1/main/...). They claim a desktop on 8 CPUs.
- **bootmgr low-address pages**: they needed a low-IPA alias backed by high DRAM. Check this first if bootmgr faults on memory (data abort, low FAR) after the PMU fix.
- **Kernel dies right after winload**: caused by missing ICC/vGIC behaviour; the fix included a zero virtual priority mask. Our bench already passes this point.
- **SMP**: `CLOCK_WATCHDOG_TIMEOUT` and `IPI_WATCHDOG_TIMEOUT` were reduced by timer and list-register coalescing and interrupt retry. This is the risk for our 8-core W2.
- **Timer**: injection, latching and duplicate pending state.
- **Display**: a virtual GOP at 1280x800 first, then the physical 2560x1600 via a DCP hand-off.
- **Packaging**: modules missing from the DSC/FDF, a missing stack before C, and the wrong boot-argument register in early firmware entry.
- **Still open for them**: about 20 s whole-system pauses, and a CPU1 SYNC exception at PSCI CPU_ON.
- **PMU / PMCCNTR / MDCR_EL2**: not mentioned in their docs.

## NT-for-ASi (amarioguy: m1n1_windows with GIC emulator, Mu UEFI, NT drivers)
- The README is the generic m1n1 one (MIT with embedded GPL/BSD parts). No technical hints. This is the NTASP group: do not copy (CLAUDE.md, `docs/adr/draft-el2-license.md`).

## bootcamparm.com
- Public status, 3 Nov: "stop before the PMCR_EL0 read". This matches our bench finding (bootmgr reads PMCCNTR_EL0 and PMCR_EL0 first).
