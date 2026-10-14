# ADR (draft): Strategy for the M1 timer, which arrives only as FIQ

- Date: 2026-10-13 · Status: **proposed**. The number is assigned at merge. Revises ADR-0004.

## Context
In A3 (`re/f0-m1-fiq-irq.md`) we verified that the ARM architectural timer of the t8103 arrives **only as FIQ** (`t8103.dtsi`, `arm,armv8-timer`, `AIC_FIQ`). There is no documented way to get it as an IRQ. ADR-0004 ("no FIQ") holds only if Windows can do without the architectural timer, or if we find an alternative.

## Decision (proposed)
Four tracks are explored in parallel, all achievable by us and **without a hypervisor**:
1. **Hardware (A9 → HW-1):** verify on the M1 the IMP-DEF bit "CONVERT_SIQ_TO_IRQ" and other Apple mechanisms.
2. **Windows and FIQs (A10 → V6 in QEMU):** if Windows ARM64 handles a timer FIQ, the constraint disappears.
3. **Timer HAL extension on the watchdog (A11):** use the watchdog `23d2b0000` (AIC IRQ 338) as the clock source, distributed to the other cores with IPIs.
4. **Test bench without a GIC (A8):** an AIC model in QEMU, to test the HAL extension on a genuinely non-GIC system.

The first positive outcome, in this order of preference (1, 2, 3), decides. Track 4 serves all three.

## Consequences
- Phase 0 gets longer with A8–A11 and with HW-1, which requires the debug host.
- If tracks 1, 2 and 3 all fail, we go back to plan B of ADR-0001 and the user decides.
