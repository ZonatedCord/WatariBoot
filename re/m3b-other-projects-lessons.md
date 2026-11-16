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

## PMUv3 on the M1: their mechanism vs ours (2026-11-16)
Their m1n1-based hypervisor's approach was studied from its code. We copied nothing; our implementation was written from this description and the Arm ARM.

| | Theirs | Ours (`m3a-catch-mode`) |
|---|---|---|
| Routing PMUv3 accesses to EL2 | HACR_EL2 bit 56 | HACR_EL2 bit 56 (`wel2.hacr`, default on the M1) [hypothesis until rung HACR0] |
| Exception format | EC 0x18, or EC 0x3F with the MSR ISS in AFSR1_EL2 | same; AFSR1 accepted only if IL=1, [63:25]=0 and the ISS decodes to an owned register; otherwise ESR, otherwise UNDEF reflected to EL1 |
| PMCCNTR_EL0 source | Apple PMC0 (the hardware cycle counter), 1:1 | virtual: CNTPCT scaled to a nominal 1 GHz, one global always-running counter (`wel2.vpmu=global`) |
| PMCR/PMCNTEN/PMOVS/PMINTEN | mapped onto Apple PMCR0/PMSR | virtual state, read back as written, N=0 |
| PMUSERENR_EL0 | reads 0, writes ignored | stored; mirrored to the hardware only on cores with PMUv3 (not on the M1) |
| Non-PMU accesses caught by the trap | not studied | reflected to the guest as EL1 UNDEF, the behaviour without HACR |
| Firmware side | nothing for the PMU | EDK2 EL1 net emulates the first accesses while EDK2's vectors are live (bootmgr replaces VBAR_EL1 right after) |
| Diagnostics | — | fixed panel status row (MDCR stuck, HACR, CATCH count/last), `GUEST EXC`, `EL1EMU count=`, proxy rung HACR0 (no jump) |

Why our counter differs: it does not touch the Apple PMU registers, which the layer already uses for its FIQ path. With the MDCR route on QEMU, the bench reaches Setup (`final1`). If Windows on the M1 needs real cycles, switching the source to PMC0 is a small change.
