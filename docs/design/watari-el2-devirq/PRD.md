# PRD: Device interrupt routing for Windows on the real M1 (Watari EL2, no ITS)

- Project: Watari Boot, subsystem "watari-el2-devirq" (GitHub issue #62) · Date: 2026-10-21 · Version: 0.1 · Status: draft
- Linked documents: [ARCHITECTURE](ARCHITECTURE.md) · [RISKS](RISKS.md) · [TEST_STRATEGY](TEST_STRATEGY.md) · [DESIGN-STATUS](DESIGN-STATUS.md) · [draft ADR](../../adr/draft-m1-device-interrupts.md) · parent design: [watari-el2](../watari-el2/PRD.md)
- Marking: **[verified: source]** = checked in a named source during this session; **[hypothesis]** = deduction or unverified. Short sources: `PCIE` = Linux `drivers/pci/controller/pcie-apple.c`; `BIND` = Linux `Documentation/devicetree/bindings/pci/apple,pcie.yaml`; `DTS` = Linux `arch/arm64/boot/dts/apple/t8103.dtsi`; `V2M` = Linux `drivers/irqchip/irq-gic-v2m.c` (all mainline `master`, read 2026-10-21, used as hardware references only, nothing copied, GPL-2.0); `QDOC` = QEMU `docs/system/arm/virt` (master); `MSDOC` = Microsoft Learn "Other ACPI namespace objects"; `F0` = `re/f0-m1-fiq-irq.md`; `FAL` = `docs/FAILURES.md`.

## 1. Problem
On the QEMU bench Windows 11 ARM64 boots to the lock screen on top of Watari EL2 (M2), but only because the physical GIC ITS is passed through: Windows' NVMe and xHCI drivers use MSI via the ITS, and with no MSI controller Windows hangs in an INTx storm even without our layer (F-23, #65). The real M1 (t8103) has **no GIC and no ITS**: interrupts come from the AIC, the internal SSD (ANS) and USB (DWC3 behind the ATC PHYs) are platform devices with their own wired AIC interrupt, and the PCIe ports have Apple's own MSI mechanism that ends in the AIC. Before M3a/M3b we must decide **what interrupt model the guest sees** and **how AIC events become virtual interrupts**, otherwise the AIC backend (#59) and the ACPI generator (acpigen) are built on an unstated assumption.

## 2. Goals and non-goals
Goals
1. G1: a decided, documented guest-visible interrupt model for the t8103 that needs **no ITS and no LPI machinery** for the first Windows targets (M3b: ANS NVMe, USB keyboard, framebuffer).
2. G2: a precise AIC → virtual SPI path (mapping, level/edge semantics, who unmasks, affinity) consistent with the existing Watari EL2 design.
3. G3: a path for Apple PCIe MSI (needed from M4: Wi-Fi, Ethernet) that is designed now, gated by one cheap hardware experiment.
4. G4: everything that can be tested before hardware is listed with concrete bench steps (M2.x / M3a / M3b).

Non-goals
- NG1: emulating a GICv3 ITS (rejected, kept only as a documented contingency).
- NG2: writing the AIC backend, the Windows drivers or the PCIe host bridge bring-up (those are #59, #48 and a future PCIe design); this design only fixes their interrupt contracts.
- NG3: AICv2 chips (M1 Pro/Max, M2+, #47): only the extension points are kept.
- NG4: power management, wake-up sources, MSI-X > 32 vectors.

## 3. Actors
| Actor | Role |
|---|---|
| Windows 11 ARM64 (unmodified) | consumes ACPI-described GSIVs and the GIC/MSI frame; knows nothing about the AIC |
| Watari EL2 | owns AIC, converts events into virtual SPIs, owns the PCIe MSI doorbell programming |
| EDK2 + acpigen | emits MADT/DSDT with GSIVs derived from the same mapping table |
| Driver authors (#48) | write ANS StorPort, DART, (later) Wi-Fi drivers against the interrupt resources ACPI gives them |
| Developer (Marco, Ciani, Ralph) | runs bench tests and, later, hardware self-tests |

## 4. User stories
- US-1: As a driver author I get a **single level-sensitive, shareable-capable interrupt resource** in `_CRS` for the ANS NVMe and for the DWC3 xHCI, and it fires when the device asserts its AIC line.
- US-2: As the layer maintainer I can test the whole AIC-to-vSPI life cycle (ack, mask, inject, EOI, unmask, re-fire) on the host and on QEMU without hardware.
- US-3: As the ACPI author I never type an interrupt number by hand: GSIV values come from the FDT/ADT through the shared mapping table, and an AIC IRQ the guest cannot receive is a build-time error.
- US-4: As the hardware tester I can inject any wired interrupt with AIC `SW_SET` and watch it arrive in an EL1 test payload, before Windows is involved.
- US-5: As a future Wi-Fi driver author I get MSI through the GICv2m frame in the MADT, with a documented fallback if the hardware cannot do it.

## 5. Functional requirements
| ID | Requirement | Level |
|---|---|---|
| FR-D1 | Each AIC wired source that a device node uses is a virtual **SPI with vINTID = 32 + hwirq** and ACPI GSIV equal to the vINTID; hwirq 0-703 and 736-895 are exposed this way; the AIC MSI pool (hwirq 704-735) is never exposed as wired | 1 |
| FR-D2 | At init the layer masks every AIC IRQ; a source is unmasked only while the guest has the SPI enabled and the source is in the allow-list generated for this machine | 1 |
| FR-D3 | Level sources: the event read masks the IRQ in the AIC; it is unmasked again only when the guest **deactivates** the vINTID (EOIR with EOImode 0, DIR with EOImode 1); a still-asserted line fires again, a silenced one does not | 1 |
| FR-D4 | Edge sources (GICD_ICFGR edge, i.e. MSI SPIs): unmasked right after injection; `GICD_ICFGR` writes are forwarded to the backend (`set_trigger`) | 1 |
| FR-D5 | `GICD_IROUTER` is translated to the AIC `TARGET_CPU` of the chosen vCPU's physical core; when a core goes OFF (PSCI `CPU_OFF`) every SPI routed to it is re-targeted | 1 |
| FR-D6 | An AIC event for a source that is not in the allow-list, or that fires more than a threshold of times per second, is logged once, masked and counted; it never causes a hang or an unbounded storm | 1 |
| FR-D7 | The MADT keeps the GIC MSI Frame structure (type 0x0D) as proven in F-11; on t8103 the frame is at an address below 4 GiB (see ARCHITECTURE section 6), SPI base 928, count 32 | 1 |
| FR-D8 | PCIe INTx: the three port interrupts (hwirq 695, 698, 701 on the 13" M1 MBP, from the FDT) are exposed as level SPIs shared by INTA-D of the ports; the layer keeps all other port events (link up/down, MSI error) masked in the port's `INTMSK` | 1 |
| FR-D9 | Software test hook: `pirq_ops.sw_trigger` (AIC `SW_SET`/`SW_CLR`) lets a test payload raise any allow-listed hwirq; counters per hwirq (events, injections, unmasks, guard trips) in the `WEL2_STATS` vendor call | 1 |
| FR-D10 | The mapping hwirq <-> vINTID <-> GSIV lives in one header (`wel2_intr_map.h`) compiled into the layer, the host tests and acpigen | 1 |
| FR-D11 | The vGIC model exposes the v2m frame registers (`MSI_TYPER`, `MSI_IIDR`, `MSI_SETSPI_NS`); a CPU write of a valid SPI number to `MSI_SETSPI_NS` sets that SPI pending (edge); other writes are ignored and logged once | 2 |
| FR-D12 | A vendor HVC (`WEL2_MSI_PORT_ENABLE`, proposed `0xC6000010`) lets EDK2 ask the layer to program one PCIe port's MSI doorbell, MSI count and INTx mask after link-up; the layer owns all `PORT_*` register knowledge | 2 |
| FR-D13 | MSI hwirq 704+k arrives as an edge vSPI 928+k; ranges outside 704-735 are never accepted as MSI | 2 |
| FR-D14 | On t8103 the vGIC advertises `LPIS=0`; the QEMU-only LPI/ITS pass-through stays behind `wel2.lpi` and the QEMU platform ops (`has_phys_its`) | 1 |

Level 1 = M3a/M3b. Level 2 = M4 (first PCIe device with MSI), designed now, gated by hardware experiment E-HW1 (TEST_STRATEGY section 5).

## 6. Non-functional requirements
| ID | Requirement | How it is met (ARCHITECTURE refs) |
|---|---|---|
| NFR-D1 Maintainability | Every module in section 3 of ARCHITECTURE has one responsibility; AIC register knowledge only in `pirq-aic-t8103`, PCIe-port register knowledge only in `msi-apple-pcie`; errors as `wel2_err_t` | sections 3, 7 |
| NFR-D2 Reproducibility | Host tests deterministic (fixed xorshift seed printed at each run), bench runs scripted with pinned QEMU 11.1.2 and EDK2 `edk2-stable202608`, hwirq tables generated from the FDT, never typed | TEST_STRATEGY sections 2-4 |
| NFR-D3 Modularity | Backends behind `pirq_ops`/`msi_ops`; vGIC never includes a backend; guard is a decorator, not a branch in the backend | section 3, 5 |
| NFR-D4 Extensibility | New SoC = new `intr_map` table + `pirq` backend (+ `msi_ops` if the PCIe MSI differs: t602x `MSIMAP`); new allowed device = one row in the allow-list; no change to vgic/trap | section 8 |
| NFR-D5 Safety | No persistent hardware state is changed (AIC/PCIe port registers reset at power cycle); the AIC watchdog and DART fault interrupts stay masked and unexposed | RISKS R-D9 |
| NFR-D6 Performance | At most 3 EL2 exits per wired interrupt (event, IAR1, EOIR1 in trap mode); measured in M3b, budget in RISKS R-D6 | section 6 |
| NFR-D7 No lost interrupt | A level source is never left masked at the AIC without a pending path to unmask it (lesson F-21, F-22) | section 4.2 |

## 7. Success metrics
- M-1: host suite `make -C el2 test` includes the new suites and passes with a fixed seed (100% of register-decoding branches of the v2m frame, 100% of the `intr_map` table).
- M-2: bench: Windows reaches the lock screen with `msi=gicv2m` (no ITS) and the layer reports MSI SPI arrivals > 0 and INTx arrivals for NVMe = 0 (answers #65), or the experiment proves Windows does not use the frame (documented, F-NN).
- M-3: M3a: a `SW_SET` of an allow-listed hwirq reaches the EL1 test payload as the right vINTID on the right core within 1 ms, for all 8 cores, in both cpuif modes.
- M-4: M3b: Windows on the M1 reaches the lock screen with the ANS completion interrupt and the DWC3 interrupt delivered as vSPIs; guard trips = 0 in a 30 minute idle run.
- M-5: zero lost-interrupt hangs (identical screen > 60 s with the layer log silent) across the soak runs.

## 8. Scope and out of scope
In scope: guest interrupt model, AIC-to-vSPI mapping and semantics, Apple PCIe MSI plan, per-device interrupt plan for M3b, bench prototypes, tests, risks.
Out of scope: ANS/RTKit/SART, DART, Type-C/ATC PHY and PCIe link bring-up, power management, AICv2, MSI-X above 32 vectors, wake from sleep.

## 9. Risks
Summarised in [RISKS](RISKS.md) (risk register R-D1..R-D12). Top three: R-D1 hardware MSI data semantics, R-D2 Windows' use of the v2m frame (#65), R-D5 undocumented AIC details.

## 10. ADR references
- Draft ADR `docs/adr/draft-m1-device-interrupts.md` (decision: SPI-first, GICv2m for MSI, no ITS emulation).
- Existing: `docs/adr/draft-plan-b-el2-layer.md`, `docs/adr/draft-timer-fiq-strategy.md`, ADR-0004 (IRQ instead of FIQ).
