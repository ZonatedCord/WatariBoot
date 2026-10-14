# RISKS: Watari EL2

- Project: Watari Boot, subsystem Watari EL2 · Date: 2026-10-14 · Version: 0.1 · Status: draft
- Linked documents: [PRD](PRD.md) · [ARCHITECTURE](ARCHITECTURE.md) · [INTERFACES](INTERFACES.md) · [TEST_STRATEGY](TEST_STRATEGY.md)
- Scale: probability (P) and impact (I) from 1 (low) to 5 (high); score = P×I. The assessments are **[hypothesis]** except where indicated.

## 1. Risk register
| ID | Risk | P | I | P×I | Mitigation | Warning sign |
|---|---|---|---|---|---|---|
| R-01 | **M1 SEIS errata**: the vGIC can generate physical SErrors in response to guest accesses to GIC registers; an unhandled trap is a hypervisor panic | 4 | 5 | 20 | `TRAP_EMULATE` mode by default with variant A (maximum trap); `HCR_EL2.AMO=1` to intercept SErrors; classification and log; `SEIS` hidden in `ICC_CTLR_EL1`; reduction to variant B only after `serror-probe` | any SError in the M3a log |
| R-02 | **vGIC fidelity** toward Windows: a wrong register leads to a silent hang (F-14) | 3 | 5 | 15 | Oracle (TEST_STRATEGY §4), "fidelity" column in INTERFACES, layer watchdog with dump, "Critical" registers reviewed by Opus | identical screen for > 60 s with no log entries |
| R-03 | **`HCR_EL2.E2H` stuck at 1** on the M1: different register format (CNTHCTL, TTBR), access to the guest context via `*_EL12`/`*_EL02`; subtle errors that QEMU does not show unless E2H=1 is reproduced | 4 | 3 | 12 | The layer sets E2H=1 in QEMU too; L0 tests on the `_EL12` accessors; review of the `CNTHCTL_EL2` bits against the manual in M0 | different behavior between QEMU and M1 on the timer |
| R-04 | **Timer FIQ path not testable in QEMU**: the real semantics of `VM_TMR_FIQ_ENA_EL2`, of ISTATUS/IMASK and of the levels only on hardware | 4 | 4 | 16 | L0 tests with fake sysregs; `timer-fiq` self-test in M3a; observation with m1n1 hv as a tool (read-only); planned alternative path: mask with `IMASK` (visible to the guest) if the IMP-DEF bit does not behave as expected | FIQ storm, lost ticks |
| R-05 | **`TRAP_EMULATE` performance**: if Windows uses `ICC_PMR_EL1` for the IRQL and `TC` intercepts it, every IRQL change is a trap (thousands per second) | 3 | 4 | 12 | Measure in M0/M1 the frequency of `PMR`, `IAR`, `EOIR` from the trace (TEST_STRATEGY §4); variant B (no `TC`) if the safety proof allows it; thresholds in NFR-1 | > 300 exits/s/vCPU at rest |
| R-06 | **Stage-2 abort with `ISV=0`** (instructions with writeback, load/store pair, compiler accesses to GIC regions): no information on the register | 2 | 4 | 8 | Panic with dump on first occurrence; if observed, a minimal instruction decoder (via `AT S1E1R` + instruction read) as an added module; F-12 shows the symptom in QEMU/HVF | `PANIC_ISV0` |
| R-07 | **Debugging without a host**: on the MacBook the layer has no channel other than the framebuffer and disk; a hang at EL2 leaves no trace | 4 | 4 | 16 | Ladder: (1) ring log in reserved RAM + console on the iBoot framebuffer (`simple-framebuffer`) with dump at panic; (2) per-vCPU software watchdog and controlled reset; (3) `WEL2_LOG` from EDK2 to disk/ESP (log file) [hypothesis]; (4) **debug host recommended for M3** (second computer, already among the blockers in STATUS: Pi 5 or laptop). The RAM ring may not survive a reset [hypothesis]: to be tested | no trace after a hang |
| R-08 | **Licenses**: m1n1 ≥ 2.0.0 is GPL-2.0-or-later (MIT up to 1.9.9); KVM is GPL-2.0; NTASP declares MIT but derives from both | 3 | 4 | 12 | New layer written from specification; no code copied; "read, close, write" process and similarity scan; `draft-el2-license.md`; possible recourse to a GPL license for the image if m1n1 code were copied | a file with a structure identical to a GPL source |
| R-09 | **MMIO attributes**: Windows maps devices as nGnRE; Apple needs non-posted writes | 3 | 4 | 12 | Device-nGnRnE stage-2 on all MMIO; `s2-attr` self-test | SError on MMIO write |
| R-10 | **Assumptions on the m1n1 hand-off** (EL2, `x0`=FDT, spin-table, FIQ state) not verified | 3 | 4 | 12 | Verification in M3a by reading `kboot.c`/`smp.c` and with a test on hardware; contract in INTERFACES §2 with fields isolated in the `plat-t8103` backend | hand-off different from what is expected |
| R-11 | **DMA toward the layer region**: the M1 has DART and not SMMU; a faulty Windows driver could write into the layer's memory | 2 | 5 | 10 | Region removed from the memory map given to EDK2/Windows; v2: program the DARTs to deny the region; document the limit in v1 | corruption of layer data |
| R-12 | **Windows requires more than expected**: ITS/LPI, Hyper-V, particular `GICD_TYPER`, SPI > 960 | 2 | 4 | 8 | V1 shows that GICv3 without ITS is sufficient on QEMU [verified: F-11]; documented fallback: add modules (extension point) | hang with a MADT different from V1 |
| R-13 | **Slow TCG**: Windows boot in tens of minutes, slow development cycle | 4 | 2 | 8 | L0/L2 tests before L3; test payload; 1.5× threshold against the TCG baseline | iterations > 1 h |
| R-14 | **SMP and deadlock** (locks, lost notification IPIs) | 3 | 4 | 12 | Fixed lock order, lock watchdog, `sgi-ipi` N×(N-1) test, stress test with crossed IPIs | `PANIC_LOCK_TIMEOUT` |
| R-15 | **Low maturity of the sources**: NTASP `hv_vgic.c` incomplete and of unproven quality; no documented Windows on top of a hypervisor on M1 with real peripherals | 3 | 3 | 9 | The sources are only a reference; fidelity verified with our own oracle | — |
| R-16 | **Duration**: the estimates (8-12 weeks QEMU, 14-24 hardware) are hypotheses of a single developer with agents | 4 | 3 | 12 | Milestones with measurable criteria; M0 as a 1-2 week spike that reduces uncertainty | M0 beyond 3 weeks |
| R-17 | **Hardware safety**: an EL2 layer with a bug can write where it must not | 2 | 5 | 10 | The layer touches neither disk nor boot policy; absolute rule of backup and confirmation; reset always possible from DFU/revive | — |
| R-18 | **Inconsistency with ADR-0001 and with CLAUDE.md** ("no hypervisor" constraint) | 2 | 3 | 6 | The plan B draft is accepted by Marco; update CLAUDE.md at merge: "dual boot, no macOS underneath, minimal EL2 layer allowed" | — |

