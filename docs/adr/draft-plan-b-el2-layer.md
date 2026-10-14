# ADR (draft): adopt plan B, Windows dual boot on top of a minimal EL2 layer

- Date: 2026-10-14 · Status: **accepted by Marco** ("se la B è teoricamente possibile, adottala e vai" — "if B is theoretically possible, adopt it and go"). The number is assigned at merge. Amends ADR-0001 (which forbade the hypervisor).

## Context
Phase 0 and the first experiments have, in practice, closed the **pure native** path:
- **V2 (F-14):** without GICD in the MADT, Windows 11 ARM64 does not reach the session. The GIC appears to be mandatory.
- **F-04, F-05:** support for non-GIC controllers is inside Microsoft's HAL; the interface for providing them via HAL extension has been removed.
- **V3 (F-15):** Windows still uses the parking protocol, but core bring-up deadlocks and then bugchecks 0x5C. After 6 targeted tests the cause has not been found. The control with PSCI, instead, works (`docs/img/a-t1-controllo-fadt-psci-lock.png`).
- **V6 (F-13):** the timer as FIQ is inconclusive.

The research `re/planB-feasibility.md` concludes that plan B is **theoretically possible** (confidence about 85%):
- the M1 implements in hardware the CPU interface of the virtual GICv3 (`ICH_*`); Linux KVM uses it, with the maintenance interrupt via `AIC_VGIC_MI` (verified in `torvalds/linux` `irq-apple-aic.c`);
- Windows works with a GICv3 + PSCI (V1 and control T1);
- reference building blocks exist (m1n1 hv, NTASP `hv_vgic.c`/`hv_psci.c`).

## Decision
Watari Boot becomes: **Windows ARM64 dual boot on Apple Silicon on top of a minimal EL2 layer** ("Watari EL2") that:
- presents Windows with a **virtual GICv3**: distributor and redistributor emulated with stage-2 traps, CPU interface in hardware with the list registers; the AIC interrupts of the peripherals become virtual SPIs;
- provides **PSCI** via HVC (core bring-up);
- delivers the **timer** as a vIRQ (the timer FIQ is intercepted at EL2);
- translates Windows' SGIs into AIC IPIs;
- leaves **everything else native**: cores at full speed, identity stage-2, real peripherals driven by the Windows drivers.

There is no macOS underneath: Windows is chosen at boot, like Asahi. Our EDK2, `acpigen` and the QEMU bench are reused.

## Alternatives (rejected)
- **Native with the HAL's internal functions** (NtHalExts style): requires analyzing Microsoft binaries (excluded, F-01) and is fragile with every Windows build.
- **Keep looking for the pure native path:** V2 indicates the GIC is mandatory, so even if the parking were solved the controller would remain blocking.
- **Reuse NTASP:** incomplete and of low quality (ADR-0007); used only as a reference.

## Consequences
- The `CLAUDE.md` constraint "native, no hypervisor" becomes "dual boot, no macOS underneath, minimal EL2 layer allowed".
- Prototype: the M1 has no nested virtualization in HVF, so the first development is done in QEMU **TCG** with `virtualization=on`, then on the MacBook (the debug computer is needed).
- Estimate (hypothesis): about 2–3 months for the QEMU prototype, another 3–6 months for the MacBook up to the lock screen.
- Main risks: the M1 SEIS errata on the vGIC; fidelity of the GICD/GICR emulation; m1n1 code designed for debugging; licenses (KVM GPL-2.0, m1n1 MIT).
- Next step: a design session with `morasca-agent` on the EL2 layer.
