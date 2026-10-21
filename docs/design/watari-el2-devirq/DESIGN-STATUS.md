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

## Implemented (M2.x-2 / M2.x-3, #67, 2026-10-21)
Done and covered by host tests (`el2/tests/test_intr_map.c`, 23 cases) and the QEMU scripts (`el2/boot/run-all.sh`, both cpuif modes):
- `el2/include/wel2_intr_map.h` + `el2/vgic/intr_map.c`: section 7.1 with the collision resolved as recommended (hwirq 704-735 is the MSI pool, vINTID 736-767 is an unowned hole, hwirq >= 736 stays at 32 + h). Beyond the design: a default-deny allow-list (`intr_find`, `intr_exposed_vintid`, `intr_exposed_hwirq` = the inverse of `intid_of`), trigger per entry (`intr_trigger`), `msi_exposed` switch for level 2, `intr_map_validate` (range, pool, duplicate, layout errors), static t8103 example table (ANS 590, DWC3 777/857, PCIe ports 695/698/701; the ANS mailbox and DART lines are denied). Exposing the PCIe port lines and keeping the mailbox denied are **[hypothesis]**.
- `el2/include/wel2_pirq.h` + `el2/vgic/pirq.c`: the `pirq_ops` contract (section 4.2) as a struct of callbacks plus the policy: arrival -> vINTID with default deny, level masked until the guest deactivates, edge latched (unmask at injection when the backend's event read masks, `auto_masks`), `strict_level`, guest ICFGR vs platform trigger table, re-target on CPU_OFF/CPU_ON (section 4.3 item 2).
- Backends: `el2/boot/pirq_qemu.c` (QEMU GIC, behaviour identical to before, F-25 kept) in the layer; `el2/tests/fake_pirq.h` (GIC-like and AIC-like fake) in the host tests. `cpuif.c`, `psci.c`, `main.c` use `g_pirq`.
- `tools/acpigen/tests/test_gsiv.c`: pins GSIV = `intr_gsiv()` for the j293 numbers of section 6, compiled against the same header.

Host-side follow-ups (#67 remainder, not in the boot path; `make -C el2 test` and `run-all.sh` green):
- `el2/include/wel2_pirq_guard.h` + `el2/vgic/pirq_guard.c` (`test_pirq_guard.c`, 9 cases): `pirq-guard` as a decorator over a backend's `pirq_ops` plus `pirq_guard_arrive()`. Fixed-window budget per physical source with the caller's generic timer count; over budget: source masked at the backend, event not injected, trip counted, logged once per source; unmasks issued by pirq are swallowed while tripped; it returns at `pirq_guard_tick()` after the cool-down, or at once when the guest disables and re-enables it (a guest-disabled source is not re-armed by the timer); denied sources are counted and logged once; INTIDs below 32 (timer PPI, SGIs) are passed through with no budget. Fixed windows allow a burst of up to 2 x budget straddling a boundary (accepted); an edge event dropped by a trip is lost (a level source re-fires after the cool-down). Wiring into `main.c` / the AIC backend and the numbers (budget, window, cool-down) are open; the `guard` L2 payload case of TEST_STRATEGY is not written.
- `el2/include/wel2_v2m.h` + `el2/vgic/v2m.c` (`test_v2m.c`, 11 cases): GICv2m-style frame model (TYPER 0x008, SETSPI_NS 0x040, IIDR 0xFCC, rest RAZ/WI), doorbell write latches the SPI pending as an edge in the vGIC, out-of-range INTIDs ignored and counted, `v2m_init_from_map()` takes the pool (928..959) from the `intr_map`. Offsets and SETSPI/IIDR/TYPER fields: **[verified: IHI 0069H.b Table 12-26, 12.9.42, 12.9.43, 12.9.45]** (the GICM_ frame of the GICv3 spec; the document gives TYPER at 0x008 in the table and 0x004 in the register description); the GICv2m TYPER layout base[25:16]/count[9:0] is **[hypothesis]** (design source V2M, not in the local specs; compatible with the v3 layout for 928/32 and QEMU's 80/64), the IIDR value is a placeholder. Tests also show a doorbell write and a pirq edge arrival of the same pool vector merge into one delivery. Not done: the M3a mapping of the 16 KB stage-2 page that hosts the frame, and the data mismatch hypotheses H-A/H-B/H-C (need hardware).
- `el2/tests/test_pirq_eoi.c` (5 cases): EOImode 1 on a level source through `vcif` + `pirq`, as `cpuif.c` flushes: EOIR drops priority only, the source stays masked, DIR deactivates and unmasks; GIC-like and AIC-like (strict level, a guest enable between EOIR and DIR does not unmask); EOImode 0 control. The existing code already behaved this way; no fix was needed.

Deviations and open points:
- Re-target is implemented in the layer (`pirq_vcpu_off/on`), not as `SET_TARGET` effects emitted by `vgic_vcpu_reset` (4.3 item 2): the vGIC keeps the guest's `IROUTER` untouched, which GICv3 requires, and the physical routing is a layer concern. An SPI whose `IROUTER` names an off vCPU therefore stays pending in the model until the guest migrates it or the vCPU returns; the physical interrupt is still taken on an online core (and a level source stays masked, per the "target OFF" row of 4.2). `IROUTER.IRM = 1` (1-of-N) is routed once at enable time, as before; it is not re-evaluated on CPU_OFF.
- `acpigen` has no DSDT/device-entry generator, so "generate GSIVs for device entries" is reduced to the contract test plus a TODO in `tools/acpigen/README.md` (M3b, #48). the three items left over from #67 are now done on the host (next block); `pirq_arrive` itself still only counts denied events and masks them.
- The table-defined trigger is not reflected in the vGIC's `GICD_ICFGR` readback (the guest reads what it wrote); ACPI tells Windows the same trigger, so it programs the same value. Not exercised against the real AIC: `auto_masks`, edge latching while masked are **[hypothesis]** until E-HW0.