## 2. Deep dives

### 2.1 SEIS errata (R-01)
- **Facts [verified]:** KVM works around the problem on M1 (Icestorm, Firestorm), M1 Pro/Max and M2: the original commit `df652bcf1136` intercepts all guest GIC accesses, sanitizes them and emulates them using the hardware "only for signaling"; the commit `d11a327ed9` restricts the workaround to the known Apple CPUs, zeroes `ICH_VTR_SEIS_MASK` in the global state and sets `group0_trap`/`group1_trap`; `__vgic_v3_read_ctlr` shows `SEIS` if the register declares it [verified: reading of the commits and summaries, 2026-10-14]. NTASP cites the errata in its `hv_vgic.c` ("an errata here exists on pre-M3 SoCs that can result in a host SError") [verified: PB §1].
- **Unknown [hypothesis]:** *which* precise accesses trigger the physical SError and whether they concern only the group registers or also `PMR`/`CTLR`. Hence the choice to start from maximum trap.
- **Design consequence:** the `HW` mode advertised in the plan B draft is not the default. The overhead is 2 traps per interrupt.
- **Planned proof:** `serror-probe` in M3a, with SErrors routed to EL2 (`AMO=1`) so that an error is a recorded event and not a machine reset.

### 2.2 Performance (R-05, NFR-1)
- Overhead per peripheral interrupt in `TRAP_EMULATE`: physical IRQ at EL2 (1 exit) + `IAR1` (1) + `EOIR1` (1) + possible forwarding IPI. With an order of magnitude of 1-3 µs per exit (PB §8, [hypothesis]) → 3-9 µs per interrupt. Relevant only at high rates (NVMe, network). For drivers under development (USB mass storage in the first step) it is acceptable.
- The timer costs ≥ 2 exits per tick (FIQ + EOI) plus, with `TC`, the `PMR` exits. Windows has ticks of 1-15 ms and uses coalescing [hypothesis]: order of 100-1000 exits/s per core.
- Literature: on ARM the overhead is dominated by the hypervisor software (injection and trap), not by stage-2 [verified: Dall et al., ISCA 2016, cited in PB §8].
- Mitigations, in order of cost: trap variant B; faster EOI with LR.EOI in `HW` mode if safe; large blocks in stage-2; avoid long locks.
- Measurement: FR-15 counters, benchmark with a control guest (TEST_STRATEGY §6). The NFR-1 thresholds are provisional.

