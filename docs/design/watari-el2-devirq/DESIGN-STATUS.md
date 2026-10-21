# DESIGN-STATUS: watari-el2-devirq

- Project: Watari Boot, subsystem "watari-el2-devirq" (#62) · Date: 2026-10-21 · Version: 0.1 · Status: draft
- Linked documents: [PRD](PRD.md) · [ARCHITECTURE](ARCHITECTURE.md) · [RISKS](RISKS.md) · [TEST_STRATEGY](TEST_STRATEGY.md) · [draft ADR](../../adr/draft-m1-device-interrupts.md)
- Deviation from the standard MorascaAgent file set (same as `watari-el2`): the stack is imposed (ADR-0001 of the project, C11 + AArch64 asm), so there is no new ADR-0001; design patterns and interfaces in the small are sections 7.4 and 7 of ARCHITECTURE; ANALYSIS content is in RISKS.

## Phases
- [x] 1. Discovery: done (repo docs, F-11/F-14/F-21/F-23, issues 42, 47, 48, 58, 59, 62, 64, 65, Linux/QEMU/Microsoft sources)
- [x] 2. Analysis: done (RISKS)
- [x] 3. Stack recommendation: done (imposed; no change; see deviation note)
- [x] 4. Architecture: done
- [x] 5. Design patterns: done (ARCHITECTURE 7.4)
- [x] 6. Design-in-the-small: done (ARCHITECTURE 7)
- [x] 7. Test strategy: done
- [x] 8. PRD assembly: done
- [x] 9. Self-review: done (below)
- [x] 10. Report: done

## Quality gate
- Mantenibilita: pass (single responsibility per module, errors as `wel2_err_t`, naming as parent)
- Riproducibilita: pass (generated tables, fixed seeds, pinned QEMU/EDK2, explicit `msi=`)
- Modularita: pass (acyclic graph ARCHITECTURE 3.2, two small interfaces)
- Estensibilita: pass (ARCHITECTURE 8)

## Self-review notes
- Mechanical checks: no TBD/TODO markers; every FR-D/NFR-D appears in ARCHITECTURE section 11 or the PRD NFR table; no empty section.
- Consistency with the parent design: eight findings listed in ARCHITECTURE section 10 (C1-C8); none blocks M2.x. The parent documents were NOT edited (out of scope of this session); the corrections should be applied when the ADR is approved.
- Not verified in this session: the `+0x40` match granularity of the Apple doorbell, `PORT_MSIBASE` semantics, AIC edge latching, Windows use of the v2m frame, StorPort binding of an ACPI-enumerated device. All are marked [hypothesis] and have a named experiment.

## Blocking questions
- (none)

## Non-blocking questions
- Q-D1 .. Q-D7 in RISKS section 5 (notably Q-D1: does the F-23 `its=off` MADT contain a GIC MSI Frame; Q-D7: is building QEMU for #42 acceptable, which needs a backup and Marco's go-ahead).

## Q-D1 answered (2026-10-21, M2.x-1 on the QEMU 11.1.2 bench) [verified: guest-RAM MADT dump, `el2/boot/m2-madt.py`]
The F-23 `its=off` MADT contains **no GIC MSI Frame and no ITS** (164 bytes; `msi=auto` without an ITS gives no controller on this QEMU). `msi=gicv2m` adds a GIC MSI Frame (type 0x0D) at `0x08020000`, flags 1 (use the MADT range), **SPI base 80, count 64** (188 bytes). Windows then boots: it allocates its MSI from the frame (INTID 80-84, edge) and reaches the desktop **both without the layer and on Watari EL2 in hw and trap cpuif modes**, once the layer forwards `GICD_ICFGR` edge (F-25). So option C (GICv2m, no ITS) is validated on the bench; the remaining unknowns are the Apple-side ones (E-HW1). Results: `el2/boot/README.md`, "MSI matrix (M2.x-1)".
