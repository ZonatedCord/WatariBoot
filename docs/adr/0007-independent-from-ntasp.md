# ADR-0007: Independent path; NTASP only as a reference

- Date: 2026-10-12 · Status: **accepted** · Links: [re/ntasp-analysis.md](../../re/ntasp-analysis.md), [re/ntasp-quality-audit.md](../../re/ntasp-quality-audit.md), [ADR-0001](0001-native-boot-without-hypervisor.md)

## Context
NT for Apple Silicon (NTASP, org `NT-for-ASi`) is the only existing project with the same goal. We analyzed it (state and architecture) and subjected it to a code quality audit. Opus rechecked the main defects directly.
- **Architecture:** Windows runs under the m1n1 hypervisor (`hv_vgic.c`, `hv_psci.c`). There is no native path and no parking protocol.
- **AIC HAL extension for Windows:** does not exist. The README of `apple_silicon_nt_drivers` says "Nothing's implemented yet".
- **UEFI AIC driver:** the AICv1 part, the one for the M1, is a stub (`AicV1/AppleAicV1Dxe.c`: `//TODO: this driver.`). The AICv2 code has bugs: `AIC_MASK_BIT` (`AppleAicLib.h:103`) cancels mask and unmask for IRQs from 5 upward.
- **ACPI tables:** they are designed for the vGIC, have invented values, and some addresses and IRQs do not match the Asahi DTS.
- **Process quality:** tests and CI are absent, commits are barely descriptive, and some files lack SPDX headers.

The user wants to reuse external code only if it is of good quality, not just to save time.

## Decision
Watari Boot is **independent** and is written from scratch.
- **Asahi Linux** (m1n1, Linux drivers, DTS) is the source of truth about the hardware.
- **NTASP** is used only as a reference: to understand where they got stuck and which data to start from. None of their files is copied. Their data (for example the CPU/PPTT topology, the MPIDRs) is used only after verifying it against the DTS or the ADT.
- Bugs found are reported to NTASP. First one: [apple_silicon_platforms_mu#23](https://github.com/NT-for-ASi/apple_silicon_platforms_mu/issues/23).

## Alternatives (rejected)
- **Contribute to NTASP:** it would lead to their hypervisor path, in conflict with ADR-0001, on a base that is about 70% to be rewritten.
- **Fork NTASP:** we would inherit their defects, debt and license provenance problems.

## Consequences
- More up-front work on the firmware (our own EDK2/Mu), in exchange for code that is ours, tested and with clean licenses from the start.
- From day one: host unit tests, CI, small commits and SPDX headers on every file.
- Plan B (ADR-0001): the NTASP hypervisor remains an existing alternative in case the native spike fails.
