# ADR-0003: ACPI Multiprocessor Parking Protocol instead of PSCI

- Status: Accepted (from the plan), with open points
- Date: 2026-10-12 · Links: [INTERFACES 4](../design/watariboot/INTERFACES.md), [RISKS R-06](../design/watariboot/RISKS.md)

## Context
The M1 has no EL3/PSCI [hypothesis, from the plan]. The secondary cores are needed. Windows supports the parking protocol [hypothesis, from the plan: not verified against a Microsoft source here]. The mailbox structure is verified on the Linux side [verified: https://codebrowser.dev/linux/linux/arch/arm64/kernel/acpi_parking_protocol.c.html].

## Decision
EDK2 parks the secondary cores in a per-core mailbox (cpu_id, reserved, entry_point), announced in the MADT (GICC, parked address and parking version). Windows starts them by writing the mailbox.

## Alternatives (rejected)
- PSCI (SMC/HVC): not available without EL3/PSCI firmware; providing it via EL2 is in essence a hypervisor (contrary to ADR-0001).
- Spin-table (from device tree): not ACPI; not usable by Windows [hypothesis].
- Single-core boot: emergency fallback (R-06).

## Consequences
- The wake uses an IPI: without a GIC it must be defined (WFE polling and/or AIC IPI) - to be verified in Phase 0.
- Dependence on how Windows interprets the MADT (GICC) without a GIC (R-02).
- The official specification has not been read: it must be read before implementing.
