# PRD: Watari EL2 (minimal EL2 layer for Windows 11 ARM64 on Apple M1)

- Project: Watari Boot, subsystem **Watari EL2** · Date: 2026-10-14 · Version: 0.1 · Status: draft
- Linked documents: [ARCHITECTURE](ARCHITECTURE.md) · [INTERFACES](INTERFACES.md) · [TEST_STRATEGY](TEST_STRATEGY.md) · [RISKS](RISKS.md) · [DESIGN-STATUS](DESIGN-STATUS.md)
- Reference ADRs: `docs/adr/draft-plan-b-el2-layer.md` (Marco's decision), `docs/adr/draft-el2-base-m1n1-or-new.md`, `docs/adr/draft-el2-license.md`
- Marking: every technical statement is either **[verified: source]** or **[hypothesis]**. The `re/*` sources are those of the repository; the web sources were read read-only on 2026-10-14.

## 1. Problem
Windows 11 ARM64 does not start natively on the M1: it requires a GIC in the MADT (V2, F-14), there is no official way to provide a non-GIC controller (F-04, F-05), secondary core bring-up with the parking protocol deadlocks (V3, F-15) and the timer is FIQ-only (F-03, F-13). Plan B (draft ADR) moves the problem under Windows: an EL2 layer presents Windows with a "standard" system (GICv3 + PSCI + timer as an interrupt) and leaves everything else native.

This document defines **what that layer must do and how it is measured**. It does not cover the Windows drivers (ANS, DART, PEP, SPI-HID), which are another subsystem.

## 2. Goals
- **O-1** Boot Windows 11 ARM64 at EL1 on the 13" M1 MacBook Pro (t8103, j293) in dual boot, with no macOS underneath.
- **O-2** Present Windows with a **virtual GICv3** faithful enough not to cause silent hangs (lesson F-14).
- **O-3** Provide **PSCI via HVC** for core bring-up.
- **O-4** Deliver the ARM **timer** as a virtual interrupt (PPI) even if the hardware exposes it as a FIQ.
- **O-5** Translate **AIC → virtual SPI** interrupts and **SGI → AIC IPI**.
- **O-6** Stay **minimal**: identity stage-2, real peripherals, 1 vCPU = 1 physical core, no scheduler.
- **O-7** Be able to **develop and test almost everything off the MacBook**: portable core, replaceable backends, prototype in QEMU TCG with `virtualization=on`.
- **O-8** Meet the four cross-cutting requirements: maintainability, reproducibility, modularity, extensibility (section 7).

## 3. Non-goals
- No macOS or Linux under Windows. No second guest. No migration, no snapshots.
- No nested virtualization: Windows must not use Hyper-V/VBS/HVCI (it will not see EL2) [hypothesis: to be confirmed in M2 that Windows does not require it].
- No ITS, no LPI, no GICv4, no legacy GICv2. PCIe MSI → SPI is out of scope for v1 (only the "vestigial" MSI Frame required by F-11 is present).
- No peripheral emulator (virtio, disk, network, GPU): the peripherals are real and driven by the Windows drivers.
- No secure world / EL3.
- No support for M1 Pro/Max/M2 (AICv2, different errata) in v1: it is an **extension point**, not a goal.
- No advanced power management (deep sleep, real `CPU_SUSPEND`): `CPU_SUSPEND` resolves as WFI.
- No interactive debugger (the m1n1 proxy is not rewritten).
- The EDK2 firmware for M1 (platform `AppleSiliconPkg`) and `acpigen` are separate subsystems: only their **contracts** are defined here (INTERFACES, sections 2 and 9).

## 4. Users and actors
| Actor | Interest |
|---|---|
| Marco (developer, initial end user) | boot Windows on his own MacBook; development in QEMU on the host Mac |
| Ciani (second developer) | work on the independent modules (vGIC, host tests) |
| Ralph / Sonnet agents | execute small tasks with automated tests; require deterministic tests |
| Windows 11 ARM64 (system actor) | consumer of the GICv3/PSCI/timer interface |
| EDK2 (system actor) | consumer of the EL1 hand-off |
| m1n1 (system actor) | loader that hands control over at EL2 |
| Future users of other Apple Silicon | extend with new backends |

## 5. User stories
- **US-1** As a developer I want to boot the layer in QEMU TCG and see on the serial port the banner, `CurrentEL=2` and the stage-2 map, so I know the foundations work.
- **US-2** As a developer I want to run the vGIC tests on the host Mac (without QEMU) in under 10 seconds, to do TDD.
- **US-3** As a developer I want EDK2 to reach the boot manager on top of the layer in QEMU, to validate GICD/GICR/timer/PSCI with a real guest.
- **US-4** As a developer I want Windows 11 to reach the lock screen on top of the layer in QEMU TCG with 4 vCPUs, to validate fidelity.
- **US-5** As a user I want to choose "Windows" when the MacBook boots and reach the lock screen (with a USB keyboard/disk), for the final milestone of v1.
- **US-6** As a developer I want a layer log that is readable even after a hang, to debug where there is no host.
- **US-7** As a future developer I want to add an SoC or a controller (AICv2) by writing only a backend.

## 6. Functional requirements
| ID | Requirement | Module (ARCHITECTURE) |
|---|---|---|
| FR-1 | Boot at EL2 both from m1n1 (payload in the style of Linux Image + FDT) and from QEMU (`-kernel`), initialize the CPUs and hand EDK2 over at EL1 with `x0` = FDT | `wel2-boot`, `wel2-fdt` |
| FR-2 | Identity stage-2 for RAM and MMIO, with: layer region unmapped, virtual GICD/GICR/MSI regions trapped, MMIO attributes forced to Device-nGnRnE | `wel2-s2` |
| FR-3 | Emulate the GICv3 distributor (GICD) with stage-2 traps, with the registers listed in INTERFACES §4 | `wel2-vgic` |
| FR-4 | Emulate the GICv3 redistributors (GICR, 2 frames of 64 KB per vCPU) | `wel2-vgic` |
| FR-5 | Manage the virtual CPU interface: list registers `ICH_LR<n>_EL2`, `ICH_HCR_EL2`, `ICH_VMCR_EL2`, maintenance interrupt, in two selectable modes (`HW` and `TRAP_EMULATE`) | `wel2-vgic`, `wel2-cpuif` |
| FR-6 | Convert the physical AIC interrupts into virtual SPIs (INTID = 32 + AIC n), with mask/unmask consistent with level semantics | `wel2-pirq` |
| FR-7 | Intercept the guest timers (FIQ on M1, PPI on QEMU) and inject them as virtual PPIs 27 (virtual) and 30 (physical) at a consistent level | `wel2-vtimer` |
| FR-8 | Translate `ICC_SGI1R_EL1`/`ICC_ASGI1R_EL1`/`ICC_SGI0R_EL1` into pending virtual SGIs on the destination vCPUs and into physical IPIs | `wel2-vgic`, `wel2-ipi` |
| FR-9 | PSCI 1.x via HVC (`VERSION`, `FEATURES`, `CPU_ON`, `CPU_OFF`, `AFFINITY_INFO`, `MIGRATE_INFO_TYPE`, `SYSTEM_OFF`, `SYSTEM_RESET`, `CPU_SUSPEND` as WFI) and base SMCCC; every other function answers `NOT_SUPPORTED` | `wel2-psci` |
| FR-10 | SMP up to 8 vCPUs, 1 vCPU = 1 physical core; secondary cores started with the backend mechanism (spin-table on M1, QEMU PSCI SMC in TCG) | `wel2-psci`, `wel2-plat-*` |
| FR-11 | Handle system-register traps with a declarative table; `ID_AA64PFR0_EL1.GIC` seen as 1; registers not in the table: default-deny policy with log (RAZ/WI only if listed) | `wel2-trap` |
| FR-12 | Handle SErrors routed to EL2 (M1 SEIS errata): classify, record, and do not propagate to the guest unless explicitly declared | `wel2-trap` |
| FR-13 | Expose a vestigial MSI Frame (GICv2m), required by Windows (F-11) | `wel2-vgic` (MSI frame) |
| FR-14 | Ring-buffer log in reserved RAM + serial + optional framebuffer; panic with register dump, LR state and last N events | `wel2-log` |
| FR-15 | Exit counters by cause (EC, sysreg, MMIO, IRQ, FIQ, SGI, MI) readable via vendor HVC and printed periodically | `wel2-trap`, `wel2-log` |
| FR-16 | Backends replaceable at build time and, where possible, at run time: `pirq` (aic-t8103, gic-qemu, fake), `ipi`, `timer-src`, `plat` (qemu-virt, t8103) | `wel2-plat-*` |

## 7. Non-functional requirements
| ID | Requirement | Measure |
|---|---|---|
| NFR-1 Performance | CPU-bound overhead ≤ 3% compared to the same guest without interrupts; ≤ 300 exits/s/vCPU with Windows idle; peripheral interrupt latency (physical IRQ → guest ISR) ≤ 20 µs median on M1 | measured in M3 with a control guest; thresholds **[hypothesis]**, to be recalibrated after the M2 measurements (FR-15 counters) |
| NFR-2 Maintainability | each module has a declared single responsibility; functions ≤ 80 lines; no `TODO` in the main branch; errors with code and log; uniform naming (INTERFACES §10) | Opus review of the diff; lint (`-Wall -Wextra -Werror`, `clang-tidy` if already present) |
| NFR-3 Reproducibility | bit-for-bit identical build given identical sources: pinned clang (version written in the repo), `-ffile-prefix-map`, no `__DATE__`; tests with fixed seed; QEMU 11.1.2 and EDK2 `edk2-stable202608` pinned; images identified by SHA-256 | `make repro-check` builds twice and compares the hashes |
| NFR-4 Modularity | the vGIC does not include platform headers; dependencies only toward interfaces (`*_ops`); acyclic graph verified by script (`tools/check-deps.sh`) | script in local CI |
| NFR-5 Extensibility | adding an SoC = new `plat/<soc>` folder + entry in a table; adding an emulated device = registering an MMIO region; adding a sysreg = one table row; no change to the dispatcher or the vGIC | trial exercise in M3 (adding a dummy handler) |
| NFR-6 Hardware safety | no write to the disk or boot policy from the layer; read only what is needed; every out-of-map MMIO access → recorded fault, not propagated | review + "unmapped access" test |
| NFR-7 Size | ≤ 12,000 lines of C (excluding tests) and image ≤ 256 KB without EDK2 **[hypothesis: KVM vgic ≈ 4-5 thousand lines, m1n1 hv ≈ 3,700, `re/planB-feasibility.md` §1-2]** | `cloc` |
| NFR-8 Licenses | no code copied from KVM/m1n1/NTASP; provenance headers on files that cite sources (see `draft-el2-license.md`) | similarity scan + review |
| NFR-9 Observability | every hang must leave a trace: per-vCPU software watchdog and dump at timeout | fault-injection test |

## 8. Success criteria per milestone (measurable)
Calendar estimates: **[hypothesis]**, one developer with agents (see `re/planB-feasibility.md` §9: QEMU prototype 8-12 weeks, hardware another 14-24).

| Milestone | What | Pass criterion (all) | Estimate |
|---|---|---|---|
| **M0 (spike)** | Foundations in QEMU TCG | (a) `qemu-system-aarch64 -M virt,virtualization=on,gic-version=3 -cpu max -kernel wel2.elf` prints the banner and `CurrentEL=2`; (b) stage-2 active; (c) a test EL1 payload (≤ 200 lines asm/C, in the repo) receives 1 injected PPI 30 and performs `ICC_IAR1`/`ICC_EOIR1`; (d) vGIC host tests: ≥ 40 tests, 100% green, < 10 s | 1-2 wks |
| **M1** | EDK2 at EL1 on top of the layer in QEMU TCG | (a) our EDK2 (`WatariQemuPkg`) reaches the UEFI Boot Manager with `-smp 4`; (b) serial: `ArmGicDxe` detects GICv3, `[Bds]BdsWait` terminates (F-08 regression); (c) EDK2 executes `PSCI SYSTEM_RESET` and the layer handles it; (d) 0 unhandled traps in the log; (e) 3 consecutive runs identical in the key events | 3-5 wks |
| **M2** | Windows 11 ARM64 on top of the layer in QEMU TCG | (a) the installed Windows (N2/V1) reaches the lock screen with `-smp 4` (screenshot + log); (b) 4 vCPUs powered on via `CPU_ON` (4 entries in the log) and none turned off by mistake; (c) 0 unclassified SErrors, 0 unknown non-RAZ sysreg traps; (d) 30 minutes of idling with no panic or `0x5C`; (e) boot time ≤ 1.5× that of the same Windows on TCG without the layer (baseline measured with the same script); (f) vGIC fidelity tests (TEST_STRATEGY §4) 100% | 5-8 wks |
| **M3a** | Layer on M1 with m1n1 and EDK2 | (a) on-hardware self-tests (AIC SW_SET → vSPI, fast IPI, timer FIQ → vPPI 30 and 27, `CPU_ON` of the 8 cores) 100% green; (b) EDK2 reaches UEFI with framebuffer; (c) overhead measured with a control guest | 6-10 wks |
| **M3b** | Windows on the MacBook | Windows 11 ARM64 from USB-C reaches the lock screen, with 8 CPUs visible; 0 panics in 30 minutes | 8-14 wks |

Moving from one milestone to the next requires Opus's review and Marco's confirmation for any disk/boot operation (M3 onward).

## 9. Success metrics (beyond the criteria)
- Fidelity: percentage of oracle MMIO reads (QEMU GICv3 trace) reproduced correctly = 100% on static registers.
- Stability: hours of Windows without a layer panic (M3b target: ≥ 2 h).
- Diagnosability: every panic provides cause, PC, EC, last 64 events (verified with fault injection).
- Traceability: every FR/NFR has at least one module and one test (table in ARCHITECTURE §10).

## 10. Scope and out-of-scope (summary)
- **In scope:** a single SoC (t8103), a single guest, GICv3 without ITS, PSCI, timer, SGI, SMP up to 8, QEMU TCG bench.
- **Out of scope:** see §3; additionally ANS/DART/PEP/SPI-HID and the EDK2 platform for M1.

## 11. Dependencies on the other subsystems
| From | What is needed | Status |
|---|---|---|
| `tools/acpigen` | `qemu-virt` model with FADT PSCI+HVC (already in use in the T1 control of F-15); new `t8103-el2` model for M3 | partial |
| `edk2-apple/WatariQemuPkg` | firmware that accepts `x0 = FDT` and starts at EL1 | exists for QEMU |
| EDK2 for M1 | FDT reading (not ADT) for the platform map | to do (non-blocking question Q-1 in RISKS §8) |
| Asahi installer / m1n1 | load the layer as a payload | to be verified in M3a |

## 12. Risks
Summary (detail in [RISKS](RISKS.md)): M1 SEIS errata; vGIC fidelity; E2H locked to 1 on the M1; timer FIQ path not testable in QEMU; trap-emulate performance; debugging without a host; licenses (m1n1 ≥ 2.0.0 is GPL).

## 13. References to the ADRs
- `docs/adr/draft-plan-b-el2-layer.md` (accepted by Marco) · `docs/adr/draft-el2-base-m1n1-or-new.md` · `docs/adr/draft-el2-license.md`.
- ADR-0001 (native without hypervisor) is amended by the plan B draft; ADR-0007 (path independent from NTASP) remains valid.