### 2.3 Debugging without a host (R-07)
The worst scenario: the layer hangs at EL2 before EDK2, black screen. Strategies in order:
1. **QEMU first:** over 90% of the logic is validated off the MacBook (L0-L3).
2. **Persistent log:** ring in reserved RAM; at panic, dump on the framebuffer (text with an 8×16 font, written directly into the iBoot framebuffer); event counter.
3. **Blinking early boot:** colors on the framebuffer for the boot phases (red = entered EL2, yellow = stage-2 active, green = EDK2 started) [hypothesis: simple technique, does not require UART].
4. **m1n1 stage 2 remains available:** if the layer does not start, the machine falls back to the Asahi menu/boot or to macOS/recovery (not modified): no risk for the hardware.
5. **Debug host** (recommended from the start of M3a): UART via USB-C with the m1n1 proxy; it is not the product channel, it is the development tool.
Question for Marco (non-blocking): is a second computer available when M3a is reached? (STATUS cites the need for a host as a blocker.)

### 2.4 Licenses (R-08)
Facts: m1n1 README: MIT up to 1.9.9, **GPL-2.0-or-later since 2.0.0**; `src/hv/hv_vm.c` and `src/main.c` carry `GPL-2.0-or-later` [verified]. The m1n1 `LICENSE` is the GPL-2 text (GitHub: GPL-2.0) and there is a `LICENSE.MIT` for earlier versions [verified]. NTASP `m1n1_windows` is declared MIT, but derives from m1n1 and states «Inspiration borrowed from the KVM vGIC driver» [verified: NTASP §6, PB §3]: legal status unclear. KVM (`arch/arm64/kvm`) is part of Linux (GPL-2.0, v2 only) [hypothesis for the SPDX of the individual files]. `irq-apple-aic.c` has a license to be verified (HTTP request blocked with 429 during the session). Full strategy in `docs/adr/draft-el2-license.md`. This document is not legal advice.

## 3. Potential debt
- Trap variant A (everything intercepted) is simple but expensive: the debt is to remove it once it is proven safe.
- The minimal FDT parser is a dependency that we own (risk of bugs on unusual FDTs); libfdt (BSD-2/GPL) as an alternative for M3.
- The fixed 8-CPU map (`WEL2_MAX_CPUS`): for M1 Pro/Max (10 cores) it needs to be parameterized.
- Distributor shared state with a single lock: sufficient at 8 cores, to be reviewed if contention is observed.

## 4. Assessment of the four key requirements
| Requirement | Assessment | Residual risks |
|---|---|---|
| Maintainability | good: small modules, declarative tables, errors as values | a single developer; the bring-up code is hard to read |
| Reproducibility | good if the TEST_STRATEGY §7 rules are respected; Windows state is not deterministic | variable TCG times |
| Modularity | high in the core; the backends concentrate the hardware dependency | strong coupling between `cpuif` and `vgic` (justified: same state) |
| Extensibility | high for SoCs and devices; `boot` remains the only point of modification | the A→B variant change requires hardware data |

## 5. Non-blocking open questions
- **Q-1** Will EDK2 for M1 read the FDT (the choice of this design) or the ADT? Impacts `M-ADT-PARSE` of the main project.
- **Q-2** Repository license (today no `LICENSE` file in the root): see `draft-el2-license.md`.
- **Q-3** Second debug computer available for M3a? (R-07)
- **Q-4** Do we want an AIC model in QEMU (requires compiling QEMU)? It is not on the critical path.
- **Q-5** M1 Pro/Max and M2 (AICv2, errata) fall under Phase 6: confirm that the `pirq` extension point is enough.
- **Q-6** NFR-1 thresholds: calibrate after the M2 measurements.

## 6. Decisions made during the design
- New layer, m1n1 only as loader (base ADR).
- cpuif mode `TRAP_EMULATE` by default on M1, selectable at runtime and tested in TCG.
- Virtual vGIC addresses in QEMU equal to the real ones, so the `acpigen qemu-virt` model is reused.
- Flat virtual MPIDR.
- Hand-off via standard FDT (`x0`) and the `/chosen/watari,el2` node.
