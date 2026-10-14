# ADR (draft): Experimental Phase 0, without analysis of Microsoft binaries

- Date: 2026-10-12 · Status: **proposed**. The number is assigned at merge (see `docs/CONTRIBUTING.md`).

## Context
The decisive question of Phase 0 is: *does Windows ARM64 accept a non-GIC interrupt controller (the AIC) provided by a HAL extension?* The original plan was to answer it by analyzing ARM64 ntoskrnl with the public symbols. During session 01, the task of obtaining and analyzing those binaries was stopped by a security filter, and it was decided not to propose it again.

## Decision
Phase 0 is resolved in two ways:
1. **Public documentation** (Microsoft Learn, the CSRT specification, WDK/HLK, the parking protocol specification) and **public precedents** (Windows on Raspberry Pi 3, which has no GIC). This is Gate A, executable immediately.
2. **Black-box experiments in QEMU**: variants of the ACPI tables and a test HAL extension built with the official WDK, observing Windows' behavior (bugcheck, debugger, loading). This is Gate B, which requires backup, free space and the official ISO.

No Microsoft binaries are disassembled in the repository or in the project's tools.

## Consequences
- The verdict is based on reproducible experimental evidence. It is also cleaner from a legal standpoint, but may be less precise on internal details.
- A Windows development environment (Windows ARM VM with the WDK) is needed, used **only as a tool**: it is not the final solution and does not conflict with ADR-0001.
