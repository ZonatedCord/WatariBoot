# RISKS and analysis: Device interrupt routing on the M1

- Project: Watari Boot, subsystem "watari-el2-devirq" (#62) · Date: 2026-10-21 · Version: 0.1 · Status: draft
- Linked documents: [PRD](PRD.md) · [ARCHITECTURE](ARCHITECTURE.md) · [TEST_STRATEGY](TEST_STRATEGY.md) · [DESIGN-STATUS](DESIGN-STATUS.md) · [draft ADR](../../adr/draft-m1-device-interrupts.md)
- Scale: probability (P) and impact (I) 1-5, score = P x I. All assessments are **[hypothesis]** unless a source is named.

## 1. Risk register
| ID | Risk | P | I | P x I | Mitigation | Warning sign |
|---|---|---|---|---|---|---|
| R-D1 | Apple port treats MSI data differently from a GICv2m frame (H-C in ARCHITECTURE 5.2); MSI-X cannot be fixed up by an ECAM shim | 3 | 3 | 9 | Experiment E-HW1 before any level-2 code; level 1 does not depend on it; t602x `MSIMAP` removes the problem there; fallback to INTx | `MSI_BAD_DATA` status bit, no AIC pulse |
| R-D2 | Windows does not use the v2m frame for MSI, or INTx is unusable (#65 storm) | 3 | 4 | 12 | Bench step M2.x-1 answers it on QEMU with no hardware; M3b needs only wired SPIs; contingency B (ITS emulation) with explicit trigger | NVMe INTx storm with `msi=gicv2m`; zero MSI SPI arrivals |
| R-D3 | ITS emulation becomes necessary (contingency B): size, fidelity, silent hangs (F-14). With the software form the #64 findings turn into: pending bitmap instead of a 64-entry queue (no drops), freely clearable `EnableLPIs`, property table read through the layer's own mapping (cacheability attributes irrelevant when only the CPU reads it) | 2 | 5 | 10 | Do not start; if started, reuse the bench pass-through tests as an oracle (QEMU's physical ITS) and add a table-walk fuzz with the fixed-seed generator | A required driver refuses v2m and INTx |
| R-D4 | Lost level interrupt: a source left masked at the AIC with nobody to unmask it (F-21, F-22: the EOI maintenance interrupt is what unmasks) | 3 | 5 | 15 | Rule table (ARCHITECTURE 4.2) as host tests; target-OFF case keeps a "remembered masked" flag; watchdog counter "masked for more than T ms with line high" in `WEL2_STATS` | Device silent, identical screen, layer log silent |
| R-D5 | Undocumented AIC behaviour on real hardware: edge latch while masked, spurious zero events, `TARGET_CPU` semantics with multi-bit masks, WHOAMI vs MPIDR order of P cores (#55), interrupt count (896 assumed) | 4 | 3 | 12 | E-HW0 self-tests (TEST_STRATEGY 5); `pirq-fake-aic` mirrors only what the Linux driver relies on [verified: re/f0-qemu-aic-models.md section 5]; single-bit targets only | Event numbers outside the generated map |
| R-D6 | Exit cost per interrupt in trap mode (3 exits: event, IAR1, EOIR1) for ANS at high IOPS; one interrupt for all completions | 3 | 3 | 9 | Measure in M3b with the existing counters; options in order: cpuif variant B (parent RISKS R-05), batching in the ANS driver (poll after the first completion), interrupt coalescing if ANS supports it | Layer CPU > 30% of a core during storage load |
| R-D7 | AICv2 and t602x differ (no legacy IPI, new register layout, `MSIMAP`) | 4 | 2 | 8 | Everything chip-specific is behind `pirq_ops`, `msi_ops`, `intr_map_t` (#47) | New SoC bring-up |
| R-D8 | DART/doorbell interplay: the DART blocks or remaps the doorbell write; IOVA range collides with 0xFFFFF000; DART faults raise interrupts (781, 861, 696...) that nobody handles | 3 | 3 | 9 | Doorbell outside RAM and outside guest IOVAs; DART interrupts masked and guard-counted; DART design (#48) must reserve the doorbell page; E-HW1 includes a DART-in-bypass run | Fault IRQ from a DART at boot |
| R-D9 | Hardware safety: AIC/port register writes, watchdog IRQ 338 | 2 | 4 | 8 | No persistent state (registers reset at power cycle); only read/mask/unmask/target and port MSI config; WDT never touched here; self-tests only after confirmed backup (project rule) | Unexpected reset |
| R-D10 | Number drift: hwirq values from mainline Linux differ from the Asahi downstream or from the real ADT | 3 | 3 | 9 | Tables generated from the machine's FDT/ADT (FR-D10); acpigen test pins the j293 values used in this document | Mismatch between DTS and ADT in the generator test |
| R-D11 | Windows expectations on GSIV/shareability of `_CRS` interrupts and `_PRT` for a shared port line | 2 | 3 | 6 | M3b first boot with ANS and xHCI exclusive; shared port INTx only in level 2; compare with the QEMU `_PRT` that works | Device Manager code 10/12 on a device |
| R-D12 | 32 MSI vectors shared by all ports can be exhausted by a multi-queue MSI-X device | 2 | 2 | 4 | Advertise 32; the driver falls back to fewer vectors; documented limit | Windows allocates fewer vectors than requested |

## 2. Dependencies (external, maturity)
| Dependency | Maturity | Used for |
|---|---|---|
| Linux `pcie-apple.c`, `apple,pcie.yaml`, `t8103.dtsi` (mainline) | stable, in production use by Asahi | Hardware reference only (GPL-2.0, not copied) |
| Linux `irq-gic-v2m.c` | stable | v2m register reference |
| Asahi docs / m1n1 | active | AIC details, PCIe bring-up (E-HW1 needs m1n1's PCIe init; **[hypothesis]** that it brings a port up without Linux) |
| QEMU 11.1.2 `virt` `msi=` option | present on the bench | M2.x-1 |
| Windows inbox xHCI, PCI, NVMe | closed, behaviour inferred | Only public Microsoft docs and observed behaviour |

## 3. Potential debt
- The H-C config-space shim, if needed, couples the layer to PCI enumeration details.
- `ITLinesNumber = 29` caps the exposed AIC range at 896; raising it to 31 is cheap but changes `GICD_TYPER` (oracle comparison, parent INTERFACES 5).
- The guard thresholds (events per second) are guesses until M3a measurements.

## 4. Assessment of the four key requirements
| Requirement | Assessment | Residual risk |
|---|---|---|
| Maintainability | Good: one owner per register family, pure tables, errors as values | Hardware-only paths (AIC, PCIe port) have few tests until M3a |
| Reproducibility | Good: generated tables, fixed seeds, scripted bench matrix | Windows timing on TCG varies (parent R-13) |
| Modularity | Good: two small interfaces, decorator for policy, acyclic graph | `wel2-msi-frame` depends on `vgic` internals for pending (declared) |
| Extensibility | Good: SoC = table + backend; device = allow-list row; ITS can be added beside the frame | Fallback H-C is a special case that touches the PCI config path |

## 5. Open questions (non-blocking)
- Q-D1: Does the F-23 `its=off` MADT contain a GIC MSI Frame? (first action of M2.x-1.)
- Q-D2: Is physical address `0xFFFFF000` free on the t8103 (ADT / m1n1 memory map)?
- Q-D3: How is an ACPI-enumerated, non-PCI storage device bound to StorPort (interrupt resource delivery)? Belongs to #48.
- Q-D4: Is the t8103 AIC IRQ count really 896 (read `AIC_INFO`)? E-HW0.
- Q-D5: Does the AIC latch an edge while the IRQ is masked? E-HW0.
- Q-D6: Is the 13" M1 MacBook Pro's internal Wi-Fi the only PCIe device with MSI worth supporting before M4? (priorities for Marco.)
- Q-D7: Is building QEMU for the #42 AIC machine acceptable (backup + go-ahead needed, project rule)?

## 6. Decisions taken during this design session
- D-1: SPI-first. Level 1 is wired vSPIs only; level 2 is GICv2m; ITS emulation is a contingency, not a plan.
- D-2: The layer, not EDK2, programs the Apple PCIe port MSI registers (single owner); EDK2 calls a vendor HVC after link-up.
- D-3: The t8103 MSI frame moves to `0xFFFFF000` (below 4 GiB), replacing `0xF_2000_0000` of the parent design.
- D-4: AIC hwirq 704-735 are never exposed as wired SPIs.
- D-5: Default deny: every AIC source is masked at init and only the generated allow-list is exposed.
- D-6: The QEMU-only LPI/ITS pass-through stays behind a flag and does not shape the t8103 design.
