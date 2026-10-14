# ADR-0001: Native boot without a hypervisor

- Status: Accepted (from the approved plan `docs/PLAN.md`); revocable only by an explicit decision of the user
- Date: 2026-10-12 · Links: [PRD](../design/watariboot/PRD.md), [RISKS R-01](../design/watariboot/RISKS.md)

## Context
The goal is to use all 8 cores, full frequencies and, in the long term, the M1 GPU, without a VM/emulator. Asahi ruled out native Windows, believing a kernel modification to be necessary [hypothesis: claim reported by the plan, source not verified]. It is believed [hypothesis] that official extensions (HAL extension via CSRT, ACPI tables, drivers) exist and are sufficient.

## Decision
Unmodified Windows ARM64 runs directly on the hardware; the chain is m1n1 -> EDK2 -> Windows. No hypervisor, emulator or VM as the final solution. QEMU is only a test bench.

## Alternatives (rejected)
- Full hypervisor (Parallels/VM style): rejected, contrary to the goal.
- Minimal EL2 shim for interrupts only (virtualization GIC interface): plan B (a), not chosen; the user decides if Phase 0 is negative [hypothesis: feasibility not verified].
- Windows kernel patch: plan B (b), research only, not distributable.

## Consequences
- Everything depends on the Phase 0 spike (R-01).
- The architecture is kept behind stable interfaces so that a possible plan B changes only the ACPI generator and the firmware (ARCHITECTURE sec. 7).
- Any deviation from native requires a new ADR and user confirmation.
