# Draft ADR: Device interrupts for Windows on the real M1 (no ITS)

- Status: **draft** (proposed 2026-10-21, to be numbered at merge; needs Marco's approval)
- Issue: #62 · Design: `docs/design/watari-el2-devirq/` (PRD, ARCHITECTURE, RISKS, TEST_STRATEGY)
- Related: `docs/adr/draft-plan-b-el2-layer.md`, `docs/adr/draft-timer-fiq-strategy.md`, ADR-0004

## Context
Windows 11 ARM64 boots to the lock screen on Watari EL2 in QEMU only with the physical GIC ITS passed through (F-23): its NVMe and xHCI drivers use MSI, and with no MSI controller it storms on INTx (#65). The real M1 has no GIC and no ITS. Its interrupts come from the AIC; the ANS NVMe (AIC 590) and the DWC3 USB controllers (AIC 777, 857) are platform devices with one wired level interrupt each; PCIe ports have an Apple MSI mechanism (a per-port doorbell address, data = vector index 0..31, 32 vectors shared by all ports, delivered as AIC hwirq 704..735, edge) [verified: Linux `pcie-apple.c`, `apple,pcie.yaml`, `t8103.dtsi`]. We must fix what interrupt model the guest sees. F-11 shows that Windows boots on GICv3 plus a GICv2m MSI frame with no ITS.

## Decision
1. **Level 1 (M3a/M3b): the guest sees only wired virtual SPIs.** vINTID = GSIV = 32 + AIC hwirq for the exposed sources, level sensitive, default-deny allow-list generated from the FDT/ADT, one shared mapping header used by the layer, the host tests and acpigen. The AIC event read masks the source; it is unmasked at guest deactivate (level) or at injection (edge). The vestigial GICv2m MSI Frame stays in the MADT.
2. **Level 2 (M4, first PCIe device with MSI): a GICv2m MSI frame backed by the Apple PCIe doorbell.** SPIs 928..959 map to AIC hwirq 704..735 (edge). The frame lives below 4 GiB (`0xFFFFF000`, doorbell `0xFFFFF040`) because t8103 `PORT_MSIADDR` is 32-bit. The layer owns all PCIe-port register knowledge and programs the ports on a vendor HVC from EDK2. How the port treats MSI data (index versus INTID) is decided by hardware experiment E-HW1 (three hypotheses with defined fallbacks, the last one a config-space rewrite for MSI, not MSI-X).
3. **No ITS emulation.** It stays a documented contingency, triggered only if Windows refuses both INTx and the v2m frame for a driver we cannot replace. The LPI/ITS pass-through remains QEMU-bench-only behind `wel2.lpi`; `GICD_TYPER.LPIS = 0` on the t8103.
4. The bench answers the open Windows questions before hardware: an MSI controller matrix on QEMU 11.1.2 (`msi=gicv2m|its|off`), then an optional QEMU AIC machine (#42).

## Status of the claims
Verified: hardware facts above (Linux sources), F-11, F-23, QEMU `msi=` option, Microsoft documentation of ACPI-enumerated xHCI (`PNP0D10`). Hypotheses: that Windows allocates MSI from the v2m frame, that inbox xHCI and a StorPort miniport accept a line-based ACPI interrupt, the Apple port data semantics, AIC edge-latch behaviour, P-core AIC indices.

## Consequences
- M3b needs no MSI at all (ANS and DWC3 are wired): the critical path uses only code that already works on the bench plus the AIC backend (#59).
- New small pieces: `wel2-intr-map`, `pirq-guard`, `wel2-msi-frame`, `msi-apple-pcie`, one `vgic_effect` (`SET_TRIGGER`), re-target on `CPU_OFF`, one vendor HVC.
- Corrections to the parent design: MSI frame address (`0xF_2000_0000` -> `0xFFFFF000`), register name (`MSI_SETSPI_NS`), hole for hwirq 704-735 in "SPI = 32 + AIC index" (details: ARCHITECTURE section 10).
- Capacity: 32 MSI vectors in total and wired sources up to AIC hwirq 895 (`ITLinesNumber` 29; raisable to 31).
- Cost per wired interrupt in trap mode: 3 EL2 exits; storage throughput on the single ANS interrupt to be measured in M3b.
- Hardware tests (E-HW0, E-HW1) need the debug host (#40) and a confirmed Time Machine backup.

## Alternatives
- **A only (wired SPIs forever):** enough for M3b, but leaves PCIe devices on shared INTx, which showed an unexplained storm on QEMU (#65). Kept as level 1, not as the end state.
- **Emulated ITS (vLPI):** the path Windows takes on QEMU; rejected as a plan because of size and fidelity risk (KVM's ITS is 2853 lines [verified: `re/planB-feasibility.md`]) and the need to map Apple's shared 32-vector pool onto (DeviceID, EventID). If ever built on the M1 it is fully software: pending bitmap instead of the 64-entry queue, freely clearable `EnableLPIs` (#64 findings do not carry over).
- **GICv3 MBIS (`GICD_SETSPI_NSR`):** same data mismatch as v2m, less precedent in Windows; not evaluated further.
- **Drop the MSI frame from the MADT on the M1:** removes a proven ingredient (F-11) for no gain.
- **Let EDK2 program the PCIe port registers:** spreads register knowledge across two components and a GPL-derived-knowledge boundary; the vendor HVC keeps one owner.
